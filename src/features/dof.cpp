#include "screenfx.h"
#include "fx_dof_prepare.h"
#include "fx_dof_gather.h"
#include "fx_dof_coc.h"
#include "fx_dof_near.h"
#include "fx_dof_smooth.h"

// Focus trace engine structs (public/cmodel.h, public/engine/IEngineTrace.h). IEngineTrace::TraceRay = slot 4.
inline constexpr int kTrace_TraceRay = 4;

struct DofPlane   { FxVec3 normal; float dist; unsigned char type, signbits, pad[2]; };
struct DofSurface { const char* name; short surfaceProps; unsigned short flags; };

struct GameTrace_t
{
    FxVec3     startpos;
    FxVec3     endpos;
    DofPlane   plane;
    float      fraction = 1.f;
    int        contents = 0;
    unsigned short dispFlags = 0;
    bool       allsolid = false;
    bool       startsolid = false;
    float      fractionleftsolid = 0.f;
    DofSurface surface{};
    int        hitgroup = 0;
    short      physicsbone = 0;
    void*      m_pEnt = nullptr;
    int        hitbox = 0;
};

// 16-aligned: engine reads Ray_t SIMD-wide
struct __declspec(align(16)) DofVecAligned {
    float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
    void Set(float X = 0.f, float Y = 0.f, float Z = 0.f) { x = X; y = Y; z = Z; }
    DofVecAligned& operator=(const FxVec3& o) { Set(o.x, o.y, o.z); return *this; }
};

struct Ray_mv
{
    DofVecAligned m_Start, m_Delta, m_StartOffset, m_Extents;
    bool m_IsRay = true, m_IsSwept = true;

    void Init(const FxVec3& start, const FxVec3& end)
    {
        const FxVec3 d = end - start;
        m_Delta.Set(d.x, d.y, d.z);
        m_IsSwept = (d.x * d.x + d.y * d.y + d.z * d.z) != 0.f;
        m_Extents.Set();
        m_IsRay = true;
        m_StartOffset.Set();
        m_Start.Set(start.x, start.y, start.z);
    }
};

// Depth of field off real z buffer. Ported from coldtires (depth_of_field.cpp): gather = qUINT ADoF,
// near plane = CinematicDOF. Engine DoDepthOfField unreachable: needs HDR_TYPE_FLOAT = mat_hdr_level 3,
// clamped to 2 (matsys_interface.cpp:210); its dest-alpha depth is 8 bit over 192u.
// Passes: prepare (frame + SIGNED coc in alpha) -> coc (near field, half res, separable; near bleed
// only) -> gather (far, spiral disc) -> near (fg over far) -> smooth (separable gaussian fills gaps).

enum DofShader { DOF_PREPARE = 0, DOF_GATHER, DOF_COC, DOF_NEAR, DOF_SMOOTH, DOF_SHADER_MAX };

static IDirect3DPixelShader9* s_ps[DOF_SHADER_MAX] = {};
static bool s_shaderFailed = false;

// frame -> stage (bb format for StretchRect) -> prepare packs colour + coc into col alpha
static IDirect3DTexture9* s_stageTex  = nullptr;
static IDirect3DSurface9* s_stageSurf = nullptr;
static IDirect3DTexture9* s_colTex    = nullptr;
static IDirect3DSurface9* s_colSurf   = nullptr;
static IDirect3DTexture9* s_blurTex   = nullptr;
static IDirect3DSurface9* s_blurSurf  = nullptr;
static IDirect3DTexture9* s_cocTex    = nullptr;
static IDirect3DSurface9* s_cocSurf   = nullptr;
static IDirect3DTexture9* s_cocTmpTex = nullptr;
static IDirect3DSurface9* s_cocTmpSurf= nullptr;

// msaa only: can't copy msaa surface to texture; resolve into plain RT first
static IDirect3DSurface9* s_resolveSurf = nullptr;

static int       s_w = 0, s_h = 0, s_cocW = 0, s_cocH = 0;
static D3DFORMAT s_fmt = D3DFMT_UNKNOWN;
static D3DMULTISAMPLE_TYPE s_msaa = D3DMULTISAMPLE_NONE;

