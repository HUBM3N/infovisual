#include "visuals.h"
#include "vmglow.h"
#include "flipworld.h"
#include "skins.h"   // Skins_HeldWeaponTurnsToGold -- the gold statue in h_CreateTFRagdoll
#include "screenfx.h"   // ScreenFx_LevelShutdown -- mat_queue_mode released before a load
#include "websurface.h"  // Web_LevelShutdown -- the placed screen dies with the map
#include "crosshair.h"  // ScopeCsgo_Active -- "crosshair" held 0 under the csgo scope

char  s_fogColorBuf[32]       = {};
char* s_fogColorOrigPtr       = nullptr;
int   s_fogColorOrigLen       = 0;
char  s_fogColorSkyBuf[32]    = {};
char* s_fogColorSkyOrigPtr    = nullptr;
int   s_fogColorSkyOrigLen    = 0;

static void* s_critMat           = nullptr;  // "effects/crit", no ref held: dropped every level change
static bool  s_matWasEnabledCrit = false;
static bool  s_matWasEnabledDR   = false;
std::atomic<bool> g_modWorldValuesDirty{true}; // menu / map textures: re-apply world + sky modulation
std::atomic<uint32_t> g_matEpoch{1};

// Drops every cached IMaterial*. Game thread only (level hooks + FrameStageNotify).
// bUnloading: eject path, materials are live and the map gets its own look back.
void ResetMatCache(bool bUnloading)
{
    s_critMat = nullptr;
    s_matWasEnabledCrit = s_matWasEnabledDR = false;
    VmResetMatCache();
    PlResetMatCache();
    ResetMapTextures(bUnloading);
}

