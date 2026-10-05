#include "../common.h"
#include "../globals.h"
#include "vmglow.h"
#include "flipworld.h"
#include "config.h"
#include "drawutils.h"
#include "watermark.h"
#include "crosshair.h"
#include "menu.h"
#include "visuals.h"
#include "misc.h"
#include "names.h"
#include "skins.h"
#include "screenfx.h"
#include "render.h"
#include "scoreboard_hook.h"
#include "netgraph.h"
#include "jobs.h"

static DWORD WINAPI WatchdogThread(LPVOID)
{
    DWORD lastPresent = 0;
    int   frozenSecs  = 0;

    while (g_run.load())
    {
        if (g_hWatchdogExitEvent)
            WaitForSingleObject(g_hWatchdogExitEvent, 1000);
        else
            Sleep(1000);

        if (!g_run.load()) break;

        DWORD curPresent = (DWORD)g_presentCount.load();
        bool  frozen     = (curPresent == lastPresent) && g_imguiInit;
        if (frozen) ++frozenSecs; else frozenSecs = 0;
        lastPresent = curPresent;

        SYSTEMTIME st;
        GetLocalTime(&st);

        char hbuf[512];
        int n = _snprintf(hbuf, sizeof(hbuf) - 1,
            "[%02d:%02d:%02d] phase=\"%s\" thread=%u presentFrames=%u fsnStage=%d "
            "inGame=%d conn=%d frozen=%s frozenSecs=%d faults=%u\n",
            st.wHour, st.wMinute, st.wSecond,
            g_phase, (unsigned)g_phaseThread.load(), curPresent,
            g_fsnStage.load(),
            (int)g_inGame.load(), (int)g_conn.load(),
            frozen ? "YES" : "no", frozenSecs,
            g_vehFaultCount.load());
        if (n > 0) hbuf[n] = '\0';

        HANDLE h = CreateFileA("C:\\infovisual\\heartbeat.txt", GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(h, hbuf, (DWORD)n, &w, nullptr);
            // No FlushFileBuffers: a synchronous disk flush every second, and the OS cache
            // already survives a game crash (same reasoning as DebugLog).
            CloseHandle(h);
        }

        // A map load stops Present for as long as it takes, so logging every
        // second turned a normal load into fifty lines of noise. Log the onset
        // and the recovery, nothing in between.
        static bool s_reportedFreeze = false;
        static int  s_freezeSecs     = 0;
        if (frozenSecs >= 5 && !s_reportedFreeze) {
            s_reportedFreeze = true;
            s_freezeSecs     = frozenSecs;
            DebugLog("WATCHDOG: Present frozen, phase=\"%s\"", g_phase);
        } else if (!frozen && s_reportedFreeze) {
            s_reportedFreeze = false;
            // Duration is the number that matters for load-time work -- without
            // it the log says a load stalled but not for how long.
            DebugLog("WATCHDOG: Present resumed after %d s, phase=\"%s\"",
                     s_freezeSecs, g_phase);
        } else if (frozen && s_reportedFreeze) {
            s_freezeSecs = frozenSecs;
        }

        // First-chance faults are cheap now (see crashhandler.cpp), but a loop
        // of them still means our own SEH-guarded code is walking bad memory.
        // Report the running total every 10 s while it climbs.
        {
            static uint32_t s_lastFaults = 0;
            static int      s_faultTick   = 0;
            uint32_t faults = g_vehFaultCount.load();
            if (faults != s_lastFaults && ++s_faultTick >= 10) {
                s_faultTick  = 0;
                DebugLog("VEH: %u first-chance faults (+%u), last=0x%IX, dumps=%u",
                         faults, faults - s_lastFaults,
                         g_vehLastAddr.load(), g_vehDumpCount.load());
                s_lastFaults = faults;
            }
        }

        // "Laggier the longer I play" needs numbers, not guesses: one line a minute.
        // Rising priv/handles/gdi = a leak; falling fps with flat memory = per-frame
        // work growing; jobs climbing = workers can't keep up.
        {
            static int      s_perfTick    = 0;
            static uint32_t s_perfFrames  = 0;
            static DWORD    s_perfStartMs = 0;
            if (++s_perfTick >= 60) {
                s_perfTick = 0;
                DWORD    now = GetTickCount();
                uint32_t pf  = (uint32_t)g_presentCount.load();
                float    fps = (s_perfStartMs && now > s_perfStartMs)
                    ? (pf - s_perfFrames) * 1000.f / (float)(now - s_perfStartMs) : 0.f;
                s_perfFrames = pf; s_perfStartMs = now;

                PROCESS_MEMORY_COUNTERS_EX pmc{}; pmc.cb = sizeof(pmc);
                GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
                DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles);
                DebugLog("perf: fps=%.0f priv=%uMB ws=%uMB handles=%u gdi=%u user=%u jobs=%d inGame=%d",
                         fps, (unsigned)(pmc.PrivateUsage >> 20), (unsigned)(pmc.WorkingSetSize >> 20),
                         (unsigned)handles,
                         (unsigned)GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
                         (unsigned)GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS),
                         Jobs::Pending(), (int)g_inGame.load());
            }
        }
    }
    return 0;
}

static LPTOP_LEVEL_EXCEPTION_FILTER s_prevExFilter = nullptr;   // restored on eject