// Smoothed focus: written by Dof_UpdateFocus (main thread), read by draw.
static volatile float s_focus = 0.f;
static void*  s_engineTrace   = nullptr;
static double s_lastFocusTime = 0.0;

void Dof_Destroy()
{
    if (s_resolveSurf) { s_resolveSurf->Release(); s_resolveSurf = nullptr; }
    if (s_cocTmpSurf)  { s_cocTmpSurf->Release();  s_cocTmpSurf  = nullptr; }
    if (s_cocTmpTex)   { s_cocTmpTex->Release();   s_cocTmpTex   = nullptr; }
    if (s_cocSurf)     { s_cocSurf->Release();     s_cocSurf     = nullptr; }
    if (s_cocTex)      { s_cocTex->Release();      s_cocTex      = nullptr; }
    if (s_blurSurf)    { s_blurSurf->Release();    s_blurSurf    = nullptr; }
    if (s_blurTex)     { s_blurTex->Release();     s_blurTex     = nullptr; }
    if (s_colSurf)     { s_colSurf->Release();     s_colSurf     = nullptr; }
    if (s_colTex)      { s_colTex->Release();      s_colTex      = nullptr; }
    if (s_stageSurf)   { s_stageSurf->Release();   s_stageSurf   = nullptr; }
    if (s_stageTex)    { s_stageTex->Release();    s_stageTex    = nullptr; }
    for (auto& ps : s_ps) { if (ps) { ps->Release(); ps = nullptr; } }
    s_w = s_h = s_cocW = s_cocH = 0;
    s_fmt  = D3DFMT_UNKNOWN;
    s_msaa = D3DMULTISAMPLE_NONE;
}

static IDirect3DPixelShader9* DofShaderFor(IDirect3DDevice9* dev, DofShader which)
{
    if (s_ps[which]) return s_ps[which];
    if (s_shaderFailed) return nullptr;

    const BYTE* bytes = nullptr;
    size_t size = 0;
    switch (which) {
        case DOF_PREPARE: bytes = k_fxDofPrepareBytes; size = sizeof(k_fxDofPrepareBytes); break;
        case DOF_GATHER:  bytes = k_fxDofGatherBytes;  size = sizeof(k_fxDofGatherBytes);  break;
        case DOF_COC:     bytes = k_fxDofCocBytes;     size = sizeof(k_fxDofCocBytes);     break;
        case DOF_NEAR:    bytes = k_fxDofNearBytes;    size = sizeof(k_fxDofNearBytes);    break;
        default:          bytes = k_fxDofSmoothBytes;  size = sizeof(k_fxDofSmoothBytes);  break;
    }

    std::vector<DWORD> code((size + 3) / 4);
    memcpy(code.data(), bytes, size);

    if (FAILED(dev->CreatePixelShader(code.data(), &s_ps[which])) || !s_ps[which]) {
        s_ps[which] = nullptr;
        s_shaderFailed = true;
        DebugLog("dof: pixel shader %d refused by the card -- depth of field is off", (int)which);
        return nullptr;
    }
    DebugLog("dof: shader %d built", (int)which);
    return s_ps[which];
}