// Crit / DR ColorModulate. FRAME_RENDER_START, game thread.
void ApplyMatColorModulate()
{
    if (!g_matSystem || !IsInGame()) return;

    float sheenCol[3];
    if (g_enableSheenAnim) GetAnimatedSheenColor(sheenCol);
    else { sheenCol[0]=g_sheenColor[0]; sheenCol[1]=g_sheenColor[1]; sheenCol[2]=g_sheenColor[2]; }

    if (g_enableCritColor || s_matWasEnabledCrit) {
        const float  white[3] = { 1.f, 1.f, 1.f };
        const float* cc = !g_enableCritColor ? white : g_critUseSheenColor ? sheenCol : g_critColor;
        __try {
            if (!s_critMat) {
                void* m = MatSysFindMaterial(g_matSystem, "effects/crit");
                s_critMat = (m && !MatIsError(m)) ? m : nullptr;
            }
            if (s_critMat) MatColorModulate(s_critMat, cc[0], cc[1], cc[2]);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) { s_critMat = nullptr; }
        s_matWasEnabledCrit = g_enableCritColor;
    }

    // HUD mats rebuilt on hud reload -> look up every time, no cache
    if (g_enableDRColor || s_matWasEnabledDR) {
        const float  white[3] = { 1.f, 1.f, 1.f };
        const float* dc = !g_enableDRColor ? white : g_drUseSheenColor ? sheenCol : g_drColor;
        __try {
            void* bg = MatSysFindMaterial(g_matSystem, "vgui/pocket_watch_bg");
            void* fg = MatSysFindMaterial(g_matSystem, "vgui/pocket_watch_fg");
            if (bg && !MatIsError(bg)) MatColorModulate(bg, dc[0], dc[1], dc[2]);
            if (fg && !MatIsError(fg)) MatColorModulate(fg, dc[0], dc[1], dc[2]);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        s_matWasEnabledDR = g_enableDRColor;
    }
}

// World / sky modulation: walk live mat list, never cache IMaterial* (IsInGame edge misses
// map change -> stale freed ptrs).
static bool  s_modApplied   = false;   // our colours are on the materials right now
static DWORD s_modReapplyAt = 0;       // late pass for sky materials that load after us

__declspec(noinline) static void ModulateMaterials_SEH(const float* world, const float* sky)
{
    unsigned short hInvalid = MatSysInvalidMaterial(g_matSystem);
    __try {
        for (unsigned short h = MatSysFirstMaterial(g_matSystem); h != hInvalid;
             h = MatSysNextMaterial(g_matSystem, h))
        {
            void* m = MatSysGetMaterial(g_matSystem, h);
            if (!m || MatIsError(m)) continue;
            const char* grp = MatGetTextureGroupName(m);
            if (!grp) continue;
            if (strncmp(grp, "World textures", 14) == 0) {
                if (world) MatColorModulate(m, world[0], world[1], world[2]);
                continue;
            }
            // Sky by group or by its skybox/ path; a bare "sky" substring also caught "skylight".
            const char* name = (strncmp(grp, "SkyBox textures", 15) == 0) ? "skybox/" : MatGetName(m);
            if (name && strncmp(name, "skybox/", 7) == 0) MatColorModulate(m, sky[0], sky[1], sky[2]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Sky name just changed: its materials load on the next draw, tint them once they exist.
static void Modulate_Resync() { s_modReapplyAt = GetTickCount() + 500; }

void Modulate_Restore()
{
    if (!s_modApplied || !g_matSystem) return;
    static const float white[3] = { 1.f, 1.f, 1.f };
    // Map textures own the world materials while active; leave their colour alone.
    ModulateMaterials_SEH(MapTexturesOwnWorld() ? nullptr : white, white);
    s_modApplied = false;
}

void Modulate_Tick()
{
    if (!g_matSystem || !IsInGame()) return;

    bool run = g_modWorldValuesDirty.exchange(false);
    static uint32_t s_epoch = 0;
    const uint32_t epoch = g_matEpoch.load();
    if (epoch != s_epoch) {
        s_epoch      = epoch;
        s_modApplied = false;                    // new map, materials are back to white
        Modulate_Resync();
    }
    if (s_modReapplyAt && GetTickCount() >= s_modReapplyAt) { s_modReapplyAt = 0; run = true; }

    if (!g_enableModulate) { Modulate_Restore(); return; }
    if (s_modApplied && !run) return;
    ModulateMaterials_SEH(MapTexturesOwnWorld() ? nullptr : g_modulateWorld, g_modulateSky);
    s_modApplied = true;
}

// Custom skybox: sv_skyname replicated, server rewrites each map load -> re-assert on drift
// via cvar SetValue so string stays engine-owned (never swap the string ptr).
ConVar*     g_cv_sv_skyname = nullptr;
static char s_mapSky[64]    = {};   // last sky the map asked for
static char s_forcedSky[64] = {};   // last value we wrote, empty = not forcing

void Sky_Restore()
{
    if (!s_forcedSky[0] || !g_cv_sv_skyname) return;
    const char* cur = g_cv_sv_skyname->GetString();
    if (cur && s_mapSky[0] && strcmp(cur, s_forcedSky) == 0)
        g_cv_sv_skyname->SetValueStr("sv_skyname", s_mapSky);
    s_forcedSky[0] = '\0';
}

void Sky_Tick()
{
    if (!g_cv_sv_skyname) return;
    if (!g_enableSkybox || !g_skyboxName[0]) { Sky_Restore(); return; }

    // Typing into the name box would load a sky per keystroke; wait for it to settle.
    static char  s_want[64] = {};
    static DWORD s_wantAt   = 0;
    char want[64];
    strncpy_s(want, g_skyboxName, _TRUNCATE);
    if (strcmp(want, s_want) != 0) { strcpy_s(s_want, want); s_wantAt = GetTickCount(); }
    if (s_forcedSky[0] && GetTickCount() - s_wantAt < 300) return;

    const char* cur = g_cv_sv_skyname->GetString();
    if (!cur || strcmp(cur, want) == 0) return;
    if (strcmp(cur, s_forcedSky) != 0) strncpy_s(s_mapSky, cur, _TRUNCATE);   // map wrote it
    if (!g_cv_sv_skyname->SetValueStr("sv_skyname", want)) return;
    strcpy_s(s_forcedSky, want);
    Modulate_Resync();
}

// IBaseClientDLL slot 6 LevelInitPostEntity, 7 LevelShutdown (sdk cdll_int.h order; 8 GetAllClasses
// in netvars.cpp). Both bump g_matEpoch -> FSN + every per-map cache key off it.
LevelFn_t o_LevelInitPostEntity = nullptr;
LevelFn_t o_LevelShutdown       = nullptr;

// Shutdown -> InitPostEntity = engine-only load time (watchdog can't split it, Present frozen whole load).
static DWORD s_levelLoadStartMs = 0;

void __fastcall h_LevelShutdown(void* rcx)
{
    if (!g_eject) {
        ++g_miscHookDepth;
        g_matEpoch.fetch_add(1);
        ResetMatCache(false);   // our refs go while the materials are still alive
        FlipWorld_LevelShutdown();   // hand cl_flipviewmodels back before a quit writes config.cfg
        // Whatever the next map precaches, it precaches at the user's mat_queue_mode.
        ScreenFx_LevelShutdown();
        Web_LevelShutdown();         // the placed screen belongs to the map that is going away
        --g_miscHookDepth;
        s_levelLoadStartMs = GetTickCount();
    }
    o_LevelShutdown(rcx);
}

void __fastcall h_LevelInitPostEntity(void* rcx)
{
    o_LevelInitPostEntity(rcx);
    if (!g_eject) {
        g_matEpoch.fetch_add(1);
        if (s_levelLoadStartMs) {
            DebugLog("level load: %u ms engine-side", GetTickCount() - s_levelLoadStartMs);
            s_levelLoadStartMs = 0;
        }
    }
}

void* GetInterface(const char* module, const char* name) {
    HMODULE mod = GetModuleHandleA(module);
    if (!mod) return nullptr;
    using CI_t = void*(*)(const char*, int*);
    auto ci = (CI_t)GetProcAddress(mod, "CreateInterface");
    return ci ? ci(name, nullptr) : nullptr;
}

// Hold cvar at our value while feature on; give user's value back once on off/eject.
// Never force "off" value each tick: clobbers user's own crosshair / sway / holiday.
struct CvarHold
{
    const char* name;
    ConVar*     cv    = nullptr;
    bool        held  = false;
    float       saved = 0.f;

    void Apply(bool want, float value)
    {
        if (!cv && g_cvar) cv = g_cvar->FindVarWalk(name);
        if (!cv) return;
        if (want) {
            if (!held) { held = true; saved = cv->GetFloat(); }
            cv->ForceFloat(value);
        } else if (held) {
            held = false;
            cv->ForceFloat(saved);
        }
    }
};

static CvarHold s_holdCheats      { "sv_cheats" };
static CvarHold s_holdFogOverride { "fog_override" };
static CvarHold s_holdFogEnable   { "fog_enable" };
static CvarHold s_holdFogEnableSky{ "fog_enableskybox" };
static CvarHold s_holdSwayScale   { "cl_wpn_sway_scale" };
static CvarHold s_holdSwayInterp  { "cl_wpn_sway_interp" };
static CvarHold s_holdBobCycle    { "cl_bobcycle" };
static CvarHold s_holdBobUp       { "cl_bobup" };
static CvarHold s_holdCrosshair   { "crosshair" };
static CvarHold s_holdHoliday     { "tf_forced_holiday" };

void VisualsCvars_Release()
{
    for (CvarHold* h : { &s_holdCheats, &s_holdFogOverride, &s_holdFogEnable, &s_holdFogEnableSky,
                         &s_holdSwayScale, &s_holdSwayInterp, &s_holdBobCycle, &s_holdBobUp,
                         &s_holdCrosshair, &s_holdHoliday })
        h->Apply(false, 0.f);
}

void UpdateVisualsCvars()
{
    if (!g_cvar) return;

    // fog_override is a cheat cvar, so custom fog holds sv_cheats too
    s_holdCheats.Apply(g_enableFog, 1.f);
    s_holdFogOverride.Apply(g_enableFog, 1.f);
    s_holdFogEnable.Apply(g_enableFog, 1.f);
    s_holdFogEnableSky.Apply(g_enableFog, 1.f);
    s_holdSwayScale.Apply(g_enableViewmodelSway, g_vmSwayScale);
    s_holdSwayInterp.Apply(g_enableViewmodelSway, g_vmSwayInterp);
    s_holdBobCycle.Apply(g_removeViewmodelBob, 999.f);
    s_holdBobUp.Apply(g_removeViewmodelBob, 0.f);
    s_holdCrosshair.Apply(g_enableCrosshair || g_chCsgoEnable || ScopeCsgo_Active(), 0.f);
    s_holdHoliday.Apply(g_hwForceHoliday, 2.f);

    {
        static ConVar* cv_fog_start            = nullptr;
        static ConVar* cv_fog_end              = nullptr;
        static ConVar* cv_fog_startskybox      = nullptr;
        static ConVar* cv_fog_endskybox        = nullptr;
        static ConVar* cv_fog_maxdensity       = nullptr;
        static ConVar* cv_fog_maxdensityskybox = nullptr;
        static ConVar* cv_fog_color            = nullptr;
        static ConVar* cv_fog_colorskybox      = nullptr;

        if (!cv_fog_start)            cv_fog_start            = g_cvar->FindVarWalk("fog_start");
        if (!cv_fog_end)              cv_fog_end              = g_cvar->FindVarWalk("fog_end");
        if (!cv_fog_startskybox)      cv_fog_startskybox      = g_cvar->FindVarWalk("fog_startskybox");
        if (!cv_fog_endskybox)        cv_fog_endskybox        = g_cvar->FindVarWalk("fog_endskybox");
        if (!cv_fog_maxdensity)       cv_fog_maxdensity       = g_cvar->FindVarWalk("fog_maxdensity");
        if (!cv_fog_maxdensityskybox) cv_fog_maxdensityskybox = g_cvar->FindVarWalk("fog_maxdensityskybox");
        if (!cv_fog_color)            cv_fog_color            = g_cvar->FindVarWalk("fog_color");
        if (!cv_fog_colorskybox)      cv_fog_colorskybox      = g_cvar->FindVarWalk("fog_colorskybox");

        auto WriteFloat = [](ConVar* cv, float v) {
            if (!cv) return;
            __try {
                uintptr_t base   = reinterpret_cast<uintptr_t>(cv);
                uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
                if (parent < 0x10000 || parent > 0x7FFFFFFFFFFF) parent = base;
                *reinterpret_cast<float*>(parent + 0x54) = v;
                *reinterpret_cast<int*>  (parent + 0x58) = static_cast<int>(v);
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        };

        // Point cvar m_pszString at our 32-byte buffer, rewrite colour each tick.
        auto WriteFogColorStr = [](ConVar* cv, char* buf, char** origPtr, int* origLen, const char* val)
        {
            if (!cv || !val) return;
            __try {
                uintptr_t base   = reinterpret_cast<uintptr_t>(cv);
                uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
                if (parent < 0x10000 || parent > 0x7FFFFFFFFFFF) parent = base;
                char** ppStr = reinterpret_cast<char**>(parent + 0x48);
                int*   pLen  = reinterpret_cast<int*>  (parent + 0x50);
                // save orig ptr once -> restore on disable/eject
                if (!*origPtr && *ppStr) {
                    *origPtr = *ppStr;
                    *origLen = *pLen;
                }
                strncpy(buf, val, 31);
                buf[31] = '\0';
                *ppStr = buf;
                *pLen  = 32;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        };

        auto RestoreFogColorStr = [](ConVar* cv, char** origPtr, int* origLen)
        {
            if (!cv || !*origPtr) return;
            __try {
                uintptr_t base   = reinterpret_cast<uintptr_t>(cv);
                uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
                if (parent < 0x10000 || parent > 0x7FFFFFFFFFFF) parent = base;
                *reinterpret_cast<char**>(parent + 0x48) = *origPtr;
                *reinterpret_cast<int*>  (parent + 0x50) = *origLen;
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            *origPtr = nullptr;
        };

        static bool s_fogWasEnabled = false;

        if (g_enableFog)
        {
            float skyStart   = g_fogLinkSkybox ? g_fogStart   : g_fogSkyboxStart;
            float skyEnd     = g_fogLinkSkybox ? g_fogEnd     : g_fogSkyboxEnd;
            float skyDensity = g_fogLinkSkybox ? g_fogDensity : g_fogSkyboxDensity;
            WriteFloat(cv_fog_start,            g_fogStart);
            WriteFloat(cv_fog_end,              g_fogEnd);
            WriteFloat(cv_fog_startskybox,      skyStart);
            WriteFloat(cv_fog_endskybox,        skyEnd);
            WriteFloat(cv_fog_maxdensity,       g_fogDensity);
            WriteFloat(cv_fog_maxdensityskybox, skyDensity);

            float fc[3];
            if (g_fogUseSheenColor && g_enableSheenAnim) GetAnimatedSheenColor(fc);
            else if (g_fogUseSheenColor) { fc[0] = g_sheenColor[0]; fc[1] = g_sheenColor[1]; fc[2] = g_sheenColor[2]; }
            else                         { fc[0] = g_fogColor[0];   fc[1] = g_fogColor[1];   fc[2] = g_fogColor[2]; }

            char colorStr[32];
            snprintf(colorStr, sizeof(colorStr), "%d %d %d",
                (int)(fc[0] * 255.f), (int)(fc[1] * 255.f), (int)(fc[2] * 255.f));

            WriteFogColorStr(cv_fog_color,       s_fogColorBuf,    &s_fogColorOrigPtr,    &s_fogColorOrigLen,    colorStr);
            WriteFogColorStr(cv_fog_colorskybox, s_fogColorSkyBuf, &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen, colorStr);
            s_fogWasEnabled = true;
        }
        else if (s_fogWasEnabled)
        {
            RestoreFogColorStr(cv_fog_color,       &s_fogColorOrigPtr,    &s_fogColorOrigLen);
            RestoreFogColorStr(cv_fog_colorskybox, &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen);
            s_fogWasEnabled = false;
        }
    }
}

#define FLOW_OUTGOING 0
#define FLOW_INCOMING 1

int g_netInfoIndex = -1;

static void DetectNetChannelIndex() {
    if (!g_engine) return;
    int candidates[] = {72,74,76,78,80,81,82,84};
    for (int idx : candidates) {
        __try {
            auto nci = VCall<INetChannelInfo*>(g_engine, idx);
            if (!nci) continue;
            float test = VCall<float>(nci, 9, 0);
            if (test >= 0.f && test < 5.f) { g_netInfoIndex = idx; break; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

void UpdateNetStats() {
    if (!g_engine) return;

    bool inGame = false;
    __try { inGame = VCall<bool>(g_engine, 26); }
    __except(EXCEPTION_EXECUTE_HANDLER) { g_conn.store(false); g_inGame.store(false); return; }

    g_inGame.store(inGame);
    if (!inGame) { g_conn.store(false); return; }

    if (g_netInfoIndex < 0) {
        DetectNetChannelIndex();
        if (g_netInfoIndex < 0) return;
    }

    __try {
        auto nci = VCall<INetChannelInfo*>(g_engine, g_netInfoIndex);
        if (!nci) { g_conn.store(false); return; }

        float in   = VCall<float>(nci, 9, FLOW_INCOMING);
        float out  = VCall<float>(nci, 9, FLOW_OUTGOING);
        float loss = VCall<float>(nci, 11, FLOW_INCOMING);

        int rawPing = (int)((in + out) * 1000.f);
        g_ping.store(rawPing > k_pingSubtract ? rawPing - k_pingSubtract : 0);
        g_loss.store((int)(loss * 100.f));
        g_conn.store(true);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_netInfoIndex = -1;
        g_conn.store(false);
    }
}

void UpdateFPS() {
    static LARGE_INTEGER freq{}, last{};
    static int frames = 0;
    static float acc = 0.f;

    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (last.QuadPart) {
        float dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
        acc += dt;
        frames++;
        if (acc >= 0.25f) {
            g_fps.store((float)frames / acc);
            frames = 0;
            acc    = 0.f;
        }
    }
    last = now;
}

// Chams. s_vmMat[0] unused (Original), [1..7] = chams mats.
static void* s_vmMat[8]         = {};
static bool  s_vmMatFilesWritten = false;

// Glow pass mat: UnlitGeneric $additive 1, separate from s_vmMat.
static void* s_vmGlowMat        = nullptr;
static bool  s_vmGlowMatWritten = false;

// True during weapon glow pass -> h_DrawModelExecute early-exit scales pBones first.
static bool s_vmGlowScaleBones = false;
static float s_scaledBones[256][3][4];

static const struct { const char* name; const char* vmt; } k_vmVmts[] = {
    { "iv_vm/flat",
        			"\"UnlitGeneric\""
			"\n{"
			"\n\t$basetexture \"white\""
			"\n}" },
    { "iv_vm/shaded",
        "\"VertexLitGeneric\""
			"\n{"
			"\n\t$basetexture \"white\""
			"\n}" },
    { "iv_vm/wireframe",
        "\"UnlitGeneric\""
			"\n{"
			"\n\t$basetexture \"white\""
			"\n\t$wireframe \"1\""
			"\n}" },
    { "iv_vm/fresnel",
        "\"VertexLitGeneric\""
			"\n{"
			"\n\t$basetexture \"white\""
			"\n\t$bumpmap \"models/player/shared/shared_normal\""
			"\n\t$color2 \"[0 0 0]\""
			"\n\t$additive \"1\""
			"\n\t$phong \"1\""
			"\n\t$phongfresnelranges \"[0 0.15 1]\""
			"\n\t$envmap \"skybox/sky_dustbowl_01\""
			"\n\t$envmapfresnel \"1\""
			"\n}" },
    { "iv_vm/shine",
        "\"VertexLitGeneric\""
			"\n{"
			"\n\t$additive \"1\""
			"\n\t$envmap \"cubemaps/cubemap_sheen002.hdr\""
			"\n\t$envmaptint \"[1 1 1]\""
			"\n}" },
    { "iv_vm/tint",
        "\"VertexLitGeneric\""
			"\n{"
			"\n\t$basetexture \"models/player/shared/ice_player\""
			"\n\t$bumpmap \"models/player/shared/shared_normal\""
			"\n\t$additive \"1\""
			"\n\t$phong \"1\""
			"\n\t$phongfresnelranges \"[0 0.001 0.001]\""
			"\n\t$envmap \"skybox/sky_dustbowl_01\""
			"\n\t$envmapfresnel \"1\""
			"\n\t$selfillum \"1\""
			"\n\t$selfillumtint \"[0 0 0]\""
			"\n}" },
    { "iv_vm/none",
        	"\"UnlitGeneric\""
			"\n{"
			"\n\t$color2 \"[0 0 0]\""
			"\n\t$additive \"1\""
			"\n}",
    }};
static_assert(sizeof(k_vmVmts)/sizeof(k_vmVmts[0]) == 7, "k_vmVmts must have 7 entries for matTypes 1..7");


// Player VMTs = vm VMTs + $model 1: renderer needs bone-weight vertex data for
// ForcedMaterialOverride on world player models, else reads uninit bones -> AV.
static const struct { const char* name; const char* vmt; } k_plVmts[] = {
    { "iv_pl/flat",
        "\"UnlitGeneric\""
        "\n{"
        "\n\t$basetexture \"white\""
        "\n\t$model \"1\""
        "\n}" },
    { "iv_pl/shaded",
        "\"VertexLitGeneric\""
        "\n{"
        "\n\t$basetexture \"white\""
        "\n\t$model \"1\""
        "\n}" },
    { "iv_pl/wireframe",
        "\"UnlitGeneric\""
        "\n{"
        "\n\t$basetexture \"white\""
        "\n\t$wireframe \"1\""
        "\n\t$model \"1\""
        "\n}" },
    { "iv_pl/fresnel",
        "\"VertexLitGeneric\""
        "\n{"
        "\n\t$basetexture \"white\""
        "\n\t$bumpmap \"models/player/shared/shared_normal\""
        "\n\t$color2 \"[0 0 0]\""
        "\n\t$additive \"1\""
        "\n\t$phong \"1\""
        "\n\t$phongfresnelranges \"[0 0.15 1]\""
        "\n\t$envmap \"skybox/sky_dustbowl_01\""
        "\n\t$envmapfresnel \"1\""
        "\n\t$model \"1\""
        "\n}" },
    { "iv_pl/shine",
        "\"VertexLitGeneric\""
        "\n{"
        "\n\t$additive \"1\""
        "\n\t$envmap \"cubemaps/cubemap_sheen002.hdr\""
        "\n\t$envmaptint \"[1 1 1]\""
        "\n\t$model \"1\""
        "\n}" },
    { "iv_pl/tint",
        "\"VertexLitGeneric\""
        "\n{"
        "\n\t$basetexture \"models/player/shared/ice_player\""
        "\n\t$bumpmap \"models/player/shared/shared_normal\""
        "\n\t$additive \"1\""
        "\n\t$phong \"1\""
        "\n\t$phongfresnelranges \"[0 0.001 0.001]\""
        "\n\t$envmap \"skybox/sky_dustbowl_01\""
        "\n\t$envmapfresnel \"1\""
        "\n\t$selfillum \"1\""
        "\n\t$selfillumtint \"[0 0 0]\""
        "\n\t$model \"1\""
        "\n}" },
    { "iv_pl/none",
        "\"UnlitGeneric\""
        "\n{"
        "\n\t$color2 \"[0 0 0]\""
        "\n\t$additive \"1\""
        "\n\t$model \"1\""
        "\n}" },
};
static_assert(sizeof(k_plVmts)/sizeof(k_plVmts[0]) == 7, "k_plVmts must have 7 entries");
static void* s_plMat[8] = {};
static bool  s_plMatFilesWritten = false;

// Write VMTs to tf/materials/iv_vm/ once; skips existing files.
static void VmWriteMatFiles()
{
    if (s_vmMatFilesWritten) return;
    s_vmMatFilesWritten = true;

    char cwd[MAX_PATH] = {};
    GetGameDir(cwd);

    char dir[MAX_PATH];
    sprintf_s(dir, "%s\\tf\\materials\\iv_vm", cwd);
    {
        char tmp[MAX_PATH]; sprintf_s(tmp, "%s\\tf", cwd);                   _mkdir(tmp);
        sprintf_s(tmp, "%s\\tf\\materials", cwd);                             _mkdir(tmp);
        sprintf_s(tmp, "%s\\tf\\materials\\iv_vm", cwd);                      _mkdir(tmp);
    }

    for (int i = 0; i < 7; ++i) {
        const char* slash = strrchr(k_vmVmts[i].name, '/');
        const char* base  = slash ? slash + 1 : k_vmVmts[i].name;
        char path[MAX_PATH];
        sprintf_s(path, "%s\\%s.vmt", dir, base);
        FILE* f = fopen(path, "r");
        if (f) { fclose(f); continue; }
        f = fopen(path, "w");
        if (f) { fputs(k_vmVmts[i].vmt, f); fclose(f); }
    }
}

void VmResetMatCache()
{
    // drop refs taken in VmGetMat; VMT files stay on disk
    for (int i = 1; i < 8; ++i) {
        if (s_vmMat[i]) {
            __try {
                if (!MatIsError(s_vmMat[i])) {
                    using Fn = void(__fastcall*)(void*);
                    ((Fn)(*(void***)s_vmMat[i])[13])(s_vmMat[i]); // DecrementReferenceCount
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_vmMat[i] = nullptr;
        }
    }
    if (s_vmGlowMat) {
        __try {
            if (!MatIsError(s_vmGlowMat)) {
                using Fn = void(__fastcall*)(void*);
                ((Fn)(*(void***)s_vmGlowMat)[13])(s_vmGlowMat); // DecrementReferenceCount
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        s_vmGlowMat = nullptr;
    }
}

// Rim glow VMT: UnlitGeneric $additive, tint from SetColorModulation.
static void VmWriteGlowMatFile()
{
    if (s_vmGlowMatWritten) return;
    s_vmGlowMatWritten = true;
    char cwd[MAX_PATH] = {};
    GetGameDir(cwd);
    { char tmp[MAX_PATH];
      sprintf_s(tmp, "%s\\tf", cwd);                    _mkdir(tmp);
      sprintf_s(tmp, "%s\\tf\\materials", cwd);          _mkdir(tmp);
      sprintf_s(tmp, "%s\\tf\\materials\\iv_vm", cwd);   _mkdir(tmp); }
    char path[MAX_PATH];
    sprintf_s(path, "%s\\tf\\materials\\iv_vm\\rimglow.vmt", cwd);
    // always overwrite: content may change between builds
    FILE* f = fopen(path, "w");
    if (f) {
        fputs(
            "\"UnlitGeneric\"\n"
            "{\n"
            "\t$basetexture \"white\"\n"
            "\t$additive \"1\"\n"
            "}", f);
        fclose(f);
    }
}

static void* VmGetGlowMat()
{
    if (!g_matSystem) return nullptr;
    VmWriteGlowMatFile();
    if (!s_vmGlowMat) {
        void* pMat = MatSysFindMaterial(g_matSystem, "iv_vm/rimglow");
        if (pMat && !MatIsError(pMat)) {
            __try {
                using Fn = void(__fastcall*)(void*);
                ((Fn)(*(void***)pMat)[12])(pMat); // IncrementReferenceCount
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_vmGlowMat = pMat;
            DebugLog("vmglow: rimglow material loaded ok ptr=%p", pMat);
        } else {
            DebugLog("vmglow: rimglow material FAILED (null=%d err=%d)",
                pMat == nullptr, pMat ? MatIsError(pMat) : 0);
        }
    }
    return s_vmGlowMat;
}

// studiohdr_t numbones. IVModelInfo::GetStudiomodel = slot 28 (Amalgam IVModelInfo.h).
// numbones @156 = id/version/checksum/name[64]/length/eyepos/illumpos/hull x2/view_bb x2/flags.
__declspec(noinline) static int GetGlowBoneCount_SEH(const void* pInfo)
{
    int n = 0;
    __try {
        const uint8_t* pInfoB = reinterpret_cast<const uint8_t*>(pInfo);
        const void* pMdl = *reinterpret_cast<const void* const*>(pInfoB + 0x20);
        if (reinterpret_cast<uintptr_t>(pMdl) < 0x10000 || !g_modelInfo) return 0;
        using GetStu_t = const void*(__fastcall*)(void*, const void*);
        const void* pStu = ((GetStu_t)(*(void***)g_modelInfo)[28])(g_modelInfo, pMdl);
        if (reinterpret_cast<uintptr_t>(pStu) < 0x10000) return 0;
        n = *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pStu) + 156);
        if (n < 1 || n > 256) n = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { n = 0; }
    return n;
}

// Scale bones around root (bone 0). Bigger shell sits deeper -> depth test passes only
// in thin ring outside silhouette = halo.
static void ScaleBonesForGlow(const void* pSrc, float (*pDst)[3][4], int nBones, float scale)
{
    const float (*src)[3][4] = reinterpret_cast<const float(*)[3][4]>(pSrc);
    const float rx = src[0][0][3];
    const float ry = src[0][1][3];
    const float rz = src[0][2][3];
    const float inv = 1.0f - scale;
    for (int b = 0; b < nBones; ++b) {
        for (int r = 0; r < 3; ++r) {
            pDst[b][r][0] = src[b][r][0] * scale;
            pDst[b][r][1] = src[b][r][1] * scale;
            pDst[b][r][2] = src[b][r][2] * scale;
            const float rp = (r == 0) ? rx : (r == 1) ? ry : rz;
            pDst[b][r][3] = src[b][r][3] * scale + rp * inv;
        }
    }
}

ForcedMaterialOverride_fwd_t o_ForcedMatOverride_fwd = nullptr;

// ForcedMaterialOverride via trampoline (slot 1 hooked); vtable if hook not in yet.
__declspec(noinline) static void VmForceMat(void* rcx, void* pMat)
{
    if (o_ForcedMatOverride_fwd) {
        __try {
            o_ForcedMatOverride_fwd(rcx, pMat, 0);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    } else {
        __try {
            using Fn = void(__fastcall*)(void*, void*, int);
            ((Fn)(*(void***)rcx)[1])(rcx, pMat, 0);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// Set viewmodel colour/blend via IVRenderView slots 6 (colour) and 4 (blend).
__declspec(noinline) static void VmSetColor(float r, float g, float b, float a)
{
    if (!g_renderView) return;
    __try {
        float col[3] = {r, g, b};
        using FnColor = void(__fastcall*)(void*, const float*);
        using FnBlend = void(__fastcall*)(void*, float);
        ((FnColor)(*(void***)g_renderView)[6])(g_renderView, col); // SetColorModulation
        ((FnBlend)(*(void***)g_renderView)[4])(g_renderView, a);   // SetBlend
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

struct VmColorState_t { float col[3]; float blend; };
__declspec(noinline) static VmColorState_t VmSaveColor()
{
    VmColorState_t s = {{1.f,1.f,1.f}, 1.f};
    if (!g_renderView) return s;
    __try {
        using FnGetBlend = float(__fastcall*)(void*);
        using FnGetColor = void(__fastcall*)(void*, float*);
        s.blend = ((FnGetBlend)(*(void***)g_renderView)[5])(g_renderView); // GetBlend
        ((FnGetColor)(*(void***)g_renderView)[7])(g_renderView, s.col);    // GetColorModulation
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return s;
}
__declspec(noinline) static void VmRestoreColor(const VmColorState_t& s)
{ VmSetColor(s.col[0], s.col[1], s.col[2], s.blend); }

// Cached IMaterial* for matType: 0 = Original (nullptr, caller skips override), 1-7 = VMT mats.
void* VmGetMat(int type)
{
    if (type <= 0) return nullptr;
    if (type > 7)  type = 1;

    if (!g_matSystem) return nullptr;
    VmWriteMatFiles();

    if (!s_vmMat[type]) {
        void* pMat = MatSysFindMaterial(g_matSystem, k_vmVmts[type - 1].name);
        if (pMat && !MatIsError(pMat)) {
            // IncrementReferenceCount (slot 12): avoids "bad reference count" log
            __try {
                using Fn = void(__fastcall*)(void*);
                ((Fn)(*(void***)pMat)[12])(pMat);
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_vmMat[type] = pMat;
        }
    }

    return s_vmMat[type];
}

// Write player VMTs to tf/materials/iv_pl/; always overwrite so $model 1 lands over old files.
static void PlWriteMatFiles()
{
    if (s_plMatFilesWritten) return;
    s_plMatFilesWritten = true;

    char cwd[MAX_PATH] = {};
    GetGameDir(cwd);
    {
        char tmp[MAX_PATH];
        sprintf_s(tmp, "%s\\tf", cwd);                    _mkdir(tmp);
        sprintf_s(tmp, "%s\\tf\\materials", cwd);          _mkdir(tmp);
        sprintf_s(tmp, "%s\\tf\\materials\\iv_pl", cwd);   _mkdir(tmp);
    }
    char dir[MAX_PATH];
    sprintf_s(dir, "%s\\tf\\materials\\iv_pl", cwd);

    for (int i = 0; i < 7; ++i) {
        const char* slash = strrchr(k_plVmts[i].name, '/');
        const char* base  = slash ? slash + 1 : k_plVmts[i].name;
        char path[MAX_PATH];
        sprintf_s(path, "%s\\%s.vmt", dir, base);
        FILE* f = fopen(path, "w");
        if (f) { fputs(k_plVmts[i].vmt, f); fclose(f); }
    }
}

void PlResetMatCache()
{
    for (int i = 1; i < 8; ++i) {
        if (s_plMat[i]) {
            __try {
                if (!MatIsError(s_plMat[i])) {
                    using Fn = void(__fastcall*)(void*);
                    ((Fn)(*(void***)s_plMat[i])[13])(s_plMat[i]); // DecrementReferenceCount
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_plMat[i] = nullptr;
        }
    }
    // The VMT files stay on disk; FindMaterial reloads them by name. No rewrite per map.
}

// Returns cached IMaterial* for player chams matType.
// Uses iv_pl/ materials which have $model 1 -- required for world-space studio models.
static void* PlGetMat(int type)
{
    if (type <= 0) return nullptr;
    if (type > 7)  type = 1;
    if (!g_matSystem) return nullptr;
    PlWriteMatFiles();

    if (!s_plMat[type]) {
        void* pMat = MatSysFindMaterial(g_matSystem, k_plVmts[type - 1].name);
        if (pMat && !MatIsError(pMat)) {
            __try {
                using Fn = void(__fastcall*)(void*);
                ((Fn)(*(void***)pMat)[12])(pMat); // IncrementReferenceCount
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_plMat[type] = pMat;
        }
    }
    return s_plMat[type] ? s_plMat[type] : nullptr;
}

// ---- World brush chams -- in-place material modification ----
// We cannot hook IMatRenderContext::Bind for world surfaces: GetRenderContext()
// from the render thread returns CMatQueuedRenderContext (threaded rendering mode),
// whose vtable is different from the CMatRenderContext that the BSP renderer uses
// internally. Hooking the queued context's Bind/Draw has no effect on world draws.
//
// Instead we use the same approach as Amalgam's OverrideWorldTextures: modify
// each world material in-place via SetShader + FindVar once per config change,
// then restore via Refresh() when disabled. This is persistent across frames.

// ---- IMaterial helpers for in-place modification -----------
__declspec(noinline) static void MatSetShader(void* pMat, const char* szShader)
{
    if (!pMat || !szShader) return;
    __try { using Fn = void(__fastcall*)(void*, const char*);
            ((Fn)(*(void***)pMat)[34])(pMat, szShader); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) static void* MatFindVar(void* pMat, const char* szVar)
{
    void* r = nullptr;
    if (!pMat || !szVar) return r;
    __try { using Fn = void*(__fastcall*)(void*, const char*, bool*, bool);
            bool found = false;
            r = ((Fn)(*(void***)pMat)[11])(pMat, szVar, &found, false); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
__declspec(noinline) static void MatVarSetString(void* pVar, const char* val)
{
    if (!pVar || !val) return;
    __try { using Fn = void(__fastcall*)(void*, const char*);
            ((Fn)(*(void***)pVar)[5])(pVar, val); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// IMaterialVar::GetStringValue -- slot 6 (imaterialvar.h order; SetFloatValue 3
// and SetStringValue 5 above already confirm that numbering).
__declspec(noinline) static const char* MatVarGetString(void* pVar)
{
    const char* r = nullptr;
    if (!pVar) return "";
    __try { using Fn = const char*(__fastcall*)(void*);
            r = ((Fn)(*(void***)pVar)[6])(pVar); }
    __except(EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
    return r ? r : "";
}
// IMaterialVar::GetTextureValue -- slot 0 -- then ITexture::GetName -- slot 0.
// A texture var only holds the VMT's string until the material is precached;
// after that it holds an ITexture, and GetStringValue no longer gives a path.
__declspec(noinline) static const char* MatVarGetTextureName(void* pVar)
{
    const char* r = nullptr;
    if (!pVar) return "";
    __try {
        using GetTex_t = void*(__fastcall*)(void*);
        void* pTex = ((GetTex_t)(*(void***)pVar)[0])(pVar);
        if (pTex) {
            using GetName_t = const char*(__fastcall*)(void*);
            r = ((GetName_t)(*(void***)pTex)[0])(pTex);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
    return r ? r : "";
}
// IMaterial::SetMaterialVarFlag -- slot 29 (imaterial.h order).
__declspec(noinline) static void MatSetVarFlag(void* pMat, int flag, bool on)
{
    if (!pMat) return;
    __try { using Fn = void(__fastcall*)(void*, int, bool);
            ((Fn)(*(void***)pMat)[29])(pMat, flag, on); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) static void MatAlphaModulate(void* pMat, float a)
{
    if (!pMat) return;
    __try { using Fn = void(__fastcall*)(void*, float);
            ((Fn)(*(void***)pMat)[27])(pMat, a); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// IMaterial::RecomputeStateSnapshots -- slot 16. Needed after changing vars
// that pick shader combos ($envmap, selfillum/wireframe flags); without it the
// change sits in the var but the compiled shader state never picks it up.
__declspec(noinline) static void MatRecomputeSnapshots(void* pMat)
{
    if (!pMat) return;
    __try { using Fn = void(__fastcall*)(void*);
            ((Fn)(*(void***)pMat)[16])(pMat); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) static void MatRefresh(void* pMat)
{
    if (!pMat) return;
    __try { using Fn = void(__fastcall*)(void*);
            ((Fn)(*(void***)pMat)[37])(pMat); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// MaterialVarFlags_t bit we use (public/materialsystem/imaterial.h).
enum { k_matVarTranslucent = 1 << 21 };

// ---- Map textures ------------------------------------------
// A texture mod (the tf/custom kind) swaps a map's look by overwriting the VTF
// files the world materials point at. We do the same thing at runtime instead:
// walk the live world materials, work out what surface each one is from its
// name, and repoint its $basetexture at the preset the user picked for that
// category. No files, no tf/custom, and it follows the user across every map.
//
// Categories come from ordered substring rules over the material's own name and,
// when the name says nothing, its $basetexture path -- the second pass is what
// catches map-local materials that reuse a shared texture, e.g.
// cp_manor/wood_beam03_burnt -> Wood/wood_beam03. Rules were built by dumping
// every world-shader VMT out of tf2_misc_dir.vpk (1114 of them) and checking
// each bucket by hand; the leftovers are signs, windows, glass and decals, which
// are exactly the surfaces a texture swap should leave alone.

// One rule = a category, the substrings that select it, and substrings that veto
// it. Order matters: the first rule that hits wins, so ceilings are tested before
// walls ("moon_ceiling_light01" is not a metal wall) and rock before organic
// ground ("blendrockgroundwall*" is a cliff face, not a floor).
struct MtRule_t {
    int                 cat;
    const char* const*  veto;
    int                 vetoCount;
    const char* const*  match;
    int                 matchCount;
};

static const char* const s_mtCeilingM[] = {
    "ceiling", "slateroof", "roof_", "_roof", "roof0", "shingles" };

static const char* const s_mtRockM[] = {
    "rockwall", "rockgroundwall", "cliff", "boulder", "quarryrock", "quarry_trans",
    "shalewall", "karst_rock", "spacerock", "rocktorock", "stonewall",
    "underworld_rockwall", "sand_cliff", "dirtrock" };

static const char* const s_mtWoodM[] = {
    "wood/", "wood_", "woodwall", "woodbeam", "woodtrim", "_wood", "redwood",
    "grain_elevator", "plank", "hay_", "fake_wood", "boardwalk", "pasterbeam",
    "swampwall015", "crate", "barrel", "track_centerpiece", "wallpaper" };

static const char* const s_mtFloorOrgV[] = { "wall" };
static const char* const s_mtFloorOrgM[] = {
    "dirtground", "dirtroad", "grass", "snow", "mudground", "swampmud",
    "sand_floor", "sand_grass", "sky_sand", "beach", "moss", "leaf", "leaves",
    "gravelground", "gravelcoal", "gravelgrayred", "blendgravel", "blendground",
    "blenddirt", "blendmud", "blendrockground", "rockground", "ground_0",
    "ground03alpha", "dirt_", "_dirt", "dirt0", "moon_dust", "moon_craters",
    "moon_ground", "spacegravel", "space_ground", "spaceground", "karst",
    "goldground", "underworld_dirt", "ash_01" };

static const char* const s_mtFloorBuiltM[] = {
    "concretefloor", "floor_tile", "floortile", "floorplate", "floor_carpet",
    "carpet0", "streetfloor", "streetroad", "roadpatch", "roadscratch",
    "cobblestone", "blendcobble", "cobble", "concrete_track", "metaltrack",
    "metalstair", "stonestair", "streetstair", "stone_floor", "ground_brick",
    "moon_floor", "concretegravel", "floor_", "_floor", "blendconcrete",
    "yellowtile", "metalgrate", "grate001", "moonbase_grate", "island_grate",
    "blendbeach" };

static const char* const s_mtWallBuiltM[] = {
    "concretewall", "computerwall", "brickwall", "metalwall", "plasterwall",
    "pasterwall", "plaster_", "cobblewall", "swampbrickwall", "swampcobblewall",
    "walltile", "metalpanel", "wallpanels", "metaltrim", "metaltruss", "ibeam",
    "imetal", "bridgebeam", "metalchrome", "metalsiding", "metalduct",
    "computer_panel", "swampcrete", "concrete-", "concrete_0", "stonetrim",
    "metal/wall", "concrete/wall", "brick/wall", "gantry_metal", "wall_island",
    "retainingwall", "buildingline", "bumpy_wall", "flat_wall", "noflat_wall",
    "swampmetal", "red_wall", "blue_wall", "red_beam", "metaldoor", "metal_door",
    "officedoor", "steeldoor", "metalrail", "vent001", "chicken_wire",
    "edge_border", "hyro_", "moon_base", "_wall", "wall0", "wall_" };

#define MT_RULE(cat, veto, match) \
    { cat, veto, (int)(sizeof(veto)/sizeof(*(veto))), match, (int)(sizeof(match)/sizeof(*(match))) }
#define MT_RULE_NV(cat, match) { cat, nullptr, 0, match, (int)(sizeof(match)/sizeof(*(match))) }

static const MtRule_t s_mtRules[] = {
    MT_RULE_NV(k_mtCeiling,    s_mtCeilingM),
    MT_RULE_NV(k_mtRock,       s_mtRockM),
    MT_RULE_NV(k_mtWood,       s_mtWoodM),
    MT_RULE   (k_mtFloorOrganic, s_mtFloorOrgV, s_mtFloorOrgM),
    MT_RULE_NV(k_mtFloorBuilt, s_mtFloorBuiltM),
    MT_RULE_NV(k_mtWallBuilt,  s_mtWallBuiltM),
};
static const int s_mtRuleCount = (int)(sizeof(s_mtRules) / sizeof(*s_mtRules));

// Lowercase + backslash-to-slash into a fixed buffer. Material names run well
// under 128; anything longer is truncated, which only costs us a late-suffix
// match on a pathological name.
static void MtNormalize(const char* src, char* dst, size_t cap)
{
    size_t i = 0;
    if (src) for (; src[i] && i + 1 < cap; ++i) {
        char ch = src[i];
        if (ch == '\\')                 ch = '/';
        else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        dst[i] = ch;
    }
    dst[i] = '\0';
}

static int MtMatchOne(const char* hay)
{
    if (!hay || !hay[0]) return -1;
    for (int r = 0; r < s_mtRuleCount; ++r) {
        const MtRule_t& R = s_mtRules[r];
        bool bVetoed = false;
        for (int v = 0; v < R.vetoCount && !bVetoed; ++v)
            if (strstr(hay, R.veto[v])) bVetoed = true;
        if (bVetoed) continue;
        for (int m = 0; m < R.matchCount; ++m)
            if (strstr(hay, R.match[m])) return R.cat;
    }
    return -1;
}

// -1 = no category, leave the material alone.
static int MtClassify(const char* szName, const char* szBaseTex)
{
    char buf[160];
    MtNormalize(szName, buf, sizeof(buf));
    int cat = MtMatchOne(buf);
    if (cat >= 0) return cat;
    // The name is the stronger signal, so only fall through to the texture path
    // once every rule has missed on the name -- concrete/concretefloor008_reflect_blue
    // points at Nature/ground_04 but is still a concrete floor.
    MtNormalize(szBaseTex, buf, sizeof(buf));
    return MtMatchOne(buf);
}

// ---- Presets ------------------------------------------------------------
// Every path here was checked against tf2_textures_dir.vpk / tf2_misc_dir.vpk,
// so they resolve on a stock install with nothing in tf/custom. "white" is the
// material system's own built-in.
struct MtPreset_t { const char* label; const char* tex; };
const MtPreset_t k_mapTexPresets[] = {
    { "Off",             nullptr                            },
    { "Snow",            "nature/snow_full001"              },
    { "Grass",           "nature/grass_07"                  },
    { "Grass (dark)",    "nature/grass_04"                  },
    { "Dirt",            "nature/dirtground006"             },
    { "Mud",             "nature/mudground001"              },
    { "Sand",            "egypt/sand_floor_05"              },
    { "Gravel",          "nature/gravelground001"           },
    { "Moss",            "winter/moss001"                   },
    { "Rock",            "nature/rockwall005"               },
    { "Rocky ground",    "nature/rockground001"             },
    { "Cliff",           "nature/cliff_terrain03"           },
    { "Lava",            "nature/underworld_lava001"        },
    { "Moon dust",       "moon/moon_ground_dust001"         },
    { "Concrete floor",  "concrete/concretefloor008"        },
    { "Concrete wall",   "concrete/concretewall012"         },
    { "Cobblestone",     "nature/underworld_cobblestone001" },
    { "Brick",           "brick/brickwall001"               },
    { "Tile",            "tile/floor_tile_004a"             },
    { "Ceiling tile",    "tile/tile_ceiling_001a"           },
    { "Slate roof",      "tile/slateroof001"                },
    { "Wood shingles",   "medieval/wood_shingles02"         },
    { "Wood floor",      "wood/wood_floor002"               },
    { "Wood planks",     "wood/wood_wall006"                },
    { "Metal",           "metal/imetal001"                  },
    { "Metal plate",     "metal/floorplate01"               },
    { "Metal grate",     "metal/metalgrate011a"             },
    { "Chrome",          "metal/metalchrome001"             },
    { "Carpet",          "concrete/floor_carpet001"         },
    { "Gold",            "nature/goldground001"             },
    { "White (flat)",    "white"                            },
    { "Custom...",       nullptr                            },   // uses cat.custom
};
const int k_mapTexPresetCount  = (int)(sizeof(k_mapTexPresets) / sizeof(*k_mapTexPresets));
const int k_mapTexPresetCustom = k_mapTexPresetCount - 1;

const char* MapTexPresetLabel(int p)
{
    return (p >= 0 && p < k_mapTexPresetCount) ? k_mapTexPresets[p].label : "Off";
}

// Menu labels for the categories, in MapTexCategory_t order.
static const char* const s_mtCatLabels[k_mapTexCatCount] = {
    "Floor (organic)", "Floor (inorganic)", "Rock / cliffs",
    "Wood", "Walls (inorganic)", "Ceilings / roofs" };

const char* MapTexCategoryLabel(int c)
{
    return (c >= 0 && c < k_mapTexCatCount) ? s_mtCatLabels[c] : "";
}

// The resolved per-category state one pass works from. A pass is spread over
// many ticks, and the menu can be editing a custom path the whole time, so the
// path is held by value rather than as a pointer into g_mapTexCats.
struct MtComposite_t {
    char  tex[k_mapTexCatCount][96] = {};   // empty = category off
    float tint[k_mapTexCatCount][4] = {};
    bool  any                       = false;
};

static void MtBuildComposite(MtComposite_t& c)
{
    c = MtComposite_t{};
    for (int i = 0; i < k_mapTexCatCount; ++i) {
        const MapTexCat_t& C = g_mapTexCats[i];
        memcpy(c.tint[i], C.tint, sizeof(c.tint[i]));
        int p = C.preset;
        if (p <= 0 || p >= k_mapTexPresetCount) continue;
        const char* src = (p == k_mapTexPresetCustom) ? C.custom : k_mapTexPresets[p].tex;
        if (!src || !src[0]) continue;
        strncpy_s(c.tex[i], src, _TRUNCATE);
        c.any = true;
    }
}

// Category of one live material, or -1 for "leave it alone". $basetexture is
// only consulted once every rule has missed on the name, so the FindVar +
// GetStringValue cost is paid on a minority of materials.
__declspec(noinline) static int MtClassifyMat(void* pMat)
{
    int cat = -1;
    __try {
        char buf[160];
        MtNormalize(MatGetName(pMat), buf, sizeof(buf));
        cat = MtMatchOne(buf);
        if (cat < 0) {
            // Once the material is precached $basetexture holds an ITexture, not
            // the VMT string, so ask the texture for its name first and only fall
            // back to the raw string (which is all there is pre-precache).
            void* pVar = MatFindVar(pMat, "$basetexture");
            const char* szTex = MatVarGetTextureName(pVar);
            if (!szTex[0]) szTex = MatVarGetString(pVar);
            MtNormalize(szTex, buf, sizeof(buf));
            cat = MtMatchOne(buf);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { cat = -1; }
    return cat;
}

// Classification has to be decided from the material's ORIGINAL state: once a
// pass has swapped $basetexture, that var names our replacement, so a second
// pass reading it back would sort the material into whatever category the
// replacement texture belongs to. The answer is therefore cached per IMaterial*
// from the first (pristine) look and only thrown away when the map changes --
// which is also what stops every pass re-running ~150 substring tests over a
// thousand-odd materials.
static std::unordered_map<void*, int> s_mtCatCache;
static uint32_t                       s_mtCatEpoch = 0xFFFFFFFFu;

static int MtCatFor(void* pMat)
{
    uint32_t epoch = g_matEpoch.load();
    if (epoch != s_mtCatEpoch) { s_mtCatCache.clear(); s_mtCatEpoch = epoch; }
    auto it = s_mtCatCache.find(pMat);
    if (it != s_mtCatCache.end()) return it->second;
    int cat = MtClassifyMat(pMat);
    s_mtCatCache.emplace(pMat, cat);
    return cat;
}

__declspec(noinline) static void ApplyMapTex_OneMat(void* pMat, const MtComposite_t& c)
{
    if (!pMat || MatIsError(pMat)) return;
    int cat = MtCatFor(pMat);
    if (cat < 0 || !c.tex[cat][0]) return;   // not ours, or this category is off
    __try {
        // Start from the VMT so switching preset does not stack on the last one.
        MatRefresh(pMat);
        // LightmappedGeneric both flattens WorldVertexTransition blends down to a
        // single texture (what a texture mod ends up looking like anyway) and
        // makes the material re-precache its vars, which is what actually loads
        // the new $basetexture string -- setting the string alone would leave the
        // old ITexture bound. RecomputeStateSnapshots then rebuilds the compiled
        // state. Same sequence the old world chams used for their Shaded mode.
        MatSetShader(pMat, "LightmappedGeneric");
        MatVarSetString(MatFindVar(pMat, "$basetexture"), c.tex[cat]);
        MatColorModulate(pMat, c.tint[cat][0], c.tint[cat][1], c.tint[cat][2]);
        MatAlphaModulate(pMat, c.tint[cat][3]);
        // World brushes are opaque by default -- alpha only blends if we say so.
        if (c.tint[cat][3] < 0.999f) MatSetVarFlag(pMat, k_matVarTranslucent, true);
        MatRecomputeSnapshots(pMat);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Tint-only update. Dragging a colour picker must not drop the game into
// mat_queue_mode 0 on every frame, so a pure tint change writes the modulation
// and nothing structural.
__declspec(noinline) static void UpdateMapTexColor_OneMat(void* pMat, const MtComposite_t& c)
{
    if (!pMat || MatIsError(pMat)) return;
    int cat = MtCatFor(pMat);
    if (cat < 0 || !c.tex[cat][0]) return;
    __try {
        MatColorModulate(pMat, c.tint[cat][0], c.tint[cat][1], c.tint[cat][2]);
        MatAlphaModulate(pMat, c.tint[cat][3]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void ResetMapTex_OneMat(void* pMat)
{
    if (!pMat || MatIsError(pMat)) return;
    __try { MatRefresh(pMat); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static uint32_t WcHashBytes(const void* p, size_t n, uint32_t h = 0x811C9DC5u)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x1000193u; }
    return h;
}

// SEH-only inner scan -- no C++ objects with dtors allowed here (C2712).
__declspec(noinline) static void WcScanLive_SEH(void** out, int* pCount, int maxOut)
{
    int n = 0;
    unsigned short hInvalid = MatSysInvalidMaterial(g_matSystem);
    __try {
        for (unsigned short h = MatSysFirstMaterial(g_matSystem);
             h != hInvalid && n < maxOut; h = MatSysNextMaterial(g_matSystem, h))
        {
            void* pMat = MatSysGetMaterial(g_matSystem, h);
            if (!pMat || MatIsError(pMat)) continue;
            const char* sGroup = MatGetTextureGroupName(pMat);
            if (!sGroup) continue;
            if (strncmp(sGroup, "World textures", 14) != 0) continue;
            out[n++] = pMat;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    *pCount = n;
}

// The pass walks the material system's own handle list instead of the cached
// g_worldMaterials set. That cache is epoch-based, so between a map unload and
// the next epoch bump it holds IMaterial* the engine has already freed --
// Refresh/SetShader on one of those faults deep inside materialsystem, which is
// what the crash log showed. Handles enumerated here are live by definition.
// Modulation keeps using the cache: it only writes colour, and it runs far more
// often than this path.
static void WcSnapshotMaterials(std::vector<void*>& out)
{
    out.clear();
    if (!g_matSystem) return;
    const int kMaxMats = 16384;
    out.resize(kMaxMats);
    int n = 0;
    WcScanLive_SEH(out.data(), &n, kMaxMats);
    out.resize(n);
}

// Last applied state -- reset by ResetMapTextures so the next tick re-applies.
static uint32_t s_wcStructSig = 0xFFFFFFFFu;
static uint32_t s_wcColorSig  = 0xFFFFFFFFu;

// A whole map's world materials in one pass means Refresh + SetShader +
// RecomputeSnapshots a thousand-odd times inside a single frame -- a visible
// freeze right after a map load, and the widest window for the materials to
// change under us. The pass is spread over ticks instead: kWcBatch materials
// per call, continuing where it left off.
enum WcPass { kWcPassNone = 0, kWcPassApply, kWcPassReset, kWcPassColor };
static std::vector<void*> s_wcPending;
static size_t             s_wcPendIdx = 0;
static int                s_wcPass    = kWcPassNone;
static MtComposite_t      s_wcPendComp;
static const size_t       kWcBatch    = 96;

// Structural material work (Refresh / SetShader / RecomputeSnapshots) frees the
// material's proxies and shader snapshots. With mat_queue_mode != 0 the
// material queue thread runs a frame behind and is still walking those --
// crash.log caught it calling through a freed proxy vtable (frame [0] was a
// heap address, not a module, below materialsystem on a vstdlib pool thread).
//
// So a structural pass runs with the material system single-threaded, the same
// thing Valve does before its own material work (nav_generate.cpp sets
// mat_queue_mode 0 and puts it back to -1 after). The old mode goes back as
// soon as the pass finishes, so the frame cost only lasts the ~1 s the pass
// takes. Colour-only passes do not need it: ColorModulate writes floats into
// vars that already exist, it frees nothing.
static ConVar* s_cvMatQueue   = nullptr;
static int     s_wcQueueSaved = INT_MIN;   // INT_MIN = not forced
static int     s_wcQueueDelay = 0;         // ticks to wait for the mode to take

// True when the material system is single-threaded for us. False means the
// structural pass must NOT run -- running it threaded is the crash.
static bool WcForceSingleThreaded()
{
    if (s_wcQueueSaved != INT_MIN) return true;      // already forced by us
    if (!g_cvar) return false;
    if (!s_cvMatQueue) s_cvMatQueue = g_cvar->FindVarWalk("mat_queue_mode");
    if (!s_cvMatQueue) return false;

    int cur = (int)s_cvMatQueue->GetFloat();
    if (cur == 0) return true;                       // already single-threaded
    if (!s_cvMatQueue->SetValueInt("mat_queue_mode", 0)) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            DebugLog("maptextures: mat_queue_mode SetValue failed -- structural pass skipped");
        }
        return false;
    }
    s_wcQueueSaved = cur;
    s_wcQueueDelay = 3;                              // mode takes at BeginFrame
    DebugLog("maptextures: mat_queue_mode %d -> 0 for structural pass", cur);
    return true;
}

static void WcRestoreQueueMode()
{
    if (s_wcQueueSaved == INT_MIN || !s_cvMatQueue) { s_wcQueueSaved = INT_MIN; return; }
    s_cvMatQueue->SetValueInt("mat_queue_mode", s_wcQueueSaved);
    DebugLog("maptextures: mat_queue_mode restored to %d", s_wcQueueSaved);
    s_wcQueueSaved = INT_MIN;
}

// False when the material system has no world materials yet -- caller then
// leaves the sigs alone so the pass is retried on the next tick.
static bool WcStartPass(int pass, const MtComposite_t& c)
{
    // Refresh / SetShader / RecomputeSnapshots free the material's proxies and
    // shader snapshots. Do that while the material queue thread is a frame
    // behind and it walks freed memory -- the materialsystem.dll fault on a
    // vstdlib pool thread. No single-threaded mode, no structural pass.
    const bool bStructural = (pass == kWcPassApply || pass == kWcPassReset);
    if (bStructural && !WcForceSingleThreaded()) { s_wcPass = kWcPassNone; return false; }

    WcSnapshotMaterials(s_wcPending);
    if (s_wcPending.empty()) {
        s_wcPass = kWcPassNone;
        if (bStructural) WcRestoreQueueMode();   // nothing to do -- give the mode back
        return false;
    }
    s_wcPendIdx  = 0;
    s_wcPass     = pass;
    s_wcPendComp = c;
    return true;
}

static void WcRunBatch()
{
    if (s_wcPass == kWcPassNone) return;
    // Wait out the frames it takes mat_queue_mode 0 to reach the material
    // system -- touching materials before that is the race we are fixing.
    if (s_wcQueueDelay > 0) { --s_wcQueueDelay; return; }
    size_t end = s_wcPendIdx + kWcBatch;
    if (end > s_wcPending.size()) end = s_wcPending.size();
    for (; s_wcPendIdx < end; ++s_wcPendIdx) {
        void* pMat = s_wcPending[s_wcPendIdx];
        if      (s_wcPass == kWcPassApply) ApplyMapTex_OneMat(pMat, s_wcPendComp);
        else if (s_wcPass == kWcPassColor) UpdateMapTexColor_OneMat(pMat, s_wcPendComp);
        else                               ResetMapTex_OneMat(pMat);
    }
    if (s_wcPendIdx >= s_wcPending.size()) {
        s_wcPass = kWcPassNone;
        s_wcPending.clear();
        WcRestoreQueueMode();
        // Refresh wiped the colour of every material it touched: hand them back to Modulate
        // now that the pass is done, not when it started.
        g_modWorldValuesDirty = true;
    }
}

void ApplyMapTextures()
{
    // World materials are only safe to mutate while a map is live -- during a
    // load or an unload the engine is creating/freeing them underneath us, and
    // any pass still in flight is holding pointers it just freed.
    if (!g_matSystem || !IsInGame()) {
        s_wcPass = kWcPassNone;
        s_wcPending.clear();
        s_wcQueueDelay = 0;
        WcRestoreQueueMode();
        return;
    }

    MtComposite_t c;
    if (g_enableMapTextures) MtBuildComposite(c);
    const bool bEnable = c.any;

    // Structure sig: anything that needs a full Refresh + SetShader pass, i.e.
    // which texture each category is pointed at. The map epoch is in here too,
    // so a map change re-runs the pass against the new material set.
    struct { uint32_t epoch; int enable; int presets[k_mapTexCatCount];
             uint32_t texHash; int translucent; } st{};
    st.epoch   = g_matEpoch.load();
    st.enable  = bEnable ? 1 : 0;
    st.texHash = 0x811C9DC5u;
    for (int i = 0; i < k_mapTexCatCount; ++i) {
        st.presets[i] = bEnable ? g_mapTexCats[i].preset : 0;
        // Hash the resolved path, not just the preset index -- editing the
        // custom box has to re-trigger the pass on the same preset index.
        if (bEnable && c.tex[i][0])
            st.texHash = WcHashBytes(c.tex[i], strlen(c.tex[i]), st.texHash);
        if (bEnable && c.tex[i][0] && c.tint[i][3] < 0.999f) st.translucent = 1;
    }
    uint32_t structSig = WcHashBytes(&st, sizeof(st));

    // Colour sig: the cheap per-material update path.
    uint32_t colorSig = bEnable ? WcHashBytes(c.tint, sizeof(c.tint)) : 0u;

    // Nothing changed -- keep feeding whatever pass is still in flight.
    if (structSig == s_wcStructSig && colorSig == s_wcColorSig) { WcRunBatch(); return; }

    if (structSig != s_wcStructSig) {
        if (!WcStartPass(bEnable ? kWcPassApply : kWcPassReset, c))
            return;             // no materials yet -- retry next tick, sigs unchanged
        s_wcStructSig = structSig;
        s_wcColorSig  = colorSig;
        WcRunBatch();
        return;
    }

    s_wcColorSig = colorSig;
    if (!bEnable) return;
    // A structural pass already writes the current colours -- just retarget it
    // instead of starting a competing colour pass.
    if (s_wcPass == kWcPassApply) { s_wcPendComp = c; WcRunBatch(); return; }
    if (WcStartPass(kWcPassColor, c)) WcRunBatch();
}

// True while any category is actually swapping a texture -- world modulation
// has to stay off those materials or the two features fight over the colour.
bool MapTexturesOwnWorld()
{
    if (!g_enableMapTextures) return false;
    for (int i = 0; i < k_mapTexCatCount; ++i) {
        int p = g_mapTexCats[i].preset;
        if (p <= 0 || p >= k_mapTexPresetCount) continue;
        if (p == k_mapTexPresetCustom) { if (g_mapTexCats[i].custom[0]) return true; }
        else if (k_mapTexPresets[p].tex)                                return true;
    }
    return false;
}

// Restore all world materials from their VMT. Only safe while the materials
// are known live (DLL unload) -- on a map change they are already freed, so
// ResetMatCache() just drops our state instead of touching them.
void ResetMapTextures(bool bRefreshMaterials)
{
    // Drop any batched pass first -- its pointers are from before this reset.
    s_wcPass = kWcPassNone;
    s_wcPending.clear();
    s_wcQueueDelay = 0;
    // Same reason: the cache is keyed by IMaterial*, and a map change frees them.
    s_mtCatCache.clear();
    s_mtCatEpoch = 0xFFFFFFFFu;

    if (bRefreshMaterials) {
        // Eject path: has to finish now, so this one is not batched. Refresh is
        // still structural, so the queue thread has to be off for it. When the
        // caller already forced the mode and waited out the frames (the game
        // thread eject restore), don't sleep again -- that Sleep would stall
        // FrameStageNotify for nothing.
        bool bWasForced = (s_wcQueueSaved != INT_MIN);
        bool bForced    = WcForceSingleThreaded();
        if (bForced && !bWasForced) Sleep(150);
        std::vector<void*> mats;
        WcSnapshotMaterials(mats);
        for (void* pMat : mats) ResetMapTex_OneMat(pMat);
    }
    s_wcStructSig = 0xFFFFFFFFu;
    s_wcColorSig  = 0xFFFFFFFFu;
    // Never leave the game in forced single-threaded mode -- this also runs on
    // the eject path.
    WcRestoreQueueMode();
}


// ---- Eject restore (game thread) --------------------------------------------
// MatRefresh / DecrementReferenceCount / the precip teardown all free things
// the game thread is drawing that same frame. Running them from the eject
// thread crashed inside stdshader_dx8 with no infovisual frame in the stack --
// the game thread walked a shader snapshot we had just freed underneath it.
// So the eject thread only *asks*; the work runs here, inside
// FrameStageNotify, where nothing else is touching those materials.
//
// 0 = idle, 1 = requested, 2 = queue mode forced, waiting for it to take,
// 3 = done (also set when there is nothing to restore).
std::atomic<int>  g_ejectRestoreState{0};
std::atomic<bool> g_ejectRestoreDidWork{false};
static int        s_ejectRestoreWait = 0;
static void       VmFlatAlpha_RestoreAll();   // transparent viewmodel section, below

void EjectRestore_Service()
{
    int st = g_ejectRestoreState.load();
    if (st != 1 && st != 2) return;

    if (st == 1) {
        Sky_Restore();
        FlipWorld_Release();   // cl_flipviewmodels goes back through SetValue: this thread only
        // No map = no live world materials and no precip entity; nothing to do.
        if (!g_matSystem || !IsInGame()) { g_ejectRestoreState.store(3); return; }
        // Refresh is structural, so the material queue thread has to be off
        // for the whole pass -- same rule as ApplyMapTextures.
        if (!WcForceSingleThreaded()) {
            DebugLog("eject restore: mat_queue_mode force failed -- skipping material restore");
            g_ejectRestoreState.store(3);
            return;
        }
        s_ejectRestoreWait = 3;          // mode only takes at the next BeginFrame
        g_ejectRestoreState.store(2);
        return;
    }

    if (s_ejectRestoreWait > 0) { --s_ejectRestoreWait; return; }

    // Kill the CPrecipitation entity we spawned before anything drops its
    // material -- the engine finds a live entity in that slot otherwise.
    if (g_entityList && (g_precipEntity || g_precipNetworkable))
        Precip_TearDown(2047);

    VmFlatAlpha_RestoreAll();            // $blendtintbybasealpha back on viewmodel materials
    Modulate_Restore();                  // sky + any world material Refresh below won't reach
    ResetMatCache(true);                 // restores world materials, drops our refs
    GlowVM_LevelUnload();                // glow + flip world RT/material refs, same thread rule
    FlipWorld_LevelUnload();
    g_ejectRestoreDidWork.store(true);
    g_ejectRestoreState.store(3);
    DebugLog("eject restore: world materials restored on game thread");
}

// Returns true if entIdx is a CTFViewModel or CTFWearableVM entity.
__declspec(noinline) static bool VmIsViewmodelEntity_SEH(int entIdx)
{
    if (!g_entityList || entIdx <= 0) return false;
    __try {
        using GetEnt_t  = void*(__fastcall*)(void*, int);
        using GetVoid_t = void*(__fastcall*)(void*);
        void* pEnt  = ((GetEnt_t) (*(void***)g_entityList)[3])(g_entityList, entIdx);
        if (!pEnt)  return false;
        void* pNetw = ((GetVoid_t)(*(void***)pEnt)[4])(pEnt);
        if (!pNetw) return false;
        void* pCC   = ((GetVoid_t)(*(void***)pNetw)[2])(pNetw);
        if (!pCC)   return false;
        const char* name = *reinterpret_cast<const char**>(
            reinterpret_cast<uint8_t*>(pCC) + 16);
        // Arms drawn by CTFViewModel; weapon viewmodel drawn by CTFWearableVM.
        bool result = name && (strcmp(name, "CTFViewModel") == 0
                           || strcmp(name, "CTFWearableVM")  == 0);
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// ---- Player chams: attachment ownership -------------------------------------
// entity index -> owning player index (1..32), 0 = not held by a player.
// Rebuilt once per frame from FrameStageNotify; see RebuildAttachmentOwners.
static const int   k_attachMaxEnt = 2048;
static uint8_t     s_attachOwner[k_attachMaxEnt] = {};

// Two different netvars answer "who is holding this", and each is only reliable
// for one kind of entity:
//
//   Cosmetics  -> m_hNetworkMoveParent. CEconWearable::Equip calls
//                 FollowEntity(pOwner) (econ_wearable.cpp:192), which parents them.
//   Weapons    -> the OWNER'S m_hMyWeapons array. Weapons are equipped the same
//                 way (basecombatweapon_shared.cpp:987) but their move-parent
//                 reads back 0 on the client.
//
// m_hOwner would be the cheap per-entity answer, but it only exists on
// DT_BaseCombatWeapon: reading that offset on a rocket or a prop reads whatever
// happens to live there. So walk the players instead and let each one declare
// its own weapons. Projectiles, dropped weapons and buildings are in nobody's
// m_hMyWeapons, so they never end up in the table -- which matters because
// rockets and arrows share models/weapons/w_models/ with real weapons and no
// model-path test can separate them.
void RebuildAttachmentOwners()
{
    memset(s_attachOwner, 0, sizeof(s_attachOwner));
    if (g_offMyWeapons <= 0 || !g_entityList) return;

    __try {
        for (int p = 1; p <= 32; ++p) {
            void* pPlayer = GetClientEntity(p);
            if (!pPlayer) continue;
            const uint8_t* wpns = reinterpret_cast<const uint8_t*>(pPlayer) + g_offMyWeapons;
            for (int i = 0; i < 48; ++i) {   // MAX_WEAPONS, shareddefs.h:118
                unsigned int h = *reinterpret_cast<const unsigned int*>(wpns + i * 4);
                int idx = (int)(h & 0x3FFF);
                // 0x3FFF is the invalid-handle index; loadout preview models and
                // other unowned weapons read back as that.
                if (idx > 32 && idx < k_attachMaxEnt)
                    s_attachOwner[idx] = (uint8_t)p;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Returns true if pInfo is a weapon or carried item held by a live player.
// Cosmetics under models/player/items/ come through PlDetectPlayer instead --
// they match the player path test there.
static bool PlDetectAttachment(const void* pInfo)
{
    if (!pInfo) return false;
    __try {
        // Attachments are their own entities, always above the player slots.
        int entIdx = *reinterpret_cast<const int*>(
            reinterpret_cast<const uint8_t*>(pInfo) + 0x44);
        if (entIdx <= 32 || entIdx >= k_attachMaxEnt) return false;

        int ownerIdx = s_attachOwner[entIdx];   // weapons

        if (ownerIdx == 0 && g_offMoveParent > 0) {
            // Cosmetics and other worn items: parented to the wearer. A
            // first-person wearable parents to the viewmodel entity instead
            // (econ_wearable.cpp:189), so its parent index lands above 32 and
            // falls out here.
            void* pEnt = GetClientEntity(entIdx);
            if (!pEnt) return false;
            unsigned int hParent = *reinterpret_cast<unsigned int*>(
                reinterpret_cast<uintptr_t>(pEnt) + g_offMoveParent);
            int parentIdx = (int)(hParent & 0x3FFF);
            if (parentIdx >= 1 && parentIdx <= 32) ownerIdx = parentIdx;
        }
        if (ownerIdx < 1 || ownerIdx > 32) return false;

        // The viewmodel is owned by the local player; vm chams handle it.
        if (VmIsViewmodelEntity_SEH(entIdx)) return false;

        if (!g_plChamsLocalPlayer && ownerIdx == GetLocalPlayerIndex()) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}


// DrawModelExecute_t and o_DrawModelExecute declared in visuals.h / defined below.
// ForcedMaterialOverride_t already typedef'd as ForcedMaterialOverride_fwd_t above.

// Aliases o_ForcedMatOverride_fwd (declared above VmForceMat) so there is one storage location.
static ForcedMaterialOverride_fwd_t& o_ForcedMatOverride = o_ForcedMatOverride_fwd;
bool  g_vmChamsHookInstalled  = false;
bool  g_weaponHooksInstalled  = false;

// True during chams draw -- prevents TF2's ForcedMaterialOverride from resetting our override.
bool g_vmIsRendering    = false;

void __fastcall h_ForcedMatOverride(void* rcx, void* pMat, int nType)
{
    // Block TF2's ForcedMaterialOverride only while CHAMS are rendering.
    if (g_vmIsRendering) {
        return;
    }
    if (o_ForcedMatOverride) o_ForcedMatOverride(rcx, pMat, nType);
}

// Re-entrance guard: our chams calls re-enter DrawModelExecute via the trampoline.
static thread_local int t_vmDepth = 0;

static void VmCullMode(int mode); // forward decl -- defined below

using DrawModelExecute_t = void(__fastcall*)(void*, const void*, const void*, const void*);
DrawModelExecute_t o_DrawModelExecute = nullptr;

// CViewRender::DrawViewModels
using DrawViewModels_t = void(__fastcall*)(void*, const void*, bool);
DrawViewModels_t o_DrawViewModels = nullptr;

void __fastcall h_DrawViewModels(void* rcx, const void* pViewSetup, bool drawViewmodel)
{
    if (!g_viewRender && rcx) g_viewRender = rcx;
    o_DrawViewModels(rcx, pViewSetup, drawViewmodel);   // Flip World already mirrored the world in h_DoPostSSE
}


// __try bodies extracted into noinline helpers to satisfy C2712
// (RAII / thread_local and __try cannot coexist in the same function).

// Player chams draw -- does NOT set g_vmIsRendering because world-space player
// models need the engine's own ForcedMaterialOverride calls to pass through.
// We just override material+color, call original once, then restore.
__declspec(noinline) static void PlChamDraw_SEH(
    void* rcx, const void* pState, const void* pInfo, const void* pBones,
    void* pOverride, float r, float g, float b, float a)
{
    VmColorState_t saved = VmSaveColor();
    __try {
        if (pOverride) VmForceMat(rcx, pOverride);
        VmSetColor(r, g, b, a);
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        if (pOverride) VmForceMat(rcx, nullptr);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    VmRestoreColor(saved);
}

// Set depth range on the material render context.
// zFar=0.02f squashes all depth to near-zero, making the model draw on top of everything.
// zFar=1.0f restores normal depth range.
// IMatRenderContext vtable: slot 98 = GetRenderContext (on IMaterialSystem),
// IMatRenderContext vtable: slot 11 = DepthRange, slot 1 = Release.
// Flip backface culling to CW so the inside faces of the model are rendered.
// MATERIAL_CULLMODE_CW = 1 (culls CW winding = shows inward-facing polygons).
// MATERIAL_CULLMODE_CCW = 0 (default, culls CCW = shows outward-facing polygons).
// IMatRenderContext vtable: slot 98 = GetRenderContext (on IMaterialSystem),
//                           slot 40 = CullMode, slot 1 = Release.
__declspec(noinline) static void VmCullMode(int mode)
{
    if (!g_matSystem) return;
    __try {
        using GetRC_t   = void*(__fastcall*)(void*);
        using CullMode_t = void(__fastcall*)(void*, int);
        using Release_t  = void(__fastcall*)(void*);
        void* pRC = ((GetRC_t)(*(void***)g_matSystem)[98])(g_matSystem);
        if (!pRC) return;
        ((CullMode_t)(*(void***)pRC)[40])(pRC, mode);
        // Do NOT call Release() here -- GetRenderContext() does not AddRef
        // the returned pointer. Calling Release() decrements the RC refcount below
        // baseline (freeing it if refcount=1), causing a worker crash at fsn_6.
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Disable/restore depth test+write via IMatRenderContext::OverrideDepthEnable (slot 74).
// Used by glow pass to draw the additive overlay on top of everything when ignoreZ is set.
__declspec(noinline) static void VmGlowOverrideDepth(bool enable, bool writeEnable)
{
    if (!g_matSystem) return;
    __try {
        using GetRC_t = void*(__fastcall*)(void*);
        using ODE_t   = void(__fastcall*)(void*, bool, bool);
        void* pRC = ((GetRC_t)(*(void***)g_matSystem)[98])(g_matSystem);
        if (!pRC) return;
        ((ODE_t)(*(void***)pRC)[74])(pRC, enable, writeEnable);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Bone-scaled additive halo via DrawModelExecute path (arms viewmodel).
// Bones scaled 5% outward from root → shell has greater depth than original →
// depth test (LESSEQUAL) fails inside silhouette, passes only in the ring outside.
// OverrideDepthEnable(true, false): depth test ON (halo appears outside silhouette),
// depth write OFF (halo doesn't corrupt the depth buffer).
__declspec(noinline) static void VmGlowDrawDME_SEH(
    void* rcx, const void* pState, const void* pInfo, const void* pBones)
{
    void* pMat = VmGetGlowMat();
    if (!pMat || !pBones) return;
    int nBones = GetGlowBoneCount_SEH(pInfo);
    if (nBones < 1) return;
    ScaleBonesForGlow(pBones, s_scaledBones, nBones, 1.05f);
    VmColorState_t saved = VmSaveColor();
    g_vmIsRendering = true;
    __try {
        // depth test ON + depth write OFF → halo only outside silhouette
        VmGlowOverrideDepth(true, false);
        VmForceMat(rcx, pMat);
        VmSetColor(g_vmWeaponGlowColor[0], g_vmWeaponGlowColor[1], g_vmWeaponGlowColor[2], g_vmWeaponGlowColor[3]);
        o_DrawModelExecute(rcx, pState, pInfo, s_scaledBones);
        VmGlowOverrideDepth(true, true); // restore
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        VmGlowOverrideDepth(true, true);
    }
    g_vmIsRendering = false;
    VmForceMat(rcx, nullptr);
    VmRestoreColor(saved);
}

// Bone-scaled additive halo via InternalDraw path (weapon viewmodel).
// s_vmGlowScaleBones tells h_DrawModelExecute's early-exit to scale pBones.
__declspec(noinline) static void VmWeaponGlowDraw_SEH(void* rcx_ent)
{
    void* pMat = VmGetGlowMat();
    if (!pMat || !o_CBaseAnimatingInternalDraw) return;
    VmColorState_t saved = VmSaveColor();
    g_vmIsRendering    = true;
    s_vmGlowScaleBones = true;
    __try {
        VmGlowOverrideDepth(true, false);
        VmForceMat(g_modelRender, pMat);
        VmSetColor(g_vmWeaponGlowColor[0], g_vmWeaponGlowColor[1], g_vmWeaponGlowColor[2], g_vmWeaponGlowColor[3]);
        o_CBaseAnimatingInternalDraw(rcx_ent, 1);
        VmGlowOverrideDepth(true, true);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        VmGlowOverrideDepth(true, true);
    }
    s_vmGlowScaleBones = false;
    g_vmIsRendering    = false;
    VmForceMat(g_modelRender, nullptr);
    VmRestoreColor(saved);
}

__declspec(noinline) static void VmChamDraw_SEH(
    void* rcx, const void* pState, const void* pInfo, const void* pBones,
    void* pOverride, float r2, float g2, float b2, float a2, bool ignorez = false)
{
    VmColorState_t saved = VmSaveColor();
    g_vmIsRendering = true;
    __try {
        if (pOverride) VmForceMat(rcx, pOverride);
        VmSetColor(r2, g2, b2, a2);
        if (ignorez) VmCullMode(1 - VmGameCull()); // the opposite of the game's mode = show inside faces
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        if (ignorez) VmCullMode(VmGameCull());     // restore the game's mode (CW on a flipped viewmodel)
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (ignorez) VmCullMode(VmGameCull());
    }
    g_vmIsRendering = false;
    if (pOverride) VmForceMat(rcx, nullptr);
    VmRestoreColor(saved);
}

// ---- No hats helpers ---------------------------------------
static const char* GetEntityModelName(void* pEnt)
{
    if (!pEnt || !g_modelInfo || g_offModelIndex <= 0) return nullptr;
    const char* name = nullptr;
    __try {
        int nModelIndex = *reinterpret_cast<int*>(
            reinterpret_cast<uintptr_t>(pEnt) + g_offModelIndex);
        if (nModelIndex <= 0) return nullptr;
        using GetModel_t     = const void*(__fastcall*)(void*, int);
        using GetName_t      = const char*(__fastcall*)(void*, const void*);
        const void* pModel   = ((GetModel_t)(*(void***)g_modelInfo)[1])(g_modelInfo, nModelIndex);
        if (!pModel || reinterpret_cast<uintptr_t>(pModel) < 0x10000) return nullptr;
        name = ((GetName_t)(*(void***)g_modelInfo)[3])(g_modelInfo, pModel);
        if (reinterpret_cast<uintptr_t>(name) < 0x10000) return nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return name;
}

static bool IsHatModel(const char* modelPath)
{
    if (!modelPath) return false;

    // Exceptions use the path after "models/" so they match vanilla, workshop,
    // and workshop_partner variants equally (all share the same tail path).
    static const char* kExceptions[] = {
        "player/items/sniper/knife_shield",
        "player/items/demo/pegleg",
        "player/items/demo/demo_booties",
        "player/items/sniper/xms_sniper_commandobackpack",
        "player/items/sniper/croc_shield",
        "_zombie.mdl",  // Voodoo-Cursed Soul / zombie cosmetic -- preserve zombie appearance
        nullptr
    };
    for (int i = 0; kExceptions[i]; ++i)
        if (strstr(modelPath, kExceptions[i])) return false;

    static const char* kHatDirs[] = {
        "models/player/items",
        "models/workshop/player/items",
        "models/workshop_partner/player/items",
        nullptr
    };
    for (int i = 0; kHatDirs[i]; ++i)
        if (strstr(modelPath, kHatDirs[i])) return true;

    return false;
}

static int GetClassFromPlayerModelName(const char* mdlName);

// h_DrawModelExecute answers the same handful of questions about a model path
// on every draw call, and there are hundreds of those per frame. Answering them
// costs about twenty strstr calls over a path that never changes.
//
// The engine interns model names, so within a map one pointer is one path. Classify
// once, key the answer on the pointer, and the per-draw cost drops to a hash and
// a compare. Direct-mapped, so a collision just re-classifies. The table is wiped
// when g_matEpoch moves: a purged model's name can be freed and its address reused
// by another model on the next map, which would inherit the old answer (a weapon
// classed as a hat vanishes under No hats).
struct ModelFacts {
    bool    isHat;
    bool    isPlayerBase;
    uint8_t playerClass;   // 1..9 from the path, 0 if none
    uint8_t vmKind;        // transparent viewmodel: 0 none, 1 vm-only model, 2 weapon model (vm only at entity_index -1)
    bool    vmArms;        // vm chams: _arms or /v_models/
    bool    vmWeapon;      // vm chams: /c_models/, not arms, not an _animations model
    bool    plPath;        // player chams: a player or cosmetic path, not viewmodel arms
    bool    plItems;       // .../items/ -- a cosmetic, its own entity
    uint8_t goldKind;      // reskin fallback: 1 stock wrench, 2 stock frying pan
};

// Transparent viewmodel classification (port of SimpleVisual ViewmodelModelKind).
// 1 = model only the viewmodel draws: classic v_ models, c_ arms.
// 2 = c_/w_ weapon model. Same file in your hands and on someone's back, so it only counts
//     when entity_index is -1: C_ViewmodelAttachmentModel is built through
//     InitializeAsClientEntityByIndex, whose first line is `index = -1` (c_baseentity.cpp).
// "/c_models/" + "/weapons/" match stock AND models/workshop{,_partner}/weapons/ -- community
// weapons live there (CEconEntity uses GetPlayerDisplayModel, econ_entity.cpp).
static uint8_t ViewmodelModelKind(const char* p)
{
    if (strstr(p, "/v_models/")) return 1;
    if (strstr(p, "_arms.mdl"))  return 1;
    if (strstr(p, "/c_models/")) return 2;
    if (strstr(p, "/weapons/"))  return 2;
    return 0;
}

static const ModelFacts& ModelFactsFor(const char* mdl)
{
    constexpr int kBits = 9, kSize = 1 << kBits;
    static const char* s_key[kSize] = {};
    static ModelFacts  s_val[kSize] = {};
    static const ModelFacts s_empty = {};

    if (!mdl) return s_empty;

    static uint32_t s_epoch = 0;
    const uint32_t epoch = g_matEpoch.load(std::memory_order_relaxed);
    if (epoch != s_epoch) {
        s_epoch = epoch;
        memset(s_key, 0, sizeof(s_key));
    }

    size_t h = reinterpret_cast<uintptr_t>(mdl);
    h = (h >> 4) ^ (h >> 13);
    const int slot = (int)(h & (kSize - 1));

    if (s_key[slot] == mdl) return s_val[slot];

    ModelFacts f{};
    f.isHat = IsHatModel(mdl);
    f.isPlayerBase = (strstr(mdl, "models/player/")
                   || strstr(mdl, "models/workshop/player/")
                   || strstr(mdl, "models/workshop_partner/player/"))
                   && !strstr(mdl, "models/player/items");
    f.playerClass = (uint8_t)GetClassFromPlayerModelName(mdl);
    f.vmKind = ViewmodelModelKind(mdl);
    f.vmArms   = strstr(mdl, "_arms") || strstr(mdl, "/v_models/");
    f.vmWeapon = !f.vmArms && strstr(mdl, "/c_models/") && !strstr(mdl, "_animations");
    // Player models and cosmetics: models/player/ (base classes, stock cosmetics),
    // models/workshop/player/ and models/workshop_partner/player/ (workshop cosmetics).
    f.plPath   = !f.vmArms && (strstr(mdl, "models/player/")
                            || strstr(mdl, "models/workshop/player/")
                            || strstr(mdl, "models/workshop_partner/player/"));
    f.plItems  = strstr(mdl, "/items/") != nullptr;
    f.goldKind = strstr(mdl, "c_wrench/c_wrench") ? 1 : strstr(mdl, "c_frying_pan/c_frying_pan") ? 2 : 0;

    s_key[slot] = mdl;
    s_val[slot] = f;
    return s_val[slot];
}

// Player model (not local player unless opt-in). pInfo: entIdx at +0x44.
static bool PlDetectPlayer(const ModelFacts& mf, const uint8_t* pInfoB)
{
    if (!mf.plPath) return false;
    // Cosmetics are separate entities; they only count when the attachments option is on.
    if (!g_plChamsAttachments && mf.plItems) return false;
    if (!g_plChamsLocalPlayer) {
        const int entIdx = *reinterpret_cast<const int*>(pInfoB + 0x44);
        if (entIdx > 0 && entIdx == GetLocalPlayerIndex()) return false;
    }
    return true;
}

// Weapon/arms viewmodel draw. The model path decides first (cached); only a match pays for
// the entity lookup that confirms a CTFViewModel / CTFWearableVM.
static bool VmDetectType(const ModelFacts& mf, const uint8_t* pInfoB, bool& outWeapon, bool& outArms)
{
    outWeapon = outArms = false;
    if (!mf.vmArms && !mf.vmWeapon) return false;
    if (!VmIsViewmodelEntity_SEH(*reinterpret_cast<const int*>(pInfoB + 0x44))) return false;
    outArms   = mf.vmArms;
    outWeapon = mf.vmWeapon;
    return true;
}

// ---- Transparent viewmodel ------------------------------------------------------------
// Blend: IVRenderView::SetBlend around the weapon/arms draw. Engine reads r_blend inside
// DrawModelExecute, so set right before the original and put back right after -- r_blend is
// file-scope engine state, every later model would inherit it otherwise.
static bool VmAlphaWanted()
{
    return g_vmAlphaEnable && g_vmAlpha < 0.999f && g_renderView != nullptr;   // 1 = no-op
}
static float VmAlphaMul() { return VmAlphaWanted() ? g_vmAlpha : 1.f; }

static void VmAlphaDME(bool on, void* rcx, const void* pState, const void* pInfo, const void* pBones)
{
    if (!on) { o_DrawModelExecute(rcx, pState, pInfo, pBones); return; }
    const VmColorState_t saved = VmSaveColor();
    VmSetColor(saved.col[0], saved.col[1], saved.col[2], saved.blend * g_vmAlpha);
    o_DrawModelExecute(rcx, pState, pInfo, pBones);
    VmRestoreColor(saved);
}

// Flat alpha ($blendtintbybasealpha). SetBlend feeds modulation alpha, and model shaders then
// multiply the BASE TEXTURE alpha into it -- on TF2 weapons that is the phong mask, mostly
// dark, so 0.50 renders as 0.50 * mask per pixel:
//   skin_ps20b.fxc                      alpha = lerp( baseColor.a * alpha, alpha, ... )
//   vertexlit_and_unlit_generic_ps2x    alpha *= baseColor.a
//   dx8 VertexLitGeneric.psh            r0.a = t0.a * c3.a
// $blendtintbybasealpha guards the multiply in all three (dx8 picks
// VertexLitGeneric_BlendTint.psh: `mov r0.a, c3`). Its tint half is identity here:
// modulation rgb stays white. Static combo -> needs RecomputeStateSnapshots, which is
// structural: never inside DrawModelExecute, and only with mat_queue_mode 0 (same
// force/wait/restore as map textures). Flip lands a few frames after first draw.
//
// Materials come from pState, not GetModelMaterialCount (returned 0 for every vm model):
//   DrawModelState_t : m_pStudioHWData 0x08
//   studiohwdata_t   : m_NumLODs 0x04, m_pLODs 0x08
//   studioloddata_t  : numMaterials 0x0C, ppMaterials 0x10, stride 0x30
static const size_t kVmLodStride = 0x30;
static const int    kVmMatSlots  = 64;   // power of 2: slot = hash & (n-1)
static const int    kVmMatMax    = 24;
struct VmMatEntry {
    const void*   key;               // model_t*
    void*         mat[kVmMatMax];    // IMaterial*, for the re-snapshot
    void*         var[kVmMatMax];    // its $blendtintbybasealpha
    unsigned char orig[kVmMatMax];
    int           n;                 // 0 = cached "nothing here"
    bool          applied;           // currently flipped to 1
    bool          evict;             // another model wants the slot -- restore, then free
};
static VmMatEntry s_vmFlatMat[kVmMatSlots] = {};
static bool       s_vmFlatPatched = false;
static uint32_t   s_vmFlatEpoch   = 0;
static int        s_vmFlatWait    = -1;   // -1 = not holding mat_queue_mode; else frames left

// IMaterialVar::SetIntValue slot 4 (SetFloatValue 3 / SetStringValue 5 above); m_intVal +0x10.
__declspec(noinline) static void MatVarSetInt(void* pVar, int val)
{
    if (!pVar) return;
    __try { using Fn = void(__fastcall*)(void*, int);
            ((Fn)(*(void***)pVar)[4])(pVar, val); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
__declspec(noinline) static int MatVarGetIntRaw(void* pVar)
{
    int v = -1;
    __try { v = *reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pVar) + 0x10); }
    __except(EXCEPTION_EXECUTE_HANDLER) { v = -1; }
    return v;
}
// MatFindVar ignores `found`; here a missing var must read as null (shader lacks it = skip).
__declspec(noinline) static void* MatFindVarStrict(void* pMat, const char* szVar)
{
    void* r = nullptr;
    __try { using Fn = void*(__fastcall*)(void*, const char*, bool*, bool);
            bool found = false;
            r = ((Fn)(*(void***)pMat)[11])(pMat, szVar, &found, false);
            if (!found) r = nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
    return r;
}

// Hand back mat_queue_mode unless map textures is mid-pass (its own end restores it).
static void VmFlatAlpha_ReleaseQueue()
{
    if (s_vmFlatWait < 0) return;
    s_vmFlatWait = -1;
    if (s_wcPass == kWcPassNone) WcRestoreQueueMode();
}

// Map change frees model_t, IMaterial and IMaterialVar alike -- drop, never restore.
static void VmFlatAlpha_CheckEpoch()
{
    const uint32_t ep = g_matEpoch.load(std::memory_order_relaxed);
    if (ep == s_vmFlatEpoch) return;
    s_vmFlatEpoch = ep;
    memset(s_vmFlatMat, 0, sizeof(s_vmFlatMat));
    s_vmFlatPatched = false;
    VmFlatAlpha_ReleaseQueue();
}

static void VmFlatAlpha_Write(VmMatEntry& e, bool on)
{
    for (int i = 0; i < e.n; ++i) {
        MatVarSetInt(e.var[i], on ? 1 : e.orig[i]);
        MatRecomputeSnapshots(e.mat[i]);
    }
    e.applied = on;
}

// FSN stage 5 (game thread, outside any draw).
void VmFlatAlpha_Tick()
{
    VmFlatAlpha_CheckEpoch();
    if (!IsInGame()) { VmFlatAlpha_ReleaseQueue(); return; }   // load/unload: materials in flux
    const bool want = VmAlphaWanted() && g_vmFlatAlpha;
    if (!want && !s_vmFlatPatched) {
        bool evicting = false;
        for (const auto& e : s_vmFlatMat) evicting |= e.evict;
        if (!evicting) { VmFlatAlpha_ReleaseQueue(); return; }
    }

    bool dirty = false;
    for (auto& e : s_vmFlatMat) {
        if (e.evict && !e.applied) { e = VmMatEntry{}; continue; }   // nothing to undo
        if (e.key && e.n > 0 && (e.evict || e.applied != want)) dirty = true;
    }
    if (!dirty) { VmFlatAlpha_ReleaseQueue(); return; }

    // Structural: material queue thread off first, then wait for BeginFrame to take it.
    if (s_vmFlatWait < 0) {
        if (!WcForceSingleThreaded()) return;   // retry next frame; threaded = the crash
        s_vmFlatWait = 3;
    }
    // Map textures may have handed the mode back while we waited.
    if (s_wcQueueSaved == INT_MIN && s_cvMatQueue && (int)s_cvMatQueue->GetFloat() != 0) {
        s_vmFlatWait = -1;
        return;
    }
    if (s_vmFlatWait > 0) { --s_vmFlatWait; return; }

    bool anyOn = false;
    for (auto& e : s_vmFlatMat) {
        if (!e.key || e.n == 0) continue;
        const bool w = e.evict ? false : want;   // evicting = last pointer to these mats: restore now
        if (e.applied != w) VmFlatAlpha_Write(e, w);
        if (e.evict) { e = VmMatEntry{}; continue; }
        anyOn |= e.applied;
    }
    s_vmFlatPatched = anyOn;
    VmFlatAlpha_ReleaseQueue();
}

// Eject: EjectRestore_Service already forced mat_queue_mode 0 and waited.
static void VmFlatAlpha_RestoreAll()
{
    VmFlatAlpha_CheckEpoch();
    for (auto& e : s_vmFlatMat)
        if (e.key && e.applied) VmFlatAlpha_Write(e, false);
    memset(s_vmFlatMat, 0, sizeof(s_vmFlatMat));
    s_vmFlatPatched = false;
    s_vmFlatWait    = -1;   // eject path restores the mode itself
}

__declspec(noinline) static void VmFlatAlpha_TakeLods(VmMatEntry* e, const void* pState)
{
    __try {
        const uint8_t* st = reinterpret_cast<const uint8_t*>(pState);
        const uint8_t* hw = *reinterpret_cast<const uint8_t* const*>(st + 0x08);
        if (reinterpret_cast<uintptr_t>(hw) < 0x10000) return;
        const int nLods = *reinterpret_cast<const int*>(hw + 0x04);
        const uint8_t* lods = *reinterpret_cast<const uint8_t* const*>(hw + 0x08);
        if (nLods <= 0 || nLods > 16 || reinterpret_cast<uintptr_t>(lods) < 0x10000) return;
        // every LOD: same IMaterial* repeats (deduped), and cannot miss the one about to bind
        for (int l = 0; l < nLods; ++l) {
            const uint8_t* lod = lods + static_cast<size_t>(l) * kVmLodStride;
            const int nMat = *reinterpret_cast<const int*>(lod + 0x0C);
            void** pp = *reinterpret_cast<void** const*>(lod + 0x10);
            if (nMat <= 0 || nMat > 64 || reinterpret_cast<uintptr_t>(pp) < 0x10000) continue;
            for (int i = 0; i < nMat && e->n < kVmMatMax; ++i) {
                void* pMat = pp[i];
                if (reinterpret_cast<uintptr_t>(pMat) < 0x10000) continue;
                bool dup = false;
                for (int j = 0; j < e->n; ++j) dup |= (e->mat[j] == pMat);
                if (dup) continue;
                void* pVar = MatFindVarStrict(pMat, "$blendtintbybasealpha");
                if (!pVar) continue;
                e->orig[e->n] = (MatVarGetIntRaw(pVar) == 1) ? 1u : 0u;
                e->mat[e->n]  = pMat;
                e->var[e->n]  = pVar;
                ++e->n;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Draw hook: discovery only. Writes happen in VmFlatAlpha_Tick.
static void VmFlatAlpha_SetFor(const void* pMdl, const void* pState)
{
    if (!pMdl || !pState) return;
    VmFlatAlpha_CheckEpoch();
    VmMatEntry* e = &s_vmFlatMat[(reinterpret_cast<uintptr_t>(pMdl) >> 4) & (kVmMatSlots - 1)];
    if (e->key == pMdl) return;
    // Collision with an applied entry: overwriting it would leave its materials flat for the
    // rest of the map. Tick restores + frees it; this model claims the slot on a later draw.
    if (e->key && e->applied) { e->evict = true; return; }
    *e = VmMatEntry{};
    e->key = pMdl;
    VmFlatAlpha_TakeLods(e, pState);
}

// ---- Bodygroups with cosmetics hidden (No hats, Zombify) ---------------------
// The client already rebuilds a player's m_nBody from every weapon and wearable he
// has (CTFPlayerShared::RecalculatePlayerBodygroups, tf_player_shared.cpp), so the
// body in ModelRenderInfo_t is exact and stable frame to frame. Hiding cosmetics
// only has to take back what cosmetics hid:
//   * groups only cosmetics touch     -> cleared
//   * groups only weapons touch       -> left alone (Gunslinger arm, Huntsman arrows,
//                                        Sydney Sleeper bullets, Short Circuit)
//   * groups both touch               -> kept while the player owns the functional item
// The old version rebuilt the body from zero off an entity-list scan on the Present
// thread: every missed frame dropped a group (flicker), and the Gunslinger has no
// world model to find, so its arm was always wiped.
//
// Tables: models/player/<class>.mdl bodyparts (index, base, submodel count) crossed
// with every item's player_bodygroups / additional_hidden_bodygroups /
// wm_bodygroup_override in tf/scripts/items/items_game.txt. Groups not listed keep
// the engine's value.
enum BodyFn : uint8_t { kFnNone, kFnDemoBoots, kFnMedicPack, kFnHeavyFists, kFnThruster };
struct BodyRule { uint8_t base, count, fn; };   // fn = kFnNone: always cleared

static const BodyRule k_bodyRules[10][4] = {
    {},
    /* 1 scout    */ { {1,2,kFnNone}, {2,2,kFnNone}, {4,2,kFnNone}, {8,2,kFnNone} },   // hat headphones shoes_socks dogtags
    /* 2 sniper   */ { {2,2,kFnNone} },                                              // hat
    /* 3 soldier  */ { {2,2,kFnNone}, {4,2,kFnNone}, {8,2,kFnNone} },                 // hat medal grenades
    /* 4 demo     */ { {2,2,kFnDemoBoots}, {4,2,kFnNone} },                           // shoes grenades
    /* 5 medic    */ { {1,2,kFnMedicPack} },                                         // medic_backpack
    /* 6 heavy    */ { {1,3,kFnHeavyFists}, {3,2,kFnNone} },                          // hands bullets
    /* 7 pyro     */ { {1,2,kFnNone}, {2,2,kFnNone}, {4,2,kFnThruster} },             // head grenades backpack
    /* 8 spy      */ {},
    /* 9 engineer */ { {1,2,kFnNone} },                                              // hat
};

static bool DefIn(int def, BodyFn fn)
{
    switch (fn) {
    case kFnDemoBoots:  return def == 405 || def == 608;                  // Ali Baba's Wee Booties, Bootlegger
    case kFnMedicPack:  return def == 411 || def == 998;                  // Quick-Fix, Vaccinator
    case kFnThruster:   return def == 1179;                               // Thermal Thruster
    case kFnHeavyFists: return def == 43  || def == 239 || def == 331 || def == 587 || def == 656
                            || def == 1084 || def == 1100 || def == 1184; // every tf_weapon_fists that hides hands
    default:            return false;
    }
}

static uint8_t s_bodyFnOwned[33];   // bit (1 << BodyFn) per player, rebuilt each frame on the game thread
static int     s_offItemDef   = 0;  // m_iItemDefinitionIndex from DT_EconEntity
static int     s_offWearables = 0;  // CUtlVector<EHANDLE> m_hMyWearables

// m_hMyWearables is a RecvPropUtlVector: the named prop sits at offset 0 and the real
// vector offset lives in the child props' CRecvPropExtra_UtlVector (dt_utlvector_recv.cpp:
// 4 pointers, stride, m_Offset).
__declspec(noinline) static int ResolveWearablesOffset()
{
    RecvTable* t = FindNetworkTable("DT_TFPlayer");
    if (!t) return -1;
    int result = -1;
    struct Walker {
        static int Find(RecvTable* tab, int base, int depth) {
            if (!tab || depth > 6) return -1;
            for (int i = 0; i < tab->m_nProps; ++i) {
                RecvProp& p = tab->m_pProps[i];
                if (!p.m_pVarName) continue;
                if (strcmp(p.m_pVarName, "m_hMyWearables") == 0 && p.m_pDataTable &&
                    p.m_pDataTable->m_nProps > 0 && p.m_pDataTable->m_pProps[0].m_pExtraData) {
                    const uint8_t* extra = static_cast<const uint8_t*>(p.m_pDataTable->m_pProps[0].m_pExtraData);
                    int vecOff = *reinterpret_cast<const int*>(extra + 36);
                    return base + p.m_Offset + vecOff;
                }
                if (p.m_pDataTable) {
                    int r = Find(p.m_pDataTable, base + p.m_Offset, depth + 1);
                    if (r >= 0) return r;
                }
            }
            return -1;
        }
    };
    __try { result = Walker::Find(t, 0, 0); } __except(EXCEPTION_EXECUTE_HANDLER) { result = -1; }
    return result;
}

__declspec(noinline) static uint8_t OwnedBodyFns_SEH(void* pPlayer, int cls)
{
    uint8_t owned = 0;
    __try {
        if (cls == 4) {
            if (s_offWearables <= 0) return 0;
            const uint8_t* vec = reinterpret_cast<const uint8_t*>(pPlayer) + s_offWearables;
            const unsigned int* h = *reinterpret_cast<const unsigned int* const*>(vec);
            int count = *reinterpret_cast<const int*>(vec + 16);           // CUtlVector m_Size
            if (!h || count <= 0 || count > 16) return 0;
            for (int i = 0; i < count; ++i) {
                void* pWear = GetClientEntity((int)(h[i] & 0x3FFF));
                if (pWear && DefIn(*reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pWear) + s_offItemDef), kFnDemoBoots))
                    owned |= 1 << kFnDemoBoots;
            }
            return owned;
        }
        const BodyFn fn = (cls == 5) ? kFnMedicPack : (cls == 6) ? kFnHeavyFists : kFnThruster;
        const uint8_t* wpns = reinterpret_cast<const uint8_t*>(pPlayer) + g_offMyWeapons;
        for (int i = 0; i < 48; ++i) {                                  // MAX_WEAPONS
            unsigned int h = *reinterpret_cast<const unsigned int*>(wpns + i * 4);
            if (h == 0 || h == 0xFFFFFFFF) continue;
            void* pWpn = GetClientEntity((int)(h & 0x3FFF));
            if (pWpn && DefIn(*reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pWpn) + s_offItemDef), fn))
                owned |= 1 << fn;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return owned;
}

// FRAME_RENDER_START: which mixed groups each player really owns the functional item for.
void BodyRules_Tick()
{
    memset(s_bodyFnOwned, 0, sizeof(s_bodyFnOwned));
    if ((!g_noHats && !g_zombify) || g_offMyWeapons <= 0) return;
    if (s_offItemDef == 0) {
        RecvTable* t = FindNetworkTable("DT_EconEntity");
        int off = t ? FindPropOffset(t, "m_iItemDefinitionIndex") : -1;
        s_offItemDef = (off > 0) ? off : -1;
    }
    if (s_offWearables == 0) {
        int off = ResolveWearablesOffset();
        s_offWearables = (off > 0) ? off : -1;
        DebugLog("bodygroups: m_iItemDefinitionIndex=%d m_hMyWearables=%d", s_offItemDef, s_offWearables);
    }
    if (s_offItemDef <= 0) return;

    for (int p = 1; p <= 32; ++p) {
        const int cls = GetPlayerClass(p);
        if (cls < 4 || cls > 7) continue;                               // demo medic heavy pyro only
        if (void* pPlayer = GetClientEntity(p)) s_bodyFnOwned[p] = OwnedBodyFns_SEH(pPlayer, cls);
    }
}

// Player a base model belongs to: the entity itself, or a ragdoll's m_hPlayer.
__declspec(noinline) static int BodyOwnerIndex(int entIdx)
{
    if (entIdx >= 1 && entIdx <= 32) return entIdx;
    if (entIdx <= 32 || g_offRagdollPlayer <= 0) return 0;
    void* pRag = GetClientEntity(entIdx);
    if (!pRag) return 0;
    __try {
        const int idx = (int)(*reinterpret_cast<const unsigned int*>(
            reinterpret_cast<const uint8_t*>(pRag) + g_offRagdollPlayer) & 0x3FFF);
        return (idx >= 1 && idx <= 32) ? idx : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// Body with every cosmetic-only group back at its default. modelCls comes from the
// model path, so a disguised spy is cleaned with the disguise class's layout.
static int CleanBody(int ownerIdx, int modelCls, int body)
{
    if (modelCls < 1 || modelCls > 9 || body <= 0) return body;
    const uint8_t owned = (ownerIdx >= 1 && ownerIdx <= 32) ? s_bodyFnOwned[ownerIdx] : 0;
    for (const BodyRule& r : k_bodyRules[modelCls]) {
        if (!r.base) break;
        if (r.fn != kFnNone && (owned & (1 << r.fn))) continue;
        const int sub = (body / r.base) % r.count;
        body -= sub * r.base;
    }
    return body;
}

// ---- No hats: particle effect suppression ------------------
//
// Unusual effects and cosmetic-emitted particles (e.g. cigar smoke) are
// CNewParticleEffect objects managed by CParticleProperty (m_Particles embedded
// in C_BaseEntity).  They render separately from the model and are not caught by
// the DrawModelExecute hat-model skip.
//
// Layout (Source SDK / TF2 x64):
//   C_BaseEntity + g_offParticleProp
//      --  CParticleProperty::m_pOuter     [+0]  ptr back to the entity  (used to locate the property)
//        CParticleProperty::m_ParticleEffects [+8]  CUtlVector<ParticleEffectList_t>
//          m_pMemory   [+8+0]  = ptr to allocated array of ParticleEffectList_t
//          m_nAllocCount[+8+8]
//          m_nGrowSize [+8+12]
//          m_Size      [+8+16] = number of active effects
//   ParticleEffectList_t (32 bytes per entry):
//     pControlPoints  [+0]  CUtlVector (20 bytes)
//     (4 bytes padding)
//     pParticleEffect [+24] CSmartPtr<CNewParticleEffect> -- first 8 bytes = raw ptr
//
// CNewParticleEffect primary vtable = IParticleEffect vtable (at object+0).
// IParticleEffect methods (MSVC x64, 1 dtor slot -- confirmed: slot 11 = GetEffectName from debug log):
//   0: ~dtor  1: Update  2: StartRender  3: ShouldSimulate  4: SetShouldSimulate
//   5: SimulateParticles  6: RenderParticles  7: NotifyRemove  8: NotifyDestroyParticle
//   9: GetSortOrigin  10: GetParticlePosition  11: GetEffectName
// Locates CParticleProperty within an entity by scanning for the m_pOuter back-pointer.
// Caches result in g_offParticleProp (shared for all entity types -- m_Particles is in C_BaseEntity).
static int FindParticlePropOff(void* pEnt)
{
    if (g_offParticleProp >= 0)  return g_offParticleProp;
    if (g_offParticleProp == -1) return -1;

    const uintptr_t self = reinterpret_cast<uintptr_t>(pEnt);
    const uint8_t*  b    = reinterpret_cast<const uint8_t*>(pEnt);

    for (int off = 0x80; off < 0x2000; off += 8) {
        __try {
            if (*reinterpret_cast<const uintptr_t*>(b + off) != self) continue;
            // Validate next 20 bytes look like a CUtlVector (m_ParticleEffects).
            // m_Size (at +24 relative to m_pOuter) must be small and non-negative.
            int sz = *reinterpret_cast<const int*>(b + off + 24);
            if (sz < 0 || sz > 32) continue;
            int al = *reinterpret_cast<const int*>(b + off + 16);
            if (al < 0 || al > 64) continue;
            g_offParticleProp = off;
            DebugLog("noHats: CParticleProperty found at entity+0x%X", off);
            return off;
        } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
    }
    g_offParticleProp = -1;
    DebugLog("noHats: CParticleProperty not found");
    return -1;
}

// ---- Killstreak eye glow CP9 color write -------------------
// Scans the local player's CParticleProperty every frame and writes sheen color
// to m_ControlPoints[9].m_Position (CP9 = CUSTOM_COLOR_CP1 = eye glow color) in
// every "killstreak_eyes_*" effect AND all of its child CParticleCollections.
//
// Root-only write is insufficient: CParticleCollection::SetControlPoint propagates
// to children (sub-emitters). TF2 sets CP9 to team color via SetControlPoint, which
// reaches all children. Our per-frame write must do the same.
//
// Child layout (CParticleCollection, no IParticleEffect prefix):
//   CPC+1052 = m_ControlPoints[9].m_Position
//   CPC+8828 = m_pNext  (next sibling in parent's m_Children list)
//   CPC+8884 = m_Children.m_pHead  (head of own children list)
//
// Why scan rather than capture in h_CParticlePropCreate:
//   The hook only fires on NEW particle creations. Effects already live when the
//   checkbox is ticked would never be captured. Direct scan handles all cases.
//
// Placed after FindParticlePropOff and the k_particleEffect* constants it uses.
// WriteCP9Recursive extracted noinline so __try lives in a plain function (C2712 guard).

// Write sheen color to CP9 of a CParticleCollection and ALL its children recursively.
// pCpc: pointer to start of CParticleCollection (NOT CNewParticleEffect -- no vtable prefix).
__declspec(noinline) static void WriteCP9Recursive(uint8_t* pCpc, const float col[3], int depth)
{
    if (depth > 8 || !pCpc || reinterpret_cast<uintptr_t>(pCpc) < 0x10000) return;
    __try {
        float* pPos = reinterpret_cast<float*>(pCpc + k_cpcCP9PosOff);
        pPos[0] = col[0]; pPos[1] = col[1]; pPos[2] = col[2];
        uint8_t* pChild = *reinterpret_cast<uint8_t**>(pCpc + k_cpcChildHdOff);
        while (pChild && reinterpret_cast<uintptr_t>(pChild) > 0x10000) {
            WriteCP9Recursive(pChild, col, depth + 1);
            pChild = *reinterpret_cast<uint8_t**>(pChild + k_cpcNextOff);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) void KsEyeTick()
{
    if (!g_ksEyeUseSheenColorGlobal) return;

    int li = GetLocalPlayerIndex();
    if (li < 1) return;
    void* ent = GetClientEntity(li);
    if (!ent) return;

    int propOff = FindParticlePropOff(ent);
    if (propOff < 0) return;

    float sheenCol[3];
    if (g_enableSheenAnim) GetAnimatedSheenColor(sheenCol);
    else { sheenCol[0] = g_sheenColor[0]; sheenCol[1] = g_sheenColor[1]; sheenCol[2] = g_sheenColor[2]; }

    static int s_logCooldown = 0;
    bool doLog = (++s_logCooldown >= 300);
    if (doLog) s_logCooldown = 0;

    __try {
        uintptr_t pProp = reinterpret_cast<uintptr_t>(ent) + propOff;
        uintptr_t pMem  = *reinterpret_cast<uintptr_t*>(pProp + 8);      // m_ParticleEffects.m_pMemory
        int       cnt   = *reinterpret_cast<int*>       (pProp + 8 + 16); // m_ParticleEffects.m_Size
        if (doLog) DebugLog("KsEye: prop=%d cnt=%d sheen=(%.2f,%.2f,%.2f)",
            propOff, cnt, sheenCol[0], sheenCol[1], sheenCol[2]);
        for (int i = 0; pMem && i < cnt && i < 32; ++i) {
            void* pEff = *reinterpret_cast<void**>(
                pMem + i * k_particleEffectStride + k_particleEffectPtrOff);
            if (doLog) DebugLog("KsEye: effect[%d] ptr=%p", i, pEff);
            if (!pEff || reinterpret_cast<uintptr_t>(pEff) < 0x10000) continue;
            // Filter to killstreak_eyes_* only (slot 11 = GetEffectName).
            // Skip if name is available but doesn't match -- avoids writing to unrelated effects.
            const char* nm = VCall<const char*>(pEff, 11);
            if (doLog) DebugLog("KsEye: effect[%d] name=%s", i, nm ? nm : "(null)");
            if (!nm || strncmp(nm, "killstreak_eyes", 15) != 0) continue;
            // Write sheen color to root CPC + all child CParticleCollections.
            // CPC starts at NEF+k_nefCpcOff=NEF+16; WriteCP9Recursive handles recursion.
            if (doLog) DebugLog("KsEye: effect[%d] writing sheen=(%.2f,%.2f,%.2f) to root+children",
                i, sheenCol[0], sheenCol[1], sheenCol[2]);
            WriteCP9Recursive(reinterpret_cast<uint8_t*>(pEff) + k_nefCpcOff, sheenCol, 0);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// One-time calibration: find the byte offset of the flags bitfield inside CNewParticleEffect.
// The bitfield byte contains (LSB to MSB): m_bDontRemove m_bRemove m_bDrawn
//   m_bNeedsBBoxUpdate m_bIsFirstFrame m_bAutoUpdateBBox m_bAllocated m_bSimulate.
// We locate it by calling SetShouldSimulate(false->true) and finding which byte toggles bit 7.
static void __declspec(noinline) CalibrateBitfieldOffset(void* pEffect, void** vt)
{
    __try {
        using SetSim_t = void(__fastcall*)(void*, bool);
        auto fnSetSim = (SetSim_t)vt[k_setSimSlot];
        uint8_t* obj = (uint8_t*)pEffect;
        fnSetSim(pEffect, false);                       // ensure m_bSimulate(bit7)=0
        // pEffect is a heap allocation -- heap memory is always in committed virtual pages,
        // so a 2048-byte bulk read will not trigger an AV (and Valve VEH won't fire).
        uint8_t snap[2048];
        memcpy(snap, obj, sizeof(snap));                // snapshot with bit7=0
        fnSetSim(pEffect, true);                        // restore bit7=1
        for (int off = 8; off < (int)sizeof(snap); off++) {
            if (!(snap[off] & 0x80) && (obj[off] & 0x80)) {
                s_bRemoveOffset = off;
                DebugLog("particles: bitfield offset=%d (byte=0x%02X)", off, (unsigned)obj[off]);
                return;
            }
        }
        s_bRemoveOffset = -2; // sentinel: failed -- don't retry, use identity kill
        DebugLog("particles: bitfield calibration failed");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_bRemoveOffset = -2;
        DebugLog("particles: calibration SEH");
    }
}

// Iterates CParticleProperty effects on pEnt.
// bSim=false: kills effects matching the unusual/footstep filter by setting m_bRemove so
//   CParticleMgr::Simulate destroys them properly next frame; freezes all others via
//   SetShouldSimulate(false).
// bSim=true: re-enables simulation on surviving effects (m_bRemove ones are already gone).
static void SetEntityParticlesSimulate(void* pEnt, bool bSim)
{
    if (!pEnt) return;
    int propOff = FindParticlePropOff(pEnt);
    if (propOff < 0) return;

    const uint8_t* b = reinterpret_cast<const uint8_t*>(pEnt);
    void*  effectsArr = nullptr;
    int    numEffects  = 0;
    __try {
        effectsArr = *reinterpret_cast<void* const*>(b + propOff + 8);   // m_pMemory
        numEffects = *reinterpret_cast<const int*>  (b + propOff + 24);  // m_Size
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    if (!effectsArr || numEffects <= 0 || numEffects > 32) return;

    using SetSim_t  = void(__fastcall*)(void*, bool);
    using GetName_t = const char*(__fastcall*)(void*);
    for (int i = 0; i < numEffects; ++i) {
        void* pEffect = nullptr;
        __try {
            uint8_t* entry = reinterpret_cast<uint8_t*>(effectsArr) + i * k_particleEffectStride;
            pEffect = *reinterpret_cast<void**>(entry + k_particleEffectPtrOff);
        } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!pEffect || reinterpret_cast<uintptr_t>(pEffect) < 0x10000) continue;

        void** vt = nullptr;
        __try { vt = *reinterpret_cast<void***>(pEffect); } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!vt || reinterpret_cast<uintptr_t>(vt[k_setSimSlot]) < 0x10000) continue;

        // When suppressing: kill unusual/footstep effects by marking m_bRemove.
        // CParticleMgr picks up the flag next Simulate() and properly destroys the effect
        // (removes from leaf system, linked list, calls NotifyRemove -> delete).
        // SetShouldSimulate(false) is NOT used for these -- it would freeze particles in
        // place (stops simulation but rendering continues).
        if (!bSim) {
            const char* pName = nullptr;
            __try {
                if (reinterpret_cast<uintptr_t>(vt[11]) > 0x10000)
                    pName = ((GetName_t)vt[11])(pEffect);
            } __except(EXCEPTION_EXECUTE_HANDLER) {}

            bool isWeaponUnusualSup = pName && strstr(pName, "weapon_unusual_");
            if (pName && reinterpret_cast<uintptr_t>(pName) > 0x10000 &&
                (strncmp(pName, "halloween_boss_foot", 19) == 0 ||
                 (!isWeaponUnusualSup && (strstr(pName, "unusual_") || strstr(pName, "superrare_")))))
            {
                // Find bitfield offset on first encounter, then set m_bRemove (bit 1).
                if (s_bRemoveOffset == -1)
                    CalibrateBitfieldOffset(pEffect, vt);
                if (s_bRemoveOffset >= 0) {
                    __try {
                        *((uint8_t*)pEffect + s_bRemoveOffset) |= 0x02; // m_bRemove = true
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
                continue; // don't also call SetShouldSimulate on a dying effect
            }
        }

        __try {
            ((SetSim_t)vt[k_setSimSlot])(pEffect, bSim);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static void SuppressHatParticles(bool bSuppressed)
{
    if (g_offModelIndex <= 0) return;
    const int hiEnt = GetHighestEntityIndex();
    __try {
        for (int i = 33; i <= hiEnt; ++i) {
            void* pEnt = GetClientEntity(i);
            if (!pEnt) continue;
            const char* mdl = GetEntityModelName(pEnt);
            if (!mdl || !ModelFactsFor(mdl).isHat) continue;
            SetEntityParticlesSimulate(pEnt, !bSuppressed);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// FRAME_RENDER_START. Particles are simulated on this thread, so they are only touched
// from here. A cosmetic that just spawned waits at most 100 ms for its effects to go.
void NoHats_Tick()
{
    static bool  s_wasOn = false;
    static DWORD s_last  = 0;
    if (g_noHats) {
        const DWORD now = GetTickCount();
        if (now - s_last > 100) { s_last = now; SuppressHatParticles(true); }
    } else if (s_wasOn) {
        SuppressHatParticles(false);
    }
    s_wasOn = g_noHats;
}

// ---- Zombie cosmetic model cache ---------------------------
static const char* k_zombieModelPaths[10] = {
    nullptr,
    "models/player/items/scout/scout_zombie.mdl",
    "models/player/items/sniper/sniper_zombie.mdl",
    "models/player/items/soldier/soldier_zombie.mdl",
    "models/player/items/demo/demo_zombie.mdl",
    "models/player/items/medic/medic_zombie.mdl",
    "models/player/items/heavy/heavy_zombie.mdl",
    "models/player/items/pyro/pyro_zombie.mdl",
    "models/player/items/spy/spy_zombie.mdl",
    "models/player/items/engineer/engineer_zombie.mdl",
};

// Returns TF2 class index (1-9) from a player model path, or 0 if not matched.
static int GetClassFromPlayerModelName(const char* mdlName) {
    if (!mdlName) return 0;
    static const char* kSubs[] = { nullptr,
        "scout", "sniper", "soldier", "demo",
        "medic", "heavy", "pyro", "spy", "engineer" };
    for (int c = 1; c <= 9; ++c)
        if (strstr(mdlName, kSubs[c])) return c;
    return 0;
}

// Builds a cached mapping: s_boneMap[cls][zombiBoneIdx] = playerBoneIdx (-1 if no match).
// Uses bone NAME matching so different orderings between player and zombie studioHdrs are handled.
//
// studiohdr_t layout (MDL v48, all-int, no pointers -- same in x32 and x64):
//   +0x9C  int numbones
//   +0xA0  int boneindex   (byte offset from hdr base to first mstudiobone_t)
//
// mstudiobone_t layout (sizeof = 0xD8 = 216, no pointer fields):
//   +0x00  int sznameindex  -> bone name = (char*)bone + sznameindex
static void BuildZombieBoneMap(int cls, const void* playerHdr, const void* zombieHdr)
{
    if (s_boneMapped[cls]) return;
    s_boneMapped[cls] = true;
    memset(s_boneMap[cls], -1, 128);
    if (!playerHdr || !zombieHdr) return;

    const uint8_t* pH = reinterpret_cast<const uint8_t*>(playerHdr);
    const uint8_t* zH = reinterpret_cast<const uint8_t*>(zombieHdr);
    int pNB  = *reinterpret_cast<const int*>(pH + 0x9C);
    int pOff = *reinterpret_cast<const int*>(pH + 0xA0);
    int zNB  = *reinterpret_cast<const int*>(zH + 0x9C);
    int zOff = *reinterpret_cast<const int*>(zH + 0xA0);
    if (pNB <= 0 || pNB > 256 || zNB <= 0 || zNB > 256) return;

    const int kBS = 0xD8; // sizeof(mstudiobone_t)
    int matched = 0;
    for (int zi = 0; zi < zNB && zi < 128; ++zi) {
        const uint8_t* zb = zH + zOff + zi * kBS;
        const char*    zn = reinterpret_cast<const char*>(zb) + *reinterpret_cast<const int*>(zb);
        for (int pi = 0; pi < pNB && pi < 128; ++pi) {
            const uint8_t* pb = pH + pOff + pi * kBS;
            const char*    pn = reinterpret_cast<const char*>(pb) + *reinterpret_cast<const int*>(pb);
            if (strcmp(zn, pn) == 0) { s_boneMap[cls][zi] = (int8_t)pi; ++matched; break; }
        }
    }
    // Second pass: for still-unmatched zombie bones, walk their parent chain and
    // inherit the nearest matched ancestor's player bone index.
    // This fixes zombie-specific bones (e.g. hanging arm prop) which aren't in the
    // player studioHdr -- they'll now follow their nearest parent that IS shared.
    // mstudiobone_t: parent @ +0x04
    for (int zi = 0; zi < zNB && zi < 128; ++zi) {
        if (s_boneMap[cls][zi] >= 0) continue;
        int par = *reinterpret_cast<const int*>(zH + zOff + zi * kBS + 0x04);
        for (int d = 0; d < 16 && par >= 0 && par < zNB; ++d) {
            if (s_boneMap[cls][par] >= 0) {
                s_boneMap[cls][zi] = s_boneMap[cls][par];
                break;
            }
            par = *reinterpret_cast<const int*>(zH + zOff + par * kBS + 0x04);
        }
    }

    DebugLog("zombie boneMap cls=%d: playerBones=%d zombieBones=%d matched=%d", cls, pNB, zNB, matched);

    // Per-bone log for spy (cls==8) to diagnose any bone mismatch issues.
    if (cls == 8) {
        for (int zi = 0; zi < zNB && zi < 128; ++zi) {
            const uint8_t* zb = zH + zOff + zi * kBS;
            const char*    zn = reinterpret_cast<const char*>(zb) + *reinterpret_cast<const int*>(zb);
            int8_t pi = s_boneMap[cls][zi];
            if (pi >= 0) {
                const uint8_t* pb = pH + pOff + (int)pi * kBS;
                const char*    pn = reinterpret_cast<const char*>(pb) + *reinterpret_cast<const int*>(pb);
                DebugLog("  spy bone[%d]='%s' -> player[%d]='%s'", zi, zn, (int)pi, pn);
            } else {
                DebugLog("  spy bone[%d]='%s' -> UNMAPPED (will use bone 0)", zi, zn);
            }
        }
    }
}

// Returns studiohwdata_t* for zombie model class, loading via IMDLCache::GetHardwareData on demand.
// IMDLCache vtable layout (IAppSystem 5 slots + own methods):
//   slot 11 = GetHardwareData(MDLHandle_t) -- loads GPU/VTX data on first call.
// IVModelInfo::GetCacheHandle (slot 46) converts model_t* -> MDLHandle_t (uint16).
static void* GetZombieHWData(int cls, const void* pZombieMdl)
{
    if (s_zombieHWData[cls]) return s_zombieHWData[cls]; // cached

    // Lazy-init IMDLCache
    if (!g_mdlCache) {
        const char* names[] = {"MDLCache004", "MDLCache005", "MDLCache006", nullptr};
        const char* mods[]  = {"datacache.dll", "engine.dll", nullptr};
        for (int m = 0; mods[m] && !g_mdlCache; ++m)
            for (int n = 0; names[n] && !g_mdlCache; ++n)
                g_mdlCache = GetInterface(mods[m], names[n]);
        DebugLog("zombie: IMDLCache -> %p", g_mdlCache);
    }
    if (!g_mdlCache) return nullptr;

    // GetCacheHandle(model_t*) -> MDLHandle_t (uint16)
    using GetCacheHandle_t = unsigned short(__fastcall*)(void*, const void*);
    unsigned short handle = 0xFFFF;
    __try {
        handle = ((GetCacheHandle_t)(*(void***)g_modelInfo)[46])(g_modelInfo, pZombieMdl);
    } __except(EXCEPTION_EXECUTE_HANDLER) { handle = 0xFFFF; }
    if (handle == 0xFFFF) {
        DebugLog("zombie: GetCacheHandle cls=%d FAILED", cls);
        return nullptr;
    }

    // GetHardwareData(MDLHandle_t) loads studiohwdata_t (GPU buffers) on demand
    using GetHWData_t = void*(__fastcall*)(void*, unsigned short);
    void* hwData = nullptr;
    __try {
        hwData = ((GetHWData_t)(*(void***)g_mdlCache)[11])(g_mdlCache, handle);
    } __except(EXCEPTION_EXECUTE_HANDLER) { hwData = nullptr; }

    if (hwData) {
        s_zombieHWData[cls] = hwData;
        DebugLog("zombie: hwData cls=%d handle=0x%04X -> %p", cls, (unsigned)handle, hwData);
    } else {
        DebugLog("zombie: hwData cls=%d handle=0x%04X FAILED", cls, (unsigned)handle);
    }
    return hwData;
}

static const void* GetOrLoadZombieModel(int cls)
{
    if (cls < 1 || cls > 9 || !g_modelInfo) return nullptr;
    if (s_zombieModels[cls]) return s_zombieModels[cls];
    const char* path = k_zombieModelPaths[cls];
    if (!path) return nullptr;

    // Use RegisterDynamicModel (slot 52, bClientSide=true) which goes through
    // the full client model pipeline, ensuring studioHdr AND hardware/GPU data
    // (studioHWData) are properly initialized -- unlike FindOrLoadModel which
    // may only load MDL CPU data, causing DrawModelSetup to fail at GetHardwareData.
    // TF2 returns NEGATIVE indices for client-side models (e.g. -41, -43...).
    // We use INT_MIN as the "tried and failed (returned 0)" sentinel.
    if (s_zombieDynIdx[cls] == 0) {
        using RegisterDynamic_t = int(__fastcall*)(void*, const char*, bool);
        int reg = 0;
        __try {
            reg = ((RegisterDynamic_t)(*(void***)g_modelInfo)[52])(g_modelInfo, path, true);
        } __except(EXCEPTION_EXECUTE_HANDLER) { reg = 0; }
        s_zombieDynIdx[cls] = (reg != 0) ? reg : INT_MIN; // INT_MIN = failed
        DebugLog("zombie: RegisterDynamicModel cls=%d -> dynIdx=%d", cls, reg);
        if (reg == 0) return nullptr;
    }

    int dynIdx = s_zombieDynIdx[cls];
    if (dynIdx == INT_MIN) return nullptr; // previously failed

    // Dynamic models load asynchronously -- wait until ready before using
    using IsDynamicLoading_t = bool(__fastcall*)(void*, int);
    bool loading = false;
    __try {
        loading = ((IsDynamicLoading_t)(*(void***)g_modelInfo)[53])(g_modelInfo, dynIdx);
    } __except(EXCEPTION_EXECUTE_HANDLER) { loading = false; }
    if (loading) return nullptr; // not ready yet; will retry next frame

    // Model is ready: get the fully-initialized model_t pointer
    using GetModel_t = const void*(__fastcall*)(void*, int);
    const void* mdl = nullptr;
    __try {
        mdl = ((GetModel_t)(*(void***)g_modelInfo)[1])(g_modelInfo, dynIdx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (mdl) {
        s_zombieModels[cls] = mdl;
        DebugLog("zombie: model cls=%d ready dynIdx=%d ptr=%p", cls, dynIdx, mdl);
    } else {
        DebugLog("zombie: model cls=%d GetModel FAILED dynIdx=%d", cls, dynIdx);
    }
    return mdl;
}

static bool ShouldZombify(int entIdx)
{
    if (!g_zombify || entIdx < 1 || entIdx > 32) return false;
    int li = GetLocalPlayerIndex();
    if (entIdx == li) return g_zombifyMode >= 2;
    if (g_zombifyMode == 0) {
        int myTeam    = (li > 0) ? GetPlayerTeam(li) : 0;
        int theirTeam = GetPlayerTeam(entIdx);
        if (myTeam == 0 || theirTeam == 0) return false;
        return theirTeam != myTeam;
    }
    return GetPlayerTeam(entIdx) != 0;
}

void __fastcall h_DrawModelExecute(void* rcx, const void* pState, const void* pInfo, const void* pBones)
{
    if (g_eject || t_vmDepth > 0 || g_vmIsRendering) {
        if (s_vmGlowScaleBones && pBones) {
            int nb = GetGlowBoneCount_SEH(pInfo);
            if (nb > 0) {
                ScaleBonesForGlow(pBones, s_scaledBones, nb, 1.05f);
                o_DrawModelExecute(rcx, pState, pInfo, s_scaledBones);
            } else {
                o_DrawModelExecute(rcx, pState, pInfo, pBones);
            }
        } else {
            o_DrawModelExecute(rcx, pState, pInfo, pBones);
        }
        return;
    }

    // Every model draw of the frame comes through here. With no model feature on, skip the
    // model-name virtual call and the classification below entirely.
    const bool anyActive = g_enableVmWeaponChams || g_enableVmArmsChams || g_enablePlChams || g_enableVmWeaponGlow || g_enableVmArmsGlow;
    const bool wantVmAlpha = VmAlphaWanted();

    // Frame blending wants the viewmodel's BONE MATRICES, and this is the only place they
    // exist: the gun's animation is invisible to a depth reprojection, which can only ever see
    // what the camera did. See features/fbbones.h.
    const bool wantFbBones = g_fbEnable && g_fbViewmodel;

    if (!anyActive && !g_noHats && !g_zombify && !g_enableSkins && !wantVmAlpha && !wantFbBones) {
        o_DrawModelExecute(rcx, pState, pInfo, pBones);
        return;
    }

    // ---- Shared model info -- fetched once for no-hats and zombify ----
    const uint8_t* pInfoB  = pInfo ? reinterpret_cast<const uint8_t*>(pInfo) : nullptr;
    const void*    pMdl    = nullptr;
    const char*    mdlName = nullptr;
    if (pInfoB && g_modelInfo) {
        pMdl = *reinterpret_cast<const void* const*>(pInfoB + 0x20);
        if (reinterpret_cast<uintptr_t>(pMdl) >= 0x10000) {
            using GetName_t = const char*(__fastcall*)(void*, const void*);
            __try { mdlName = ((GetName_t)(*(void***)g_modelInfo)[3])(g_modelInfo, pMdl); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

// ---- No hats: skip cosmetic draw, restore bodygroups on player models ----
    const ModelFacts& mf = ModelFactsFor(mdlName);

    // Transparent viewmodel: arms/v_ models always, weapon models only at entity_index -1
    // (the in-hand attachment). Every plain visible draw below goes through VmAlphaDME.
    const bool vmAlpha = wantVmAlpha && pInfoB && mf.vmKind != 0
        && (mf.vmKind == 1 || *reinterpret_cast<const int*>(pInfoB + 0x44) < 0);
    if (vmAlpha && g_vmFlatAlpha) VmFlatAlpha_SetFor(pMdl, pState);

    // THE BONES, BEFORE THE DRAW: pBones is the array the renderer is about to use and the pose
    // it goes with is in pInfo, so taking both here is what keeps the gun's rigid ride out of
    // the streak -- the two can never come from different frames. Same viewmodel test the
    // transparent gun uses; kind 2 is the weapon, kind 1 the arms, and they get different
    // shares of the sixteen bone slots.
    if (wantFbBones && pInfoB && pBones && mf.vmKind != 0
        && (mf.vmKind == 1 || *reinterpret_cast<const int*>(pInfoB + 0x44) < 0))
    {
        FrameBlend_CollectBones(pState, pInfo, pBones, mf.vmKind == 2);
    }

    if (g_noHats && pMdl)
    {
        // Hide any model living in the cosmetic item directories
        if (mf.isHat) return;

        // Base model: ModelRenderInfo_t::body (+0x4C) is what this call draws with, so the
        // write is exact per draw and game state is untouched (skin +0x48, body +0x4C).
        if (mf.isPlayerBase)
        {
            const int entIdx = *reinterpret_cast<const int*>(pInfoB + 0x44);
            int* pBody = reinterpret_cast<int*>(const_cast<uint8_t*>(pInfoB) + 0x4C);
            *pBody = CleanBody(BodyOwnerIndex(entIdx), mf.playerClass, *pBody);
        }
    }

// ---- Zombify: zombie skin + cursed voodoo souls cosmetic ----
    if (g_zombify && pMdl)
    {
        if (pMdl)  // inner scope preserves original indentation
        {
            // Viewmodel arms live under models/player/ too, so they pass the
            // shared player-base test and have to be excluded here.
            bool isPlayerBase = mf.isPlayerBase && !mf.vmArms;

            // Suppress all wearable cosmetics on zombified players so zombie skin
            // shows cleanly -- no hat/glasses floating over the zombie model.
            // pInfo+0x44 for a wearable entity is the WEARABLE'S OWN entity index
            // (33+), NOT the owning player's index. Resolve the owner via m_hNetworkMoveParent
            // Use IsHatModel so that functional backpack items (Razorback, Cozy Camper,
            // Darwin's Danger Shield) are spared -- they're in kExceptions and should
            // remain visible on zombified players.
            if (!isPlayerBase && mf.isHat) {
                int cosEntIdx = *reinterpret_cast<const int*>(pInfoB + 0x44);
                int ownerIdx  = 0;
                if (cosEntIdx > 32 && g_offMoveParent > 0) {
                    void* pCos = GetClientEntity(cosEntIdx);
                    if (pCos) {
                        __try {
                            unsigned int h = *reinterpret_cast<unsigned int*>(
                                reinterpret_cast<uintptr_t>(pCos) + g_offMoveParent);
                            ownerIdx = (int)(h & 0x3FFF);
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                } else if (cosEntIdx >= 1 && cosEntIdx <= 32) {
                    ownerIdx = cosEntIdx; // fallback: entity_index was the owner
                }
                if (ownerIdx >= 1 && ownerIdx <= 32 && ShouldZombify(ownerIdx))
                    return;
            }

            if (isPlayerBase)
            {
                int entIdx = *reinterpret_cast<const int*>(pInfoB + 0x44);

                // Resolve class/team and zombify decision for both live players and
                // ragdolls.  Ragdolls have entIdx outside [1,32]; ShouldZombify rejects
                // those, so we handle them separately using the model name for class and
                // reading m_iTeamNum (from DT_BaseEntity -- same offset for all entities).
                int  cls       = 0;
                int  team      = 0;
                bool isRagdoll = (entIdx < 1 || entIdx > 32);

                if (isRagdoll) {
                    // CTFRagdoll sends m_iTeam and m_iClass as dedicated fields
                    // (NOT m_iTeamNum / m_iClass from DT_TFPlayer -- different offsets).
                    // Network table "DT_TFRagdoll", resolved in ResolveNetvars.
                    void* pRag = (entIdx > 0) ? GetClientEntity(entIdx) : nullptr;
                    if (pRag) {
                        if (g_offRagdollTeam > 0)
                            __try { team = *reinterpret_cast<int*>(
                                reinterpret_cast<uintptr_t>(pRag) + g_offRagdollTeam);
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        if (g_offRagdollClass > 0)
                            __try { cls = *reinterpret_cast<int*>(
                                reinterpret_cast<uintptr_t>(pRag) + g_offRagdollClass);
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                    // Fallback: parse class from model path if netvar unavailable
                    if (cls == 0) cls = GetClassFromPlayerModelName(mdlName);
                }

                bool doZombify = ShouldZombify(entIdx);
                // Don't zombify disguised spies -- the spy zombie mask would show the spy
                // face instead of the disguise character's face.
                if (doZombify && !isRagdoll) {
                    int modelCls  = GetClassFromPlayerModelName(mdlName);
                    int entityCls = GetPlayerClass(entIdx);
                    if (modelCls != 0 && entityCls != 0 && modelCls != entityCls)
                        doZombify = false;
                    // Also catch spy-as-spy disguise: check TF_COND_DISGUISING|TF_COND_DISGUISED
                    if (doZombify && entityCls == 8 && g_offPlayerCond > 0) {
                        void* pSpyEnt = GetClientEntity(entIdx);
                        if (pSpyEnt) {
                            int cond = 0;
                            __try { cond = *reinterpret_cast<int*>(
                                reinterpret_cast<uintptr_t>(pSpyEnt) + g_offPlayerCond); }
                            __except(EXCEPTION_EXECUTE_HANDLER) {}
                            if (cond & ((1<<2)|(1<<3))) // TF_COND_DISGUISING | TF_COND_DISGUISED
                                doZombify = false;
                        }
                    }
                }
                if (!doZombify && isRagdoll && cls > 0 && (team == 2 || team == 3)) {
                    if (g_zombifyMode >= 1) {
                        doZombify = true;
                    } else {
                        int li     = GetLocalPlayerIndex();
                        int myTeam = (li >= 1 && li <= 32) ? GetPlayerTeam(li) : 0;
                        doZombify  = (myTeam != 0 && myTeam != team);
                    }
                }

                if (doZombify)
                {
                    if (cls  == 0) cls  = GetPlayerClass(entIdx);
                    if (team == 0) team = GetPlayerTeam(entIdx);
                    // Preserve the original skin's special-state offset (->ber = +2, etc.)
                    // so that ->bered players show the ->ber-zombie skin (skin 6/7) instead
                    // of the normal zombie skin (skin 4/5).
                    // Heavy has many paint skins (0-21) -- use teamBaseSkin directly for it.
                    int origSkin = *reinterpret_cast<const int*>(pInfoB + 0x48);
                    // Spy disguise team fix: when a spy is disguised as the other team, the
                    // rendered model's skin implies the disguise team (even skin = RED, odd = BLU).
                    // Use the skin-implied team so the correct zombie texture is applied.
                    if (cls == 8 && origSkin >= 0 && origSkin <= 3) {
                        int skinTeam = ((origSkin & 1) == 0) ? 2 : 3;
                        if (skinTeam != team) team = skinTeam;
                    }
                    int teamBaseSkin = (team == 3) ? 1 : 0;
                    int zombieBaseOffset = (cls == 8) ? 22 : 4;
                    int zombieSkin = (cls != 8 && origSkin >= 0 && origSkin <= 3)
                        ? (zombieBaseOffset + origSkin)
                        : (zombieBaseOffset + teamBaseSkin);

                    uint8_t pInfoCopy[128];
                    memcpy(pInfoCopy, pInfo, 128);
                    *reinterpret_cast<int*>(pInfoCopy + 0x48) = zombieSkin;
                    // Cosmetics are suppressed on a zombie, so the groups they hid come back
                    // exactly like No hats does it (a no-op when No hats already cleaned it).
                    *reinterpret_cast<int*>(pInfoCopy + 0x4C) = CleanBody(BodyOwnerIndex(entIdx),
                        GetClassFromPlayerModelName(mdlName), *reinterpret_cast<const int*>(pInfoB + 0x4C));

                    // Draw base model with zombie skin (+ player chams if active)
                    if (g_enablePlChams && g_plChamCount > 0 && PlDetectPlayer(mf, pInfoCopy)) {
                        ++t_vmDepth;
                        for (int i = 0; i < g_plChamCount; ++i) {
                            float r2=g_plChamLayers[i].color[0], g2=g_plChamLayers[i].color[1];
                            float b2=g_plChamLayers[i].color[2], a2=g_plChamLayers[i].color[3];
                            if (g_plChamLayers[i].useSheen) { float s[3]; GetAnimatedSheenColor(s); r2=s[0]; g2=s[1]; b2=s[2]; }
                            PlChamDraw_SEH(rcx, pState, pInfoCopy, pBones, PlGetMat(g_plChamLayers[i].matType), r2, g2, b2, a2);
                        }
                        --t_vmDepth;
                    } else {
                        ++t_vmDepth;
                        o_DrawModelExecute(rcx, pState, pInfoCopy, pBones);
                        --t_vmDepth;
                    }

                    // Draw zombie cosmetic model.
                    // GetZombieHWData pre-loads GPU buffers via IMDLCache::GetHardwareData.
                    // DrawModelSetup may still fail if the engine reads hwData from the
                    // model_t directly (a separate path from MDLCache).  Try DrawModelSetup
                    // first (slot 18, pCustomBones=nullptr so SetupBones runs fresh and
                    // returns bone matrices indexed for the zombie studioHdr).  If it
                    // fails, fall back to bypassing DrawModelSetup entirely -- manually
                    // copy the player's DrawModelState_t and replace only the model
                    // fields, then call o_DrawModelExecute directly.
                    //
                    // Team color: TF2 cosmetics use skin 0=RED, 1=BLU (unlike the player
                    // base model which always has skin=0 and uses a material proxy).
                    const void* pZombieMdl = GetOrLoadZombieModel(cls);
                    if (pZombieMdl) {
                        void* zombieHWData = GetZombieHWData(cls, pZombieMdl);
                        if (zombieHWData) {
                            using GetStudio_t = void*(__fastcall*)(void*, const void*);
                            void* zombieStudioHdr = nullptr;
                            __try { zombieStudioHdr = ((GetStudio_t)(*(void***)g_modelInfo)[28])(g_modelInfo, pZombieMdl); }
                            __except(EXCEPTION_EXECUTE_HANDLER) {}

                            // Zombie cosmetics only have skin 0=RED, 1=BLU -- no ->ber variants.
                            // Always use the team base skin regardless of ->ber state; the ->ber
                            // glow on the base model is enough visual feedback.
                            int cosmeticSkin = teamBaseSkin;
                            memcpy(pInfoCopy, pInfo, 128);
                            *reinterpret_cast<uintptr_t*>(pInfoCopy + 0x20) = reinterpret_cast<uintptr_t>(pZombieMdl);
                            *reinterpret_cast<int*>(pInfoCopy + 0x48) = cosmeticSkin;
                            *reinterpret_cast<int*>(pInfoCopy + 0x4C) = 0;            // body=0: zombie cosmetics don't share player bodygroups
                            *reinterpret_cast<uint16_t*>(pInfoCopy + 0x54) = 0xFFFF;  // MODEL_INSTANCE_INVALID

                            uint8_t zombieState[64] = {};
                            void* freshBones = nullptr;
                            bool setupOk = false;
                            using DrawModelSetup_t = bool(__fastcall*)(void*, void*, void*, const void*, void**);
                            ++t_vmDepth;
                            __try {
                                setupOk = ((DrawModelSetup_t)(*(void***)rcx)[18])(
                                    rcx, pInfoCopy, zombieState, nullptr, &freshBones);
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            --t_vmDepth;

                            static bool s_dmsLog[10] = {};
                            if (!s_dmsLog[cls]) {
                                s_dmsLog[cls] = true;
                                DebugLog("zombie: DrawModelSetup cls=%d ok=%d freshBones=%p", cls, (int)setupOk, freshBones);
                            }

                            if (setupOk && freshBones) {
                                // DrawModelSetup succeeded: proper bone matrices for zombie skeleton
                                ++t_vmDepth;
                                o_DrawModelExecute(rcx, zombieState, pInfoCopy, freshBones);
                                --t_vmDepth;
                            } else if (zombieStudioHdr) {
                                // DrawModelSetup failed -- bypass it.
                                // Re-init pInfoCopy: DrawModelSetup takes pInfo by mutable ref and
                                // may have reset pInfoCopy+0x20 (pModel) back to the player's model.
                                memcpy(pInfoCopy, pInfo, 128);
                                *reinterpret_cast<uintptr_t*>(pInfoCopy + 0x20) = reinterpret_cast<uintptr_t>(pZombieMdl);
                                *reinterpret_cast<int*>(pInfoCopy + 0x48) = cosmeticSkin;
                                *reinterpret_cast<uint16_t*>(pInfoCopy + 0x54) = 0xFFFF;

                                memcpy(zombieState, pState, sizeof(zombieState));
                                *reinterpret_cast<void**>(zombieState + 0x00) = zombieStudioHdr;
                                *reinterpret_cast<void**>(zombieState + 0x08) = zombieHWData;
                                *reinterpret_cast<void**>(zombieState + 0x20) = nullptr; // m_decals

                                // Remap player bone matrices into zombie bone index ordering.
                                // Zombie model VTX vertices reference bone indices from the zombie
                                // studioHdr, which differ if zombie-specific bones are prepended
                                // before $includemodel bones.  Match by name; cache per class.
                                // studiohdr_t: numbones@+0x9C, boneindex@+0xA0
                                // mstudiobone_t: sznameindex@+0x00, sizeof=0xD8
                                const void* playerHdr = *reinterpret_cast<const void* const*>(
                                    reinterpret_cast<const uint8_t*>(pState));
                                BuildZombieBoneMap(cls, playerHdr, zombieStudioHdr);

                                if (pBones) {
                                    static alignas(16) float s_remappedBones[128][3][4];
                                    for (int zi = 0; zi < 128; ++zi) {
                                        int pi = (s_boneMap[cls][zi] >= 0) ? (int)s_boneMap[cls][zi] : 0;
                                        memcpy(s_remappedBones[zi],
                                               reinterpret_cast<const uint8_t*>(pBones) + pi * 48, 48);
                                    }
                                    ++t_vmDepth;
                                    o_DrawModelExecute(rcx, zombieState, pInfoCopy, s_remappedBones);
                                    --t_vmDepth;
                                } else {
                                    ++t_vmDepth;
                                    o_DrawModelExecute(rcx, zombieState, pInfoCopy, pBones);
                                    --t_vmDepth;
                                }
                            }
                        }
                    }
                    return;
                }
            }
        }
    }

    // ---- Reskin visual fallback: golden weapon material/skin override ----
    // TF2's item system may not apply material_override or style skins after a
    // client-side def-index rewrite. Detect golden weapons here and force the
    // correct material (golden wrench) or skin (golden frying pan).
    if (g_enableSkins && pInfoB && mdlName && g_fsn_offItemDefIndex > 0)
    {
        int reskinDef = 0;
        const bool isWrenchMdl = mf.goldKind == 1;
        const bool isPanMdl    = mf.goldKind == 2;
        if (isWrenchMdl || isPanMdl) {
            int entIdx = *reinterpret_cast<const int*>(pInfoB + 0x44);
            if (entIdx > 0) {
                void* pW = GetClientEntity(entIdx);
                if (pW) {
                    __try { reskinDef = FSN_ReadItemDefIndex(pW, g_fsn_offItemDefIndex); }
                    __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
        }
        if (isWrenchMdl && reskinDef == 169) {
            // Golden Wrench -- apply gold_wrench.vmt material override. Found again per map:
            // nothing holds a reference, so a level change may free it.
            static void*    s_goldWrenchMat   = nullptr;
            static uint32_t s_goldWrenchEpoch = 0;
            const uint32_t  epoch = g_matEpoch.load(std::memory_order_relaxed);
            if (epoch != s_goldWrenchEpoch) { s_goldWrenchEpoch = epoch; s_goldWrenchMat = nullptr; }
            if (!s_goldWrenchMat && g_matSystem)
                s_goldWrenchMat = MatSysFindMaterial(g_matSystem, "models/weapons/c_items/gold_wrench");
            if (s_goldWrenchMat && o_ForcedMatOverride) {
                o_ForcedMatOverride(rcx, s_goldWrenchMat, 0);
                VmAlphaDME(vmAlpha, rcx, pState, pInfo, pBones);
                o_ForcedMatOverride(rcx, nullptr, 0);
                return;
            }
        }
        if (isPanMdl && reskinDef == 1071) {
            // Golden Frying Pan -- force skin 2 (RED) / 3 (BLU) if not already set
            int curSkin = *reinterpret_cast<const int*>(pInfoB + 0x48);
            if (curSkin < 2) {
                uint8_t infoCopy[128];
                memcpy(infoCopy, pInfo, 128);
                *reinterpret_cast<int*>(infoCopy + 0x48) = (curSkin == 1) ? 3 : 2;
                VmAlphaDME(vmAlpha, rcx, pState, infoCopy, pBones);
                return;
            }
        }
    }

    if (!anyActive || !g_inGame.load()) {
        VmAlphaDME(vmAlpha, rcx, pState, pInfo, pBones);
        return;
    }

    ++g_miscHookDepth;

    // ---- Player chams ------------------------------------------
    // Held weapons / carried items are separate entities, so they need their own
    // test. Runs after PlDetectPlayer so the cheap path check wins on player models.
    if (g_enablePlChams && g_plChamCount > 0
        && (PlDetectPlayer(mf, pInfoB)
            || (g_plChamsAttachments && PlDetectAttachment(pInfo))))
    {
        ++t_vmDepth;
        for (int i = 0; i < g_plChamCount; ++i) {
            float r2=g_plChamLayers[i].color[0], g2=g_plChamLayers[i].color[1];
            float b2=g_plChamLayers[i].color[2], a2=g_plChamLayers[i].color[3];
            if (g_plChamLayers[i].useSheen) { float s[3]; GetAnimatedSheenColor(s); r2=s[0]; g2=s[1]; b2=s[2]; }
            PlChamDraw_SEH(rcx, pState, pInfo, pBones, PlGetMat(g_plChamLayers[i].matType), r2, g2, b2, a2);
        }
        --t_vmDepth;
        --g_miscHookDepth;
        return;
    }

    bool isWeapon = false, isArms = false;
    if (!pInfoB || !VmDetectType(mf, pInfoB, isWeapon, isArms)) {
        VmAlphaDME(vmAlpha, rcx, pState, pInfo, pBones);
        --g_miscHookDepth;
        return;
    }

    bool           doChams  = isWeapon ? g_enableVmWeaponChams  : g_enableVmArmsChams;
    VmChamLayer_t* layers   = isWeapon ? g_vmWeaponChamLayers   : g_vmArmsChamLayers;
    int            nLayers  = isWeapon ? g_vmWeaponChamCount    : g_vmArmsChamCount;

    ++t_vmDepth;

    // Glow before visible draw (stencil write + RT composite).
    if (g_enableVmArmsGlow && isArms && !g_vmIsRendering) {
        float col[4] = {g_vmArmsGlowColor[0],g_vmArmsGlowColor[1],g_vmArmsGlowColor[2],g_vmArmsGlowColor[3]};
        if (g_vmArmsGlowUseSheen) { float s[3]; GetAnimatedSheenColor(s); col[0]=s[0];col[1]=s[1];col[2]=s[2]; }
        GlowVM_DrawArms(rcx, pState, pInfo, pBones, col, g_vmArmsGlowSigma, g_vmArmsGlowStencil);
    }

    // Visible draw (chams or normal).
    if (doChams && nLayers > 0) {
        for (int i = 0; i < nLayers; ++i) {
            float r2=layers[i].color[0], g2=layers[i].color[1];
            float b2=layers[i].color[2], a2=layers[i].color[3];
            if (layers[i].useSheen) { float s[3]; GetAnimatedSheenColor(s); r2=s[0]; g2=s[1]; b2=s[2]; }
            VmChamDraw_SEH(rcx, pState, pInfo, pBones, VmGetMat(layers[i].matType), r2, g2, b2, a2 * VmAlphaMul(), layers[i].ignorez);
        }
    } else {
        VmAlphaDME(vmAlpha, rcx, pState, pInfo, pBones);
    }

    // Bone-scaled fallback halo only when RT glow is not ready.
    if (g_enableVmArmsGlow && isArms && !g_glowRTsReady)
        VmGlowDrawDME_SEH(rcx, pState, pInfo, pBones);

    --t_vmDepth;
    --g_miscHookDepth;
}


// ---- Weapon viewmodel chams -- CBaseAnimating hook chain ----
// Weapon draw path: CEconEntity::DrawOverriddenViewmodel
//   -> CBaseAnimating::DrawModel (detect caller)
//   -> CBaseAnimating::InternalDrawModel (apply chams)

using CBaseAnimatingDrawModel_t    = int(__fastcall*)(void*, int);
CBaseAnimatingDrawModel_t    o_CBaseAnimatingDrawModel    = nullptr;
CBaseAnimatingInternalDraw_t o_CBaseAnimatingInternalDraw = nullptr;
uintptr_t g_addrDrawOverriddenVMCall = 0;
// Set during DrawOverriddenViewmodel so InternalDrawModel knows it's drawing the local weapon.
static bool s_vmDrawingWeapon = false;

// Apply one material+colour layer via InternalDrawModel (SEH wrapper).
__declspec(noinline) static int VmWeaponLayerDraw_SEH(
    void* rcx_ent, void* pOverride, float r, float g, float b, float a, bool ignorez = false)
{
    int ret = 0;
    VmColorState_t saved = VmSaveColor();
    g_vmIsRendering = true;
    __try {
        if (pOverride) VmForceMat(g_modelRender, pOverride);
        VmSetColor(r, g, b, a);
        if (ignorez) VmCullMode(1 - VmGameCull()); // the opposite of the game's mode = show inside faces
        if (o_CBaseAnimatingInternalDraw)
            ret = o_CBaseAnimatingInternalDraw(rcx_ent, 1); // flags=1 -> STUDIO_RENDER
        if (ignorez) VmCullMode(VmGameCull());     // restore the game's mode (CW on a flipped viewmodel)
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (ignorez) VmCullMode(VmGameCull());
    }
    g_vmIsRendering = false;
    if (pOverride) VmForceMat(g_modelRender, nullptr);
    VmRestoreColor(saved);
    return ret;
}

// Hook: CBaseAnimating::DrawModel
// Sets s_vmDrawingWeapon when called from CEconEntity::DrawOverriddenViewmodel.
int __fastcall h_CBaseAnimatingDrawModel(void* rcx, int flags)
{
    if (!o_CBaseAnimatingDrawModel) return 0;
    if (g_eject) return o_CBaseAnimatingDrawModel(rcx, flags);

    const uintptr_t retAddr = uintptr_t(_ReturnAddress());
    if (!g_addrDrawOverriddenVMCall || retAddr != g_addrDrawOverriddenVMCall)
        return o_CBaseAnimatingDrawModel(rcx, flags);

    s_vmDrawingWeapon = true;
    ++g_miscHookDepth;
    const int r = o_CBaseAnimatingDrawModel(rcx, flags);
    --g_miscHookDepth;
    s_vmDrawingWeapon = false;
    return r;
}

// ---- the game's cull mode around a viewmodel draw ----
// C_BaseViewModel::InternalDrawModel (c_baseviewmodel.cpp:371) and C_ViewmodelAttachmentModel::InternalDrawModel
// (econ_entity.cpp:856) both set CullMode CW for a flipped model, call C_BaseAnimating::InternalDrawModel
// (h_CBaseAnimatingInternalDraw) directly, then set CCW. The return address says which one called; the flip
// test is the one it just ran (client disasm, tools/check_vtables.py):
//   vm   `call [rax + 8*slot]` = rcx->ShouldFlipViewModel(); test al; je; CullMode(1); call InternalDrawModel
//   att  `call TeamFortress_ShouldFlipClientViewModel; cmp al, [rdi + off]` (m_bAlwaysFlip); je; ...
static constexpr const char* k_vmCullSigVm  = "FF 90 ? ? 00 00 84 C0 74 11 48 8B 03 BA 01 00 00 00 48 8B CB FF 90 40 01 00 00 8B D6 48 8B CF E8";
static constexpr const char* k_vmCullSigAtt = "E8 ? ? ? ? 3A 87 ? ? 00 00 74 11 48 8B 03 BA 01 00 00 00 48 8B CB FF 90 40 01 00 00 8B D6 48 8B CF E8";
static uintptr_t s_cullRetVm = 0, s_cullRetAtt = 0;
static int       s_cullVmSlot = 0, s_cullAttOff = 0;
using TfShouldFlip_t = bool(__fastcall*)();
static TfShouldFlip_t s_cullTfShouldFlip = nullptr;
static thread_local int t_gameCull = -1;   // 1 CW, 0 CCW, -1 not inside a viewmodel draw

int VmGameCull() { return t_gameCull == 1 ? 1 : 0; }

// sig hit -> the call into InternalDrawModel at `callAt`; refuse anything that does not land on the hooked function.
static uintptr_t VmCullRet(uint8_t* hit, int callAt, uint8_t* fnInternalDraw)
{
    if (!hit) return 0;
    const uint8_t* call = hit + callAt;
    const uintptr_t target = uintptr_t(call + 5) + int32_t(*reinterpret_cast<const int32_t*>(call + 1));
    return target == uintptr_t(fnInternalDraw) ? uintptr_t(call + 5) : 0;
}

void VmCull_Resolve(HMODULE client, uint8_t* fnInternalDraw)
{
    if (!client || !fnInternalDraw) return;
    if (uint8_t* h = PatternScan(client, k_vmCullSigVm)) {
        s_cullRetVm  = VmCullRet(h, 0x20, fnInternalDraw);
        s_cullVmSlot = *reinterpret_cast<const int32_t*>(h + 2) / 8;
    }
    if (uint8_t* h = PatternScan(client, k_vmCullSigAtt)) {
        s_cullRetAtt        = VmCullRet(h, 0x23, fnInternalDraw);
        s_cullTfShouldFlip  = reinterpret_cast<TfShouldFlip_t>(uintptr_t(h + 5) + int32_t(*reinterpret_cast<const int32_t*>(h + 1)));
        s_cullAttOff        = *reinterpret_cast<const int32_t*>(h + 7);
    }
    DebugLog("vmchams: cull windows viewmodel=%p (ShouldFlipViewModel slot %d) attachment=%p (m_bAlwaysFlip +0x%X)%s",
             (void*)s_cullRetVm, s_cullVmSlot, (void*)s_cullRetAtt, s_cullAttOff,
             (s_cullRetVm && s_cullRetAtt) ? "" : " -- MISSING: glow/invert on a flipped viewmodel assume CCW");
}

__declspec(noinline) static int VmWindowCull(void* rcx, uintptr_t ret)
{
    __try {
        if (s_cullRetVm && ret == s_cullRetVm) {
            using F = bool(__fastcall*)(void*);
            return ((F)(*(void***)rcx)[s_cullVmSlot])(rcx) ? 1 : 0;
        }
        if (s_cullRetAtt && ret == s_cullRetAtt)
            return uint8_t(s_cullTfShouldFlip()) != *reinterpret_cast<const uint8_t*>(uintptr_t(rcx) + s_cullAttOff) ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static int VmInternalDrawBody(void* rcx, int flags);

int __fastcall h_CBaseAnimatingInternalDraw(void* rcx, int flags)
{
    if (!o_CBaseAnimatingInternalDraw) return 0;
    const uintptr_t ret = uintptr_t(_ReturnAddress());
    const int prev = t_gameCull;
    const int cull = (ret == s_cullRetVm || ret == s_cullRetAtt) ? VmWindowCull(rcx, ret) : -1;
    t_gameCull = cull;
    static int s_logged[2] = { -2, -2 };   // per window, so a vm and an attachment that differ do not alternate
    if (cull >= 0) {
        int& last = s_logged[ret == s_cullRetAtt ? 1 : 0];
        if (last != cull) {
            last = cull;
            DebugLog("vmchams: %s drawn with CullMode %s", ret == s_cullRetAtt ? "viewmodel attachment" : "viewmodel",
                     cull ? "CW (flipped)" : "CCW");
        }
    }
    const int r = VmInternalDrawBody(rcx, flags);
    t_gameCull = prev;
    return r;
}

static int VmInternalDrawBody(void* rcx, int flags)
{
    if (g_eject || !s_vmDrawingWeapon || !(flags & 1))
        return o_CBaseAnimatingInternalDraw(rcx, flags);

    const bool doChams = g_enableVmWeaponChams;
    const bool doGlow  = g_enableVmWeaponGlow;

    if (!IsInGame()) {
        if (doChams && g_vmWeaponChamCount > 0) {
            for (int i = 0; i < g_vmWeaponChamCount; ++i) {
                float r2=g_vmWeaponChamLayers[i].color[0], g2=g_vmWeaponChamLayers[i].color[1];
                float b2=g_vmWeaponChamLayers[i].color[2], a2=g_vmWeaponChamLayers[i].color[3];
                if (g_vmWeaponChamLayers[i].useSheen) { float s[3]; GetAnimatedSheenColor(s); r2=s[0]; g2=s[1]; b2=s[2]; }
                return VmWeaponLayerDraw_SEH(rcx, VmGetMat(g_vmWeaponChamLayers[i].matType), r2, g2, b2, a2 * VmAlphaMul(), g_vmWeaponChamLayers[i].ignorez);
            }
        }
        return o_CBaseAnimatingInternalDraw(rcx, flags);
    }

    if (!doChams && !doGlow)
        return o_CBaseAnimatingInternalDraw(rcx, flags);

    ++g_miscHookDepth;
    int ret = 0;

    // Glow pass 1+2 BEFORE visible draw: stencil writes in pass 1, RT capture in pass 2,
    // then composite (stencil=0 test) produces halo around the silhouette.
    if (doGlow && IsInGame() && !g_vmIsRendering) {
        float col[4] = {g_vmWeaponGlowColor[0],g_vmWeaponGlowColor[1],g_vmWeaponGlowColor[2],g_vmWeaponGlowColor[3]};
        if (g_vmWeaponGlowUseSheen) { float s[3]; GetAnimatedSheenColor(s); col[0]=s[0];col[1]=s[1];col[2]=s[2]; }
        GlowVM_DrawWeapon(rcx, flags, col, g_vmWeaponGlowSigma, g_vmWeaponGlowStencil);
    }

    if (doChams && g_vmWeaponChamCount > 0) {
        for (int i = 0; i < g_vmWeaponChamCount; ++i) {
            float r2=g_vmWeaponChamLayers[i].color[0], g2=g_vmWeaponChamLayers[i].color[1];
            float b2=g_vmWeaponChamLayers[i].color[2], a2=g_vmWeaponChamLayers[i].color[3];
            if (g_vmWeaponChamLayers[i].useSheen) { float s[3]; GetAnimatedSheenColor(s); r2=s[0]; g2=s[1]; b2=s[2]; }
            ret = VmWeaponLayerDraw_SEH(rcx, VmGetMat(g_vmWeaponChamLayers[i].matType), r2, g2, b2, a2 * VmAlphaMul(), g_vmWeaponChamLayers[i].ignorez);
        }
    } else {
        ret = o_CBaseAnimatingInternalDraw(rcx, flags);
    }

    // Bone-scaled fallback halo only when RT glow is not ready.
    if (doGlow && !g_glowRTsReady) VmWeaponGlowDraw_SEH(rcx);  // bone-scale fallback

    --g_miscHookDepth;
    return ret;
}



// ---- weather.cpp -------------------------------------------
std::vector<TrackedWeather_t> s_trackedWeather[8];

static void* GetClientNetworkable(int idx)
{
    if (!g_entityList) return nullptr;
    void* result = nullptr;
    __try {
        void** vtbl = *reinterpret_cast<void***>(g_entityList);
        using Fn = void*(__thiscall*)(void*, int);
        result = ((Fn)vtbl[0])(g_entityList, idx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

static void Weather_FindOffsets()
{
    if (g_offPrecipType >= 0 && g_offMins >= 0) return;

    if (g_offPrecipType < 0) {
        RecvTable* t = FindNetworkTable("DT_Precipitation");
        if (!t && g_precipClass)
            t = reinterpret_cast<ClientClass*>(g_precipClass)->m_pRecvTable;
        if (t) {
            g_offPrecipType = FindPropOffset(t, "m_nPrecipType");
            DebugLog("weather: m_nPrecipType offset=%d", g_offPrecipType);
        }
    }
    if (g_offMins < 0 || g_offMaxs < 0) {
        RecvTable* t = FindNetworkTable("DT_BaseEntity");
        if (t) {
            g_offMins = FindPropOffset(t, "m_vecMins");
            g_offMaxs = FindPropOffset(t, "m_vecMaxs");
            DebugLog("weather: m_vecMins=%d m_vecMaxs=%d", g_offMins, g_offMaxs);
        }
    }
}

// IClientNetworkable vtable update order (matches SEOwnedDE):
//   set m_nPrecipType -> PreDataUpdate -> OnPreDataChanged -> set mins/maxs ->
//   OnDataChanged -> PostDataUpdate
__declspec(noinline) static void Weather_UpdateChain(void* pNet, void* pEnt,
    int precipType, bool bSetBounds)
{
    if (!pNet || !pEnt) return;
    __try {
        void** vt = *reinterpret_cast<void***>(pNet);
        using Fn = void(__fastcall*)(void*, int);

        if (g_offPrecipType > 0)
            *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + g_offPrecipType)
                = precipType;

        ((Fn)vt[6])(pNet, 0); // PreDataUpdate
        ((Fn)vt[4])(pNet, 0); // OnPreDataChanged

        if (g_offMins > 0 && g_offMaxs > 0) {
            float* mins = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pEnt) + g_offMins);
            float* maxs = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pEnt) + g_offMaxs);
            if (bSetBounds) {
                // 8192-unit box -- covers the entire playable area of any TF2 map
                mins[0] = mins[1] = mins[2] = -8192.f;
                maxs[0] = maxs[1] = maxs[2] =  8192.f;
            } else {
                memset(mins, 0, 12); memset(maxs, 0, 12);
            }
        }

        ((Fn)vt[5])(pNet, 0); // OnDataChanged
        ((Fn)vt[7])(pNet, 0); // PostDataUpdate
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("weather: SEH in update chain");
        g_precipEntity = g_precipNetworkable = nullptr;
        s_lastAppliedMode = -99;
    }
}

// ---- CPrecipitation sprite helpers (modes 1 & 2) -----------
static void Precip_ApplyConvars()
{
    if (!g_cvar) return;
    // Cache ConVar pointers -- FindVarWalk does a full linked-list walk each call.
    // Reset cached pointers if g_cvar changed (e.g. after a re-inject).
    static ICvar* s_lastCvar = nullptr;
    static ConVar* cv_alpha   = nullptr;
    static ConVar* cv_speed   = nullptr;
    static ConVar* cv_width   = nullptr;
    static ConVar* cv_length  = nullptr;
    static ConVar* cv_radius  = nullptr;
    static ConVar* cv_sidevel = nullptr;
    if (s_lastCvar != g_cvar) {
        // g_cvar changed -- clear cache so we re-resolve below
        s_lastCvar = g_cvar;
        cv_alpha = cv_speed = cv_width = cv_length = cv_radius = cv_sidevel = nullptr;
    }
    if (!cv_alpha)   cv_alpha   = g_cvar->FindVarWalk("r_rainalpha");
    if (!cv_speed)   cv_speed   = g_cvar->FindVarWalk("r_rainspeed");
    if (!cv_width)   cv_width   = g_cvar->FindVarWalk("r_rainwidth");
    if (!cv_length)  cv_length  = g_cvar->FindVarWalk("r_rainlength");
    if (!cv_radius)  cv_radius  = g_cvar->FindVarWalk("r_RainRadius");
    if (!cv_sidevel) cv_sidevel = g_cvar->FindVarWalk("r_RainSideVel");

    if (g_rainMode == 1) {
        if (cv_alpha)   cv_alpha->ForceFloat(g_rainAlpha);
        if (cv_speed)   cv_speed->ForceFloat(g_rainSpeed);
        if (cv_width)   cv_width->ForceFloat(g_rainWidth);
        if (cv_length)  cv_length->ForceFloat(g_rainLength);
        if (cv_radius)  cv_radius->ForceFloat(g_rainRadius);
        if (cv_sidevel) cv_sidevel->ForceFloat(g_rainSideVel);
    } else if (g_rainMode == 2) {
        if (cv_alpha)   cv_alpha->ForceFloat(g_snowAlpha);
        if (cv_speed)   cv_speed->ForceFloat(g_snowSpeed);
        if (cv_width)   cv_width->ForceFloat(g_snowWidth);
        if (cv_length)  cv_length->ForceFloat(g_snowLength);
        if (cv_radius)  cv_radius->ForceFloat(g_snowRadius * std::max(0.1f, g_snowDensity));
        if (cv_sidevel) cv_sidevel->ForceFloat(g_snowSideVel);
    }
}

void Precip_TearDown(int kSlot)
{
    // Zero the bounds and type so the sprite entity stops rendering.
    void* pEnt = GetClientEntity(kSlot);
    void* pNet = GetClientNetworkable(kSlot);
    if (pEnt && pNet) Weather_UpdateChain(pNet, pEnt, 0, false);
    g_precipEntity      = nullptr;
    g_precipNetworkable = nullptr;
}

// ---- Snow sprite -- FindMaterial hook + static handle reset ----
// C_Precipitation stores its sprite as a PMaterialHandle (unsigned short) in a
// static variable (s_pMaterial_Rain in the SDK).  The initialisation guard is:
//   if (!s_pMaterial_Rain) s_pMaterial_Rain = FindMaterial("particle/rain",...);
// Once set it is never re-queried, so a FindMaterial hook alone only works on
// the very first entity creation.
//
// Strategy:
//   1. FindMaterial hook -- when snow mode is active, redirect "particle/rain"
//      to g_snowSpriteName so the initial lookup caches the right handle.
//   2. Static address scan -- locate s_pMaterial_Rain inside client.dll by
//      finding the LEA that loads "particle/rain" then the nearby word-store
//      that writes the result back.  Before every Weather_UpdateChain call we
//      reset the static to 0xFFFF (INVALID_MATERIAL_HANDLE) so OnDataChanged
//      re-runs the FindMaterial call, which the hook then intercepts with
//      whatever g_snowSpriteName currently contains.
using FindMaterial_t = void*(__fastcall*)(void*, const char*, const char*, bool, const char*);
FindMaterial_t    o_FindMaterial         = nullptr;
static unsigned short*   s_pPrecipMatHandle     = nullptr; // client.dll static
bool                     s_precipHandleSearched = false;

// Scan client.dll for the address of s_pMaterial_Rain.
// Pattern: find the "particle/rain" string literal, locate the LEA that
// references it, then find the adjacent "mov word ptr [rip+X], ax/cx" that
// stores the FindMaterial result -- that RIP-relative target is our static.
__declspec(noinline) static void FindPrecipMatHandleAddr()
{
    if (s_precipHandleSearched) return;
    s_precipHandleSearched = true;

    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;

    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), hClient, &mi, sizeof(mi));
    uint8_t* base = (uint8_t*)mi.lpBaseOfDll;
    size_t   sz   = mi.SizeOfImage;

    __try {
        // Step 1: find "particle/rain\0" in client.dll
        const char* kStr = "particle/rain";
        const size_t kStrLen = 13; // excludes null
        uint8_t* strAddr = nullptr;
        for (size_t i = 0; i + kStrLen + 1 <= sz; ++i) {
            if (base[i] == 'p' && memcmp(base + i, kStr, kStrLen) == 0 && base[i + kStrLen] == '\0') {
                strAddr = base + i;
                break;
            }
        }
        if (!strAddr) { DebugLog("weather: FindPrecipMatHandle -- string not found"); return; }
        DebugLog("weather: FindPrecipMatHandle -- string at 0x%IX", (uintptr_t)strAddr);

        // Step 2: scan for LEA rdx/r8/rcx, [rip+X] pointing at that string.
        // Encodings:
        //   48 8D 15 rel32  -- lea rdx, [rip+rel32]
        //   4C 8D 05 rel32  -- lea r8,  [rip+rel32]
        //   48 8D 0D rel32  -- lea rcx, [rip+rel32]
        for (size_t i = 0; i + 7 <= sz; ++i) {
            uint8_t* p = base + i;
            bool isLea = false;
            if ((p[0] == 0x48 || p[0] == 0x4C) &&
                p[1] == 0x8D &&
                (p[2] == 0x15 || p[2] == 0x05 || p[2] == 0x0D))
                isLea = true;
            if (!isLea) continue;

            int32_t rel = *reinterpret_cast<int32_t*>(p + 3);
            if (p + 7 + rel != strAddr) continue;

            // Step 3: within ->128 bytes of this LEA, find a RIP-relative
            // word store: 66 89 05/0D/15 rel32  (mov [rip+X], ax/cx/dx)
            for (int j = -128; j < 128; ++j) {
                uint8_t* q = p + j;
                if (q < base || q + 7 > base + sz) continue;
                if (q[0] != 0x66 || q[1] != 0x89) continue;
                if (q[2] != 0x05 && q[2] != 0x0D && q[2] != 0x15) continue;

                int32_t rel2 = *reinterpret_cast<int32_t*>(q + 3);
                unsigned short* addr = reinterpret_cast<unsigned short*>(q + 7 + rel2);
                // Sanity: must be in a writable region (not in the DLL image's .text)
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(addr, &mbi, sizeof(mbi)) &&
                    mbi.State == MEM_COMMIT &&
                    (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                    PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                {
                    s_pPrecipMatHandle = addr;
                    DebugLog("weather: FindPrecipMatHandle -- static at 0x%IX (LEA+%d)",
                             (uintptr_t)addr, j);
                    return;
                }
            }
        }
        DebugLog("weather: FindPrecipMatHandle -- word-store instruction not found near LEA");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("weather: FindPrecipMatHandle -- SEH fault");
    }
}

// Reset the cached handle so OnDataChanged re-calls FindMaterial.
static void Precip_InvalidateMatHandle()
{
    if (!s_pPrecipMatHandle) return;
    __try { *s_pPrecipMatHandle = 0xFFFF; /* INVALID_MATERIAL_HANDLE */ }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The sprite name comes straight from a menu text box, so it can name a VMT
// that does not exist or is malformed. materialsystem does not always survive
// that -- a crash log from this hook showed the fault five frames deep inside
// materialsystem, with the redirected name as the only input we control.
//
// So the substitution is attempted under SEH and the original name is used if
// it faults, and a name that has already failed once is never tried again.
static bool SpriteNameLooksSane(const char* n)
{
    if (!n || !n[0]) return false;
    size_t len = strnlen(n, 128);
    if (len == 0 || len >= 127) return false;
    if (n[0] == '/' || n[0] == '\\') return false;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)n[i];
        if (c < 0x20 || c == '"' || c == ':') return false;
    }
    return true;
}

__declspec(noinline) static void* FindMaterial_Try(
    void* rcx, const char* pName, const char* pGroup, bool bComplain, const char* pPrefix, bool* ok)
{
    void* r = nullptr;
    *ok = false;
    __try {
        r = o_FindMaterial(rcx, pName, pGroup, bComplain, pPrefix);
        *ok = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

void* __fastcall h_FindMaterial(
    void* rcx, const char* pName, const char* pTextureGroup, bool bComplain, const char* pComplainPrefix)
{
    // Name of the substitute that faulted, so it is only ever tried once.
    static char s_badSprite[128] = {};

    if (!g_eject && g_weatherEnabled && g_rainMode == 2 && pName &&
        SpriteNameLooksSane(g_snowSpriteName) &&
        strcmp(g_snowSpriteName, s_badSprite) != 0 &&
        (strcmp(pName, "particle/rain") == 0 || strcmp(pName, "particles/rain") == 0))
    {
        bool  ok  = false;
        void* mat = FindMaterial_Try(rcx, g_snowSpriteName, pTextureGroup,
                                     bComplain, pComplainPrefix, &ok);
        if (ok) {
            DebugLog("weather: FindMaterial redirect \"%s\" -> \"%s\"", pName, g_snowSpriteName);
            return mat;
        }
        strncpy(s_badSprite, g_snowSpriteName, sizeof(s_badSprite) - 1);
        s_badSprite[sizeof(s_badSprite) - 1] = 0;
        DebugLog("weather: FindMaterial faulted on \"%s\" -- redirect disabled for that name",
                 g_snowSpriteName);
    }

    return o_FindMaterial(rcx, pName, pTextureGroup, bComplain, pComplainPrefix);
}

// ---- Custom particles (mode 3) -------------------------------
// World-space DispatchEffect particles (entindex=0) have no handle: they cannot be killed and
// live until map change. So s_trackedWeather is only cleared by FSN_OnLevelChange -- switching
// the weather off or to another mode leaves the records alone, or switching back would spawn a
// second grid on top of the first.

// ---- Spawn-cell occupancy table ----------------------------
// Open-addressed, POD only: Rain_Tick uses __try, so it cannot hold locals that need
// unwinding (no std::unordered_set). 8192 slots against a ~2400-cell max grid keeps the
// load factor low and probes short. Lets the spawn scan stay O(cells) instead of O(cells^2),
// which matters once tight spacing pushes the grid into the thousands of cells.
static const int kOccSize = 8192;
static uint64_t  s_occKey[kOccSize];
static bool      s_occUsed[kOccSize];

static inline int Occ_Slot(uint64_t k)
{
    uint64_t h = k * 0x9E3779B97F4A7C15ull;
    return (int)((h >> 40) & (kOccSize - 1));
}
static inline void Occ_Clear() { memset(s_occUsed, 0, sizeof(s_occUsed)); }
static inline void Occ_Add(uint64_t k)
{
    int s = Occ_Slot(k);
    for (int n = 0; n < kOccSize; ++n) {
        if (!s_occUsed[s]) { s_occUsed[s] = true; s_occKey[s] = k; return; }
        if (s_occKey[s] == k) return;
        s = (s + 1) & (kOccSize - 1);
    }
}
static inline bool Occ_Has(uint64_t k)
{
    int s = Occ_Slot(k);
    for (int n = 0; n < kOccSize; ++n) {
        if (!s_occUsed[s]) return false;
        if (s_occKey[s] == k) return true;
        s = (s + 1) & (kOccSize - 1);
    }
    return true; // table full -- treat as occupied so we stop spawning
}

// ---- Rain_Tick -- called every FRAME_RENDER_START ----------
void Rain_Tick()
{
    if (g_eject || !g_entityList) return;
    constexpr int kSlot = 2047;

    // ---- Off / disabled ----------------------------------------
    if (!g_weatherEnabled || g_rainMode == 0) {
        if (s_lastAppliedMode == 0) return;
        if (s_lastAppliedMode == 1 || s_lastAppliedMode == 2) Precip_TearDown(kSlot);
        s_lastAppliedMode = 0;
        return;
    }

    // ---- Mode 3: Custom particle (Dynamic Grid via DispatchEffect) ----
    // Particles are dispatched as true world-space effects (no entity owner) using
    // g_fnDispatchEffect with m_fFlags=0. This avoids ALL entity-attachment crashes:
    // no CParticleProperty involved, no player entity dereference on death/respawn.
    // Trade-off: we cannot retrieve a pEffect pointer to kill individual particles,
    // so every spawned cell stays recorded until map change, when the engine GCs them.
    // The world-snapped grid prevents re-spawning at the same coordinate, so total
    // particle count is bounded by area visited / spacing^2.
    if (g_rainMode == 3) {
        if (s_lastAppliedMode != 3 && (s_lastAppliedMode == 1 || s_lastAppliedMode == 2))
            Precip_TearDown(kSlot);

        if (!g_fnDispatchEffect || !g_fnGetParticleIndex) return;

        int li = GetLocalPlayerIndex();
        void* pLocal = (li >= 1) ? GetClientEntity(li) : nullptr;
        bool isAlive = (pLocal != nullptr) && IsLocalPlayerAlive();

        if (!isAlive) { s_lastAppliedMode = 3; return; }

        float cx = 0.f, cy = 0.f, cz = 0.f;
        __try {
            const float* p = reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(pLocal) + g_offOrigin);
            cx = p[0]; cy = p[1]; cz = p[2];
        } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

        const float r = g_particleGridRadius;
        const float r2 = r * r;
        const float killR2 = (r + 500.f) * (r + 500.f);

        // Cell spacing = 1000u base + user offset. Negative offset packs cells together
        // (particles overlap -> denser), positive spreads them apart. Two floors:
        //   25u   -- keeps the grid math away from a divide-by-zero
        //   r/23  -- caps the square grid at (2*23+3)^2 cells so a tiny spacing on a big
        //            radius cannot spawn tens of thousands of particle systems
        float spacing = 1000.f + g_particleSpacingOffset;
        if (spacing < 25.f)     spacing = 25.f;
        if (spacing < r / 23.f) spacing = r / 23.f;

        // Spacing change invalidates every tracked coordinate (they sit on the old grid),
        // so drop them and refill. Old particles persist until map change, same as always.
        static float s_lastSpacing = 0.f;
        if (spacing != s_lastSpacing) {
            s_lastSpacing = spacing;
            for (int i = 0; i < 8; ++i) s_trackedWeather[i].clear();
        }

        // Spread spawns across frames -- 16 per tick keeps the game responsive during fill.
        static const int kMaxSpawnPerTick = 16;

        // A layer whose last scan found every cell spawned is skipped until its grid moves
        // (player crossed a cell, spacing/radius/particle changed) or its records change --
        // otherwise a full grid is rescanned (up to ~2400 cells) every frame for nothing.
        struct LayerFull { float ox, oy, sp, r; int pidx; size_t n; bool full; };
        static LayerFull s_layerFull[8] = {};
        int spawnedThisTick = 0;

        for (int i = 0; i < g_particleLayerCount && i < 8; ++i) {
            const char* name = g_particleLayerName[i];
            if (!name[0]) {
                s_trackedWeather[i].clear();
                continue;
            }

            // Get particle system index once per layer per tick.
            int pidx = 0;
            __try { pidx = g_fnGetParticleIndex(name); } __except(EXCEPTION_EXECUTE_HANDLER) {}
            if (pidx <= 0) continue;

            // 1. Records are never dropped mid-map. A dispatched particle has no kill handle
            //    and keeps running after the player walks away, so forgetting its cell (the
            //    old cleanup pass) spawned a second copy on top on the way back -- live
            //    particle count grew with every lap of the map and so did frame time.
            //    Keeping the record = each cell spawns once per map (level change clears).
            auto& layerVec = s_trackedWeather[i];

            // 2. Spawn pass: world-snapped grid, one dispatch per missing cell.
            if (spawnedThisTick >= kMaxSpawnPerTick) continue;

            const float finalZ = cz + g_particleLayerZ[i];
            const float invSpacing = 1.f / spacing;
            float originX = floorf(cx * invSpacing) * spacing;
            float originY = floorf(cy * invSpacing) * spacing;
            int steps = (int)ceilf(r * invSpacing) + 1;

            LayerFull& lf = s_layerFull[i];
            if (lf.full && lf.ox == originX && lf.oy == originY && lf.sp == spacing && lf.r == r
                && lf.pidx == pidx && lf.n == layerVec.size())
                continue;

            // Mark every already-spawned cell of this layer, keyed by snapped grid index.
            // Far records can't match a scan cell; skipping them keeps the table near-empty.
            Occ_Clear();
            for (const auto& tw : layerVec) {
                float dx = tw.x - cx, dy = tw.y - cy;
                if (dx * dx + dy * dy > killR2) continue;
                uint32_t kx = (uint32_t)(int32_t)lroundf(tw.x * invSpacing);
                uint32_t ky = (uint32_t)(int32_t)lroundf(tw.y * invSpacing);
                Occ_Add(((uint64_t)kx << 32) | ky);
            }

            // Collect all missing cells first, then shuffle before dispatching.
            // Without this the scan-line order fills the same corner every tick,
            // leaving the opposite side of the radius patchy until the grid is full.
            struct PendingCell { float wx, wy; };
            static std::vector<PendingCell> s_pending;
            s_pending.clear();

            // Radius measured from the centre of the player's cell, not the player: the set of
            // cells then only changes with the snapped origin, which is what s_layerFull keys on.
            const float ccx = originX + spacing * 0.5f, ccy = originY + spacing * 0.5f;
            for (int ix = -steps; ix <= steps; ++ix) {
                for (int iy = -steps; iy <= steps; ++iy) {
                    float wx = originX + ix * spacing;
                    float wy = originY + iy * spacing;
                    float dx = wx - ccx, dy = wy - ccy;
                    if (dx * dx + dy * dy > r2) continue;

                    uint32_t kx = (uint32_t)(int32_t)lroundf(wx * invSpacing);
                    uint32_t ky = (uint32_t)(int32_t)lroundf(wy * invSpacing);
                    if (Occ_Has(((uint64_t)kx << 32) | ky)) continue;

                    s_pending.push_back({ wx, wy });
                }
            }

            // Shuffle so gaps are distributed uniformly rather than clustered on one side.
            for (int k = (int)s_pending.size() - 1; k > 0; --k) {
                int j = (int)(((unsigned)rand() * ((unsigned)k + 1)) >> 15) % (k + 1);
                std::swap(s_pending[k], s_pending[j]);
            }

            for (const auto& cell : s_pending) {
                if (spawnedThisTick >= kMaxSpawnPerTick) break;

                // Dispatch world-space particle: m_fFlags=0 (no entity), m_nHitBox=index,
                // m_vOrigin = world position. Goes through ParticleEffectCallback ->
                // CNewParticleEffect::Create(NULL, name) -- no entity attachment.
                CEffectData_Weather data = {};
                data.m_vOrigin[0] = cell.wx;
                data.m_vOrigin[1] = cell.wy;
                data.m_vOrigin[2] = finalZ;
                data.m_nHitBox    = pidx;
                data.m_flScale    = 1.f;
                __try { g_fnDispatchEffect("ParticleEffect", data); } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }

                layerVec.push_back({ cell.wx, cell.wy, finalZ, nullptr });
                ++spawnedThisTick;
            }
            lf = { originX, originY, spacing, r, pidx, layerVec.size(), s_pending.empty() };
        }
        s_lastAppliedMode = 3;
        return;
    }

    // Center the grid on the local player's current position so particles appear around
    // the player regardless of where the map's world origin sits.
    // Throttle: only run entity management and mode-change logic every 6 frames.
    // Convar pushes still run every frame (cheap, cached pointers).
    static int s_tickSkip = 0;
    bool bFullTick = (++s_tickSkip >= 6);
    if (bFullTick) s_tickSkip = 0;

    // ---- Modes 1 & 2: CPrecipitation sprite --------------------
    if (!bFullTick) {
        // On non-full ticks, only push convars (cheap). Skip entity management.
        if (s_lastAppliedMode == g_rainMode)
            Precip_ApplyConvars();
        return;
    }

    if (s_lastAppliedMode == 3) s_lastAppliedMode = -99;

    // Find CPrecipitation ClientClass lazily
    if (!g_precipClass) {
        for (auto* p = GetAllClasses(); p; p = p->m_pNext) {
            if (p->m_pNetworkName && strcmp(p->m_pNetworkName, "CPrecipitation") == 0) {
                g_precipClass = p;
                DebugLog("weather: found CPrecipitation class");
                break;
            }
        }
        if (!g_precipClass) return;
    }
    Weather_FindOffsets();
    if (g_offPrecipType < 0 || g_offMins < 0) return;

    // Spawn entity at slot 2047 if not present
    void* pEnt = GetClientEntity(kSlot);
    if (!pEnt) {
        auto* cls = reinterpret_cast<ClientClass*>(g_precipClass);
        if (!cls->m_pCreateFn) return;
        __try {
            using CreateFn = void*(__cdecl*)(int, int);
            g_precipNetworkable = reinterpret_cast<CreateFn>(cls->m_pCreateFn)(kSlot, 0);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            g_precipNetworkable = nullptr; return;
        }
        pEnt = GetClientEntity(kSlot);
        if (!pEnt) { g_precipNetworkable = nullptr; return; }
        g_precipEntity      = pEnt;
        g_precipNetworkable = GetClientNetworkable(kSlot);
        s_lastAppliedMode   = -99;
        DebugLog("weather: spawned CPrecipitation at slot %d", kSlot);
    } else {
        g_precipEntity = pEnt;
        if (!g_precipNetworkable)
            g_precipNetworkable = GetClientNetworkable(kSlot);
    }

    Precip_ApplyConvars();

    // Resolve the s_pMaterial_Rain static address lazily (needs client.dll loaded).
    FindPrecipMatHandleAddr();

    // Sprite name changed while snow is running -- force a re-lookup.
    if (g_rainMode == 2 && s_snowSpriteDirty)
    {
        s_snowSpriteDirty = false;
        s_lastAppliedMode = -99;
    }

    if (s_lastAppliedMode != g_rainMode)
    {
        // Reset the cached PMaterialHandle before triggering OnDataChanged so the
        // guard condition fires and FindMaterial is called again.  Our hook then
        // redirects to g_snowSpriteName (snow) or passes through (rain).
        Precip_InvalidateMatHandle();

        int precipType = (g_rainMode == 2) ? 1 : 0;
        Weather_UpdateChain(g_precipNetworkable, g_precipEntity, precipType, true);
        s_lastAppliedMode = g_rainMode;
        DebugLog("weather: applied mode=%d precipType=%d", g_rainMode, precipType);
    }
}


// ---- ragdoll.cpp -------------------------------------------
using CreateTFRagdoll_t = void(__fastcall*)(void*);
CreateTFRagdoll_t o_CreateTFRagdoll = nullptr;

void __fastcall h_CreateTFRagdoll(void* rcx)
{
    // Whose corpse is this? m_hPlayer's low 14 bits are the entity index, and the kill set says
    // whether we made the kill. Resolved ONCE here and drained, because three things downstream
    // need the answer and the set is filled on every kill now.
    int  victimIdx  = 0;
    bool isLocalKill = false;
    if (g_offRagdollPlayer > 0) {
        int handle = 0;
        __try { handle = *reinterpret_cast<int*>((uintptr_t)rcx + g_offRagdollPlayer); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        victimIdx = handle & 0x3FFF;
        if (victimIdx > 0 && g_ragdollKillSet.count(victimIdx)) {
            isLocalKill = true;
            g_ragdollKillSet.erase(victimIdx);
        }
    }

    // Gold statue for a set_turn_to_gold reskin (Saxxy / Golden Wrench / Gold Frying Pan). The
    // attribute is server-side and never reaches us, but m_bGoldRagdoll -- which is what every
    // effect it drives actually reads (the gold material on the body and its wearables, the fixed
    // constraints, Saxxy.TurnGold) -- is set right here, before the original runs. It fires even
    // with the ragdoll effects switched off: the statue IS why the weapon is equipped, and a
    // gilded corpse is never one of the ragdolls "only my kills" removes.
    bool wantGold = false;
    if (isLocalKill && g_enableSkins && g_offRagdollGold > 0 && Skins_HeldWeaponTurnsToGold()) {
        wantGold = true;
        __try { *reinterpret_cast<bool*>((uintptr_t)rcx + g_offRagdollGold) = true; }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (g_ragdollEffectEnabled || g_ragdollOnlyKills)
    {
        // Suppress ragdolls of players not killed by local player.
        if (g_ragdollOnlyKills && !isLocalKill)
            return;

        if (g_ragdollEffectEnabled && isLocalKill)
        {
            auto wb = [&](int off, bool val) {
                if (off > 0) *reinterpret_cast<bool*>((uintptr_t)rcx + off) = val;
            };
            wb(g_offRagdollGib,    false);
            wb(g_offRagdollBurning, g_ragdollEffectBurning);
            wb(g_offRagdollElectro, g_ragdollEffectElectro);
            wb(g_offRagdollAsh,     g_ragdollEffectAsh);
            wb(g_offRagdollGold,    g_ragdollEffectGold || wantGold);
            wb(g_offRagdollIce,     g_ragdollEffectIce);
            if (g_offRagdollFeignDeath > 1)
                *reinterpret_cast<bool*>((uintptr_t)rcx + g_offRagdollFeignDeath - 1) = g_ragdollEffectDissolve;
        }
    }
    o_CreateTFRagdoll(rcx);
}

// ---- particle hooks ----------------------------------------
CParticlePropCreate_t   o_CParticlePropCreate   = nullptr;
CParticlePropCreatePt_t o_CParticlePropCreatePt = nullptr;

//   41 56 48 83 EC ? 48 8B 59 ? 49 8B F1"
// pProp = CParticleProperty* (m_pOuter at +0 = owning entity)
// This intercepts ALL entity-attached particle creations, which bypasses CTE_DispatchEffect.
// Unusual hat effects and footstep spells both go through this path.
void* __fastcall h_CParticlePropCreate(
    void*       pProp,
    const char* pszParticleName,
    int         iAttachType,
    const char* pszAttachmentName)
{
    if (pszParticleName && pProp) {
        if (g_noHats) {
            bool isWeaponUnusual = strstr(pszParticleName, "weapon_unusual_") != nullptr;
            if (!isWeaponUnusual &&
                (strstr(pszParticleName, "unusual_") ||
                 strstr(pszParticleName, "superrare_"))) {
                DebugLog("particles: blocked '%s'", pszParticleName);
                return nullptr;
            }
        }
    }
    return o_CParticlePropCreate(pProp, pszParticleName, iAttachType, pszAttachmentName);
}

// Point-based Create overload: same filter as the name-based hook above.
// Amalgam pattern: "44 89 4C 24 ? 44 89 44 24 ? 53"
void* __fastcall h_CParticlePropCreatePt(
    void* pProp, const char* pszParticleName, int iAttachType,
    int iAttachmentPoint, PtVec vecOriginOffset)
{
    if (pszParticleName && pProp) {
        if (g_noHats) {
            bool isWeaponUnusual = strstr(pszParticleName, "weapon_unusual_") != nullptr;
            if (!isWeaponUnusual &&
                (strstr(pszParticleName, "unusual_") ||
                 strstr(pszParticleName, "superrare_"))) {
                DebugLog("particles: blocked(pt) '%s'", pszParticleName);
                return nullptr;
            }
        }
    }
    return o_CParticlePropCreatePt(pProp, pszParticleName, iAttachType, iAttachmentPoint, vecOriginOffset);
}

// ---- CS:GO viewmodel bob / gun lower / landing dip -------------
#define VMCS_REMOVE_BOB g_removeViewmodelBob
#include "vm_csgo_hook.inl"
void VmCsgo_Install(HMODULE client) { VmCs_Install(client); }

__declspec(noinline) static bool CalcViewModel_Copy(const void* pEyePosition, const void* pEyeAngles,
                                                    float eye[3], float ang[3])
{
    __try {
        memcpy(eye, pEyePosition, sizeof(float) * 3);
        memcpy(ang, pEyeAngles, sizeof(float) * 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// SEH helper for h_CalcViewModelView (C2712). Writes eye+offset into out[3]; never touches the
// caller's Vector -- see h_CalcViewModelView.
__declspec(noinline) static bool CalcViewModel_ApplyOffset(const void* vm, const void* pEyePosition, const void* pEyeAngles, float out[3])
{
    __try {
        // Separate offset: vm index 1 = offhand v_model (spy watch, dead ringer, cloak and dagger).
        float ox = g_vmOffsetX, oy = g_vmOffsetY, oz = g_vmOffsetZ;
        if (g_vmOffsetSeparate && vm && g_offViewModelIndex > 0
            && *reinterpret_cast<const int*>(reinterpret_cast<const char*>(vm) + g_offViewModelIndex) != 0) {
            ox = g_vmOffset2X; oy = g_vmOffset2Y; oz = g_vmOffset2Z;
        }

        constexpr float kDeg2Rad = 3.14159265f / 180.f;
        const float* ang = reinterpret_cast<const float*>(pEyeAngles);
        float pitch = ang[0], yaw = ang[1];

        float sy = sinf(yaw   * kDeg2Rad), cy = cosf(yaw   * kDeg2Rad);
        float sp = sinf(pitch * kDeg2Rad), cp = cosf(pitch * kDeg2Rad);

        float fwd[3]   = {  cp*cy,  cp*sy, -sp };
        float right[3] = {  sy,    -cy,     0.f };
        float up[3]    = {  sp*cy,  sp*sy,  cp  };

        const float* pos = reinterpret_cast<const float*>(pEyePosition);
        out[0] = pos[0] + right[0]*ox + fwd[0]*oy + up[0]*oz;
        out[1] = pos[1] + right[1]*ox + fwd[1]*oy + up[1]*oz;
        out[2] = pos[2] + right[2]*ox + fwd[2]*oy + up[2]*oz;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// ---- CalcViewModelView hook --------------------------------
using CalcViewModelView_t = void(__fastcall*)(void*, void*, void*, void*);
CalcViewModelView_t o_CalcViewModelView = nullptr;

void __fastcall h_CalcViewModelView(void* rcx, void* owner, void* pEyePosition, void* pEyeAngles)
{
    if (g_eject) { o_CalcViewModelView(rcx, owner, pEyePosition, pEyeAngles); return; }
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    // CBasePlayer::CalcViewModelView hands EVERY viewmodel (MAX_VIEWMODELS 2) the SAME eyeOrigin
    // local. Offsetting it in place stacked: vm0 (weapon) moved 1x, vm1 moved 2x. Spy's watch is vm1
    // (tf_weapon_invis.cpp SetViewModelIndex(1)), so it drifted off the hand. Offset a copy instead.
    // CS:GO order: pitch/dip on the eye first, then the offset along the adjusted angles.
    const bool csAdjust = (g_vmCsGunLower || g_vmCsLandDip) && o_VmCsBobHelper;
    float eye[3], ang[3];
    if ((g_enableViewmodelOffset || csAdjust) && pEyePosition && pEyeAngles
        && CalcViewModel_Copy(pEyePosition, pEyeAngles, eye, ang)) {
        if (csAdjust) VmCs_EyeAdjustSafe(owner, eye, ang);
        float moved[3] = { eye[0], eye[1], eye[2] };
        if (!g_enableViewmodelOffset || CalcViewModel_ApplyOffset(rcx, eye, ang, moved)) {
            o_CalcViewModelView(rcx, owner, moved, ang);
            return;
        }
    }
    o_CalcViewModelView(rcx, owner, pEyePosition, pEyeAngles);
}

// ---- Prop modulation hooks ---------------------------------
// IStudioRender vtable slots (sdk istudiorender.h, after IAppSystem's 5):
//   27 = SetColorModulation (hooked), 28 = SetAlphaModulation (called), 30 = DrawModelStaticProp (hooked)
// DrawModelStaticProp sets g_drawingProps so the color/alpha hooks know to
// substitute the prop modulation color only during static prop rendering.
using DrawStaticProps_t            = void(__fastcall*)(void*, void**, int, bool, bool);
using SetColorModulation_t         = void(__fastcall*)(void*, const float*);
using SetAlphaModulation_t         = void(__fastcall*)(void*, float);
using DrawModelStaticProp_t        = void(__fastcall*)(void*, const void*, const void*, int);
static DrawStaticProps_t           o_DrawStaticProps           = nullptr;
static SetColorModulation_t        o_SetColorModulation        = nullptr;
static SetAlphaModulation_t        o_SetAlphaModulation        = nullptr;
static DrawModelStaticProp_t       o_DrawModelStaticProp       = nullptr;

static void __fastcall h_SetColorModulation(void* rcx, const float* pColor)
{
    if (!g_drawingProps || !g_enableModulate || g_eject)
        return o_SetColorModulation(rcx, pColor);

    float flColor[3] = { g_modulateProps[0], g_modulateProps[1], g_modulateProps[2] };
    o_SetColorModulation(rcx, flColor);
}




// IStudioRender::ForcedMaterialOverride is vtbl slot 33.
// Slot derived from confirmed slots: SetColorModulation=27, SetAlphaModulation=28,
// DrawModel=29, DrawModelStaticProp=30, DrawStaticPropDecals=31,
// DrawStaticPropShadows=32, ForcedMaterialOverride=33.
__declspec(noinline) static void PropForceMat(void* rcx, void* pMat)
{
    __try {
        using Fn = void(__fastcall*)(void*, void*, int);
        ((Fn)(*(void***)rcx)[33])(rcx, pMat, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Draw one prop cham layer: set forced material + color, call original draw.
// Extracted as noinline to allow __try alongside the RAII guard in the caller.
__declspec(noinline) static void PropChamDrawLayer_SEH(
    void* rcx, const void* pState, const void* modelToWorld, int flags,
    void* pMat, float r, float g, float b, float a)
{
    __try {
        PropForceMat(rcx, pMat);
        float col[3] = { r, g, b };
        o_SetColorModulation(rcx, col);
        o_SetAlphaModulation(rcx, a);
        o_DrawModelStaticProp(rcx, pState, modelToWorld, flags);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void __fastcall h_DrawModelStaticProp(void* rcx, const void* pState, const void* modelToWorld, int flags)
{
    if (g_eject) { o_DrawModelStaticProp(rcx, pState, modelToWorld, flags); return; }
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    if (!g_enablePropChams || g_propChamCount <= 0) {
        // No prop chams -- normal draw; colour is set by h_SetColorModulation above.
        o_DrawModelStaticProp(rcx, pState, modelToWorld, flags);
        return;
    }

    // Draw each cham layer.  o_SetColorModulation bypasses the modulate hook so
    // we set exactly the layer colour; ForcedMaterialOverride persists until cleared.
    for (int i = 0; i < g_propChamCount; ++i) {
        void* pMat = VmGetMat(g_propChamLayers[i].matType); // type 0 -- nullptr -- original mat
        float r  = g_propChamLayers[i].color[0];
        float g2 = g_propChamLayers[i].color[1];
        float b  = g_propChamLayers[i].color[2];
        float a  = g_propChamLayers[i].color[3];
        if (g_propChamLayers[i].useSheen) {
            float s[3]; GetAnimatedSheenColor(s);
            r = s[0]; g2 = s[1]; b = s[2];
        }
        PropChamDrawLayer_SEH(rcx, pState, modelToWorld, flags, pMat, r, g2, b, a);
    }
    PropForceMat(rcx, nullptr); // clear forced material so subsequent props render normally
}

static void __fastcall h_DrawStaticProps(void* rcx, void** pProps, int count, bool bShadowDepth, bool drawVCollide)
{
    if (g_eject) { o_DrawStaticProps(rcx, pProps, count, bShadowDepth, drawVCollide); return; }
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    g_drawingProps.store(true);
    o_DrawStaticProps(rcx, pProps, count, bShadowDepth, drawVCollide);
    g_drawingProps.store(false);
}

// ---- Prop hook installer (called from PollThread + poll loop until resolved) ----
bool g_propHooksInstalled = false;

void TryInstallPropHooks()
{
    if (g_propHooksInstalled) return;

    // TF2 x64: "studiorender.dll"; also try vstudiorender.dll as fallback.
    if (!g_studioRender)
    {
        const char* srModules[] = {"studiorender.dll", "vstudiorender.dll", nullptr};
        const char* srNames[]   = {"VStudioRender025", "VStudioRender026", "VStudioRender027", nullptr};
        for (int m = 0; srModules[m] && !g_studioRender; ++m)
            for (int i = 0; srNames[i] && !g_studioRender; ++i)
                g_studioRender = GetInterface(srModules[m], srNames[i]);
        DebugLog("prop hooks: studioRender resolve -> %p", g_studioRender);
    }
    if (!g_studioRender) return; // not loaded yet -- retry next tick

    __try {
        void** vtbl = *(void***)g_studioRender;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return;
        if (MH_CreateHook(vtbl[27], (LPVOID)h_SetColorModulation,       (LPVOID*)&o_SetColorModulation)       == MH_OK) MH_EnableHook(vtbl[27]);
        // Alpha is only ever passed through, so call slot 28 directly instead of detouring
        // every studio draw for it.
        o_SetAlphaModulation = (SetAlphaModulation_t)vtbl[28];
        if (MH_CreateHook(vtbl[30], (LPVOID)h_DrawModelStaticProp,      (LPVOID*)&o_DrawModelStaticProp)      == MH_OK) MH_EnableHook(vtbl[30]);
        DebugLog("prop hooks: slot27=%p slot28=%p slot30=%p", vtbl[27], vtbl[28], vtbl[30]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("prop hooks: SEH during vtable hook install");
        return;
    }

    // CStaticPropMgr_DrawStaticProps in engine.dll -- outer bracket that sets
    // g_drawingProps before any SetColorModulation calls fire for the batch.
    {
        HMODULE engine = GetModuleHandleA("engine.dll");
        if (engine)
        {
            uint8_t* addr = PatternScan(engine,
                "4C 8B DC 49 89 5B ? 49 89 6B ? 49 89 73 ? 57 41 54 41 55 41 56 41 57 48 83 EC ? 4C 8B 3D");
            if (addr)
            {
                if (MH_CreateHook(addr, (LPVOID)h_DrawStaticProps, (LPVOID*)&o_DrawStaticProps) == MH_OK)
                    MH_EnableHook(addr);
                DebugLog("prop hooks: DrawStaticProps=%p", addr);
            }
            else
                DebugLog("prop hooks: DrawStaticProps pattern not found");
        }
    }

    g_propHooksInstalled = true;
}
