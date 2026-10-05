#include "screenfx.h"
#include "../common.h"    // MinHook
#include "websurface.h"   // Web_WantsSingleThreadedMat: shares mat_queue_mode hold
#include "render.h"       // SceneFx_PreHud
#include "fx_probe.h"
#include "fx_quad_vs.h"

// Depth source: own INTZ, filled by RESZ hack (amd/intel) or NvAPI StretchRectEx (nvidia, msaa ok).
// Resolve ONLY at post slot pos. Measured msaa 8x: post slot min=0.911 live; EndScene 1.0 wiped;
// depth clear #2 1.0 (follows 3D skybox). Multicore -> queue onto render thread via matsys call
// queue; mat_queue_mode 0 forced only if queue refuses.

static const D3DFORMAT k_fmtINTZ = static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
static const D3DFORMAT k_fmtRESZ = static_cast<D3DFORMAT>(MAKEFOURCC('R','E','S','Z'));

// driver watches D3DRS_POINTSIZE for this (shaderapidx8.cpp:12487)
static constexpr DWORD k_reszCode = 0x7fa05000;

// nvapi by hand: entry points via nvapi_QueryInterface id, all __cdecl, NVAPI_OK = 0
using NvQueryInterface_t = void* (__cdecl*)(unsigned int);
using NvInitialize_t     = int   (__cdecl*)();
using NvRegister_t       = int   (__cdecl*)(IDirect3DResource9*);
using NvUnregister_t     = int   (__cdecl*)(IDirect3DResource9*);
using NvStretchRectEx_t  = int   (__cdecl*)(IDirect3DDevice9*, IDirect3DResource9*, const RECT*,
                                            IDirect3DResource9*, const RECT*, D3DTEXTUREFILTERTYPE);

static NvRegister_t      s_nvRegister   = nullptr;
static NvUnregister_t    s_nvUnregister = nullptr;
static NvStretchRectEx_t s_nvStretchEx  = nullptr;

static IDirect3DTexture9* s_ownDepth = nullptr;   // our INTZ copy of z buffer
static int  s_texW = 0, s_texH = 0;
static bool s_reszCap      = false;
static bool s_nvapiChecked = false;
static bool s_nvapiOk      = false;
static bool s_useNvapi     = false;   // route in force (flips if other reads dead)
static void* s_nvRegTex    = nullptr;             // registering same resource twice = error
static bool s_capsLogged   = false;

static bool FxHasRoute() { return s_reszCap || s_nvapiOk; }

static int  s_frame        = 0;                   // bumped in h_Present
static int  s_resolvedFrm  = -1;                  // one resolve per frame, first asker wins
static int  s_vmFrame      = -1;                  // frame gun pass ran for
static int  s_liveFrame    = -1;                  // frame depth last proven live

static IDirect3DVertexShader9*      s_quadVs   = nullptr;
static IDirect3DVertexDeclaration9* s_quadDecl = nullptr;

static IDirect3DPixelShader9* s_probePs   = nullptr;
static IDirect3DTexture9*     s_probeTex  = nullptr;
static IDirect3DSurface9*     s_probeSurf = nullptr;
static IDirect3DSurface9*     s_probeSys  = nullptr;   // SYSTEMMEM, lockable readback end
static D3DFORMAT              s_probeFmt  = D3DFMT_UNKNOWN;

inline constexpr int k_probeW = 64;
inline constexpr int k_probeH = 36;

// No pass draws till probe sees real depth: dead resolve = far plane everywhere = whole frame
// blurred, so Unknown and Dead both draw nothing.
enum class FxHealth { Unknown, Live, Dead };
static FxHealth s_health     = FxHealth::Unknown;
static int      s_deadStreak = 0;
static int      s_probeWait  = 0;        // frames till next probe
static uint32_t s_healthEpoch = 0;       // g_matEpoch verdict belongs to
static bool     s_lastQueued = false;    // route verdict belongs to
static bool     s_msaa       = false;

inline constexpr int k_probeUnknown = 20;     // frames between probes while undecided
inline constexpr int k_probeDead    = 120;    // ... after dead verdict, so it recovers
// 20s = how long a SILENTLY dead route (resolve says ok, writes nothing) blurs frame before probe notices
inline constexpr int k_probeLive    = 1200;
inline constexpr int k_deadNeeded   = 3;      // consecutive dead probes (sky-only view reads dead)

// Route recovery: nothing outside device reset remakes INTZ / nvapi reg, so a route dying
// mid-session stayed dead till restart. Detectors ask rebuild: ScreenFx_Destroy + FxEnsure next frame.
static bool s_rebuildWanted = false;
static int  s_rebuildFrame  = -100000;        // s_frame of last rebuild
static int  s_rebuildTries  = 0;              // in a row, no live depth since
inline constexpr int k_rebuildCooldown = 120; // ~2s between tries, no thrash
inline constexpr int k_rebuildMaxTries = 4;   // route gone for good stops asking

static volatile bool s_cqRan     = false;   // a queued functor actually ran
static volatile int  s_cqQueued  = 0;
static bool          s_cqRefused = false;   // queue said no, or never ran what it took

// View setup snapshot from game post pass. Torn read = one frame of wrong focus, never crash.
static volatile float s_viewFov   = 0.f;
static volatile float s_viewNear  = 0.f;
static volatile float s_viewFar   = 0.f;
static volatile int   s_viewW     = 0;
static volatile int   s_viewH     = 0;
static volatile bool  s_viewValid = false;

// widest setup = main view; cleared with device (res change makes old max unpassable)
static int   s_maxSetupW = 0;
static DWORD s_presentTid = 0;   // the render thread, learned in h_Present

