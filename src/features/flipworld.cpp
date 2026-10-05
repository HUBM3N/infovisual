#include "../globals.h"
#include "../common.h"   // ConVar / ICvar, PatternScan
#include "flipworld.h"
#include "websurface.h"
#include "screenfx.h"    // ScreenFx_OnPush2DView: bloom/cc ride this hook, before hud

// Engine glue for flip world (Aletherium CFlipWorld port). Every engine number = flipslot:: constant
// (flipmath.h); tools/check_vtables.py asserts them + REFUSES raw slot numbers here. Decisions live in
// FlipCoreTick (flipmath.h), tested by tests/flipworld_test.cpp.

using namespace flipslot;

extern bool g_flipWorld;   // feature toggle (defined in dllmain.cpp)

static void* s_flipTex    = nullptr;   // ITexture*  full-frame-buffer copy of the world frame
static void* s_flipMat    = nullptr;   // IMaterial* UnlitGeneric, $basetexture=FlipWorldTexture, $ignorez=1
static bool  s_flipReady  = false;
static int   s_flipW      = 0;         // backbuffer size s_flipTex was made at
static int   s_flipH      = 0;
static bool  s_vmtWritten = false;     // VMT written once per session

// FlipCore = game-thread state; eject fallback tick takes same lock. Other threads read only atomics below.
static FlipCore s_core;
static SRWLOCK  s_coreLock = SRWLOCK_INIT;
static bool     s_active   = false;              // wanted && owner, as of the last tick
static std::atomic<bool>     s_pubMirroring{false};
static std::atomic<bool>     s_pubInWorldPass{false};
static std::atomic<DWORD>    s_gameTid{0};
static std::atomic<uint64_t> s_lastTickMs{0};

struct FlipLock {
    FlipLock()  { AcquireSRWLockExclusive(&s_coreLock); }
    ~FlipLock() { ReleaseSRWLockExclusive(&s_coreLock); }
};
struct FlipHookDepth {   // eject drain counts these
    FlipHookDepth()  { ++g_miscHookDepth; }
    ~FlipHookDepth() { --g_miscHookDepth; }
};

// One owner per process (SimpleVisual carries flip world too): two copies = double mirror (none) +
// cl_flipviewmodels fight. Named mapping per pid holds owner token + whether owner's frame is mirrored
// (other copy's depth passes need it).
struct FlipShared { LONG owner; LONG frameMirrored; };
static constexpr LONG k_flipToken = 0x31535046;   // "FPS1" (SimpleVisual uses "DIH1")
static HANDLE      s_sharedMap = nullptr;
static FlipShared* s_shared    = nullptr;

static void FlipSharedOpen()
{
    if (s_shared) return;
    char name[64];
    sprintf_s(name, "Local\\TF2FlipWorld-%lu", GetCurrentProcessId());
    s_sharedMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(FlipShared), name);
    if (s_sharedMap) s_shared = static_cast<FlipShared*>(MapViewOfFile(s_sharedMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FlipShared)));
    if (!s_shared) DebugLog("flipworld: shared owner mapping FAILED -- flip world stays off");
}

static void FlipPublish()
{
    const bool m = s_active && s_core.track.mirrored;
    s_pubMirroring.store(m);
    s_pubInWorldPass.store(s_core.track.inWorldPass);
    if (s_shared && s_core.owner) InterlockedExchange(&s_shared->frameMirrored, m ? 1 : 0);
}

