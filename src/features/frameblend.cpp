#include "screenfx.h"
#include "fbbones.h"
#include "fbmath.h"
#include "fx_fb_velocity.h"
#include "fx_fb_tile_h.h"
#include "fx_fb_tile_v.h"
#include "fx_fb_neighbour.h"
#include "fx_fb_gather.h"

#include <atomic>
#include <math.h>
#include <string.h>
#include <vector>

// Frame blending = per-pixel motion blur off readable z (screenfx.cpp). Ported from coldtires
// (true_motion_blur.cpp). Engine mat_motion_blur = one vector per frame, useless here.
// Velocity = unproject depth through LAST frame's camera. Filter: McGuire 2012 reconstruction +
// Guertin 2014 weighting: velocity buffer -> KxK max tiles -> 5x5 neighbour max (blur must cross
// own edge) -> gather. Taps follow streak length (capped); exposure = shutter time / frametime.
// Runs TWICE a frame:
//   world run  post slot, before DrawViewModels (viewrender.cpp:2160): camera reproj + player boxes
//   gun run    ViewDrawFade (gun drawn, no hud): gun velocity from BONES (fbbones.h), so reload/swing
//              smears, own walk/turn doesn't. Split makes the fence cheap.

// collision hull is what player OCCUPIES, not looks like; too little margin leaves held weapon sharp
static constexpr float k_playerMargin = 12.f;

// floor tight: widening down drags ground along with him
static constexpr float k_playerFloor = 2.f;

// teleport / respawn / slot change would streak across map
static constexpr float k_playerTeleport = 256.f;

// standing hull VEC_HULL_MIN/MAX; crouch only shorter
static constexpr float k_hullMins[3] = { -24.f, -24.f, 0.f };
static constexpr float k_hullMaxs[3] = {  24.f,  24.f, 82.f };

static constexpr int k_maxPlayers    = 10;   // five constant registers each
static constexpr int k_playerSlots   = 65;   // by entity index
static constexpr int k_fbRegisters   = 130;  // c0..c129: ps_3_0 allows 224

// must match fx_fb_velocity.hlsl (players c16, bones c66 x4 regs); tests/fbmath_test.cpp checks
static constexpr int k_boneBase   = 66;
static constexpr int k_playerBase = 16;

// Main thread computes whole reprojection; draw thread gets numbers only (view setup gone by then).
struct FbFrame
{
    bool  valid = false;      // false = history is a lie (first frame, teleport, stall)
    int   frame = 0;

    float fov     = 90.f;
    float prevFov = 90.f;

    float fovVm   = 54.f;     // gun's own projection
    float zNearVm = 1.f;
    float zFarVm  = 1000.f;

    float reproj[3][4] = {};       // rows of prev_view^T * cur_view, eye move in .w
    float viewToWorld[3][4] = {};  // camera axes as columns, eye in .w (player box test)

    float basis[3][3] = {};        // camera, for bone conversion
    float origin[3]   = {};

    float exposure = 0.f;     // frames of travel shutter is open: shutter / frametime

    // gun: uncapped by world amount; transforms RUN for shutter (true arc, not stretched chord)
    float gunExposure = 0.f;

    int   playerCount = 0;
    float playerBox[k_maxPlayers][2][4] = {};
    float playerRows[k_maxPlayers][3][4] = {};
};

// Main writes one slot, render reads other.
// ponytail: torn read = one frame wrong velocity; 3 slots if a stall ever laps reader.
static FbFrame          s_slot[2];
static std::atomic<int> s_ready{ -1 };

static bool   s_havePrev   = false;
static double s_prevTime   = 0.0;
static float  s_prevFov    = 90.f;
static float  s_prevOrigin[3] = {};
static float  s_prevBasis[3][3] = {};

// player pose LAST frame by ent index + frame written; frame != prev = no history (dormant/spawn/new owner)
static int   s_playerFrame[k_playerSlots] = {};
static float s_playerPose[k_playerSlots][3] = {};

static std::atomic<int> s_frameId{ 0 };

enum FbShader { FB_VELOCITY = 0, FB_TILE_H, FB_TILE_V, FB_NEIGHBOUR, FB_GATHER, FB_SHADER_MAX };

static IDirect3DPixelShader9* s_ps[FB_SHADER_MAX] = {};