bool ScreenFx_View(FxViewSetup& out)
{
    if (!s_viewValid) return false;
    out.fov    = s_viewFov;
    out.zNear  = s_viewNear;
    out.zFar   = s_viewFar;
    out.width  = s_viewW;
    out.height = s_viewH;
    return out.zFar > out.zNear && out.zNear > 0.f;
}

IDirect3DTexture9* ScreenFx_DepthTexture()
{
    return (s_ownDepth && FxHasRoute()) ? s_ownDepth : nullptr;
}

void ScreenFx_NewFrame()
{
    ++s_frame;
    FrameBlend_NewFrame();   // frame id bone sets + camera history pair on
    s_presentTid = GetCurrentThreadId();
}

static bool FxNvapiReady()
{
    if (s_nvapiChecked) return s_nvapiOk;
    s_nvapiChecked = true;

    HMODULE mod = GetModuleHandleA("nvapi64.dll");
    if (!mod) mod = LoadLibraryA("nvapi64.dll");
    if (!mod) {
        DebugLog("screenfx: no nvapi64.dll -- depth resolve has no nvidia route");
        return false;
    }

    const auto query = reinterpret_cast<NvQueryInterface_t>(GetProcAddress(mod, "nvapi_QueryInterface"));
    if (!query) { DebugLog("screenfx: nvapi_QueryInterface missing"); return false; }

    NvInitialize_t init = nullptr;
    __try {
        init           = reinterpret_cast<NvInitialize_t>(query(0x0150E828));
        s_nvRegister   = reinterpret_cast<NvRegister_t>(query(0xA064BDFC));
        s_nvUnregister = reinterpret_cast<NvUnregister_t>(query(0xBB2B17AA));
        s_nvStretchEx  = reinterpret_cast<NvStretchRectEx_t>(query(0x22DE03AA));
    } __except(EXCEPTION_EXECUTE_HANDLER) { init = nullptr; }

    if (!init || !s_nvRegister || !s_nvStretchEx) {
        DebugLog("screenfx: nvapi entry points missing (init=%p reg=%p stretch=%p)",
                 (void*)init, (void*)s_nvRegister, (void*)s_nvStretchEx);
        return false;
    }

    int status = -1;
    __try { status = init(); } __except(EXCEPTION_EXECUTE_HANDLER) { status = -1; }

    s_nvapiOk = (status == 0);
    DebugLog("screenfx: nvapi init %d", status);
    return s_nvapiOk;
}

static void FxWantRebuild(const char* why)
{
    if (s_rebuildWanted) return;
    // rebuild loop = churn every 2s forever; give up after 4. Live verdict / reset / res change zero it.
    if (s_rebuildTries >= k_rebuildMaxTries) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("^FX: depth route stayed dead over %d rebuilds -- leaving it off", k_rebuildMaxTries); }
        return;
    }
    s_rebuildWanted = true;
    DebugLog("^FX: depth route rebuild asked for -- %s", why);
}

static void FxResolveNvapi(IDirect3DDevice9* dev, IDirect3DSurface9* depthSurf)
{
    if (!dev || !depthSurf || !s_ownDepth || !s_nvStretchEx || !s_nvRegister) return;

    // own INTZ: registered once + kept (we own lifetime; ScreenFx_Destroy unregisters first)
    if (s_nvRegTex != s_ownDepth) {
        const int st = s_nvRegister(s_ownDepth);
        s_nvRegTex = s_ownDepth;
        DebugLog("screenfx: nvapi register texture %d", st);
    }

    // ENGINE depth surface not ours: register/stretch/unregister inside this one call, caller holds ref.
    // Map load = matsys device restore w/o Reset (h_Reset never fires); a kept reg left driver holding
    // freed surface -> garbage vtables crash in materialsystem/shaderapidx9 on foreign threads.
    // Non-zero register status ignored: engine registers same surface for own resolve.
    const int reg = s_nvRegister((IDirect3DResource9*)depthSurf);
    const int st = s_nvStretchEx(dev, depthSurf, nullptr, s_ownDepth, nullptr, D3DTEXF_POINT);
    if (s_nvUnregister) s_nvUnregister((IDirect3DResource9*)depthSurf);

    static int s_lastStatus = -1;
    if (st != s_lastStatus) { s_lastStatus = st; DebugLog("screenfx: nvapi resolve %d (register %d)", st, reg); }

    // failed StretchRectEx writes nothing + keeps failing every frame. 2 in a row before rebuild
    // (one bad frame during res change not worth it).
    static int s_failStreak = 0;
    if (st != 0) { if (++s_failStreak >= 2) FxWantRebuild("nvapi resolve is failing"); }
    else s_failStreak = 0;
}

// RESZ: INTZ on sampler 0, dummy point, magic POINTSIZE -> driver resolves BOUND depth surface
// (msaa or not). Sequence = CShaderAPIDx8::CopyRenderTargetToTextureEx.
static void FxResolveResz(IDirect3DDevice9* dev)
{
    if (!dev || !s_ownDepth) return;

    static const float dummy[3] = { 0.f, 0.f, 0.f };

    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZ);
    dev->SetTexture(0, s_ownDepth);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);

    // never rasterised; only makes sure sampler 0 is really bound
    dev->DrawPrimitiveUP(D3DPT_POINTLIST, 1, dummy, 3 * sizeof(float));

    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);

    // this pair IS the resolve; w/o second write it fires only first frame
    dev->SetRenderState(D3DRS_POINTSIZE, k_reszCode);
    dev->SetRenderState(D3DRS_POINTSIZE, 0);

    dev->SetTexture(0, nullptr);
}

static void FxHealthReset()
{
    s_health     = FxHealth::Unknown;
    s_deadStreak = 0;
    s_probeWait  = 0;
}