static LONG WINAPI UnhandledExFilter(EXCEPTION_POINTERS* ep)
{
    if (!ep) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    if (code == 0xE06D7363 || code == EXCEPTION_BREAKPOINT ||
        code == EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;

    SYSTEMTIME st; GetLocalTime(&st);
    DebugLog("[%02d:%02d:%02d] UNHANDLED EXCEPTION code=0x%08X addr=0x%IX "
             "phase=\"%s\" thread=%u presentFrames=%u fsnStage=%d",
             st.wHour, st.wMinute, st.wSecond,
             (unsigned)code, (uintptr_t)ep->ExceptionRecord->ExceptionAddress,
             g_phase, (unsigned)g_phaseThread.load(),
             (unsigned)g_presentCount.load(), g_fsnStage.load());

    HANDLE hProc = GetCurrentProcess();
    static volatile LONG s_sym = 0;
    if (InterlockedExchange(&s_sym, 1) == 0)
        SymInitialize(hProc, nullptr, TRUE);

    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf = {};
    DWORD mach = IMAGE_FILE_MACHINE_AMD64;
    sf.AddrPC.Offset    = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 32; ++i)
    {
        if (!StackWalk64(mach, hProc, GetCurrentThread(), &sf, &ctx,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (!sf.AddrPC.Offset) break;
        void* addr = (void*)sf.AddrPC.Offset;
        HMODULE hm = nullptr;
        char    mn[64] = "<unknown>";
        uintptr_t off = 0;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)addr, &hm) && hm)
        {
            char full[MAX_PATH]; GetModuleFileNameA(hm, full, MAX_PATH);
            const char* sl = strrchr(full, '\\');
            strncpy(mn, sl ? sl + 1 : full, sizeof(mn) - 1);
            off = (uintptr_t)addr - (uintptr_t)hm;
        }
        DebugLog("  [%2d] %s + 0x%IX", i, mn, off);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}


using CL_CheckForPureServerWhitelist_t = void(__fastcall*)(void** pFilesToReload);
static CL_CheckForPureServerWhitelist_t o_CL_CheckForPureServerWhitelist = nullptr;

static void __fastcall h_CL_CheckForPureServerWhitelist(void** pFilesToReload)
{
    if (g_enablePureBypass) return;
    o_CL_CheckForPureServerWhitelist(pFilesToReload);
}