static IDirect3DTexture9* s_stageTex  = nullptr;   // frame copy, target's format
static IDirect3DSurface9* s_stageSurf = nullptr;

static IDirect3DSurface9* s_resolveSurf = nullptr; // msaa only: resolve hop to stage

static IDirect3DTexture9* s_infoTex  = nullptr;    // velocity buffer
static IDirect3DSurface9* s_infoSurf = nullptr;

static IDirect3DTexture9* s_tileRowTex  = nullptr;
static IDirect3DSurface9* s_tileRowSurf = nullptr;
static IDirect3DTexture9* s_tileTex     = nullptr;
static IDirect3DSurface9* s_tileSurf    = nullptr;
static IDirect3DTexture9* s_nbrTex      = nullptr;
static IDirect3DSurface9* s_nbrSurf     = nullptr;

static int  s_w = 0, s_h = 0;
static int  s_tileSize = 0, s_tileW = 0, s_tileH = 0;
static D3DFORMAT s_fmt = D3DFMT_UNKNOWN;
static D3DMULTISAMPLE_TYPE s_msaa = D3DMULTISAMPLE_NONE;
static bool s_shaderFailed = false;

void FrameBlend_NewFrame()
{
    s_frameId.fetch_add(1, std::memory_order_relaxed);
}

void FrameBlend_Reset()
{
    s_havePrev = false;
    s_ready.store(-1, std::memory_order_release);
    FbBones::Flush();
}

__declspec(noinline) static bool FbReadOrigin(const void* ent, float out[3])
{
    __try {
        const float* o = reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(ent) + (size_t)g_offOrigin);
        out[0] = o[0]; out[1] = o[1]; out[2] = o[2];
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// Other players: camera reproj only knows the EYE. No velocity-target shader in engine, so player =
// world-space box tested per pixel. RIGID translation only; turn left out on purpose (legs follow
// feet yaw, chest follows eyes: no single rotation, mouse flick would streak a still man).
static void FbCollectPlayers(FbFrame& f, const float basis[3][3], const float origin[3])
{
    fbm::ViewToWorld(basis, origin, f.viewToWorld);

    if (!g_fbPlayers || g_offOrigin <= 0) return;

    struct Entry {
        float distSq;
        float box[2][4];
        float rows[3][4];
    };

    Entry entries[k_playerSlots] = {};
    int   total = 0;

    const int local = GetLocalPlayerIndex();

    for (int idx = 1; idx < k_playerSlots; ++idx)
    {
        if (idx == local) continue;

        void* ent = GetClientEntity(idx);
        if (!ent) continue;

        float where[3];
        if (!FbReadOrigin(ent, where)) continue;

        // pose ALWAYS remembered: unmeasurable frame is still next frame's baseline (else sharp forever)
        const bool havePrev = (s_playerFrame[idx] == f.frame - 1);

        const float was[3] = { s_playerPose[idx][0], s_playerPose[idx][1], s_playerPose[idx][2] };

        s_playerFrame[idx]   = f.frame;
        s_playerPose[idx][0] = where[0];
        s_playerPose[idx][1] = where[1];
        s_playerPose[idx][2] = where[2];

        if (!havePrev || !f.valid || total >= k_playerSlots) continue;

        const float travel[3] = { where[0] - was[0], where[1] - was[1], where[2] - was[2] };

        if (travel[0] * travel[0] + travel[1] * travel[1] + travel[2] * travel[2] >
            k_playerTeleport * k_playerTeleport) continue;

        // dormant = origin frozen = travel 0 = rows equal camera's (like a wall). No dormancy test.
        Entry& e = entries[total++];

        // eye step minus his travel; still player = camera reproj exactly
        const float shift[3] = { origin[0] - s_prevOrigin[0] - travel[0],
                                 origin[1] - s_prevOrigin[1] - travel[1],
                                 origin[2] - s_prevOrigin[2] - travel[2] };

        fbm::Reprojection(s_prevBasis, basis, shift, e.rows);

        for (int a = 0; a < 3; ++a) {
            e.box[0][a] = where[a] + k_hullMins[a] + ((a == 2) ? k_playerFloor : -k_playerMargin);
            e.box[1][a] = where[a] + k_hullMaxs[a] + k_playerMargin;
        }

        const float away[3] = { where[0] - origin[0], where[1] - origin[1], where[2] - origin[2] };
        e.distSq = away[0] * away[0] + away[1] * away[1] + away[2] * away[2];
    }

    if (total <= 0) return;

    // nearest first get the 10 slots (insertion sort, once a frame)
    for (int i = 1; i < total; ++i) {
        Entry key = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].distSq > key.distSq) { entries[j + 1] = entries[j]; --j; }
        entries[j + 1] = key;
    }

    const int kept = (total < k_maxPlayers) ? total : k_maxPlayers;

    // BACK TO FRONT: shader can't break unrolled loop, LAST containing box wins -> nearest last
    for (int slot = 0; slot < kept; ++slot) {
        const Entry& e = entries[kept - 1 - slot];
        memcpy(f.playerBox[slot][0], e.box[0], sizeof(float) * 4);
        memcpy(f.playerBox[slot][1], e.box[1], sizeof(float) * 4);
        for (int r = 0; r < 3; ++r) memcpy(f.playerRows[slot][r], e.rows[r], sizeof(float) * 4);
    }

    f.playerCount = kept;
}