void ScreenFx_Destroy()
{
    // nvapi lets go BEFORE resource dies, else holds freed pointer
    if (s_nvUnregister && s_nvRegTex) s_nvUnregister((IDirect3DResource9*)s_nvRegTex);
    s_nvRegTex = nullptr;

    Ssao_Destroy();
    Dof_Destroy();
    FrameBlend_Destroy();

    if (s_quadDecl)  { s_quadDecl->Release();  s_quadDecl  = nullptr; }
    if (s_quadVs)    { s_quadVs->Release();    s_quadVs    = nullptr; }
    if (s_probeSys)  { s_probeSys->Release();  s_probeSys  = nullptr; }
    if (s_probeSurf) { s_probeSurf->Release(); s_probeSurf = nullptr; }
    if (s_probeTex)  { s_probeTex->Release();  s_probeTex  = nullptr; }
    if (s_probePs)   { s_probePs->Release();   s_probePs   = nullptr; }
    if (s_ownDepth)  { s_ownDepth->Release();  s_ownDepth  = nullptr; }

    s_texW = s_texH = 0;
    s_probeFmt    = D3DFMT_UNKNOWN;
    s_resolvedFrm = -1;
    s_maxSetupW   = 0;
    s_rebuildWanted = false;   // this IS the rebuild
    FxHealthReset();
}

// Own INTZ at frame size. False = no depth route.
static bool FxEnsure(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    if (!dev) return false;
    // INTZ with no route != depth source
    if (s_ownDepth && s_texW == (int)desc.Width && s_texH == (int)desc.Height)
        return FxHasRoute();

    // new size = reset / video change: dead route worth retrying
    if (s_texW) s_rebuildTries = 0;

    ScreenFx_Destroy();

    IDirect3D9* d3d = nullptr;
    if (SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d)
    {
        const bool resz = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                        D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET,
                                        D3DRTYPE_SURFACE, k_fmtRESZ));
        const bool intz = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                        D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL,
                                        D3DRTYPE_TEXTURE, k_fmtINTZ));
        s_reszCap = resz && intz;

        if (!s_capsLogged) {
            s_capsLogged = true;
            D3DADAPTER_IDENTIFIER9 id{};
            if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id)))
                DebugLog("screenfx: adapter %s vendor 0x%04x resz=%d intz=%d",
                         id.Description, (unsigned)id.VendorId, (int)resz, (int)intz);
        }
        d3d->Release();
    }

    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_DEPTHSTENCIL,
                                    k_fmtINTZ, D3DPOOL_DEFAULT, &s_ownDepth, nullptr);
    if (FAILED(hr) || !s_ownDepth) {
        s_ownDepth = nullptr;
        s_reszCap  = false;
        DebugLog("screenfx: own INTZ creation FAILED hr=0x%08lx -- no depth route", (unsigned long)hr);
        return false;
    }

    // ask BOTH routes: RESZ-capable card can still resolve dead (driver ignores magic), probe then
    // flips route. Here not at resolve: ScreenFx_DepthTexture answers off result.
    FxNvapiReady();

    s_texW = (int)desc.Width;
    s_texH = (int)desc.Height;

    if (!FxHasRoute()) {
        DebugLog("screenfx: neither RESZ nor NvAPI -- depth resolve impossible on this card");
        return false;
    }
    s_useNvapi = !s_reszCap;   // RESZ first: vendor-neutral
    DebugLog("screenfx: depth source up %dx%d route=%s (resz=%d nvapi=%d)",
             s_texW, s_texH, s_useNvapi ? "nvapi" : "resz", (int)s_reszCap, (int)s_nvapiOk);
    return true;
}

bool ScreenFx_QuadReady(IDirect3DDevice9* dev)
{
    if (s_quadVs && s_quadDecl) return true;
    if (!dev) return false;

    if (!s_quadVs) {
        std::vector<DWORD> code((sizeof(k_fxQuadVsBytes) + 3) / 4);
        memcpy(code.data(), k_fxQuadVsBytes, sizeof(k_fxQuadVsBytes));
        if (FAILED(dev->CreateVertexShader(code.data(), &s_quadVs))) {
            DebugLog("screenfx: quad vertex shader creation failed");
            s_quadVs = nullptr;
            return false;
        }
    }

    if (!s_quadDecl) {
        static const D3DVERTEXELEMENT9 k_elems[] = {
            { 0,  0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            D3DDECL_END()
        };
        if (FAILED(dev->CreateVertexDeclaration(k_elems, &s_quadDecl))) {
            DebugLog("screenfx: quad vertex declaration creation failed");
            s_quadDecl = nullptr;
            return false;
        }
    }
    return true;
}

void ScreenFx_BindQuad(IDirect3DDevice9* dev)
{
    dev->SetVertexShader(s_quadVs);
    dev->SetVertexDeclaration(s_quadDecl);
}

// half-texel shift on POSITION (clip space), target's texel; without it frame creeps
void ScreenFx_DrawQuad(IDirect3DDevice9* dev, int targetW, int targetH)
{
    const float dx = (targetW > 0) ? 1.f / (float)targetW : 0.f;
    const float dy = (targetH > 0) ? 1.f / (float)targetH : 0.f;

    const FxQuadVert quad[4] = {
        { -1.f - dx,  1.f + dy, 0.f, 1.f, 0.f, 0.f },
        {  1.f - dx,  1.f + dy, 0.f, 1.f, 1.f, 0.f },
        { -1.f - dx, -1.f + dy, 0.f, 1.f, 0.f, 1.f },
        {  1.f - dx, -1.f + dy, 0.f, 1.f, 1.f, 1.f },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(FxQuadVert));
}

// DrawPrimitiveUP nulls stream 0 + indices and matsys caches both -> restore or next engine
// mesh draws from NULL stream.
struct FxSavedState
{
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex0;
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    IDirect3DSurface9*           rt0;
    UINT   streamOffset, streamStride;
    DWORD  fvf;
    D3DVIEWPORT9 vp;
    DWORD  rs[10];
    DWORD  samp[6];
};

static const D3DRENDERSTATETYPE k_fxRS[10] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_STENCILENABLE, D3DRS_POINTSIZE,
};
static const D3DSAMPLERSTATETYPE k_fxSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void FxSave(IDirect3DDevice9* dev, FxSavedState& s)
{
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetTexture(0, &s.tex0);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetRenderTarget(0, &s.rt0);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 10; ++i) dev->GetRenderState(k_fxRS[i], &s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->GetSamplerState(0, k_fxSamp[i], &s.samp[i]);
}

