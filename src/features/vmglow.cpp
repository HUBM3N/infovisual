#include "../globals.h"
#include "vmglow.h"
#include "visuals.h"
#include "flipworld.h"

// Viewmodel glow (amalgam port). PushRenderTargetAndViewport (105) crashes in this build ->
// GetRenderTarget (7) + SetRenderTarget (6).
//   Pass 1: invisible draw, stencil=1 on model pixels.
//   Pass 2: colored draw into RB1, blur RB1 -> RB2 -> RB1, composite RB1 where stencil=0.
// RTs made in CViewRender::LevelInit, matsys textures: survive device reset, released on game thread at eject.

bool        g_glowRTsReady = false;
LevelInit_t o_LevelInit    = nullptr;

static void* s_glowRB1          = nullptr;  // ITexture*  "RenderBuffer1"
static void* s_glowRB2          = nullptr;  // ITexture*  "RenderBuffer2"
static void* s_glowMatGlowColor = nullptr;  // IMaterial* dev/glow_color
static void* s_glowMatHalo      = nullptr;  // IMaterial* iv_glow/halo (additive, reads RB1)
static void* s_glowMatBlurX     = nullptr;  // IMaterial* iv_glow/blurX
static void* s_glowMatBlurY     = nullptr;  // IMaterial* iv_glow/blurY
static void* s_glowBloomVar     = nullptr;  // IMaterialVar* $bloomamount on blurY