static bool DofBuild(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    const int width = (int)desc.Width, height = (int)desc.Height;

    if (s_colTex && s_w == width && s_h == height && s_fmt == desc.Format && s_msaa == desc.MultiSampleType)
        return true;

    Dof_Destroy();

    // stage = RT format (StretchRect). col forced A8R8G8B8: bb usually X8, no alpha for coc.
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                  D3DPOOL_DEFAULT, &s_stageTex, nullptr)) ||
        FAILED(s_stageTex->GetSurfaceLevel(0, &s_stageSurf)))
    { DebugLog("dof: stage target creation failed"); Dof_Destroy(); return false; }

    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_colTex, nullptr)) ||
        FAILED(s_colTex->GetSurfaceLevel(0, &s_colSurf)))
    { DebugLog("dof: colour target creation failed"); Dof_Destroy(); return false; }

    // gather target when passes follow; A8R8G8B8 so coc alpha survives
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_blurTex, nullptr)) ||
        FAILED(s_blurTex->GetSurfaceLevel(0, &s_blurSurf)))
    { DebugLog("dof: blur target creation failed"); Dof_Destroy(); return false; }

    // near coc field: half res (blur of blur radius), fp16 so bleed doesn't band. Missing = no bleed only.
    const UINT cocW = (desc.Width  / 2u) < 1u ? 1u : desc.Width  / 2u;
    const UINT cocH = (desc.Height / 2u) < 1u ? 1u : desc.Height / 2u;

    for (const D3DFORMAT fmt : { D3DFMT_A16B16G16R16F, D3DFMT_A8R8G8B8 })
    {
        if (SUCCEEDED(dev->CreateTexture(cocW, cocH, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &s_cocTex, nullptr)) &&
            SUCCEEDED(s_cocTex->GetSurfaceLevel(0, &s_cocSurf)) &&
            SUCCEEDED(dev->CreateTexture(cocW, cocH, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &s_cocTmpTex, nullptr)) &&
            SUCCEEDED(s_cocTmpTex->GetSurfaceLevel(0, &s_cocTmpSurf)))
            break;

        if (s_cocTmpSurf) { s_cocTmpSurf->Release(); s_cocTmpSurf = nullptr; }
        if (s_cocTmpTex)  { s_cocTmpTex->Release();  s_cocTmpTex  = nullptr; }
        if (s_cocSurf)    { s_cocSurf->Release();    s_cocSurf    = nullptr; }
        if (s_cocTex)     { s_cocTex->Release();     s_cocTex     = nullptr; }
    }
    if (!s_cocSurf || !s_cocTmpSurf)
        DebugLog("dof: near plane coc target unavailable -- no near bleed");

    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE &&
        FAILED(dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE,
                                       0, FALSE, &s_resolveSurf, nullptr)))
    { DebugLog("dof: msaa resolve target creation failed"); Dof_Destroy(); return false; }

    s_w = width; s_h = height; s_cocW = (int)cocW; s_cocH = (int)cocH;
    s_fmt = desc.Format; s_msaa = desc.MultiSampleType;
    DebugLog("dof: built %dx%d msaa %d coc %dx%d", width, height, (int)desc.MultiSampleType, s_cocW, s_cocH);
    return true;
}

// ITraceFilter: 2 virtuals, no dtor: 0 ShouldHitEntity, 1 GetTraceType. No team rules.
struct DofFocusFilter
{
    void** vfptr;
    void*  pSkip;

    static bool __fastcall sShouldHit(DofFocusFilter* self, void* pEnt, int)
    {
        return pEnt && pEnt != self->pSkip;
    }
    // TRACE_EVERYTHING: hitboxes too, head in front of wall gets focus
    static int __fastcall sTraceType(DofFocusFilter*) { return 0; }

    void Setup(void* skip)
    {
        static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType };
        vfptr = vt;
        pSkip = skip;
    }
};

// SOLID | MOVEABLE | MONSTER | HITBOX. No window/grate/debris: glass can't steal focus.
inline constexpr unsigned k_dofFocusMask = 0x1 | 0x4000 | 0x2000000 | 0x40000000;

static void DofTraceLine(const FxVec3& start, const FxVec3& end, void* filter, GameTrace_t* tr)
{
    tr->fraction = 1.f;
    tr->endpos   = end;
    if (!s_engineTrace) return;

    Ray_mv ray;
    ray.Init(start, end);
    using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
    __try {
        void** vt = *(void***)s_engineTrace;
        ((TraceRay_t)vt[kTrace_TraceRay])(s_engineTrace, ray, k_dofFocusMask, filter, tr);
    } __except(EXCEPTION_EXECUTE_HANDLER) { tr->fraction = 1.f; tr->endpos = end; }
}

// AngleVectors, forward only
static FxVec3 DofForward(const FxVec3& ang)
{
    const float k = 3.14159265f / 180.f;
    const float sp = sinf(ang.x * k), cp = cosf(ang.x * k);
    const float sy = sinf(ang.y * k), cy = cosf(ang.y * k);
    return FxVec3(cp * cy, cp * sy, -sp);
}