static void FxRestore(IDirect3DDevice9* dev, FxSavedState& s)
{
    for (int i = 0; i < 10; ++i) dev->SetRenderState(k_fxRS[i], s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->SetSamplerState(0, k_fxSamp[i], s.samp[i]);
    if (s.rt0) dev->SetRenderTarget(0, s.rt0);
    dev->SetViewport(&s.vp);
    dev->SetTexture(0, s.tex0);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    // FVF + decl share slot: restore only the one in force
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    if (s.tex0)    s.tex0->Release();
    if (s.ps)      s.ps->Release();
    if (s.vs)      s.vs->Release();
    if (s.decl)    s.decl->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
    if (s.rt0)     s.rt0->Release();
}

// Probe: 64x36 depth taps via shader + readback (INTZ can't be StretchRect'd/locked).
// Readback stalls gpu -> only while verdict open.
static bool FxEnsureProbe(IDirect3DDevice9* dev)
{
    if (s_probePs && s_probeTex && s_probeSurf && s_probeSys) return true;

    if (!s_probePs) {
        std::vector<DWORD> code((sizeof(k_fxProbeBytes) + 3) / 4);
        memcpy(code.data(), k_fxProbeBytes, sizeof(k_fxProbeBytes));
        if (FAILED(dev->CreatePixelShader(code.data(), &s_probePs))) {
            DebugLog("screenfx: probe shader creation failed");
            return false;
        }
    }

    // A32B32G32R32F = exact depth; A8R8G8B8 fallback still tells sane from insane
    if (!s_probeTex) {
        s_probeFmt = D3DFMT_A32B32G32R32F;
        HRESULT hr = dev->CreateTexture(k_probeW, k_probeH, 1, D3DUSAGE_RENDERTARGET,
                                        s_probeFmt, D3DPOOL_DEFAULT, &s_probeTex, nullptr);
        if (FAILED(hr)) {
            s_probeFmt = D3DFMT_A8R8G8B8;
            hr = dev->CreateTexture(k_probeW, k_probeH, 1, D3DUSAGE_RENDERTARGET,
                                    s_probeFmt, D3DPOOL_DEFAULT, &s_probeTex, nullptr);
        }
        if (FAILED(hr)) { s_probeTex = nullptr; DebugLog("screenfx: probe RT creation failed"); return false; }
    }
    if (!s_probeSurf && FAILED(s_probeTex->GetSurfaceLevel(0, &s_probeSurf))) { s_probeSurf = nullptr; return false; }

    // GetRenderTargetData: dest SYSTEMMEM, same size + format
    if (!s_probeSys &&
        FAILED(dev->CreateOffscreenPlainSurface(k_probeW, k_probeH, s_probeFmt,
                                                D3DPOOL_SYSTEMMEM, &s_probeSys, nullptr)))
    { s_probeSys = nullptr; DebugLog("screenfx: probe sysmem surface failed"); return false; }

    return true;
}

struct FxProbeResult { float lo, hi, centre; int zeros, ones, n; };

static bool FxProbe(IDirect3DDevice9* dev, FxProbeResult& r)
{
    if (!ScreenFx_DepthTexture() || !FxEnsureProbe(dev)) return false;

    struct Vert { float x, y, z, rhw, u, v; };
    const float W = (float)k_probeW, H = (float)k_probeH;
    const Vert quad[4] = {
        { -0.5f,    -0.5f,     0.f, 1.f, 0.f, 0.f },
        { W - 0.5f, -0.5f,     0.f, 1.f, 1.f, 0.f },
        { -0.5f,    H - 0.5f,  0.f, 1.f, 0.f, 1.f },
        { W - 0.5f, H - 0.5f,  0.f, 1.f, 1.f, 1.f },
    };

    FxSavedState st{};
    FxSave(dev, st);

    // msaa depth bound -> SetRenderTarget refuses plain 64x36 target; unbind first
    IDirect3DSurface9* oldDepth = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&oldDepth))) oldDepth = nullptr;
    dev->SetDepthStencilSurface(nullptr);

    dev->SetRenderTarget(0, s_probeSurf);
    const D3DVIEWPORT9 vp{ 0, 0, k_probeW, k_probeH, 0.f, 1.f };
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
    dev->SetVertexShader(nullptr);
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetPixelShader(s_probePs);
    dev->SetTexture(0, ScreenFx_DepthTexture());
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    // POINT: INTZ via linear sampler silently returns 0 on some drivers
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);

    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vert));

    FxRestore(dev, st);
    dev->SetDepthStencilSurface(oldDepth);
    if (oldDepth) oldDepth->Release();

    if (FAILED(dev->GetRenderTargetData(s_probeSurf, s_probeSys))) return false;

    D3DLOCKED_RECT lr{};
    if (FAILED(s_probeSys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;

    r = { 1e9f, -1e9f, 0.f, 0, 0, 0 };
    for (int y = 0; y < k_probeH; ++y)
    {
        const uint8_t* row = (const uint8_t*)lr.pBits + (size_t)y * lr.Pitch;
        for (int x = 0; x < k_probeW; ++x)
        {
            const float d = (s_probeFmt == D3DFMT_A32B32G32R32F)
                ? ((const float*)row)[x * 4]                 // rgba32f, r first
                : row[x * 4 + 2] / 255.f;                    // a8r8g8b8 is b,g,r,a
            if (d < r.lo) r.lo = d;
            if (d > r.hi) r.hi = d;
            if (d <= 0.0001f) ++r.zeros;
            if (d >= 0.9999f) ++r.ones;
            if (y == k_probeH / 2 && x == k_probeW / 2) r.centre = d;
            ++r.n;
        }
    }
    s_probeSys->UnlockRect();
    return true;
}

// Health step, once per resolved frame. True = the passes may draw this frame.
static bool FxHealthStep(IDirect3DDevice9* dev, bool queued)
{
    const uint32_t epoch = g_matEpoch.load(std::memory_order_relaxed);
    if (epoch != s_healthEpoch || queued != s_lastQueued) {
        s_healthEpoch = epoch;
        s_lastQueued  = queued;
        FxHealthReset();
    }

    if (--s_probeWait > 0) return s_health == FxHealth::Live;

    FxProbeResult r{};
    if (!FxProbe(dev, r)) { s_probeWait = k_probeDead; return s_health == FxHealth::Live; }

    // live = enough taps neither far nor zero. Sky-only view has none -> dead needs several probes.
    const bool live = (r.n - r.ones - r.zeros) >= 8;
    const FxHealth before = s_health;

    if (live) {
        s_health     = FxHealth::Live;
        s_deadStreak = 0;
        s_probeWait  = k_probeLive;
        s_rebuildTries = 0;
    } else if (++s_deadStreak >= k_deadNeeded) {
        s_health    = FxHealth::Dead;
        s_probeWait = k_probeDead;
        // rebuild ONLY on live -> dead (route broke). Never-live = usually sky view; rebuild = hitch, no fix.
        if (before == FxHealth::Live)
            FxWantRebuild("the probe reads the depth as empty after it had been live");
        // caps can't tell which route driver really honours: try other one
        const bool other = s_useNvapi ? s_reszCap : s_nvapiOk;
        if (other) {
            s_useNvapi = !s_useNvapi;
            DebugLog("screenfx: depth dead on %s -- switching route to %s",
                     s_useNvapi ? "resz" : "nvapi", s_useNvapi ? "nvapi" : "resz");
            FxHealthReset();   // new route: clean verdict, probed next frame
        }
    } else {
        s_probeWait = k_probeUnknown;
    }

    if (s_health != before) {
        FxViewSetup view{};
        float lin = -1.f;
        if (ScreenFx_View(view)) {
            const float den = view.zFar - r.centre * (view.zFar - view.zNear);
            if (den > 0.0001f) lin = view.zNear * view.zFar / den;
        }
        DebugLog("^FX: depth %s -- min=%.4f max=%.4f centre=%.4f (%.0fu) far=%d zero=%d of %d, "
                 "route=%s %s msaa=%d",
                 s_health == FxHealth::Live ? "LIVE" : (s_health == FxHealth::Dead ? "DEAD" : "unknown"),
                 r.lo, r.hi, r.centre, lin, r.ones, r.zeros, r.n,
                 s_useNvapi ? "nvapi" : "resz", queued ? "queued" : "direct", (int)s_msaa);
    }
    return s_health == FxHealth::Live;
}

// Render thread, at post slot pos (direct or replayed by call queue).
static void FxRunPasses(IDirect3DDevice9* dev, bool queued)
{
    if (!dev || g_eject || (!g_aoEnable && !g_dofEnable && !g_fbEnable)) return;
    if (s_frame == s_resolvedFrm || !IsInGame()) return;

    // BEFORE anything touches texture: nothing of ours in flight here (same promise as h_Reset)
    if (s_rebuildWanted && (s_frame - s_rebuildFrame) > k_rebuildCooldown) {
        s_rebuildFrame = s_frame;
        ++s_rebuildTries;
        DebugLog("^FX: rebuilding the depth source (attempt %d)", s_rebuildTries);
        ScreenFx_Destroy();   // clears s_rebuildWanted; FxEnsure below remakes and re-registers
    }
    // off pass frees targets here, on drawing thread (no-op once empty). INTZ + fb targets stay
    // till reset/eject while all passes off: nothing runs here then to free them safely.
    if (!g_aoEnable)  Ssao_Destroy();
    if (!g_dofEnable) Dof_Destroy();

    // backbuffer target only: reflection/refraction/RT passes have foreign depth
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool onBB = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        onBB = (rt == bb);

    D3DSURFACE_DESC desc{};
    const bool described = bb && SUCCEEDED(bb->GetDesc(&desc));
    if (bb) bb->Release();

    IDirect3DSurface9* depthSurf = nullptr;
    if (!onBB || !described || FAILED(dev->GetDepthStencilSurface(&depthSurf)) || !depthSurf) {
        if (rt) rt->Release();
        return;
    }

    D3DSURFACE_DESC dsDesc{};
    if (SUCCEEDED(depthSurf->GetDesc(&dsDesc)))
        s_msaa = dsDesc.MultiSampleType != D3DMULTISAMPLE_NONE;

    if (FxEnsure(dev, desc))
    {
        s_resolvedFrm = s_frame;

        FxSavedState st{};
        FxSave(dev, st);
        if (s_useNvapi) FxResolveNvapi(dev, depthSurf);
        else            FxResolveResz(dev);
        FxRestore(dev, st);

        FxViewSetup view{};
        if (FxHealthStep(dev, queued) && ScreenFx_View(view))
        {
            s_liveFrame = s_frame;   // gun pass rides this verdict, no second probe

            // order: AO (sharp geometry) -> lens -> shutter. Gun not drawn yet (FxViewmodelPass).
            if (g_aoEnable)  Ssao_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view);
            if (g_dofEnable) Dof_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view);
            if (g_fbEnable)  FrameBlend_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view, false);
        }
    }

    depthSurf->Release();
    rt->Release();
}