// IMatRenderContext helpers (slots from amalgam header)
static void* GlowGetRC()
{
    using F = void*(__fastcall*)(void*);
    void* pRC = nullptr;
    __try { pRC = ((F)(*(void***)g_matSystem)[98])(g_matSystem); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return pRC;
}

// slot 6:  SetRenderTarget(ITexture*)
static void GlowSetRT(void* pRC, void* pTex)
{
    using F = void(__fastcall*)(void*, void*);
    __try { ((F)(*(void***)pRC)[6])(pRC, pTex); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// slot 7:  GetRenderTarget() -> ITexture*, no AddRef: never Release
static void* GlowGetRT(void* pRC)
{
    using F = void*(__fastcall*)(void*);
    void* r = nullptr;
    __try { r = ((F)(*(void***)pRC)[7])(pRC); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

// slot 12: ClearBuffers(bColor, bDepth, bStencil)
static void GlowClearBufs(void* pRC, bool col, bool dep, bool sten)
{
    using F = void(__fastcall*)(void*, bool, bool, bool);
    __try { ((F)(*(void***)pRC)[12])(pRC, col, dep, sten); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// slot 40: CullMode (0 CCW, 1 CW; imaterialsystem.h:180)
// Glow runs inside game's vm cull window (c_baseviewmodel.cpp:374, econ_entity.cpp:860: CW when flipped).
// Model draws keep that mode (VmGameCull); only screen-space blur/composite CCW, then game mode back.
// Forcing CCW = flipped gun drew back faces (inside-out with cl_flipviewmodels 1).
static void GlowCull(void* pRC, int m)
{
    using F = void(__fastcall*)(void*, int);
    __try { ((F)(*(void***)pRC)[40])(pRC, m); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// slot 73: ClearColor4ub
static void GlowClear4ub(void* pRC, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    using F = void(__fastcall*)(void*, uint8_t, uint8_t, uint8_t, uint8_t);
    __try { ((F)(*(void***)pRC)[73])(pRC, r, g, b, a); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// slot 103: DrawScreenSpaceRectangle
static void GlowDrawSSRect(void* pRC, void* mat, int dx, int dy, int dw, int dh, int sw, int sh)
{
    using F = void(__fastcall*)(void*, void*, int, int, int, int, float, float, float, float, int, int, void*, int, int);
    __try { ((F)(*(void***)pRC)[103])(pRC, mat, dx, dy, dw, dh, 0.f, 0.f, (float)(sw-1), (float)(sh-1), sw, sh, nullptr, 1, 1); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void GlowStencil(void* pRC, bool en,
                        int fn, int ref, unsigned tmask, unsigned wmask,
                        int fail, int zfail, int pass)
{
    auto S = [&](int sl, int v) {
        using F = void(__fastcall*)(void*, int); __try { ((F)(*(void***)pRC)[sl])(pRC, v); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    };
    auto SU = [&](int sl, unsigned v) {
        using F = void(__fastcall*)(void*, unsigned); __try { ((F)(*(void***)pRC)[sl])(pRC, v); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    };
    S(117, (int)en); S(121, fn); S(122, ref); SU(123, tmask); SU(124, wmask);
    S(118, fail); S(119, zfail); S(120, pass);
}

static void* GlowFindMat(const char* name)
{
    void* p = MatSysFindMaterial(g_matSystem, name, false);
    return (p && !MatIsError(p)) ? p : nullptr;
}

static void GlowRef(void* p, int delta)
{
    if (!p) return;
    using F = void(__fastcall*)(void*);
    int slot = delta > 0 ? 12 : 13;
    __try { ((F)(*(void***)p)[slot])(p); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void GlowReleaseTex(void*& p)
{
    if (!p) return;
    using F = void(__fastcall*)(void*);
    __try { ((F)(*(void***)p)[11])(p); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    p = nullptr;
}

static void GlowWriteVMTs(const char* cwd)
{
    char dir[MAX_PATH];
    sprintf_s(dir, "%s\\tf\\materials\\iv_glow", cwd);
    { char t[MAX_PATH];
      sprintf_s(t,"%s\\tf",cwd);          _mkdir(t);
      sprintf_s(t,"%s\\tf\\materials",cwd);_mkdir(t);
      _mkdir(dir); }
    auto w = [&](const char* name, const char* body) {
        char p[MAX_PATH]; sprintf_s(p,"%s\\%s.vmt",dir,name);
        FILE* f = fopen(p,"w"); if(f){fputs(body,f);fclose(f);}
    };
    w("halo",  "\"UnlitGeneric\"\n{\n\t$basetexture \"RenderBuffer1\"\n\t$additive \"1\"\n}");
    w("blurX", "\"BlurFilterX\"\n{\n\t$basetexture \"RenderBuffer1\"\n}");
    w("blurY", "\"BlurFilterY\"\n{\n\t$basetexture \"RenderBuffer2\"\n}");
}

static void GlowSetBlend(float a)
{
    if (!g_renderView) return;
    using F = void(__fastcall*)(void*,float);
    __try { ((F)(*(void***)g_renderView)[4])(g_renderView, a); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
static float GlowGetBlend()
{
    float v=1.f; if (!g_renderView) return v;
    using F = float(__fastcall*)(void*);
    __try { v=((F)(*(void***)g_renderView)[5])(g_renderView); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return v;
}
static void GlowSetColor(float r,float g,float b)
{
    if (!g_renderView) return;
    float c[3]={r,g,b};
    using F = void(__fastcall*)(void*,const float*);
    __try { ((F)(*(void***)g_renderView)[6])(g_renderView,c); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
static void GlowGetColor(float* out)
{
    out[0]=out[1]=out[2]=1.f; if (!g_renderView) return;
    using F = void(__fastcall*)(void*,float*);
    __try { ((F)(*(void***)g_renderView)[7])(g_renderView,out); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
static void GlowFMO(void* mat)
{
    if (!g_modelRender) return;
    if (o_ForcedMatOverride_fwd) {
        __try { o_ForcedMatOverride_fwd(g_modelRender, mat, 0); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    } else {
        using F = void(__fastcall*)(void*,void*,int);
        __try { ((F)(*(void***)g_modelRender)[1])(g_modelRender,mat,0); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

using DrawFn = void(*)(void* ctx, const void* a, const void* b, const void* c);

// Pass 1: invisible draw, stencil=1 where entity pixels land
__declspec(noinline) static void GlowFirstPass_SEH(
    void* pRC, DrawFn fn, void* ctx, const void* a, const void* b, const void* c)
{
    float sb = GlowGetBlend(); float sc[3]; GlowGetColor(sc);
    GlowFMO(s_glowMatGlowColor);
    GlowSetBlend(0.f); GlowSetColor(1,1,1);
    GlowStencil(pRC, true, 8/*ALWAYS*/, 1, 0x0, 0xFF,
                3/*REPLACE*/, 3/*REPLACE*/, 3/*REPLACE*/);
    g_vmIsRendering = true;
    __try { fn(ctx,a,b,c); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    g_vmIsRendering = false;
    GlowStencil(pRC, false, 8, 0, 0xFF, 0xFF, 1, 1, 1);
    GlowFMO(nullptr); GlowSetBlend(sb); GlowSetColor(sc[0],sc[1],sc[2]);
}

// Pass 2: colored draw into RB1 -> blur -> composite halo where stencil=0
__declspec(noinline) static void GlowSecondPass_SEH(
    void* pRC, DrawFn fn, void* ctx, const void* a, const void* b, const void* c,
    int w, int h, float col[4], float sigma, int stencil, int gameCull)
{
    float sb = GlowGetBlend(); float sc[3]; GlowGetColor(sc);

    void* savedRT = GlowGetRT(pRC);
    GlowSetRT(pRC, s_glowRB1);
    GlowClear4ub(pRC, 0, 0, 0, 0);
    GlowClearBufs(pRC, true, false, false);
    GlowFMO(s_glowMatGlowColor);
    GlowSetColor(col[0], col[1], col[2]);
    GlowSetBlend(col[3]);
    g_vmIsRendering = true;
    __try { fn(ctx,a,b,c); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    g_vmIsRendering = false;
    GlowFMO(nullptr);
    GlowSetRT(pRC, savedRT);

    // screen-space from here: CCW; game mode back at end
    GlowCull(pRC, 0);

    if (sigma > 0.f) {
        if (s_glowBloomVar) {
            using SetF = void(__fastcall*)(void*,float);
            __try { ((SetF)(*(void***)s_glowBloomVar)[3])(s_glowBloomVar, sigma); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        GlowSetRT(pRC, s_glowRB2);
        GlowDrawSSRect(pRC, s_glowMatBlurX, 0, 0, w, h, w, h);
        GlowSetRT(pRC, s_glowRB1);
        GlowDrawSSRect(pRC, s_glowMatBlurY, 0, 0, w, h, w, h);
        GlowSetRT(pRC, savedRT);
    }

    // composite only outside silhouette (stencil=0)
    GlowStencil(pRC, true, 3/*EQUAL*/, 0, 0xFF, 0x0, 1, 1, 1);
    GlowSetBlend(1.f); GlowSetColor(1,1,1);

    // pixel-shift halo: RB1 at N-px offsets fills ring around silhouette
    if (stencil > 0) {
        int iSide   = (stencil + 1) / 2;
        int iCorner = stencil / 2;
        GlowDrawSSRect(pRC, s_glowMatHalo, -iSide, 0,       w, h, w, h);
        GlowDrawSSRect(pRC, s_glowMatHalo,  iSide, 0,       w, h, w, h);
        GlowDrawSSRect(pRC, s_glowMatHalo, 0,     -iSide,   w, h, w, h);
        GlowDrawSSRect(pRC, s_glowMatHalo, 0,      iSide,   w, h, w, h);
        if (iCorner > 0) {
            GlowDrawSSRect(pRC, s_glowMatHalo, -iCorner, -iCorner, w, h, w, h);
            GlowDrawSSRect(pRC, s_glowMatHalo,  iCorner,  iCorner, w, h, w, h);
            GlowDrawSSRect(pRC, s_glowMatHalo,  iCorner, -iCorner, w, h, w, h);
            GlowDrawSSRect(pRC, s_glowMatHalo, -iCorner,  iCorner, w, h, w, h);
        }
    }
    if (sigma > 0.f)
        GlowDrawSSRect(pRC, s_glowMatHalo, 0, 0, w, h, w, h);

    GlowStencil(pRC, false, 8, 0, 0xFF, 0xFF, 1, 1, 1);
    GlowSetBlend(sb); GlowSetColor(sc[0],sc[1],sc[2]);
    GlowCull(pRC, gameCull);   // following visible draw needs vm window's own mode
}

void GlowVM_LevelUnload()
{
    g_glowRTsReady = false;
    GlowRef(s_glowMatGlowColor,-1); GlowRef(s_glowMatHalo,-1);
    GlowRef(s_glowMatBlurX,-1);     GlowRef(s_glowMatBlurY,-1);
    GlowReleaseTex(s_glowRB1); GlowReleaseTex(s_glowRB2);
    s_glowMatGlowColor=s_glowMatHalo=s_glowMatBlurX=s_glowMatBlurY=s_glowBloomVar=nullptr;
}

static int s_glowW = 0, s_glowH = 0;   // backbuffer size the RTs were made at

static void GlowRTSize(int& w, int& h)
{
    w = h = 0;
    using GetDim = void(__fastcall*)(void*,int*,int*);
    __try { ((GetDim)(*(void***)g_matSystem)[31])(g_matSystem,&w,&h); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (w<=0||h<=0) { w=1280; h=720; }
}

// RTs + materials kept across maps (refs held); only new bb size remakes
bool GlowVM_NeedsRT()
{
    if (!g_matSystem) return false;
    int w, h; GlowRTSize(w, h);
    return !(g_glowRTsReady && w == s_glowW && h == s_glowH);
}

void GlowVM_LevelInit()
{
    if (!GlowVM_NeedsRT()) return;
    GlowVM_LevelUnload();

    int w, h; GlowRTSize(w, h);
    s_glowW = w; s_glowH = h;

    // 85 = CreateNamedRenderTargetTextureEx; outside an 82/83 block it calls End itself
    // (materialsystem disasm). 84/86 are other overloads: wrong args = garbage RTs / AVs.
    using CreateRT = void*(__fastcall*)(void*,const char*,int,int,int,int,int,unsigned,unsigned);
    __try {
        s_glowRB1 = ((CreateRT)(*(void***)g_matSystem)[85])(g_matSystem,"RenderBuffer1",w,h,8,2,0,0x200Cu,1u);
        s_glowRB2 = ((CreateRT)(*(void***)g_matSystem)[85])(g_matSystem,"RenderBuffer2",w,h,8,2,0,0x200Cu,1u);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (!s_glowRB1||!s_glowRB2) {
        DebugLog("GlowVM_LevelInit: RT creation FAILED"); GlowVM_LevelUnload(); return;
    }
    using IncTex = void(__fastcall*)(void*);
    __try { ((IncTex)(*(void***)s_glowRB1)[10])(s_glowRB1); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { ((IncTex)(*(void***)s_glowRB2)[10])(s_glowRB2); } __except(EXCEPTION_EXECUTE_HANDLER) {}

    s_glowMatGlowColor = GlowFindMat("dev/glow_color");
    if (s_glowMatGlowColor) GlowRef(s_glowMatGlowColor,+1);

    // VMTs never change: write once
    static bool s_vmtWritten = false;
    if (!s_vmtWritten) {
        char cwd[MAX_PATH]={}; GetGameDir(cwd);
        GlowWriteVMTs(cwd);
        s_vmtWritten = true;
    }
    s_glowMatHalo  = GlowFindMat("iv_glow/halo");  if (s_glowMatHalo)  GlowRef(s_glowMatHalo,+1);
    s_glowMatBlurX = GlowFindMat("iv_glow/blurX"); if (s_glowMatBlurX) GlowRef(s_glowMatBlurX,+1);
    s_glowMatBlurY = GlowFindMat("iv_glow/blurY"); if (s_glowMatBlurY) GlowRef(s_glowMatBlurY,+1);

    if (s_glowMatBlurY) {
        bool found=false;
        using FV = void*(__fastcall*)(void*,const char*,bool*,bool);
        __try { s_glowBloomVar=((FV)(*(void***)s_glowMatBlurY)[11])(s_glowMatBlurY,"$bloomamount",&found,false); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    g_glowRTsReady = s_glowRB1&&s_glowRB2&&s_glowMatGlowColor&&s_glowMatHalo&&s_glowMatBlurX&&s_glowMatBlurY;
    DebugLog("GlowVM_LevelInit: %dx%d ok=%d rb1=%p rb2=%p glow=%p halo=%p bx=%p by=%p",
             w,h,(int)g_glowRTsReady,s_glowRB1,s_glowRB2,
             s_glowMatGlowColor,s_glowMatHalo,s_glowMatBlurX,s_glowMatBlurY);
}

// IMaterialSystem 82 BeginRenderTargetAllocation / 83 EndRenderTargetAllocation (tools/check_vtables.py).
static void GlowRTAllocBlock(int slot)
{
    using F = void(__fastcall*)(void*);
    __try { ((F)(*(void***)g_matSystem)[slot])(g_matSystem); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// 85 outside a block calls End (83) itself even if RT exists; End = full device release/reacquire
// (all textures re-downloaded). RTs live across maps; new ones share one Begin/End = one restore.
void __fastcall h_LevelInit(void* rcx)
{
    if (o_LevelInit) o_LevelInit(rcx);
    const bool batch = g_matSystem && (GlowVM_NeedsRT() || FlipWorld_NeedsRT());
    const DWORD t0 = GetTickCount();
    if (batch) GlowRTAllocBlock(82);
    GlowVM_LevelInit();
    FlipWorld_LevelInit();   // flip RT made at same point
    if (batch) {
        GlowRTAllocBlock(83);
        DebugLog("level init: render targets made in %u ms (one device restore)", GetTickCount() - t0);
    }
}

static void InternalDrawFn(void* ctx, const void* fp, const void*, const void*)
{
    if (o_CBaseAnimatingInternalDraw)
        o_CBaseAnimatingInternalDraw(ctx, *(const int*)fp);
}
static void DMEDrawFn(void* ctx, const void* pState, const void* pInfo, const void* pBones)
{
    if (o_DrawModelExecute) o_DrawModelExecute(ctx, pState, pInfo, pBones);
}

void GlowVM_DrawWeapon(void* rcx_ent, int flags, float col[4], float sigma, int stencil)
{
    if (!g_glowRTsReady) return;
    if (sigma <= 0.f && stencil <= 0) return;
    void* pRC = GlowGetRC(); if (!pRC) return;
    int w=0,h=0;
    using GD = void(__fastcall*)(void*,int*,int*);
    __try { ((GD)(*(void***)g_matSystem)[31])(g_matSystem,&w,&h); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (w<=0||h<=0) return;
    GlowFirstPass_SEH(pRC, InternalDrawFn, rcx_ent, &flags, nullptr, nullptr);
    GlowSecondPass_SEH(pRC, InternalDrawFn, rcx_ent, &flags, nullptr, nullptr, w, h, col, sigma, stencil, VmGameCull());
}

void GlowVM_DrawArms(void* rcx, const void* pState, const void* pInfo, const void* pBones,
                     float col[4], float sigma, int stencil)
{
    if (!g_glowRTsReady) return;
    if (sigma <= 0.f && stencil <= 0) return;
    void* pRC = GlowGetRC(); if (!pRC) return;
    int w=0,h=0;
    using GD = void(__fastcall*)(void*,int*,int*);
    __try { ((GD)(*(void***)g_matSystem)[31])(g_matSystem,&w,&h); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (w<=0||h<=0) return;
    GlowFirstPass_SEH(pRC, DMEDrawFn, rcx, pState, pInfo, pBones);
    GlowSecondPass_SEH(pRC, DMEDrawFn, rcx, pState, pInfo, pBones, w, h, col, sigma, stencil, VmGameCull());
}