// Main thread, post slot (entity list lives here).
void FrameBlend_UpdateView(const float origin[3], const float angles[3],
                           float fov, float fovVm, float zNearVm, float zFarVm)
{
    if (!g_fbEnable) {
        s_havePrev = false;   // reading across off-gap = teleport
        s_ready.store(-1, std::memory_order_release);
        return;
    }

    FbFrame f{};

    f.frame   = s_frameId.load(std::memory_order_relaxed);
    f.fov     = fov;
    f.fovVm   = (fovVm > 1.f && fovVm < 179.f) ? fovVm : fov;
    f.zNearVm = (zNearVm > 0.f) ? zNearVm : 1.f;
    f.zFarVm  = (zFarVm > f.zNearVm) ? zFarVm : (f.zNearVm + 1000.f);

    float basis[3][3];   // right, up, forward (shader order)
    fbm::Basis(angles, basis);

    // QPC real time: tick clock gives several frames same stamp -> speed 0 or spike
    static const LARGE_INTEGER freq = [] { LARGE_INTEGER q{}; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER ctr{};
    QueryPerformanceCounter(&ctr);
    const double now = (freq.QuadPart > 0) ? (double)ctr.QuadPart / (double)freq.QuadPart : 0.0;

    const double elapsed = s_havePrev ? (now - s_prevTime) : 0.0;

    const float step[3] = { origin[0] - s_prevOrigin[0],
                            origin[1] - s_prevOrigin[1],
                            origin[2] - s_prevOrigin[2] };

    const float travelled = sqrtf(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]);

    // stall / teleport / spectator hop: one frame no blur, camera still remembered below
    f.valid = s_havePrev && elapsed > 0.0 && elapsed < (1.0 / 15.0) && travelled < 128.f;

    // runs on invalid frames too (writes next baseline); before prev camera is overwritten below
    FbCollectPlayers(f, basis, origin);

    if (f.valid)
    {
        f.prevFov = s_prevFov;

        fbm::Reprojection(s_prevBasis, basis, step, f.reproj);

        // reproj = ONE frame of motion, shutter = TIME -> divide by frametime. Cap 8 frames (detail runs out).
        const float shutter = ((g_fbShutter < 0.f) ? 0.f : (g_fbShutter > 60.f ? 60.f : g_fbShutter)) * 0.001f;
        const float frames  = (float)(shutter / elapsed);
        const float capped  = (frames > 8.f) ? 8.f : frames;

        f.exposure    = ((g_fbAmount < 0.f) ? 0.f : g_fbAmount) * capped;
        f.gunExposure = capped;
    }

    memcpy(f.basis, basis, sizeof(basis));
    memcpy(f.origin, origin, sizeof(float) * 3);

    const int ready = s_ready.load(std::memory_order_relaxed);
    const int next  = (ready == 0) ? 1 : 0;
    s_slot[next] = f;
    s_ready.store(next, std::memory_order_release);

    s_havePrev   = true;
    s_prevTime   = now;
    s_prevFov    = fov;
    s_prevOrigin[0] = origin[0];
    s_prevOrigin[1] = origin[1];
    s_prevOrigin[2] = origin[2];
    memcpy(s_prevBasis, basis, sizeof(basis));
}