// Gun pass. Post slot (viewrender.cpp:2157) runs before DrawViewModels (2160), so no gun there.
// Runs from ViewDrawFade (2185): gun drawn, hud not. Depth resolved AGAIN: only copy w/ gun, in
// 0..0.1 DepthRange slice (viewrender.cpp:1109).
static void FxViewmodelPass(IDirect3DDevice9* dev)
{
    if (!dev || g_eject || !g_fbEnable || !g_fbViewmodel) return;
    if (s_vmFrame == s_frame || !IsInGame()) return;

    // no proven depth this frame = no blur (FxRunPasses owns verdict)
    if (s_liveFrame != s_frame) return;

    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* bb = nullptr;
    bool onBB = false;
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
        SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        onBB = (rt == bb);

    D3DSURFACE_DESC desc{};
    const bool described = bb && SUCCEEDED(bb->GetDesc(&desc));
    if (bb) bb->Release();

    IDirect3DSurface9* depthSurf = nullptr;
    if (!onBB || !described || FAILED(dev->GetDepthStencilSurface(&depthSurf)) || !depthSurf) {
        if (rt) rt->Release();
        return;
    }

    if (FxEnsure(dev, desc))
    {
        s_vmFrame = s_frame;

        FxSavedState st{};
        FxSave(dev, st);
        if (s_useNvapi) FxResolveNvapi(dev, depthSurf);
        else            FxResolveResz(dev);
        FxRestore(dev, st);

        FxViewSetup view{};
        if (ScreenFx_View(view))
            FrameBlend_Draw(dev, rt, desc, ScreenFx_DepthTexture(), view, true);
    }

    depthSurf->Release();
    rt->Release();
}