// ---- One-shot interface resolution + hook install ----------
// Runs once, 3 s after inject, off the render thread. Everything here is
// "find the interface, install the detour"; nothing periodic belongs in it.
static void PollThread() {
    Sleep(3000);

    // Everything below runs ONCE. Injected at game launch, MainThread only waited for engine.dll +
    // d3d9, so this used to run before client.dll was loaded: every `if (hClient)` block was skipped
    // for the whole session (no FrameStageNotify, DoPostSSE, DrawViewModels, LevelInit, level hooks
    // = world modulate, chams, glow, weather and flip world all dead, heartbeat fsnStage=-1).
    // Wait for client.dll AND g_pClientMode, which CHLClient::Init sets (clientmode_tf.cpp:318).
    // 500 ms steps: eject joins this thread with a 2 s timeout.
    {
        uint8_t* cmRef = nullptr;
        for (int i = 0; !g_eject.load(); ++i) {
            if (HMODULE hClient = GetModuleHandleA("client.dll")) {
                if (!cmRef) cmRef = PatternScan(hClient, "48 8B 0D ? ? ? ? 48 8B 10 48 8B 19 48 8B C8 FF 92");
                if (cmRef) {
                    __try {
                        const int32_t rel = *reinterpret_cast<int32_t*>(cmRef + 3);
                        g_clientModePtr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(cmRef) + 7 + rel);
                    } __except(EXCEPTION_EXECUTE_HANDLER) { g_clientModePtr = nullptr; }
                }
                if (g_clientModePtr || (!cmRef && i >= 20)) break;   // no pattern at all: don't hang, install what we can
            }
            if (i == 10) DebugLog("poll_thread: waiting for client.dll to initialize");
            Sleep(500);
        }
        if (g_eject.load()) return;
        DebugLog("poll_thread: client.dll ready, clientmode=%p", g_clientModePtr);
    }

    const char* engineNames[] = {"VEngineClient014","VEngineClient013","VEngineClient015",nullptr};
    for (int i = 0; engineNames[i] && !g_engine; ++i)
        g_engine = (IEngineClient*)GetInterface("engine.dll", engineNames[i]);

    {
        const char* cvarNames[]   = {"VEngineCvar004","VEngineCvar007","VEngineCvar003",nullptr};
        const char* cvarModules[] = {"vstdlib.dll","tier0.dll"};
        for (auto* mod : cvarModules) {
            for (int i = 0; cvarNames[i] && !g_cvar; ++i)
                g_cvar = (ICvar*)GetInterface(mod, cvarNames[i]);
            if (g_cvar) break;
        }
    }
    g_matSystem = GetInterface("materialsystem.dll", "VMaterialSystem082");

    // ---- IVModelRender + IVModelInfoClient ---------------------
    if (!g_modelRender)
        g_modelRender = GetInterface("engine.dll", "VEngineModel016");
    if (!g_renderView)
        g_renderView  = GetInterface("engine.dll", "VEngineRenderView014");
    // ViewDrawFade: the one window where the gun is painted and the hud is not, which is where
    // frame blending smears the viewmodel by what its bones did.
    ScreenFx_InstallViewmodelPass();
    if (!g_modelInfo) {
        const char* miNames[] = {"VModelInfoClient006","VModelInfoClient007",nullptr};
        for (int i = 0; miNames[i] && !g_modelInfo; ++i)
            g_modelInfo = GetInterface("engine.dll", miNames[i]);
    }
    if (!g_effects)
        g_effects = GetInterface("engine.dll", "VEngineEffects001");
    // Install IVModelRender hooks (slot 1=ForcedMaterialOverride, slot 19=DrawModelExecute).
    if (g_modelRender && !g_vmChamsHookInstalled) {
        __try {
            void** vtbl = *(void***)g_modelRender;
            if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000) {
                if (vtbl[1] && MH_CreateHook(vtbl[1], (LPVOID)h_ForcedMatOverride,
                        (LPVOID*)&o_ForcedMatOverride_fwd) == MH_OK) {
                    MH_EnableHook(vtbl[1]);
                    DebugLog("vmchams: ForcedMaterialOverride hook installed (slot 1)");
                }
                if (vtbl[19] && MH_CreateHook(vtbl[19], (LPVOID)h_DrawModelExecute,
                        (LPVOID*)&o_DrawModelExecute) == MH_OK) {
                    MH_EnableHook(vtbl[19]);
                    g_vmChamsHookInstalled = true;
                    DebugLog("vmchams: DrawModelExecute hook installed (slot 19)");
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            DebugLog("vmchams: SEH during hook install");
        }
    }

    // ---- Weapon viewmodel hooks --------------------------------
    if (!g_weaponHooksInstalled) {
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (hClient) {
            // CBaseAnimating::DrawModel -- unique prologue in TF2 x64
            uint8_t* fnDrawModel = PatternScan(hClient,
                "4C 8B DC 49 89 5B ? 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC");
            // Call site inside CEconEntity::DrawOverriddenViewmodel that calls DrawModel
            uint8_t* fnOverriddenCall = PatternScan(hClient,
                "8B 97 ? ? ? ? 85 D2 74 ? 83 FA ? 74 ? 0F B7 EA 4C 8B 05 ? ? ? ? 8B CD 49 83 C0 ? 48 C1 E1");
            // CBaseAnimating::InternalDrawModel
            uint8_t* fnInternalDraw = PatternScan(hClient,
                "48 8B C4 55 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 44 8B 81");
            DebugLog("vmchams: weapon sigs: DrawModel=%p OverriddenCall=%p InternalDraw=%p",
                (void*)fnDrawModel, (void*)fnOverriddenCall, (void*)fnInternalDraw);
            if (fnDrawModel && fnOverriddenCall && fnInternalDraw) {
                g_addrDrawOverriddenVMCall = uintptr_t(fnOverriddenCall);
                VmCull_Resolve(hClient, fnInternalDraw);   // before the hook goes live
                bool ok1 = MH_CreateHook(fnDrawModel,    (LPVOID)h_CBaseAnimatingDrawModel,
                                          (LPVOID*)&o_CBaseAnimatingDrawModel)    == MH_OK;
                bool ok2 = MH_CreateHook(fnInternalDraw, (LPVOID)h_CBaseAnimatingInternalDraw,
                                          (LPVOID*)&o_CBaseAnimatingInternalDraw) == MH_OK;
                if (ok1) MH_EnableHook(fnDrawModel);
                if (ok2) MH_EnableHook(fnInternalDraw);
                g_weaponHooksInstalled = ok1 && ok2;
                DebugLog("vmchams: weapon hooks: DrawModel=%d InternalDraw=%d addr=%p",
                    (int)ok1, (int)ok2, (void*)g_addrDrawOverriddenVMCall);
            }
        }
    }

    // ---- CBaseAnimating::SetSequence hook -- animation remap for reskins ----
    // Pattern from Amalgam: "40 53 48 83 EC ? 48 8B D9 39 91 ? ? ? ? 74 ? 89 91" in client.dll
    // Hook fires on every SetSequence call; only acts when the entity is the local viewmodel and
    // an animation remap is configured for the active reskin.
    if (!g_setSeqHookInstalled) {
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (hClient) {
            uint8_t* fnSetSeq = PatternScan(hClient,
                "40 53 48 83 EC ? 48 8B D9 39 91 ? ? ? ? 74 ? 89 91");
            if (fnSetSeq) {
                if (MH_CreateHook(fnSetSeq, (LPVOID)h_CBaseAnimating_SetSequence,
                                  (LPVOID*)&o_CBaseAnimating_SetSequence) == MH_OK) {
                    MH_EnableHook(fnSetSeq);
                    g_setSeqHookInstalled = true;
                    DebugLog("skins: SetSequence hook installed at %p", (void*)fnSetSeq);
                } else {
                    DebugLog("skins: SetSequence MH_CreateHook FAILED at %p", (void*)fnSetSeq);
                }
            } else {
                DebugLog("skins: SetSequence pattern NOT FOUND");
            }
        }
    }

    // ---- IClientModeShared::DoPostScreenSpaceEffects hook (slot 39) ----
    // Runs the viewmodel glow pipeline at a safe point (not from draw hooks). g_clientModePtr
    // comes from the client.dll wait above; null there means the pattern is missing.
    if (!g_doPostHookInstalled && g_clientModePtr) {
        __try {
            void** vtbl = *(void***)g_clientModePtr;
            if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000 && vtbl[39]) {
                if (MH_CreateHook(vtbl[39], (LPVOID)h_DoPostSSE,
                                  (LPVOID*)&o_DoPostSSE) == MH_OK) {
                    MH_EnableHook(vtbl[39]);
                    g_doPostHookInstalled = true;
                    DebugLog("glow: DoPostScreenSpaceEffects hook installed (slot 39)");
                } else {
                    DebugLog("glow: DoPostScreenSpaceEffects hook FAILED");
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            DebugLog("glow: DoPostScreenSpaceEffects hook SEH");
        }
    }

    // ---- IClientModeShared::CreateMove hook (slot 21) -- Flip World control mirror ----
    // Same vtable as the DoPostScreenSpaceEffects hook above. Slot confirmed: counting the SDK
    // iclientmode.h vtable from the destructor lands DoPostScreenSpaceEffects at slot 39 (the
    // confirmed real-binary slot), which puts CreateMove at slot 21. Mirrors A/D + mouse yaw.
    {
        static bool s_flipCMInstalled = false;
        if (!s_flipCMInstalled && g_clientModePtr) {
            __try {
                void** vtbl = *(void***)g_clientModePtr;
                if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000 && vtbl[flipslot::cmCreateMove]) {
                    if (MH_CreateHook(vtbl[flipslot::cmCreateMove], (LPVOID)h_FlipCreateMove,
                                      (LPVOID*)&o_FlipCreateMove) == MH_OK) {
                        MH_EnableHook(vtbl[flipslot::cmCreateMove]);
                        s_flipCMInstalled = true;
                        DebugLog("flipworld: ClientModeShared::CreateMove hook installed (slot 21)");
                    } else {
                        DebugLog("flipworld: CreateMove hook FAILED");
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                DebugLog("flipworld: CreateMove hook SEH");
            }
        }
    }

    // ---- IVEngineClient::WorldToScreenMatrix hook (slot 36) -- Flip World HUD mirror ----
    // HUD elements pinned to a world point (damage numbers, on-screen callouts, target ids)
    // project through this matrix (sdk view_scene.cpp:96/111). Mirroring its x keeps them over
    // the mirrored world; the angle-placed ones are flipworld.cpp's HUD patches. Slot 36 = engine CEngineClient thunk `mov rcx,[g_EngineRenderer];
    // mov rax,[rcx]; jmp [rax+68h]`, asserted by tools/check_vtables.py.
    {
        static bool s_flipW2SInstalled = false;
        if (!s_flipW2SInstalled && g_engine) {
            __try {
                void** vtbl = *(void***)g_engine;
                if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000 && vtbl[flipslot::engWorldToScreenMatrix]) {
                    if (MH_CreateHook(vtbl[flipslot::engWorldToScreenMatrix], (LPVOID)h_FlipW2S, (LPVOID*)&o_FlipW2S) == MH_OK) {
                        MH_EnableHook(vtbl[flipslot::engWorldToScreenMatrix]);
                        s_flipW2SInstalled = true;
                        FlipWorld_Install();   // config writer hook + angle-placed HUD patch sites
                        DebugLog("flipworld: WorldToScreenMatrix hook installed (slot 36)");
                    } else {
                        DebugLog("flipworld: WorldToScreenMatrix hook FAILED");
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                DebugLog("flipworld: WorldToScreenMatrix hook SEH");
            }
        }
    }

    // ---- CViewRender::DrawViewModels hook ----------------------
    // Glow second pass (RT switch + model draw + composite) runs AFTER original here.
    // Pattern from Amalgam; function lives in client.dll.
    if (!g_drawViewModelsHookInstalled) {
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (hClient) {
            uint8_t* fn = PatternScan(hClient,
                "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 8B FA");
            if (fn) {
                if (MH_CreateHook(fn, (LPVOID)h_DrawViewModels,
                                  (LPVOID*)&o_DrawViewModels) == MH_OK) {
                    MH_EnableHook(fn);
                    g_drawViewModelsHookInstalled = true;
                    DebugLog("glow: DrawViewModels hook installed at %p", (void*)fn);
                } else {
                    DebugLog("glow: DrawViewModels hook FAILED at %p", (void*)fn);
                }
            } else {
                DebugLog("glow: DrawViewModels pattern NOT FOUND");
            }
        }
    }

    // ---- IViewRender pointer (for LevelInit hook) --------------
    // Pattern from Amalgam: "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 48 8B 03 48 8D 54 24"
    // Reads RIP-relative pointer at +3 (instr len 7) to get IViewRender**.
    if (!g_viewRender) {
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (hClient) {
            uint8_t* addr = PatternScan(hClient,
                "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B 03 48 8D 54 24");
            if (addr) {
                __try {
                    int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
                    void** ppView = reinterpret_cast<void**>(
                        reinterpret_cast<uintptr_t>(addr) + 7 + (uintptr_t)(uint32_t)rel);
                    g_viewRender = *ppView;
                } __except(EXCEPTION_EXECUTE_HANDLER) { g_viewRender = nullptr; }
                DebugLog("vmglow: IViewRender scan=%p ptr=%p", (void*)addr, g_viewRender);
            }
        }
    }

    // ---- CViewRender::LevelInit hook (vtable slot 1) -----------
    // Fires during map load. Creates glow RTs via
    // IMaterialSystem::CreateNamedRenderTargetTextureEx (slot 85; works inside or outside
    // a Begin/EndRenderTargetAllocation block)
    // so they're properly registered for device-reset recreation.
    if (!g_levelInitHookInstalled && g_viewRender) {
        __try {
            void** vtbl = *(void***)g_viewRender;
            if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000 && vtbl[1]) {
                if (MH_CreateHook(vtbl[1], (LPVOID)h_LevelInit, (LPVOID*)&o_LevelInit) == MH_OK) {
                    MH_EnableHook(vtbl[1]);
                    g_levelInitHookInstalled = true;
                    DebugLog("vmglow: LevelInit hook installed at %p", vtbl[1]);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            DebugLog("vmglow: LevelInit hook SEH");
        }
    }

    // Hook FindMaterial (slot 71) to redirect rain particle to snow sprite.
    if (g_matSystem && !o_FindMaterial)
    {
        void* pFindMat = (*(void***)g_matSystem)[71];
        if (pFindMat) {
            MH_CreateHook(pFindMat, (void*)h_FindMaterial, (void**)&o_FindMaterial);
            MH_EnableHook(pFindMat);
            DebugLog("weather: FindMaterial hook installed at 0x%IX", (uintptr_t)pFindMat);
        }
    }

    // ---- IStudioRender + prop modulation hooks -----------------
    TryInstallPropHooks();

    const char* entListNames[] = {"VClientEntityList003","VClientEntityList002",nullptr};
    for (int i = 0; entListNames[i] && !g_entityList; ++i)
        g_entityList = (IClientEntityList*)GetInterface("client.dll", entListNames[i]);

    // ---- Custom particle function pointers (mode 3) + footstep spell hooks ----
    {
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (hClient) {
            if (!g_fnDispatchEffect) {
                uint8_t* addr = PatternScan(hClient,
                    "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 48 8B FA 48 8D 4C 24");
                if (addr)
                    g_fnDispatchEffect = reinterpret_cast<CTE_DispatchEffect_t>(addr);
            }
            // ---- TF_IsHolidayActive hook (footstep spells) -------------
            if (!o_TF_IsHolidayActive) {
                uint8_t* addr = PatternScan(hClient,
                    "48 83 EC ? 48 8B 05 ? ? ? ? 44 8B C9");
                if (addr) {
                    if (MH_CreateHook((LPVOID)addr, (LPVOID)h_TF_IsHolidayActive,
                                      (LPVOID*)&o_TF_IsHolidayActive) == MH_OK)
                    {
                        MH_EnableHook((LPVOID)addr);
                        DebugLog("hwfoot: TF_IsHolidayActive hook installed at %p", (void*)addr);
                    }
                    else
                        DebugLog("hwfoot: TF_IsHolidayActive hook FAILED at %p", (void*)addr);
                }
                else
                    DebugLog("hwfoot: TF_IsHolidayActive pattern NOT FOUND");
            }
            if (!g_addrFireEvent_IsHolidayCall) {
                uint8_t* addr = PatternScan(hClient,
                    "84 C0 74 ? 45 33 C9 C6 44 24 ? ? 4C 8B C6");
                if (addr) {
                    g_addrFireEvent_IsHolidayCall = reinterpret_cast<uintptr_t>(addr);
                    DebugLog("hwfoot: FireEvent_IsHolidayCall site at %p", (void*)addr);
                }
                else
                    DebugLog("hwfoot: FireEvent_IsHolidayCall pattern NOT FOUND");
            }
            // ---- CAttributeManager::AttribHookInt hook (footstep color) ----
            if (!o_AttribHookInt) {
                uint8_t* addr = PatternScan(hClient,
                    "4C 8B DC 49 89 5B ? 49 89 6B ? 49 89 73 ? 57 41 54 41 55 41 56 41 57 48 83 EC ? 48 8B 3D ? ? ? ? 4C 8D 35");
                if (addr) {
                    if (MH_CreateHook((LPVOID)addr, (LPVOID)h_AttribHookInt,
                                      (LPVOID*)&o_AttribHookInt) == MH_OK)
                    {
                        MH_EnableHook((LPVOID)addr);
                        DebugLog("hwfoot: AttribHookInt hook installed at %p", (void*)addr);
                    }
                    else
                        DebugLog("hwfoot: AttribHookInt hook FAILED at %p", (void*)addr);
                }
                else
                    DebugLog("hwfoot: AttribHookInt pattern NOT FOUND");
            }
            if (!g_addrFireEvent_AttribHookCall) {
                uint8_t* addr = PatternScan(hClient, "8B F8 83 BE");
                if (addr) {
                    g_addrFireEvent_AttribHookCall = reinterpret_cast<uintptr_t>(addr);
                    DebugLog("hwfoot: FireEvent_AttribHookCall site at %p", (void*)addr);
                }
                else
                    DebugLog("hwfoot: FireEvent_AttribHookCall pattern NOT FOUND");
            }
            if (!g_fnGetParticleIndex) {
                uint8_t* addr = PatternScan(hClient,
                    "40 53 48 83 EC ? 48 8B D9 48 85 C9 74 ? 48 8B 0D ? ? ? ? 48 8B D3 48 8B 01 FF 50 ? 3D");
                if (addr) {
                    g_fnGetParticleIndex = reinterpret_cast<Get_ParticleSystemIndex_t>(addr);
                    DebugLog("weather: Get_ParticleSystemIndex=%p", (void*)addr);
                }
            }
            if (!o_CParticlePropCreate) {
                uint8_t* addr = PatternScan(hClient,
                    "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B 59 ? 49 8B F1");
                if (addr) {
                    if (MH_CreateHook((LPVOID)addr, (LPVOID)h_CParticlePropCreate,
                                      (LPVOID*)&o_CParticlePropCreate) == MH_OK)
                    {
                        MH_EnableHook((LPVOID)addr);
                        DebugLog("particles: CParticleProperty::Create hook installed at %p", (void*)addr);
                    }
                    else
                        DebugLog("particles: CParticleProperty::Create hook FAILED at %p", (void*)addr);
                }
                else
                    DebugLog("particles: CParticleProperty::Create pattern NOT FOUND");
            }
            if (!o_CParticlePropCreatePt) {
                uint8_t* addr = PatternScan(hClient, "44 89 4C 24 ? 44 89 44 24 ? 53");
                if (addr) {
                    if (MH_CreateHook((LPVOID)addr, (LPVOID)h_CParticlePropCreatePt,
                                      (LPVOID*)&o_CParticlePropCreatePt) == MH_OK)
                    {
                        MH_EnableHook((LPVOID)addr);
                        DebugLog("particles: CParticleProperty::Create(pt) hook installed at %p", (void*)addr);
                    }
                    else
                        DebugLog("particles: CParticleProperty::Create(pt) hook FAILED at %p", (void*)addr);
                }
                else
                    DebugLog("particles: CParticleProperty::Create(pt) pattern NOT FOUND");
            }
            // ---- RagdollEffects ----------------------------------------
            if (!o_CreateTFRagdoll) {
                uint8_t* addr = PatternScan(hClient,
                    "48 89 4C 24 ? 55 53 56 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 8B 91");
                if (addr) {
                    if (MH_CreateHook((LPVOID)addr, (LPVOID)h_CreateTFRagdoll,
                                      (LPVOID*)&o_CreateTFRagdoll) == MH_OK)
                    {
                        MH_EnableHook((LPVOID)addr);
                        DebugLog("ragdollFX: CreateTFRagdoll hook installed at %p", (void*)addr);
                    }
                    else
                        DebugLog("ragdollFX: CreateTFRagdoll hook FAILED at %p", (void*)addr);
                }
                else
                    DebugLog("ragdollFX: CreateTFRagdoll pattern NOT FOUND");
            }
            // ---- Static amby crosshair ---------------------------------
            if (!o_CTFRevolver_GetWeaponCrosshairScale) {
                uint8_t* addr = PatternScan(hClient,
                    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F2 48 8B F9 E8 ? ? ? ? 48 8B D8 48 85 C0 0F 84");
                if (addr) {
                    if (MH_CreateHook((LPVOID)addr,
                                      (LPVOID)h_CTFRevolver_GetWeaponCrosshairScale,
                                      (LPVOID*)&o_CTFRevolver_GetWeaponCrosshairScale) == MH_OK)
                    {
                        MH_EnableHook((LPVOID)addr);
                        DebugLog("amby: CTFRevolver_GetWeaponCrosshairScale hook installed at %p", (void*)addr);
                    }
                    else
                        DebugLog("amby: CTFRevolver_GetWeaponCrosshairScale hook FAILED at %p", (void*)addr);
                }
                else
                    DebugLog("amby: CTFRevolver_GetWeaponCrosshairScale pattern NOT FOUND");
            }
        }
    }

    ResolveSheenSigs();

    // Install equip region hook now if it was enabled in the loaded config
    if (g_enableEquipRegionUnlock)
        InstallEquipRegionHook();

    ScoreboardHook_Install();

    // ---- IGameEventManager2 -- FireEvent hook (killfeed filter) ----
    if (!g_gameEventManager)
    {
        HMODULE hEngine = GetModuleHandleA("engine.dll");
        if (hEngine)
        {
            using CI_t = void*(*)(const char*, int*);
            auto ci = (CI_t)GetProcAddress(hEngine, "CreateInterface");
            if (ci)
            {
                const char* gemNames[] = {"GAMEEVENTSMANAGER002","GAMEEVENTSMANAGER001",nullptr};
                for (int i = 0; gemNames[i] && !g_gameEventManager; ++i)
                    g_gameEventManager = ci(gemNames[i], nullptr);
            }
        }
    }
    if (g_gameEventManager && !o_FireEvent)
    {
        __try
        {
            void** vtbl = *(void***)g_gameEventManager;
            if (vtbl && vtbl[8] && !o_FireEvent)
            {
                if (MH_CreateHook(vtbl[8], (LPVOID)h_FireEvent,
                                  (LPVOID*)&o_FireEvent) == MH_OK)
                {
                    MH_EnableHook(vtbl[8]);
                    DebugLog("killfeed: FireEventClientSide hook installed (slot 8)");
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            DebugLog("killfeed: SEH during hook install");
        }
    }

    {
        HMODULE client = GetModuleHandleA("client.dll");
        HMODULE engine = GetModuleHandleA("engine.dll");
        if (client && engine)
        {
            static void* g_clientDLL2 = nullptr;
            if (!g_clientDLL2)
            {
                using CI_t = void*(*)(const char*, int*);
                auto ci = (CI_t)GetProcAddress(client, "CreateInterface");
                if (ci) {
                    const char* names[] = {"VClient018","VClient017","VClient016",nullptr};
                    for (int i = 0; names[i] && !g_clientDLL2; ++i)
                        g_clientDLL2 = ci(names[i], nullptr);
                }
            }

            // Level boundaries: IBaseClientDLL 6 LevelInitPostEntity, 7 LevelShutdown.
            if (g_clientDLL2 && !o_LevelShutdown) {
                __try {
                    void** vtbl = *(void***)g_clientDLL2;
                    if (MH_CreateHook(vtbl[6], (LPVOID)h_LevelInitPostEntity, (LPVOID*)&o_LevelInitPostEntity) == MH_OK)
                        MH_EnableHook(vtbl[6]);
                    if (MH_CreateHook(vtbl[7], (LPVOID)h_LevelShutdown, (LPVOID*)&o_LevelShutdown) == MH_OK)
                        MH_EnableHook(vtbl[7]);
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                DebugLog("level hooks: init=%d shutdown=%d", o_LevelInitPostEntity != nullptr, o_LevelShutdown != nullptr);
            }

            uint8_t* fnFSN_engine = PatternScan(engine, "4C 8B DC 56 48 83 EC");
            if (fnFSN_engine)
            {
                MH_CreateHook(fnFSN_engine, (LPVOID)h_FrameStageNotify, (LPVOID*)&o_FrameStageNotify);
                MH_EnableHook(fnFSN_engine);
            }
            // Scan for the FSN(3) call site inside CL_ProcessPacketEntities.
            // The return address (call_addr + 5) is what _ReturnAddress() yields
            // when our hooked FSN is invoked from that specific CALL instruction.
            {
                uint8_t* fsnCall = PatternScan(engine,
                    "E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 44 8B F0");
                if (fsnCall) {
                    g_skinPreWriteRetAddr = reinterpret_cast<uintptr_t>(fsnCall) + 5;
                    DebugLog("FSN: skinPreWriteRetAddr = 0x%IX (call at 0x%IX)",
                             g_skinPreWriteRetAddr, (uintptr_t)fsnCall);
                } else {
                    DebugLog("FSN: CL_ProcessPacketEntities FSN call pattern NOT FOUND");
                }
            }
        }
    }

    {
        HMODULE client = GetModuleHandleA("client.dll");
        if (client)
        {
            auto addr = PatternScan(client, "48 89 5C 24 ? 56 48 83 EC ? 48 63 F2");
            if (addr)
            {
                MH_CreateHook(addr, (LPVOID)h_GetPlayerName, (LPVOID*)&o_GetPlayerName);
                MH_EnableHook(addr);
            }
        }
    }

    {
        auto addr = PatternScan(GetModuleHandleA("engine.dll"),
            "40 56 48 83 EC ? 83 3D ? ? ? ? ? 48 8B F1 0F 8E");
        if (addr)
        {
            MH_CreateHook((LPVOID)addr, (LPVOID)h_CL_CheckForPureServerWhitelist,
                          (LPVOID*)&o_CL_CheckForPureServerWhitelist);
            MH_EnableHook((LPVOID)addr);
        }
    }

    {
        HMODULE client = GetModuleHandleA("client.dll");
        if (client)
        {
            const char* sheenPatterns[] = {
                "48 89 54 24 ? 55 57 41 54 48 8D 6C 24",
                "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 49",
                "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 4C 8B EA",
                nullptr
            };
            uint8_t* addr = nullptr;
            for (int pi = 0; sheenPatterns[pi]; ++pi) {
                addr = PatternScan(client, sheenPatterns[pi]);
                if (addr) break;
            }
            if (addr)
            {
                MH_CreateHook((LPVOID)addr, (LPVOID)h_SheenOnBind, (LPVOID*)&o_SheenOnBind);
                MH_EnableHook((LPVOID)addr);
            }
        }
    }

    if (g_cvar && !g_cv_sv_skyname)
        g_cv_sv_skyname = g_cvar->FindVarWalk("sv_skyname");

    {
        HMODULE client = GetModuleHandleA("client.dll");
        if (client)
        {
            const char* vmvPatterns[] = {
                "48 89 74 24 ? 55 41 56 41 57 48 8D AC 24",
                "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 49 8B F0 48 8B EA 48 8B F9 33 DB",
                nullptr
            };
            uint8_t* addr = nullptr;
            for (int pi = 0; vmvPatterns[pi]; ++pi) {
                addr = PatternScan(client, vmvPatterns[pi]);
                if (addr) break;
            }
            if (addr) {
                MH_CreateHook((LPVOID)addr, (LPVOID)h_CalcViewModelView, (LPVOID*)&o_CalcViewModelView);
                MH_EnableHook((LPVOID)addr);
            }
            VmCsgo_Install(client);   // CalcViewModelBobHelper -- CS:GO viewmodel bob
            ScopeCsgo_Install(client); // CHudScope::Paint + sniper dot -- CS:GO scope
        }
    }

    DebugLog("poll_thread: hooks installed");
    ConsoleColorPrint(0, 255, 255, "infovisual on\n");
    NetGraph_Install();

    g_fullyInjected.store(true);
    SetPhase("poll_done");

    // No loop here on purpose. This thread only does the one-shot interface
    // resolution and hook install; anything periodic has to touch engine state,
    // which is only safe from the render thread, so it lives in h_Present.
}

void ResetFogCvars()
{
    if (!g_cvar) return;
    auto RestorePtr = [](const char* name, char** origPtr, int* origLen) {
        if (!*origPtr) return;
        ConVar* cv = g_cvar->FindVarWalk(name);
        if (!cv) return;
        __try {
            uintptr_t base   = reinterpret_cast<uintptr_t>(cv);
            uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
            if (parent < 0x10000 || parent > 0x7FFFFFFFFFFF) parent = base;
            *reinterpret_cast<char**>(parent + 0x48) = *origPtr;
            *reinterpret_cast<int*>  (parent + 0x50) = *origLen;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        *origPtr = nullptr;
    };
    RestorePtr("fog_color",       &s_fogColorOrigPtr,    &s_fogColorOrigLen);
    RestorePtr("fog_colorskybox", &s_fogColorSkyOrigPtr, &s_fogColorSkyOrigLen);
    VisualsCvars_Release();
    ScopeCsgo_Restore();   // TF's scope back
    FlipWorld_Release();
}

// ---- Main thread -------------------------------------------
static void MainThread(HMODULE hMod) {
    for (int i = 0; i < 60 && !g_eject; ++i) {
        if (GetModuleHandleW(L"d3d9.dll") && GetModuleHandleW(L"engine.dll")) break;
        Sleep(500);
    }
    Sleep(1000);

    void* vt[100]{};
    for (int i = 0; i < 30 && !g_eject; ++i) {
        if (GetDX9VTable(vt, 100) && vt[17]) break;
        Sleep(500);
    }
    if (!vt[17] || g_eject) { ExitThread(0); return; }

    InitializeCriticalSectionAndSpinCount(&g_attrLock, 1000);
    g_attrLockInit = true;

    Jobs::Start();
    SkinCfgs_Init();   // before any hook: the skin config map must not grow once they run
    MH_Initialize();

    if (HMODULE hUser32 = GetModuleHandleA("user32.dll"))
    {
        void* pSCP = GetProcAddress(hUser32, "SetCursorPos");
        if (pSCP) MH_CreateHook(pSCP, h_SetCursorPos, (void**)&o_SetCursorPos);
    }

    MH_CreateHook(vt[17], h_Present, (void**)&o_Present);
    MH_CreateHook(vt[16], h_Reset,   (void**)&o_Reset);
    MH_EnableHook(MH_ALL_HOOKS);

    _mkdir(CONFIG_DIR);
    DeleteFileA("C:\\infovisual\\heartbeat.txt");
    {
        std::ifstream test(CONFIG_PATH);
        if (!test) SaveConfig();
    }
    LoadConfig();

    s_prevExFilter = SetUnhandledExceptionFilter(UnhandledExFilter);
    DebugLog("infovisual loaded, hooks installed");
    SetPhase("main_wait");

    g_hWatchdogExitEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_hWatchdogThread = CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr);

    g_hPollThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        PollThread();
        return 0;
    }, nullptr, 0, nullptr);

    while (!g_eject) {
        if (GetAsyncKeyState(VK_F11) & 1) g_eject.store(true);
        Sleep(50);
    }
    DebugLog("eject triggered, phase=\"%s\"", g_phase);
    ConsoleColorPrint(0, 255, 255, "infovisual off\n");

    // Eject runs a long teardown and a hang anywhere in it looks identical from
    // outside, so each step logs where it got to. The last line in debug.log is
    // the step that didn't finish.
    int probeN = 0;
    auto probe = [&](const char* label) { DebugLog("eject %02d: %s", ++probeN, label); };

    probe("eject_start");

    g_run.store(false);
    if (g_hWatchdogExitEvent) SetEvent(g_hWatchdogExitEvent);
    probe("run_cleared");

    // Disable all features first so hooks that are still mid-flight finish their
    // current call cleanly and don't start new work.
    g_enableDRColor       = false;
    g_enableCritColor     = false;
    g_enableVmWeaponChams = false;
    g_enableVmArmsChams   = false;
    g_enablePlChams       = false;
    g_enableModulate      = false;
    g_weatherEnabled      = false;
    g_enableMediaPlayer   = false;

    // Stop media player thread (may already be stopping after a switch-off)
    g_mpRunning = false;
    if (HANDLE h = InterlockedExchangePointer(&g_mpThread, nullptr)) { WaitForSingleObject(h, 3000); CloseHandle(h); }
    // Stop waveform capture thread
    if (g_wfRunning) {
        g_wfRunning = false;
        if (g_wfThread) { WaitForSingleObject(g_wfThread, 3000); CloseHandle(g_wfThread); g_wfThread = nullptr; }
        for (int i = 0; i < 5; i++) g_wfBands[i] = 0.f;
    }

    // Restore the world BEFORE the hooks come off, and do it on the game
    // thread: MatRefresh and DecrementReferenceCount free shader snapshots and
    // proxies the game thread is drawing right now. Doing that from here is
    // what crashed inside stdshader_dx8 (no infovisual frame in the stack).
    // h_FrameStageNotify(FRAME_RENDER_START) runs the pass and flips the state
    // to 3; if it never gets there (not in game, queue mode stuck, engine
    // frozen), we just skip the material work rather than race it.
    g_ejectRestoreState.store(1);
    for (int i = 0; i < 300 && g_ejectRestoreState.load() != 3; ++i) Sleep(10);
    if (g_ejectRestoreState.load() != 3)
        DebugLog("eject restore: timed out, world materials left as-is");
    probe("world_restored");

    // Disable hooks so no new calls enter our code.
    MH_DisableHook(MH_ALL_HOOKS);
    probe("hooks_disabled_early");

    // Give MinHook's trampoline a moment to fully unwind from any currently
    // executing hook invocations before we start touching shared data.
    // Without this, h_FrameStageNotify or h_Present can be mid-execution
    // accessing g_entityList/g_matSystem right as we null them below.
    Sleep(50);

    // Drain all hook depth counters -- wait for every in-flight hook call to
    // finish.  Do this BEFORE touching shared data (entityList, matSystem, etc.)
    // so we don't pull the rug out from under a running hook.
    for (int i = 0; i < 300 && g_hookDepth.load()    > 0; ++i) Sleep(10);
    for (int i = 0; i < 300 && g_fsnDepth.load()     > 0; ++i) Sleep(10);
    for (int i = 0; i < 300 && g_miscHookDepth.load() > 0; ++i) Sleep(10);
    probe("hooks_drained");
    FlipWorld_Unload();   // flip world's owner token must not outlive this DLL

    Jobs::Stop();
    Font_DropPending();   // a font atlas a worker finished but Present never swapped in
    probe("jobs_stopped");

    // Now safe to join background threads -- they check g_run and g_eject.
    if (g_hPollThread) {
        WaitForSingleObject(g_hPollThread, 2000);
        CloseHandle(g_hPollThread);
        g_hPollThread = nullptr;
    }
    probe("poll_joined");

    if (g_hWatchdogThread) {
        WaitForSingleObject(g_hWatchdogThread, 2000);
        CloseHandle(g_hWatchdogThread);
        g_hWatchdogThread = nullptr;
    }
    if (g_hWatchdogExitEvent) {
        CloseHandle(g_hWatchdogExitEvent);
        g_hWatchdogExitEvent = nullptr;
    }
    probe("watchdog_joined");

    // Now that all hooks are drained and threads are stopped, it's safe to
    // reset shared state. No engine thread can be touching this data anymore.

    // Precip teardown + the world material restore ran on the game thread above
    // (EjectRestore_Service) whenever a map was live. Only fall back to doing
    // it here when that pass never ran -- out of the game nothing is drawing
    // our cached materials, so dropping the refs from this thread is fine, but
    // in game it is the crash we just fixed.
    if (!g_ejectRestoreDidWork.load()) {
        if (g_entityList && (g_precipEntity || g_precipNetworkable)) {
            constexpr int kSlot = 2047;
            Precip_TearDown(kSlot);
        }
        ResetMatCache(false);   // drop our pointers, do NOT Refresh live materials
        GlowVM_LevelUnload();   // glow + flip RT/material refs, no map drawing them now
        FlipWorld_LevelUnload();
    }
    ResetFogCvars();
    ResetFSNState();
    probe("state_reset");

    // Do NOT null FakeSheenIndexVar vtable.
    // If a material bind is mid-flight (or some engine code temporarily holds a
    // pointer to our fake var), nulling the vtable turns a benign stray call
    // into an immediate call-through-null crash.
    probe("sheen_vtable_kept");

    NetGraph_Shutdown();
    MH_Uninitialize();
    if (g_attrLockInit) {
        DeleteCriticalSection(&g_attrLock);
        g_attrLockInit = false;
    }
    probe("mh_uninit");

    ScreenFx_RestoreQueueMode();   // hand mat_queue_mode back to whatever the user had
    probe("queuemode_restored");

    if (g_showMenu) ShowCursor(FALSE);
    ClipCursor(nullptr);
    probe("cursor_restored");

    Render_ReleaseAllTextures();
    probe("textures_released");

    if (g_imguiInit)
    {
        probe("imgui_shutdown_begin");
        if (g_origWndProc && g_hwnd)
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
        probe("wndproc_restored");
        ImGui_ImplDX9_Shutdown();
        probe("imgui_dx9_shutdown");
        ImGui_ImplWin32_Shutdown();
        probe("imgui_win32_shutdown");
        ImGui::DestroyContext();
        probe("imgui_ctx_destroyed");
    }

    WriteEjectLog("IMGUI_SHUTDOWN");
    probe("ejectlog_written");

    // Remove the VEH while the DLL is still fully loaded and g_hVEH is valid.
    g_crashData->active = 0;
    if (g_hVEH) { RemoveVectoredExceptionHandler(g_hVEH); g_hVEH = nullptr; }
    SetUnhandledExceptionFilter(s_prevExFilter);
    probe("veh_removed");

    // Soft eject: hooks/threads/features are disabled, but we do NOT unload the DLL.
    // This avoids crashes from any engine-owned stale pointers into our module.
    ExitThread(0);
}

// ---- DllMain -----------------------------------------------
BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        g_hSelf = hMod;
        InstallPersistentCrashHandler();
        HANDLE t = CreateThread(nullptr, 0,
            [](LPVOID p) -> DWORD { MainThread((HMODULE)p); return 0; },
            hMod, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