void FrameBlend_CollectBones(const void* pState, const void* pInfo, const void* pBones, bool weapon)
{
    if (!g_fbEnable || !g_fbViewmodel || s_shaderFailed) return;
    FbBones::Collect(pState, pInfo, pBones, weapon, s_frameId.load(std::memory_order_relaxed));
}

static void FbReleaseTarget(IDirect3DTexture9*& tex, IDirect3DSurface9*& surf)
{
    if (surf) { surf->Release(); surf = nullptr; }
    if (tex)  { tex->Release();  tex  = nullptr; }
}

void FrameBlend_Destroy()
{
    FbReleaseTarget(s_nbrTex, s_nbrSurf);
    FbReleaseTarget(s_tileTex, s_tileSurf);
    FbReleaseTarget(s_tileRowTex, s_tileRowSurf);
    FbReleaseTarget(s_infoTex, s_infoSurf);
    FbReleaseTarget(s_stageTex, s_stageSurf);

    if (s_resolveSurf) { s_resolveSurf->Release(); s_resolveSurf = nullptr; }

    for (auto& ps : s_ps) { if (ps) { ps->Release(); ps = nullptr; } }

    s_w = s_h = 0;
    s_tileSize = s_tileW = s_tileH = 0;
    s_fmt  = D3DFMT_UNKNOWN;
    s_msaa = D3DMULTISAMPLE_NONE;

    s_havePrev = false;
}

static IDirect3DPixelShader9* FbShaderFor(IDirect3DDevice9* dev, FbShader which)
{
    if (s_ps[which]) return s_ps[which];
    if (s_shaderFailed) return nullptr;

    const BYTE* bytes = nullptr;
    size_t size = 0;
    switch (which) {
        case FB_VELOCITY:  bytes = k_fxFbVelocityBytes;  size = sizeof(k_fxFbVelocityBytes);  break;
        case FB_TILE_H:    bytes = k_fxFbTileHBytes;     size = sizeof(k_fxFbTileHBytes);     break;
        case FB_TILE_V:    bytes = k_fxFbTileVBytes;     size = sizeof(k_fxFbTileVBytes);     break;
        case FB_NEIGHBOUR: bytes = k_fxFbNeighbourBytes; size = sizeof(k_fxFbNeighbourBytes); break;
        default:           bytes = k_fxFbGatherBytes;    size = sizeof(k_fxFbGatherBytes);    break;
    }

    std::vector<DWORD> code((size + 3) / 4);
    memcpy(code.data(), bytes, size);

    if (FAILED(dev->CreatePixelShader(code.data(), &s_ps[which])) || !s_ps[which]) {
        s_ps[which] = nullptr;
        s_shaderFailed = true;   // one pass missing = feature off
        DebugLog("frameblend: pixel shader %d refused by the card -- frame blending is off", (int)which);
        return nullptr;
    }
    DebugLog("frameblend: shader %d built", (int)which);
    return s_ps[which];
}

// velocity + tiles signed, hundreds of px: needs float
static bool FbVectorTarget(IDirect3DDevice9* dev, UINT w, UINT h,
                           IDirect3DTexture9*& tex, IDirect3DSurface9*& surf)
{
    const D3DFORMAT fmts[2] = { D3DFMT_A16B16G16R16F, D3DFMT_A32B32G32R32F };

    for (D3DFORMAT f : fmts) {
        if (SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, &tex, nullptr)) &&
            SUCCEEDED(tex->GetSurfaceLevel(0, &surf)))
            return true;

        FbReleaseTarget(tex, surf);
    }
    return false;
}