static void* FlipGetRC()
{
    // never Release: this build's GetRenderContext gives no owned ref, Release crashes
    using F = void*(__fastcall*)(void*);
    void* pRC = nullptr;
    __try { pRC = ((F)(*(void***)g_matSystem)[matGetRenderContext])(g_matSystem); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return pRC;
}

static void FlipGetScreen(int* w, int* h)
{
    *w = 0; *h = 0;
    using GD = void(__fastcall*)(void*, int*, int*);
    __try { ((GD)(*(void***)g_matSystem)[matGetBackBufferDimensions])(g_matSystem, w, h); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static bool FlipCopyFrame(void* pRC, void* pTex)
{
    using F = void(__fastcall*)(void*, void*);
    __try { ((F)(*(void***)pRC)[ctxCopyRenderTargetToTex])(pRC, pTex); return true; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// view rect w/ source texels reversed (FlipMirrorSrcRect); copy = whole bb so texel = pixel
static bool FlipDrawMirror(void* pRC, void* mat, int x, int y, int w, int h, int bbW, int bbH)
{
    using F = void(__fastcall*)(void*, void*, int, int, int, int,
                                float, float, float, float, int, int, void*, int, int);
    float src[4];
    FlipMirrorSrcRect(x, y, w, h, src);
    __try {
        ((F)(*(void***)pRC)[ctxDrawScreenSpaceRect])(pRC, mat, x, y, w, h, src[0], src[1], src[2], src[3], bbW, bbH, nullptr, 1, 1);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

static void FlipRef(void* p, int slot)
{
    using F = void(__fastcall*)(void*);
    __try { ((F)(*(void***)p)[slot])(p); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// CViewSetup rect: mat_viewportscale < 1 = sub-rect stretched later (viewrender.cpp:2263).
// Implausible -> full bb.
static void FlipViewRect(const void* setup, int bbW, int bbH, int& x, int& y, int& w, int& h)
{
    x = 0; y = 0; w = bbW; h = bbH;
    if (!setup) return;
    int vx = 0, vy = 0, vw = 0, vh = 0;
    __try {
        const uintptr_t p = reinterpret_cast<uintptr_t>(setup);
        vx = *reinterpret_cast<const int*>(p + vsX);
        vy = *reinterpret_cast<const int*>(p + vsY);
        vw = *reinterpret_cast<const int*>(p + vsWidth);
        vh = *reinterpret_cast<const int*>(p + vsHeight);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (vx >= 0 && vy >= 0 && vw > 0 && vh > 0 && vx + vw <= bbW && vy + vh <= bbH) {
        x = vx; y = vy; w = vw; h = vh;
    }
}

static void FlipWriteVMT(const char* cwd)
{
    char dir[MAX_PATH];
    sprintf_s(dir, "%s\\tf\\materials\\iv_flipworld", cwd);
    { char t[MAX_PATH];
      sprintf_s(t, "%s\\tf", cwd);            _mkdir(t);
      sprintf_s(t, "%s\\tf\\materials", cwd); _mkdir(t);
      _mkdir(dir); }
    char p[MAX_PATH]; sprintf_s(p, "%s\\flip.vmt", dir);
    FILE* f = fopen(p, "w");
    if (f) {
        fputs("\"UnlitGeneric\"\n{\n\t$basetexture \"FlipWorldTexture\"\n\t$ignorez \"1\"\n}", f);
        fclose(f);
    }
}

void FlipWorld_LevelUnload()
{
    s_flipReady = false;
    if (s_flipMat) { FlipRef(s_flipMat, matDecRef); s_flipMat = nullptr; }
    if (s_flipTex) { FlipRef(s_flipTex, texDecRef); s_flipTex = nullptr; }
}

static void FlipRTSize(int* w, int* h)
{
    FlipGetScreen(w, h);
    if (*w <= 0 || *h <= 0) { *w = 1280; *h = 720; }
}

// RT made at first map load even if feature off, kept across maps. Map load = only safe time:
// CreateNamedRTEx ends alloc block itself (mid-match races queued render thread; = full device
// restore). ~6 MB VRAM at 1080p beats "reload map before flip works". Only new bb size remakes.
bool FlipWorld_NeedsRT()
{
    if (!g_matSystem) return false;
    int w = 0, h = 0;
    FlipRTSize(&w, &h);
    return !(s_flipTex && s_flipMat && w == s_flipW && h == s_flipH);
}

void FlipWorld_LevelInit()
{
    if (!g_matSystem) return;
    // LevelShutdown only closed gate; last map's RT + material still ours
    if (!FlipWorld_NeedsRT()) { s_flipReady = true; return; }
    FlipWorld_LevelUnload();   // size changed or never made

    int w = 0, h = 0;
    FlipRTSize(&w, &h);

    // same shape as vmglow RenderBuffer1/2: sizeMode 8 (literal), RGB888 (2), shared depth (0), CLAMPS|CLAMPT, HDR flag
    using CreateRT = void*(__fastcall*)(void*, const char*, int, int, int, int, int, unsigned, unsigned);
    __try {
        s_flipTex = ((CreateRT)(*(void***)g_matSystem)[matCreateNamedRTEx])(g_matSystem, "FlipWorldTexture",
                                                                             w, h, 8, 2, 0, 0x200Cu, 1u);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (!s_flipTex) { DebugLog("FlipWorld_LevelInit: RT creation FAILED"); FlipWorld_LevelUnload(); return; }
    FlipRef(s_flipTex, texIncRef);

    if (!s_vmtWritten) {
        char cwd[MAX_PATH] = {}; GetGameDir(cwd);
        FlipWriteVMT(cwd);
        s_vmtWritten = true;
    }
    s_flipMat = MatSysFindMaterial(g_matSystem, "iv_flipworld/flip", false);
    if (s_flipMat && !MatIsError(s_flipMat)) FlipRef(s_flipMat, matIncRef);
    else s_flipMat = nullptr;

    s_flipReady = s_flipTex && s_flipMat;
    s_flipW = w; s_flipH = h;
    DebugLog("FlipWorld_LevelInit: %dx%d ok=%d tex=%p mat=%p", w, h, (int)s_flipReady, s_flipTex, s_flipMat);
}

// g_eject here: eject releases cl_flipviewmodels on game thread while CreateMove hook still live;
// a tick seeing feature on would retake the hold before hooks come off.
static bool FlipWanted()
{
    return !g_eject.load() && g_flipWorld && s_flipReady && g_inGame.load() && s_shared != nullptr;
}

bool FlipWorld_Mirroring()
{
    return s_pubMirroring.load();
}

static bool OnGameThread() { const DWORD t = s_gameTid.load(); return t && GetCurrentThreadId() == t; }

void FlipWorld_RenderStart()
{
    if (!OnGameThread()) return;
    FlipLock _l;
    FlipTrackFrameStart(s_core.track);
    FlipPublish();
}

void FlipWorld_WorldPass()
{
    if (!OnGameThread()) return;
    FlipLock _l;
    FlipTrackWorldPass(s_core.track);
}

// Push2DView hook (below) ends 3D pass for W2S
using Push2D_t = void(__fastcall*)(void*, const void*, int, void*, void*);
static Push2D_t o_FlipPush2D = nullptr;

// End of h_DoPostSSE: world + particles drawn, DrawViewModels next (viewrender.cpp:2157/2160).
void FlipWorld_Render(const void* pViewSetup)
{
    if (!s_active || !g_matSystem || !s_flipTex || !s_flipMat) return;

    void* pRC = FlipGetRC();
    if (!pRC) return;

    int bbW = 0, bbH = 0;
    FlipGetScreen(&bbW, &bbH);
    if (bbW <= 0 || bbH <= 0) return;
    int x, y, w, h;
    FlipViewRect(pViewSetup, bbW, bbH, x, y, w, h);

    if (!FlipCopyFrame(pRC, s_flipTex)) return;                           // capture the frame so far
    if (!FlipDrawMirror(pRC, s_flipMat, x, y, w, h, bbW, bbH)) return;    // draw it back, mirrored
    {
        FlipLock _l;
        FlipTrackMirror(s_core.track);
        if (!o_FlipPush2D) FlipTrackTwoD(s_core.track);   // no 2D push hook: gun beams get mirrored W2S
        FlipPublish();
    }

    static int s_lastRect[4] = { -1, -1, -1, -1 };
    if (s_lastRect[0] != x || s_lastRect[1] != y || s_lastRect[2] != w || s_lastRect[3] != h) {
        s_lastRect[0] = x; s_lastRect[1] = y; s_lastRect[2] = w; s_lastRect[3] = h;
        DebugLog("flipworld: mirroring before DrawViewModels (world + particles, gun unmirrored), view %d,%d %dx%d%s",
                 x, y, w, h, o_FlipPush2D ? "" : " -- no Push2DView hook, W2S world pass ends at the mirror");
    }
}

// angle-placed HUD patches (flipmath.h k_flipHudPatches)
struct FlipPatchSite { uint8_t* at; bool mirrored; };
static FlipPatchSite s_patch[k_flipHudPatchCount] = {};

// 1 = reads `want`, 0 = other state, -1 = neither / unreadable
static int FlipPatchState(const uint8_t* at, const FlipHudPatch& p, bool want)
{
    __try {
        if (memcmp(at, FlipHudPatchBytes(p, want), p.len) == 0)  return 1;
        if (memcmp(at, FlipHudPatchBytes(p, !want), p.len) == 0) return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static bool FlipPatchWrite(uint8_t* at, const FlipHudPatch& p, bool mirrored)
{
    const unsigned char* bytes = FlipHudPatchBytes(p, mirrored);
    bool ok = false;
    __try {
        DWORD old = 0;
        if (VirtualProtect(at, p.len, PAGE_EXECUTE_READWRITE, &old)) {
            for (int i = 0; i < p.len; ++i) if (at[i] != bytes[i]) at[i] = bytes[i];
            VirtualProtect(at, p.len, old, &old);
            FlushInstructionCache(GetCurrentProcess(), at, p.len);
            ok = true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

static void FlipHudPatchesResolve()
{
    const HMODULE client = GetModuleHandleA("client.dll");
    int found = 0;
    for (int i = 0; i < k_flipHudPatchCount; ++i) {
        const FlipHudPatch& p = k_flipHudPatches[i];
        uint8_t* hit = client ? PatternScan(client, p.sig) : nullptr;
        uint8_t* at  = hit ? hit + p.at : nullptr;
        if (at && FlipPatchState(at, p, false) < 0) at = nullptr;   // neither shipped nor patched: refuse
        s_patch[i] = { at, false };
        if (at) ++found;
        else DebugLog("flipworld: HUD patch '%s' NOT FOUND -- that element stays unmirrored", p.what);
    }
    DebugLog("flipworld: %d/%d angle-placed HUD patches resolved", found, k_flipHudPatchCount);
}

// patched code = vgui paint/think, game thread only. Core calls this on game thread, or after it stalled 1s.
static void FlipHudPatchesSet(bool mirrored)
{
    for (int i = 0; i < k_flipHudPatchCount; ++i) {
        FlipPatchSite& s = s_patch[i];
        if (!s.at || s.mirrored == mirrored) continue;
        const FlipHudPatch& p = k_flipHudPatches[i];
        const int st = FlipPatchState(s.at, p, mirrored);
        if (st < 0 || (st == 0 && !FlipPatchWrite(s.at, p, mirrored))) {
            DebugLog("flipworld: HUD patch '%s' could not be %s -- dropped", p.what, mirrored ? "applied" : "restored");
            s.at = nullptr;
            continue;
        }
        s.mirrored = mirrored;
    }
}

// engine side of FlipCoreTick
static ConVar* s_cvYaw     = nullptr;
static ConVar* s_cvFlipVM  = nullptr;
static int     s_offFlipVM = 0;         // DT_TFPlayer m_bFlipViewModels: 0 unresolved, -1 missing

using WriteVars_t = void(__fastcall*)(void*, void*, bool);
static WriteVars_t o_FlipWriteVars = nullptr;

// projectile hand as networked / demo-recorded: CTFPlayer::m_bFlipViewModels (c_tf_player.cpp:3763). -1 = unknown.
static int FlipNetworkedHand()
{
    if (s_offFlipVM == 0) {
        if (RecvTable* t = FindNetworkTable("DT_TFPlayer")) {      // null = tables not up yet, retry
            const int off = FindPropOffset(t, "m_bFlipViewModels");
            s_offFlipVM = off > 0 ? off : -1;
            DebugLog("flipworld: m_bFlipViewModels offset 0x%X", s_offFlipVM);
        }
    }
    if (s_offFlipVM <= 0) return -1;
    const int idx = GetLocalPlayerIndex();
    void* p = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!p) return -1;
    int v = -1;
    __try { v = *reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(p) + s_offFlipVM) ? 1 : 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { v = -1; }
    return v;
}

static bool FlipEngineBool(int slot)
{
    bool r = false;
    __try { r = VCall<bool>(g_engine, slot); } __except(EXCEPTION_EXECUTE_HANDLER) { r = false; }
    return r;
}

// m_pszString of a cvar whose string is a lone digit, else null.
static char* FlipCvarString(ConVar* cv)
{
    char* s = nullptr;
    __try {
        const uintptr_t base   = reinterpret_cast<uintptr_t>(cv);
        uintptr_t       parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
        if (!parent) parent = base;
        s = *reinterpret_cast<char**>(parent + cvString);
        if (s && (s[0] == 0 || s[1] != 0)) s = nullptr;   // touch both bytes here, under SEH
    } __except(EXCEPTION_EXECUTE_HANDLER) { s = nullptr; }
    return s;
}

static void FlipSetStringDigit(char* s, char digit)
{
    __try { s[0] = digit; } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// hand in m_pszString (server + config.cfg see it). m_nValue poked while held, never source. -1 = unreadable.
static int FlipCvarStringHand(ConVar* cv)
{
    int h = -1;
    __try {
        const uintptr_t base   = reinterpret_cast<uintptr_t>(cv);
        uintptr_t       parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
        if (!parent) parent = base;
        if (const char* s = *reinterpret_cast<char**>(parent + cvString)) h = atof(s) != 0.0 ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { h = -1; }
    return h;
}

static int FlipCvarInt(ConVar* cv)
{
    int v = 0;
    __try {
        const uintptr_t base   = reinterpret_cast<uintptr_t>(cv);
        uintptr_t       parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
        if (!parent) parent = base;
        v = *reinterpret_cast<const int*>(parent + cvInt);
    } __except(EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

struct FlipEnv {
    bool TakeOwner()
    {
        if (!s_shared) return false;
        const LONG was = InterlockedCompareExchange(&s_shared->owner, k_flipToken, 0);
        if (was == 0 || was == k_flipToken) { DebugLog("flipworld: this DLL owns flip world"); return true; }
        static uint64_t s_nextLog = 0;
        if (GetTickCount64() >= s_nextLog) {
            s_nextLog = GetTickCount64() + 60000;
            DebugLog("flipworld: another DLL in this process owns flip world -- this copy stays off");
        }
        return false;
    }
    void DropOwner()
    {
        if (!s_shared) return;
        InterlockedExchange(&s_shared->frameMirrored, 0);
        InterlockedCompareExchange(&s_shared->owner, 0, k_flipToken);
        DebugLog("flipworld: flip world ownership released");
    }
    bool  HasYaw()           { return s_cvYaw != nullptr; }
    float Yaw()              { return s_cvYaw->GetFloat(); }
    void  SetYaw(float v)    { s_cvYaw->ForceFloat(v); }   // never the string: config.cfg keeps the player's
    void  SetHudPatches(bool on) { FlipHudPatchesSet(on); }
    bool  HasHand()          { return s_cvFlipVM != nullptr; }
    int   Hand()             { const int h = FlipCvarStringHand(s_cvFlipVM); return h >= 0 ? h : (FlipCvarInt(s_cvFlipVM) ? 1 : 0); }
    int   ClientHand()       { return FlipCvarInt(s_cvFlipVM); }
    // ForceValue: raw int + float, no callback -> server keeps hearing string
    void  SetClientHand(int value)
    {
        s_cvFlipVM->ForceValue(value);
        DebugLog("flipworld: cl_flipviewmodels client int -> %d (string %d)", value, Hand());
    }
    // SetValue NOT ForceFloat: only ChangeStringValue runs engine's global callback that sends NET_SetConVar
    bool SetHand(int value, int cur, int prefAfter, int target)
    {
        const bool ok = s_cvFlipVM->SetValueInt("cl_flipviewmodels", value);
        static bool s_failLogged = false;
        if (ok || !s_failLogged) {
            DebugLog("flipworld: cl_flipviewmodels %d -> %d (%s, player's own %d)%s", cur, value,
                     prefAfter < 0 ? "released" : target < 0 ? "hold, live" : "hold, demo's recorded hand",
                     prefAfter >= 0 ? prefAfter : value, ok ? "" : " SETVALUE FAILED, retrying");
            s_failLogged = !ok;
        }
        return ok;
    }
    bool IsHLTV()       { return FlipEngineBool(engIsHLTV); }
    bool IsDemo()       { return FlipEngineBool(engIsPlayingDemo); }
    int  RecordedHand() { return FlipNetworkedHand(); }
    // eject w/o game-thread release: hold stays this session, but string (config.cfg) gets own digit
    // now since config writer hook is coming off
    void SaveOwnHandForConfig(int own)
    {
        if (char* s = FlipCvarString(s_cvFlipVM)) FlipSetStringDigit(s, own ? '1' : '0');
        DebugLog("flipworld: eject could not release cl_flipviewmodels on the game thread -- it stays at %d "
                 "for this session, config.cfg will save the player's own %d", own ? 0 : 1, own);
    }
};

static void FlipRunTick(bool onGameThread)
{
    const uint64_t now = GetTickCount64();
    if (onGameThread) {
        s_gameTid.store(GetCurrentThreadId());
        s_lastTickMs.store(now);
        if (g_cvar) {
            if (!s_cvYaw)    s_cvYaw    = g_cvar->FindVar("m_yaw");
            if (!s_cvFlipVM) s_cvFlipVM = g_cvar->FindVar("cl_flipviewmodels");
        }
    }
    const uint64_t last = s_lastTickMs.load();
    const bool wanted = onGameThread && FlipWanted();
    const FlipCoreIn in = { wanted, o_FlipWriteVars != nullptr, onGameThread,
                            !onGameThread && (last == 0 || now - last >= 1000) };
    FlipLock _l;
    FlipEnv env;
    FlipCoreTick(s_core, env, in);
    s_active = wanted && s_core.owner;
    FlipPublish();
}

void FlipWorld_Tick()
{
    FlipRunTick(true);
}

// LevelShutdown (game thread): no mirror / hand hold between maps
void FlipWorld_LevelShutdown()
{
    s_flipReady = false;
    FlipRunTick(true);
}

// eject: EjectRestore_Service on game thread; thread.cpp call = fallback
void FlipWorld_Release()
{
    FlipRunTick(OnGameThread());
}

// after hooks off + drained: token must not outlive DLL
void FlipWorld_Unload()
{
    if (!s_shared) return;
    InterlockedCompareExchange(&s_shared->owner, 0, k_flipToken);
    if (s_core.owner) InterlockedExchange(&s_shared->frameMirrored, 0);
    UnmapViewOfFile(s_shared);
    CloseHandle(s_sharedMap);
    s_shared = nullptr; s_sharedMap = nullptr;
}

// CCvarUtilities::WriteVariables hook: while held, config.cfg gets player's own digit (FlipCfgWrite)
static void __fastcall h_FlipWriteVars(void* rcx, void* buffer, bool allVars)
{
    FlipHookDepth _d;
    int pref = -1;
    { FlipLock _l; pref = s_core.vmPref; }
    char* s = (pref >= 0 && s_cvFlipVM) ? FlipCvarString(s_cvFlipVM) : nullptr;
    FlipCfgWrite(s, pref, [&] { o_FlipWriteVars(rcx, buffer, allVars); });
}

// IVRenderView::Push2DView hook: 3D pass over
static void __fastcall h_FlipPush2D(void* rcx, const void* view, int flags, void* rt, void* frustum)
{
    FlipHookDepth _d;
    if (OnGameThread()) {
        FlipLock _l;
        FlipTrackTwoD(s_core.track);
        FlipPublish();
    }
    ScreenFx_OnPush2DView();   // frame done, hud next: bloom + cc
    o_FlipPush2D(rcx, view, flags, rt, frustum);
}

// CTFViewModel::CalcViewModelLag hook: sway under mirror (FlipMirrorSway)
static constexpr const char* k_flipSwaySig =
    "48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 80 00 00 00 48 8B F1 49 8B F8 48 8B 0D ? ? ? ? 48 8B EA 48 8B 01 FF 50 70 84 C0 0F 85 ? ? ? ? 48 8B 05 ? ? ? ? 0F 57 C0 0F 2F 40 54";
using VmLag_t = void(__fastcall*)(void*, float*, float*, float*);
static VmLag_t o_FlipVmLag = nullptr;

__declspec(noinline) static bool FlipReadVec3(const float* p, float out[3])
{
    __try { out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; return true; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

__declspec(noinline) static void FlipSwayAt(const float before[3], float* origin, const float* angles)
{
    __try { FlipMirrorSway(before, origin, angles); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void __fastcall h_FlipVmLag(void* rcx, float* origin, float* angles, float* original)
{
    FlipHookDepth _d;
    float before[3];
    const bool mirror = FlipWorld_Mirroring() && origin && angles && FlipReadVec3(origin, before);
    o_FlipVmLag(rcx, origin, angles, original);
    if (mirror) FlipSwayAt(before, origin, angles);
    static bool s_was = false;
    if (s_was != mirror) {
        s_was = mirror;
        DebugLog("flipworld: weapon sway %s", mirror ? "mirrored" : "normal");
    }
}

static void FlipHookSlot(void* iface, int slot, void* detour, void** orig, const char* what)
{
    void* fn = nullptr;
    __try {
        void** vt = iface ? *(void***)iface : nullptr;
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000) fn = vt[slot];
    } __except(EXCEPTION_EXECUTE_HANDLER) { fn = nullptr; }
    if (fn && MH_CreateHook(fn, detour, orig) == MH_OK) {
        MH_EnableHook(fn);
        DebugLog("flipworld: %s hook installed", what);
    } else {
        *orig = nullptr;
        DebugLog("flipworld: %s hook FAILED", what);
    }
}

// PollThread, once: shared owner mapping, config writer hook, 2D push hook, HUD patch sites.
void FlipWorld_Install()
{
    FlipSharedOpen();
    const HMODULE engine = GetModuleHandleA("engine.dll");
    // CCvarUtilities::WriteVariables(CUtlBuffer&, bool): `%s "%s"\n` of every FCVAR_ARCHIVE cvar.
    uint8_t* wv = engine ? PatternScan(engine, "40 57 41 54 41 56 41 57 48 83 EC 48 48 8B 0D ? ? ? ? 45 33 E4 4C 89 A4 24 88 00 00 00 45 0F B6 F0 4C 8B FA") : nullptr;
    if (wv && MH_CreateHook(wv, (LPVOID)h_FlipWriteVars, (LPVOID*)&o_FlipWriteVars) == MH_OK) {
        MH_EnableHook(wv);
        DebugLog("flipworld: config writer hook installed at %p", wv);
    } else {
        o_FlipWriteVars = nullptr;
        DebugLog("flipworld: config writer hook FAILED -- the hand will not be inverted");
    }
    FlipHookSlot(g_renderView, rvPush2DView, (void*)h_FlipPush2D, (void**)&o_FlipPush2D, "IVRenderView::Push2DView");
    const HMODULE client = GetModuleHandleA("client.dll");
    uint8_t* lag = client ? PatternScan(client, k_flipSwaySig) : nullptr;
    if (lag && MH_CreateHook(lag, (LPVOID)h_FlipVmLag, (LPVOID*)&o_FlipVmLag) == MH_OK) {
        MH_EnableHook(lag);
        DebugLog("flipworld: CTFViewModel::CalcViewModelLag hook installed at %p", lag);
    } else {
        o_FlipVmLag = nullptr;
        DebugLog("flipworld: CTFViewModel::CalcViewModelLag hook FAILED -- weapon sway stays unmirrored");
    }
    FlipHudPatchesResolve();
}

FlipCreateMove_t o_FlipCreateMove = nullptr;

static void FlipMirrorCmdAt(void* pCmd)   // SEH leaf (C2712)
{
    __try {
        const uintptr_t p = reinterpret_cast<uintptr_t>(pCmd);
        FlipMirrorCmd(*reinterpret_cast<float*>(p + cmdSidemove), *reinterpret_cast<int*>(p + cmdButtons));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// web surface: mouse1/2 on screen = page clicks; strip IN_ATTACK|IN_ATTACK2 (in_buttons.h bits 0, 11)
static void WebStripFireAt(void* pCmd)   // SEH leaf (C2712)
{
    __try {
        *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pCmd) + cmdButtons) &= ~((1 << 0) | (1 << 11));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

bool __fastcall h_FlipCreateMove(void* rcx, float flInputSampleTime, void* pCmd)
{
    FlipHookDepth _d;
    FlipWorld_Tick();
    // BEFORE original: C_TFPlayer::CreateMove turns sidemove into taunt yaw/kart steer then zeroes it
    // (c_tf_player.cpp:6567/6607). Checksum taken after hook returns -> mirrored cmd is sent.
    if (pCmd && FlipWorld_Mirroring()) FlipMirrorCmdAt(pCmd);
    const bool ret = o_FlipCreateMove(rcx, flInputSampleTime, pCmd);
    if (pCmd && Web_BlockFire()) WebStripFireAt(pCmd);
    return ret;
}

// WorldToScreenMatrix hook: game + render thread callers -> mirrored copy thread_local, read at once.
FlipW2S_t o_FlipW2S = nullptr;

__declspec(noinline) static bool FlipCopyMatrix(float dst[4][4], const float* src)
{
    __try { memcpy(dst, src, sizeof(float) * 16); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

const float* __fastcall h_FlipW2S(void* rcx)
{
    FlipHookDepth _d;
    const float* m = o_FlipW2S(rcx);
    if (!m || !FlipW2SMirrored(FlipWorld_Mirroring(), OnGameThread(), s_pubInWorldPass.load())) return m;

    thread_local float t[4][4];
    if (!FlipCopyMatrix(t, m)) return m;
    if (!FlipIsProjection(t)) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("flipworld: WorldToScreenMatrix returned a non-projection matrix -- HUD left unmirrored"); }
        return m;
    }
    FlipMirrorW2S(t);
    return t[0];
}