// Call queue: IMaterialSystem::GetRenderContext 98, IMatRenderContext::GetCallQueue 147 (sdk header +
// IRefCounted's 2 slots; anchors SetRenderTarget 6, ClearBuffers 12, Viewport 38, CullMode 40,
// OverrideDepthEnable 74, DrawScreenSpaceRectangle 103, SetStencilEnable 117 match),
// ICallQueue::QueueFunctorInternal 0.
// Fake CFunctor vtable (tier1/functors.h): AddRef 0, Release 1, dtor 2, operator() 3. Never-freed
// static -> refs answer 1, dtor no-op. Queue calls operator() then Release, or only Release on flush.
inline constexpr int k_slotGetRenderContext = 98;
inline constexpr int k_slotGetCallQueue     = 147;

struct FxFunctor { void** vfptr; unsigned userId; };

static int   __fastcall FxFn_AddRef (void*)             { return 1; }
static int   __fastcall FxFn_Release(void*)             { return 1; }
static void* __fastcall FxFn_Dtor   (void* p, unsigned) { return p; }
static void  __fastcall FxFn_Call   (void*)
{
    s_cqRan = true;
    FxRunPasses(g_d3dDev, true);
}

// gun pass: own functor, queued from ViewDrawFade (after DrawViewModels)
static void __fastcall FxFn_CallVm(void*)
{
    s_cqRan = true;
    FxViewmodelPass(g_d3dDev);
}

// bloom + cc, queued from Push2DView: frame done, hud next
static void __fastcall FxFn_CallHud(void*)
{
    SceneFx_PreHud(g_d3dDev);
}

static void* s_fxFnVt[4] = { (void*)&FxFn_AddRef, (void*)&FxFn_Release, (void*)&FxFn_Dtor, (void*)&FxFn_Call };
static void* s_fxFnVmVt[4] = { (void*)&FxFn_AddRef, (void*)&FxFn_Release, (void*)&FxFn_Dtor, (void*)&FxFn_CallVm };
static void* s_fxFnHudVt[4] = { (void*)&FxFn_AddRef, (void*)&FxFn_Release, (void*)&FxFn_Dtor, (void*)&FxFn_CallHud };

static FxFunctor s_fxFunctor    = { s_fxFnVt,    0 };
static FxFunctor s_fxFunctorVm  = { s_fxFnVmVt,  1 };
static FxFunctor s_fxFunctorHud = { s_fxFnHudVt, 2 };