static bool FbBuild(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    const int width  = (int)desc.Width;
    const int height = (int)desc.Height;

    if (s_stageTex && s_w == width && s_h == height &&
        s_fmt == desc.Format && s_msaa == desc.MultiSampleType)
        return true;

    // shaders survive resize, targets don't
    IDirect3DPixelShader9* kept[FB_SHADER_MAX] = {};
    for (int i = 0; i < FB_SHADER_MAX; ++i) { kept[i] = s_ps[i]; s_ps[i] = nullptr; }

    FrameBlend_Destroy();

    for (int i = 0; i < FB_SHADER_MAX; ++i) s_ps[i] = kept[i];

    // stage = RT format, else StretchRect refuses
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                  desc.Format, D3DPOOL_DEFAULT, &s_stageTex, nullptr)) ||
        FAILED(s_stageTex->GetSurfaceLevel(0, &s_stageSurf)))
    {
        DebugLog("frameblend: stage target creation failed");
        FrameBlend_Destroy();
        return false;
    }

    // tile edge K caps streak at 2K (gather sees neighbours only); ~54px at 1080p
    const int tile = (height / 20 < 8) ? 8 : (height / 20);

    const UINT tileW = (UINT)((width  + tile - 1) / tile);
    const UINT tileH = (UINT)((height + tile - 1) / tile);

    if (!FbVectorTarget(dev, desc.Width, desc.Height, s_infoTex, s_infoSurf) ||
        !FbVectorTarget(dev, tileW, desc.Height, s_tileRowTex, s_tileRowSurf) ||
        !FbVectorTarget(dev, tileW, tileH, s_tileTex, s_tileSurf) ||
        !FbVectorTarget(dev, tileW, tileH, s_nbrTex, s_nbrSurf))
    {
        DebugLog("frameblend: no floating point render target -- velocity buffer unavailable");
        FrameBlend_Destroy();
        s_shaderFailed = true;
        return false;
    }

    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE &&
        FAILED(dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
                                       D3DMULTISAMPLE_NONE, 0, FALSE, &s_resolveSurf, nullptr)))
    {
        DebugLog("frameblend: msaa resolve target creation failed");
        FrameBlend_Destroy();
        return false;
    }

    s_w = width; s_h = height;
    s_tileSize = tile; s_tileW = (int)tileW; s_tileH = (int)tileH;
    s_fmt = desc.Format; s_msaa = desc.MultiSampleType;

    DebugLog("frameblend: built %dx%d msaa %d tile %d (%dx%d)",
             width, height, (int)desc.MultiSampleType, tile, s_tileW, s_tileH);
    return true;
}

// Saved/restored exactly (same list as ssao.cpp).
struct FbSaved
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
    DWORD rs[16];
    DWORD samp[3][6];
    float psc[k_fbRegisters][4];
};

static const D3DRENDERSTATETYPE k_fbRS[16] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE,
    D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_CULLMODE,
    D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_STENCILENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_FILLMODE,
    // RESZ resolve writes its trigger here just before this pass
    D3DRS_POINTSIZE, D3DRS_SEPARATEALPHABLENDENABLE,
};

static const D3DSAMPLERSTATETYPE k_fbSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void FbSave(IDirect3DDevice9* dev, FbSaved& s)
{
    if (FAILED(dev->GetDepthStencilSurface(&s.depthStencil))) s.depthStencil = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 16; ++i) dev->GetRenderState(k_fbRS[i], &s.rs[i]);
    for (int u = 0; u < 3; ++u) {
        dev->GetTexture(u, &s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->GetSamplerState(u, k_fbSamp[i], &s.samp[u][i]);
    }
    dev->GetPixelShaderConstantF(0, s.psc[0], k_fbRegisters);
}

static void FbRestore(IDirect3DDevice9* dev, FbSaved& s)
{
    // CONSTANTS before shader: shader set last writes its own def literals into unread regs
    dev->SetPixelShaderConstantF(0, s.psc[0], k_fbRegisters);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);

    // FVF FIRST, decl LAST, fvf only if set: SetFVF builds own decl; SetFVF(0) w/ decl leaves none
    if (s.fvf) dev->SetFVF(s.fvf);
    dev->SetVertexDeclaration(s.decl);

    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    for (int u = 0; u < 3; ++u) {
        dev->SetTexture(u, s.tex[u]);
        for (int i = 0; i < 6; ++i) dev->SetSamplerState(u, k_fbSamp[i], s.samp[u][i]);
    }
    for (int i = 0; i < 16; ++i) dev->SetRenderState(k_fbRS[i], s.rs[i]);
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