// Main thread (post slot): entity list + collision belong here.
void Dof_UpdateFocus(const FxVec3& origin, const FxVec3& angles)
{
    if (!g_dofEnable) return;

    float target = g_dofDistance;

    if (g_dofAutoFocus)
    {
        if (!s_engineTrace) s_engineTrace = GetInterface("engine.dll", "EngineTraceClient003");

        const FxVec3 fwd = DofForward(angles);

        void* local = nullptr;
        const int idx = GetLocalPlayerIndex();
        if (idx > 0) local = GetClientEntity(idx);

        DofFocusFilter filter; filter.Setup(local);
        GameTrace_t tr{};
        DofTraceLine(origin, origin + fwd * 8192.f, &filter, &tr);

        // view-space DEPTH, not distance: project hit onto forward
        const FxVec3  hit  = tr.endpos - origin;
        const float dist = hit.x * fwd.x + hit.y * fwd.y + hit.z * fwd.z;
        const float lim  = (g_dofFocusLimit < 64.f) ? 64.f : (g_dofFocusLimit > 8192.f ? 8192.f : g_dofFocusLimit);
        target = (dist < 16.f) ? 16.f : (dist > lim ? lim : dist);
    }

    if (s_focus <= 0.f) s_focus = target;

    // exp approach on AnimTime: fps-independent, host_framerate recordings pull at playback speed
    const double t  = AnimTime();
    double dt = (s_lastFocusTime > 0.0) ? (t - s_lastFocusTime) : 0.0;
    s_lastFocusTime = t;
    if (dt < 0.0 || dt > 0.25) dt = 0.25;

    const float speed = (g_dofFocusSpeed < 0.1f) ? 0.1f : (g_dofFocusSpeed > 40.f ? 40.f : g_dofFocusSpeed);
    const float rate  = (dt > 0.0) ? 1.f - expf(-speed * (float)dt) : 1.f;

    s_focus += (target - s_focus) * rate;
}

struct DofSaved
{
    IDirect3DSurface9*           depthStencil;
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex[3];
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    UINT  streamOffset, streamStride;
    DWORD fvf;
    D3DVIEWPORT9 vp;
    DWORD rs[13];
    DWORD samp[3][6];
    float psc[6][4];   // c0..c5
};

static const D3DRENDERSTATETYPE k_dofRS[13] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_STENCILENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE,
    // RESZ resolve writes its trigger here
    D3DRS_POINTSIZE, D3DRS_SEPARATEALPHABLENDENABLE,
};
static const D3DSAMPLERSTATETYPE k_dofSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void DofSave(IDirect3DDevice9* dev, DofSaved& s)
{
    if (FAILED(dev->GetDepthStencilSurface(&s.depthStencil))) s.depthStencil = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 13; ++i) dev->GetRenderState(k_dofRS[i], &s.rs[i]);
    for (int u = 0; u < 3; ++u) {
        dev->GetTexture(u, &s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_dofSamp[i], &s.samp[u][i]);
    }
    dev->GetPixelShaderConstantF(0, s.psc[0], 6);
}

static void DofRestore(IDirect3DDevice9* dev, DofSaved& s)
{
    dev->SetPixelShaderConstantF(0, s.psc[0], 6);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    if (s.fvf) dev->SetFVF(s.fvf);
    dev->SetVertexDeclaration(s.decl);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);
    for (int u = 0; u < 3; ++u) {
        dev->SetTexture(u, s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_dofSamp[i], s.samp[u][i]);
    }
    for (int i = 0; i < 13; ++i) dev->SetRenderState(k_dofRS[i], s.rs[i]);
    dev->SetViewport(&s.vp);
    dev->SetDepthStencilSurface(s.depthStencil);

    if (s.depthStencil) s.depthStencil->Release();
    if (s.decl)         s.decl->Release();
    for (auto* t : s.tex) if (t) t->Release();
    if (s.vs)      s.vs->Release();
    if (s.ps)      s.ps->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
}