// Main thread. False = no queue (context not queued, or slots led nowhere).
static bool FxQueueFunctor(FxFunctor* fn)
{
    if (!g_matSystem) return false;
    __try {
        void** mvt = *(void***)g_matSystem;
        if (reinterpret_cast<uintptr_t>(mvt) < 0x10000) return false;
        // no Release: this build's GetRenderContext gives no ref (see flipworld.cpp)
        void* rc = ((void*(__fastcall*)(void*))mvt[k_slotGetRenderContext])(g_matSystem);
        if (reinterpret_cast<uintptr_t>(rc) < 0x10000) return false;

        void** rvt = *(void***)rc;
        if (reinterpret_cast<uintptr_t>(rvt) < 0x10000) return false;
        void* cq = ((void*(__fastcall*)(void*))rvt[k_slotGetCallQueue])(rc);
        if (reinterpret_cast<uintptr_t>(cq) < 0x10000) return false;

        void** qvt = *(void***)cq;
        if (reinterpret_cast<uintptr_t>(qvt) < 0x10000) return false;
        ((void(__fastcall*)(void*, void*))qvt[0])(cq, fn);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// CViewSetup offsets (public/view_shared.h); all members up to zFar 4 bytes -> x64 = 32-bit SDK
inline constexpr size_t k_vsWidth  = 16;
inline constexpr size_t k_vsHeight = 24;
inline constexpr size_t k_vsFov    = 56;
inline constexpr size_t k_vsFovVm  = 60;   // fovViewmodel
inline constexpr size_t k_vsOrigin = 64;   // Vector
inline constexpr size_t k_vsAngles = 76;   // QAngle
inline constexpr size_t k_vsZNear  = 88;
inline constexpr size_t k_vsZFar   = 92;
inline constexpr size_t k_vsZNearVm = 96;  // zNearViewmodel
inline constexpr size_t k_vsZFarVm  = 100;

static void FxReadViewSetup(const void* setup)
{
    const uintptr_t p = reinterpret_cast<uintptr_t>(setup);
    float fov = 0.f, zn = 0.f, zf = 0.f, fovVm = 0.f, znVm = 0.f, zfVm = 0.f;
    int   w = 0, h = 0;
    FxVec3 origin, angles;
    __try {
        fov = *reinterpret_cast<const float*>(p + k_vsFov);
        fovVm = *reinterpret_cast<const float*>(p + k_vsFovVm);
        zn  = *reinterpret_cast<const float*>(p + k_vsZNear);
        zf  = *reinterpret_cast<const float*>(p + k_vsZFar);
        znVm = *reinterpret_cast<const float*>(p + k_vsZNearVm);
        zfVm = *reinterpret_cast<const float*>(p + k_vsZFarVm);
        w   = *reinterpret_cast<const int*>  (p + k_vsWidth);
        h   = *reinterpret_cast<const int*>  (p + k_vsHeight);
        const float* o = reinterpret_cast<const float*>(p + k_vsOrigin);
        const float* a = reinterpret_cast<const float*>(p + k_vsAngles);
        origin = FxVec3(o[0], o[1], o[2]);
        angles = FxVec3(a[0], a[1], a[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    // out-of-range = offsets wrong
    const bool sane = fov > 1.f && fov < 179.f &&
                      zn  > 0.f && zn  < 64.f  &&
                      zf  > zn  && zf  < 1.0e6f &&
                      w   > 0   && h   > 0;
    if (!sane) return;

    // sub-views (water reflection, RT cameras) hit this slot smaller; main view = biggest
    if (w > s_maxSetupW) s_maxSetupW = w;
    if (w < s_maxSetupW) return;

    s_viewFov  = fov;
    s_viewNear = zn;
    s_viewFar  = zf;
    s_viewW    = w;
    s_viewH    = h;
    s_viewValid = true;

    // main thread only: focus trace walks collision world, fb reads entity list.
    // Gun has own fov + z range (own projection).
    Dof_UpdateFocus(origin, angles);

    const float fbOrigin[3] = { origin.x, origin.y, origin.z };
    const float fbAngles[3] = { angles.x, angles.y, angles.z };
    const bool  vmSane = (znVm > 0.f && zfVm > znVm && zfVm < 1.0e6f);
    FrameBlend_UpdateView(fbOrigin, fbAngles, fov, fovVm,
                          vmSane ? znVm : zn, vmSane ? zfVm : zf);
}

void ScreenFx_OnPostScreenSpaceEffects(const void* setup)
{
    if (g_eject || (!g_aoEnable && !g_dofEnable && !g_fbEnable)) return;
    if (setup) FxReadViewSetup(setup);
    if (!s_presentTid) return;

    // QUEUE FIRST, never "am I the presenting thread": map load presents from main thread, so
    // present tid is stale early in map -> one d3d9 device driven from two threads. Queued
    // context gives a call queue, single-threaded gives none.
    if (!s_cqRefused && FxQueueFunctor(&s_fxFunctor)) {
        // taken, never run = slots wrong for this build
        if (++s_cqQueued > 240 && !s_cqRan) {
            s_cqRefused = true;
            DebugLog("^FX: call queue took %d functors and ran none -- forcing mat_queue_mode 0", s_cqQueued);
        }
        return;
    }

    // no queue = single-threaded matsys, post slot IS render thread: draw here. Any other
    // thread must not touch d3d: skip, take mat_queue_mode 0.
    if (GetCurrentThreadId() != s_presentTid) {
        if (!s_cqRefused) {
            s_cqRefused = true;
            DebugLog("^FX: call queue refused -- forcing mat_queue_mode 0 for the depth passes");
        }
        return;
    }
    FxRunPasses(g_d3dDev, false);
}

// IVRenderView::ViewDrawFade slot 24 (public/ivrenderview.h, DrawBrushModel = 0, no virtual dtor;
// SetBlend 4 / GetBlend 5 from same count used by visuals.cpp). viewrender.cpp:2185: once per
// RenderView, after DrawViewModels + DrawUnderwaterOverlay, before engine post.
inline constexpr int k_slotViewDrawFade = 24;

using ViewDrawFade_t = void(__fastcall*)(void*, unsigned char*, void*);
static ViewDrawFade_t o_ViewDrawFade = nullptr;

static void FxViewmodelDispatch()
{
    // raw d3d off main thread while matsys records = runs inside PREVIOUS frame's playback.
    // Queued -> replays on render thread at this exact pos.
    if (!s_presentTid) return;

    if (GetCurrentThreadId() == s_presentTid) {
        FxViewmodelPass(g_d3dDev);
        return;
    }

    const bool queued = !s_cqRefused && FxQueueFunctor(&s_fxFunctorVm);

    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("^FX: gun pass %s from ViewDrawFade (thread %lu, render thread %lu)",
                 queued ? "QUEUED onto the render thread" : "REFUSED -- the gun stays sharp",
                 GetCurrentThreadId(), s_presentTid);
    }
}

// Main thread. Armed by ViewDrawFade, spent by first Push2DView after. Escape menu's 2nd push
// (view.cpp:1252, after hud) finds false, draws nothing.
static bool s_hudArmed = false;

static void __fastcall h_ViewDrawFade(void* thisptr, unsigned char* color, void* material)
{
    o_ViewDrawFade(thisptr, color, material);

    s_hudArmed = true;
    if (!g_eject && g_fbEnable && g_fbViewmodel) FxViewmodelDispatch();
}

// Push2DView: flipworld.cpp owns hook (slot 39, check_vtables.py; one MinHook per address), calls
// ScreenFx_OnPush2DView before original. viewrender.cpp:2298, after gun + engine post + overlays,
// before VGui_Paint 2401 = last point w/o hud.
static void FxPreHudDispatch()
{
    // queue first, else direct only on presenting thread. Miss -> Present fallback draws it.
    if (!s_presentTid) return;
    if (!s_cqRefused && FxQueueFunctor(&s_fxFunctorHud)) return;
    if (GetCurrentThreadId() == s_presentTid) SceneFx_PreHud(g_d3dDev);
}

// Main thread, BEFORE original Push2DView (it switches to hud 2D state).
void ScreenFx_OnPush2DView()
{
    if (!s_hudArmed) return;
    s_hudArmed = false;
    if (!g_eject && (g_bloomEnable || g_colorCorrEnable)) FxPreHudDispatch();
}

void ScreenFx_InstallViewmodelPass()
{
    if (o_ViewDrawFade || !g_renderView) return;

    void* fn = nullptr;
    __try {
        void** vt = *(void***)g_renderView;
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000) fn = vt[k_slotViewDrawFade];
    } __except(EXCEPTION_EXECUTE_HANDLER) { fn = nullptr; }

    if (fn && MH_CreateHook(fn, (LPVOID)h_ViewDrawFade, (LPVOID*)&o_ViewDrawFade) == MH_OK) {
        MH_EnableHook(fn);
        DebugLog("screenfx: IVRenderView::ViewDrawFade hook installed at %p (the gun's blur pass)", fn);
    } else {
        o_ViewDrawFade = nullptr;
        DebugLog("screenfx: ViewDrawFade hook FAILED -- the viewmodel will not blend, bloom/cc stay over the hud");
    }
}

// mat_queue_mode fallback, only when queue refused. Via ClientCmd_Unrestricted (slot 106): change
// callback applies mode, raw value write does nothing.
static bool s_queueForced   = false;
static int  s_queueOriginal = -1;

static void FxEngineExec(const char* cmd)
{
    if (!g_engine) return;
    __try {
        void** vt = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vt) < 0x10000) return;
        if (reinterpret_cast<uintptr_t>(vt[106]) < 0x10000) return;
        ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, cmd);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static ConVar* FxQueueCvar()
{
    static ConVar* s_cv    = nullptr;
    static int     s_tries = 0;
    if (!s_cv && g_cvar && s_tries < 32) { ++s_tries; s_cv = g_cvar->FindVarWalk("mat_queue_mode"); }
    return s_cv;
}

static void FxQueueModePutBack(const char* why)
{
    if (!s_queueForced) return;
    s_queueForced = false;
    // SetValueInt first: ClientCmd waits for next Cbuf_Execute, map change has no frames till
    // load done -> put-back lands too late. SetValue fires same change callback.
    ConVar* cv = FxQueueCvar();
    if (!cv || !cv->SetValueInt("mat_queue_mode", s_queueOriginal)) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "mat_queue_mode %d", s_queueOriginal);
        FxEngineExec(cmd);
    }
    DebugLog("screenfx: mat_queue_mode put back to %d (%s)", s_queueOriginal, why);
}