// Bones -> pass one. Proxy = world->world transform; shader wants view now -> view then:
//   w = eye + basis^T p;  w' = A w + a;  p' = basis (w' - eye)
// THIS frame's camera at both ends: fbbones.h already removed the ride, only animation left.
static int FbBuildBones(const FbFrame& f, float block[k_fbRegisters][4])
{
    const FbBones::Set set = FbBones::Read();

    // older than prev frame = gun stopped drawing. ONE frame slack: a missed part draw would
    // drop blur for one frame = visible pop.
    if (set.count <= 0 || set.frame < f.frame - 1) return 0;

    // >30 deg/frame = anim reset, not animation (x8 shutters paints screen black): proxy FLATTENED
    // (kept, list shape fixed). Under it shutter capped per bone so arc stays <45 deg.
    constexpr float k_flatTurn = 0.5236f;   // 30 deg in one frame
    constexpr float k_arcCap   = 0.7854f;   // 45 deg across whole shutter

    const int count = (set.count < FbBones::kMaxProxies) ? set.count : FbBones::kMaxProxies;

    for (int i = 0; i < count; ++i)
    {
        const FbBones::Proxy& p = set.proxy[i];

        float trace = p.rot[0][0] + p.rot[1][1] + p.rot[2][2];
        float cosT  = (trace - 1.f) * 0.5f;
        if (cosT < -1.f) cosT = -1.f;
        if (cosT >  1.f) cosT =  1.f;

        const bool impossible = acosf(cosT) > k_flatTurn;

        float rot[3][3] = {};
        float centerThen[3] = {};

        if (impossible) {
            for (int a = 0; a < 3; ++a) { rot[a][a] = 1.f; centerThen[a] = p.center[a]; }
        } else {
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) rot[r][c] = p.rot[r][c];
                centerThen[r] = p.centerThen[r];
            }
        }

        float rows[3][4] = {};
        fbm::BoneRows(f.basis, f.origin, rot, p.center, centerThen, rows);

        // one anim frame run for shutter (arc, not chord), capped at k_arcCap
        float spinTrace = rows[0][0] + rows[1][1] + rows[2][2];
        float spinCos   = (spinTrace - 1.f) * 0.5f;
        if (spinCos < -1.f) spinCos = -1.f;
        if (spinCos >  1.f) spinCos =  1.f;

        const float spin = acosf(spinCos);

        float boneExposure = f.gunExposure;
        if (spin > 1e-4f && k_arcCap / spin < boneExposure) boneExposure = k_arcCap / spin;

        fbm::PowerRows(rows, boneExposure);

        // bone center into view space
        const float away[3] = { p.center[0] - f.origin[0],
                                p.center[1] - f.origin[1],
                                p.center[2] - f.origin[2] };

        const int base = k_boneBase + i * 4;

        for (int a = 0; a < 3; ++a)
            block[base][a] = f.basis[a][0] * away[0] + f.basis[a][1] * away[1] + f.basis[a][2] * away[2];

        for (int r = 0; r < 3; ++r) memcpy(block[base + 1 + r], rows[r], sizeof(float) * 4);
    }

    // empty slots parked far (1/d^4 ~ 0): ps_3_0 can't index constants, walks all 16
    for (int i = count; i < FbBones::kMaxProxies; ++i) {
        const int base = k_boneBase + i * 4;
        block[base][0] = block[base][1] = block[base][2] = 1e6f;
    }

    return count;
}