void Dof_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
              const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
              const FxViewSetup& view)
{
    if (!dev || !target || !depth || s_shaderFailed) return;
    if (!ScreenFx_QuadReady(dev)) return;
    if (!DofBuild(dev, desc)) return;

    IDirect3DPixelShader9* prepare = DofShaderFor(dev, DOF_PREPARE);
    IDirect3DPixelShader9* gather  = DofShaderFor(dev, DOF_GATHER);
    if (!prepare || !gather) return;

    const bool wantNear   = g_dofNearBlur && s_cocSurf && s_cocTmpSurf;
    const bool wantSmooth = g_dofSmooth;

    IDirect3DPixelShader9* cocShader    = wantNear   ? DofShaderFor(dev, DOF_COC)    : nullptr;
    IDirect3DPixelShader9* nearShader   = wantNear   ? DofShaderFor(dev, DOF_NEAR)   : nullptr;
    IDirect3DPixelShader9* smoothShader = wantSmooth ? DofShaderFor(dev, DOF_SMOOTH) : nullptr;
    const bool nearOn = cocShader && nearShader;

    // must be frame sized, else not scene depth
    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("dof: depth target size does not match the frame"); }
        return;
    }

    // msaa: resolve into plain surface, then copy
    HRESULT blit = dev->StretchRect(target, nullptr, s_resolveSurf ? s_resolveSurf : s_stageSurf,
                                    nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(blit) && s_resolveSurf)
        blit = dev->StretchRect(s_resolveSurf, nullptr, s_stageSurf, nullptr, D3DTEXF_NONE);
    if (FAILED(blit)) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("dof: frame blit failed 0x%08lx", (unsigned long)blit); }
        return;
    }

    DofSaved st{};
    DofSave(dev, st);

    const float width = (float)s_w, height = (float)s_h;
    const D3DVIEWPORT9 fullVp{ 0, 0, (DWORD)s_w, (DWORD)s_h, 0.f, 1.f };
    const D3DVIEWPORT9 cocVp { 0, 0, (DWORD)s_cocW, (DWORD)s_cocH, 0.f, 1.f };

    dev->SetDepthStencilSurface(nullptr);
    dev->SetViewport(&fullVp);
    ScreenFx_BindQuad(dev);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);

    // s0 frame linear; s1 INTZ POINT (not filterable); s2 half-res coc linear (else 2x2 blocks)
    for (DWORD u = 0; u < 3; ++u) {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
        const DWORD filt = (u == 1) ? D3DTEXF_POINT : D3DTEXF_LINEAR;
        dev->SetSamplerState(u, D3DSAMP_MINFILTER, filt);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER, filt);
    }
    dev->SetTexture(1, depth);

    const float focus     = s_focus;
    const float ramp      = (g_dofRamp < 1.f) ? 1.f : g_dofRamp;
    const float halfBand  = g_dofRange * 0.5f;
    const float maxBlur   = (g_dofMaxBlur < 0.f) ? 0.f : g_dofMaxBlur;

    // near ramp capped at room between eye and band, so near side hits full blur at eye
    const float nearRamp = std::max(std::min(ramp, focus - halfBand), 1.f);

    const float edgeSoftness = 1.f;   // surfaces behind focal plane mix freely

    const float block[4][4] = {
        { view.zNear, view.zFar, focus, 1.f / ramp },
        { halfBand, maxBlur, 1.f / width, 1.f / height },
        { g_dofViewmodelSharp ? 0.1f : 0.f, g_dofNearBlur ? 1.f : 0.f, 0.f, 1.f },   // nearOn implies g_dofNearBlur
        { g_dofBokehBoost, g_dofBokehThreshold, edgeSoftness, 1.f / nearRamp },
    };
    dev->SetPixelShaderConstantF(0, block[0], 4);

    // c5.x near plane in pass after gather; c5.y gather taps per px radius (smooth fills gaps -> half)
    const float c5[4] = { nearOn ? 1.f : 0.f, smoothShader ? 1.25f : 2.5f, 0.f, 0.f };
    dev->SetPixelShaderConstantF(5, c5, 1);

    const DWORD colourOnly = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;

    // prepare
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
    dev->SetRenderTarget(0, s_colSurf);
    dev->SetTexture(0, s_stageTex);
    dev->SetPixelShader(prepare);
    ScreenFx_DrawQuad(dev, s_w, s_h);

    // near coc field, while col target still holds prepared frame
    if (nearOn) {
        const float cocH[4] = { 1.f, 0.f, 1.f, 0.f };   // z = pass that unpacks prepared alpha
        const float cocV[4] = { 0.f, 1.f, 0.f, 0.f };

        dev->SetPixelShader(cocShader);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
        dev->SetViewport(&cocVp);
        dev->SetRenderTarget(0, s_cocTmpSurf);
        dev->SetTexture(0, s_colTex);
        dev->SetPixelShaderConstantF(4, cocH, 1);
        ScreenFx_DrawQuad(dev, s_cocW, s_cocH);

        dev->SetRenderTarget(0, s_cocSurf);
        dev->SetViewport(&cocVp);
        dev->SetTexture(0, s_cocTmpTex);
        dev->SetPixelShaderConstantF(4, cocV, 1);
        ScreenFx_DrawQuad(dev, s_cocW, s_cocH);
    }

    // gather; with near on = far plane only, into blur target
    const bool gatherToFrame = !nearOn && !smoothShader;

    dev->SetRenderTarget(0, gatherToFrame ? target : s_blurSurf);
    dev->SetViewport(&fullVp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, gatherToFrame ? colourOnly : 0x0f);
    dev->SetTexture(0, s_colTex);
    dev->SetPixelShader(gather);
    ScreenFx_DrawQuad(dev, s_w, s_h);

    // smooth src + scratch; near pass swaps them (reads far from blur target)
    IDirect3DTexture9* smoothSrc     = s_blurTex;
    IDirect3DSurface9* smoothScratch = s_colSurf;
    IDirect3DTexture9* smoothScratchTex = s_colTex;

    if (nearOn) {
        const bool nearToFrame = !smoothShader;

        // textures BEFORE target: col surface still bound on s0 from gather; d3d9 refuses read+write
        dev->SetTexture(0, s_blurTex);
        dev->SetTexture(2, s_cocTex);
        dev->SetRenderTarget(0, nearToFrame ? target : s_colSurf);
        dev->SetViewport(&fullVp);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, nearToFrame ? colourOnly : 0x0f);
        dev->SetPixelShader(nearShader);
        ScreenFx_DrawQuad(dev, s_w, s_h);

        smoothSrc        = s_colTex;
        smoothScratch    = s_blurSurf;
        smoothScratchTex = s_blurTex;
    }

    // smooth: horizontal into free target, vertical onto frame
    if (smoothShader) {
        // bleed = ADoF factor. amount follows blur radius (reach = 2*coc*amount), fixed = smooth IS blur at small max
        const float bleed = 1.f - edgeSoftness;
        const float amount = (maxBlur * 0.5f > 4.f) ? 4.f : maxBlur * 0.5f;

        const float horizontal[4] = { 1.f, 0.f, bleed, amount };
        const float vertical[4]   = { 0.f, 1.f, bleed, amount };

        dev->SetPixelShader(smoothShader);
        // sampler before target again (they swap)
        dev->SetTexture(0, smoothSrc);
        dev->SetRenderTarget(0, smoothScratch);
        dev->SetViewport(&fullVp);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);
        dev->SetPixelShaderConstantF(4, horizontal, 1);
        ScreenFx_DrawQuad(dev, s_w, s_h);

        dev->SetTexture(0, smoothScratchTex);
        dev->SetRenderTarget(0, target);
        dev->SetViewport(&fullVp);
        // frame alpha = engine dest-alpha depth: colour only
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, colourOnly);
        dev->SetPixelShaderConstantF(4, vertical, 1);
        ScreenFx_DrawQuad(dev, s_w, s_h);
    }

    DofRestore(dev, st);

    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        DebugLog("^FX: dof drew %dx%d focus=%.1f zn=%.2f zf=%.0f blur=%.1f near=%d smooth=%d",
                 s_w, s_h, focus, view.zNear, view.zFar, maxBlur, (int)nearOn, (int)(smoothShader != nullptr));
    }
}