// Map change: give mode back before load. ScreenFx_Tick can't: rides Present, which stops till map in.
void ScreenFx_LevelShutdown()
{
    FxQueueModePutBack("map change");

    // fb keys poses by model ptr; map change recycles model_t addresses. Camera history = teleport.
    FrameBlend_Reset();
}

void ScreenFx_Tick()
{
    ConVar* s_cv = FxQueueCvar();
    if (!s_cv) return;

    // hold only in a map: mode 0 through load = single-threaded precache (slow load).
    // Web surface shares same queue fallback -> one hold covers both.
    const bool want = (s_cqRefused && (g_aoEnable || g_dofEnable || g_fbEnable) && IsInGame()) ||
                      Web_WantsSingleThreadedMat();
    if (!want) { FxQueueModePutBack("no map or depth passes off"); return; }

    const int now = (int)s_cv->GetFloat();
    if (!s_queueForced) {
        s_queueForced   = true;
        s_queueOriginal = now;
        DebugLog("screenfx: forcing mat_queue_mode 0 (was %d)", now);
    }
    // re-asserted: config exec / video menu put it back
    if (now != 0) FxEngineExec("mat_queue_mode 0");
}

void ScreenFx_RestoreQueueMode()
{
    FxQueueModePutBack("eject");
}

const char* ScreenFx_Status()
{
    if (!g_aoEnable && !g_dofEnable && !g_fbEnable) return "";
    if (!IsInGame())                 return "Depth: waiting for a map";
    if (s_texW && !FxHasRoute()) return "Depth: this GPU has no depth resolve (RESZ/NvAPI)";
    switch (s_health) {
    case FxHealth::Live:
        return s_lastQueued ? "Depth: OK (queued on render thread)" : "Depth: OK";
    case FxHealth::Dead:
        return s_msaa ? "Depth: unreadable, effects paused. Try Antialiasing off"
                      : "Depth: unreadable, effects paused";
    default:
        return "Depth: checking...";
    }
}