void FrameBlend_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                     const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                     const FxViewSetup& view, bool gunPass)
{
    if (!dev || !target || !depth || s_shaderFailed || !g_fbEnable) return;
    if (gunPass && !g_fbViewmodel) return;
    if (!ScreenFx_QuadReady(dev)) return;

    const int ready = s_ready.load(std::memory_order_acquire);
    if (ready < 0) return;

    const FbFrame f = s_slot[ready];
    if (!f.valid) return;

    const float amount = (g_fbVmAmount < 0.f) ? 0.f : ((g_fbVmAmount > 64.f) ? 64.f : g_fbVmAmount);
    if (!gunPass && f.exposure <= 0.f) return;
    if (gunPass && amount <= 0.f) return;

    if (!FbBuild(dev, desc)) return;

    IDirect3DPixelShader9* velocity  = FbShaderFor(dev, FB_VELOCITY);
    IDirect3DPixelShader9* tileH     = FbShaderFor(dev, FB_TILE_H);
    IDirect3DPixelShader9* tileV     = FbShaderFor(dev, FB_TILE_V);
    IDirect3DPixelShader9* neighbour = FbShaderFor(dev, FB_NEIGHBOUR);
    IDirect3DPixelShader9* gather    = FbShaderFor(dev, FB_GATHER);
    if (!velocity || !tileH || !tileV || !neighbour || !gather) return;

    // must be frame sized, else not scene depth
    D3DSURFACE_DESC dd{};
    if (FAILED(depth->GetLevelDesc(0, &dd)) || dd.Width != desc.Width || dd.Height != desc.Height) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("frameblend: depth target size does not match the frame"); }
        return;
    }

    const float width  = (float)s_w;
    const float height = (float)s_h;

    // setup fov is HORIZONTAL; vertical from frame's own aspect
    const float aspect = (height > 0.f) ? width / height : 1.777778f;

    const float fovClamp = (f.fov < 1.f) ? 1.f : (f.fov > 170.f ? 170.f : f.fov);
    const float tanX     = tanf(fovClamp * 0.5f * 3.14159265f / 180.f);
    const float tanY     = tanX / aspect;

    const float prevClamp = (f.prevFov < 1.f) ? 1.f : (f.prevFov > 170.f ? 170.f : f.prevFov);
    const float prevTanX  = tanf(prevClamp * 0.5f * 3.14159265f / 180.f);
    const float prevTanY  = prevTanX / aspect;

    const float vmClamp = (f.fovVm < 1.f) ? 1.f : (f.fovVm > 170.f ? 170.f : f.fovVm);
    const float vmTanX  = tanf(vmClamp * 0.5f * 3.14159265f / 180.f);
    const float vmTanY  = vmTanX / aspect;

    const float ceilingPx = (float)(s_tileSize * 2);   // neighbour search reaches 2 tiles

    const int taps = (g_fbSamples < 4) ? 4 : ((g_fbSamples > 64) ? 64 : g_fbSamples);   // cap, not count

    // streak capped to taps: longer = taps spread apart = separate ghost copies
    float maxLength = ((g_fbMaxStreak < 0.f) ? 0.f : g_fbMaxStreak) * height;
    const float lengthCeiling = (ceilingPx < (float)taps) ? ceilingPx : (float)taps;
    if (maxLength < 4.f) maxLength = 4.f;
    if (maxLength > lengthCeiling) maxLength = lengthCeiling;

    float block[k_fbRegisters][4] = {};

    block[0][0] = view.zNear;
    block[0][1] = view.zFar;
    block[0][2] = tanX;
    block[0][3] = tanY;

    block[1][0] = prevTanX;
    block[1][1] = prevTanY;
    block[1][2] = 0.1f;             // raw depth range gun squeezed into
    block[1][3] = f.exposure;

    block[2][0] = (float)taps;
    block[2][1] = maxLength;
    block[2][2] = (float)s_tileSize;
    block[2][3] = (float)(f.frame & 63);

    for (int r = 0; r < 3; ++r) memcpy(block[3 + r], f.reproj[r], sizeof(float) * 4);

    block[6][0] = width;
    block[6][1] = height;
    block[6][2] = 1.f / ((width  > 1.f) ? width  : 1.f);
    block[6][3] = 1.f / ((height > 1.f) ? height : 1.f);

    block[7][0] = (float)s_tileW;
    block[7][1] = (float)s_tileH;
    block[7][2] = 1.f / ((s_tileW > 0) ? (float)s_tileW : 1.f);
    block[7][3] = 1.f / ((s_tileH > 0) ? (float)s_tileH : 1.f);

    // depth separation sharpness, fraction of distance (24 cut streaks at every model crease)
    block[8][0] = 12.f;

    // FENCE at half world zNear: gun pixels parked below, world never linearises below
    block[8][1] = ((view.zNear > 0.01f) ? view.zNear : 0.01f) * 0.5f;
    block[8][2] = gunPass ? 0.f : (float)f.playerCount;
    block[8][3] = gunPass ? 1.f : 0.f;

    for (int r = 0; r < 3; ++r) memcpy(block[9 + r], f.viewToWorld[r], sizeof(float) * 4);

    block[12][0] = (f.zNearVm > 0.01f) ? f.zNearVm : 0.01f;
    block[12][1] = (f.zFarVm > block[12][0] + 1.f) ? f.zFarVm : (block[12][0] + 1.f);
    block[12][2] = vmTanX;
    block[12][3] = vmTanY;

    int boneCount = 0;
    if (gunPass) boneCount = FbBuildBones(f, block);

    block[13][0] = (float)boneCount;
    block[13][1] = amount;

    // gun diagnostic 1/s. "gun not blurring": no set (DME never saw vm / early-out), age >1
    // (matsys queue runs draws late = chop), or shutter not moving. Healthy = age 0 or 1.
    if (gunPass) {
        static ULONGLONG s_lastReport = 0;
        const ULONGLONG nowMs = GetTickCount64();
        if (nowMs - s_lastReport > 1000) {
            s_lastReport = nowMs;
            const FbBones::Set set = FbBones::Read();
            DebugLog("^FX: frame blending gun -- bones %d (weapon %d) age %d shutter %.2f amount %.2f",
                     set.count, set.weapon, set.count > 0 ? (f.frame - set.frame) : -1,
                     f.gunExposure, amount);
        }
    }

    if (gunPass && boneCount <= 0) return;

    if (!gunPass) {
        for (int i = 0; i < k_maxPlayers; ++i) {
            const int base = k_playerBase + i * 5;

            if (i >= f.playerCount) {
                // parked inside out: mins > maxs, contains nothing
                for (int a = 0; a < 3; ++a) {
                    block[base][a]     =  1e9f;
                    block[base + 1][a] = -1e9f;
                }
                continue;
            }

            memcpy(block[base],     f.playerBox[i][0], sizeof(float) * 4);
            memcpy(block[base + 1], f.playerBox[i][1], sizeof(float) * 4);
            for (int r = 0; r < 3; ++r) memcpy(block[base + 2 + r], f.playerRows[i][r], sizeof(float) * 4);
        }
    }

    FbSaved st{};
    FbSave(dev, st);

    // msaa: resolve into plain surface, then copy
    HRESULT blit = dev->StretchRect(target, nullptr, s_resolveSurf ? s_resolveSurf : s_stageSurf,
                                    nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(blit) && s_resolveSurf)
        blit = dev->StretchRect(s_resolveSurf, nullptr, s_stageSurf, nullptr, D3DTEXF_NONE);

    if (FAILED(blit)) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("frameblend: frame blit failed 0x%08lx", (unsigned long)blit); }
        FbRestore(dev, st);
        return;
    }

    dev->SetDepthStencilSurface(nullptr);
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
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0f);

    // POINT everywhere but colour: INTZ unfilterable, velocity averaged across silhouette = garbage
    for (DWORD u = 0; u < 3; ++u) {
        dev->SetSamplerState(u, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(u, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(u, D3DSAMP_SRGBTEXTURE, 0);
        dev->SetSamplerState(u, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(u, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    }

    // Block re-uploaded EVERY draw, BEFORE SetPixelShader: shader defs (e.g. velocity c7, c14/c15)
    // overwrite unread regs on set, so one upload leaves later passes reading stale literals.
    const auto pass = [&](IDirect3DSurface9* surface, int w, int h, IDirect3DPixelShader9* shader)
    {
        const D3DVIEWPORT9 vp{ 0, 0, (DWORD)w, (DWORD)h, 0.f, 1.f };
        dev->SetRenderTarget(0, surface);
        dev->SetViewport(&vp);
        dev->SetPixelShaderConstantF(0, block[0], k_fbRegisters);
        dev->SetPixelShader(shader);
        ScreenFx_DrawQuad(dev, w, h);
    };

    dev->SetTexture(0, depth);
    pass(s_infoSurf, s_w, s_h, velocity);

    // tile max, one axis at a time
    dev->SetTexture(0, s_infoTex);
    pass(s_tileRowSurf, s_tileW, s_h, tileH);

    dev->SetTexture(0, s_tileRowTex);
    pass(s_tileSurf, s_tileW, s_tileH, tileV);

    dev->SetTexture(0, s_tileTex);
    pass(s_nbrSurf, s_tileW, s_tileH, neighbour);

    // gather onto frame: colour filtered
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

    // rgb only: alpha is engine's dest-alpha depth
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                        D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);

    dev->SetTexture(0, s_stageTex);
    dev->SetTexture(1, s_infoTex);
    dev->SetTexture(2, s_nbrTex);
    pass(target, s_w, s_h, gather);

    FbRestore(dev, st);

    static bool s_logged[2] = { false, false };
    if (!s_logged[gunPass ? 1 : 0]) {
        s_logged[gunPass ? 1 : 0] = true;
        DebugLog("^FX: frame blending %s drew %dx%d tile %d exposure=%.2f gun=%.2f bones=%d "
                 "players=%d taps=%d max=%.0fpx",
                 gunPass ? "gun" : "world", s_w, s_h, s_tileSize, f.exposure, f.gunExposure,
                 boneCount, f.playerCount, taps, maxLength);
    }
}
