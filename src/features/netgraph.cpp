#include "netgraph.h"

// CNetGraphPanel::DrawTextFields (vgui_netgraphpanel.cpp) -> call [rax+510h], 0x510/8 = slot 162.
// Swap only known netgraph triples: normal 229/229/178, orange 255/125/31 (bad lerp),
// red 255/31/31 (sv fps<10), yellow 255/255/31 + 255/255/0 (same tier, one picker).
static constexpr int k_drawColoredTextSlot = 162;
static constexpr int k_graphR  = 229, k_graphG  = 229, k_graphB  = 178;
static constexpr int k_orangeR = 255, k_orangeG = 125, k_orangeB = 31;
static constexpr int k_redR    = 255, k_redG    = 31,  k_redB    = 31;
static constexpr int k_yel1R   = 255, k_yel1G   = 255, k_yel1B   = 31;
static constexpr int k_yel2R   = 255, k_yel2G   = 255, k_yel2B   = 0;

// IMatSystemSurface.h: DrawColoredText returns int, not void.
using DrawColoredText_t = int(*)(void*, unsigned int, int, int, int, int, int, int, const char*, ...);
static DrawColoredText_t o_DrawColoredText = nullptr;

// Varargs can't forward -> format here, pass "%s". Costs _vsnprintf on EVERY colored
// VGUI string, so toggle off = hook disabled (NetGraph_Tick), never run-and-skip.
static int h_DrawColoredText(void* thisptr, unsigned int font, int x, int y,
                             int r, int g, int b, int a, const char* fmt, ...)
{
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt ? fmt : "", ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;

    if (g_ngColorEnable) {
        const float* pick = nullptr;
        if      (r == k_graphR  && g == k_graphG  && b == k_graphB)  pick = g_ngTextColor;
        else if (r == k_orangeR && g == k_orangeG && b == k_orangeB) pick = g_ngOrangeColor;
        else if (r == k_redR    && g == k_redG    && b == k_redB)    pick = g_ngRedColor;
        else if (r == k_yel1R   && g == k_yel1G   && b == k_yel1B)   pick = g_ngYellowColor;
        else if (r == k_yel2R   && g == k_yel2G   && b == k_yel2B)   pick = g_ngYellowColor;
        if (pick) {
            r = (int)(pick[0] * 255.f);
            g = (int)(pick[1] * 255.f);
            b = (int)(pick[2] * 255.f);
            a = (int)(pick[3] * 255.f);
        }
    }

    return o_DrawColoredText(thisptr, font, x, y, r, g, b, a, "%s", buf);
}

// CreateInterface("MatSystemSurface008") null on this build -> read client.dll's
// g_pMatSystemSurface at DrawTextFields: mov rcx,[rip+X] ... mov edi,0E5h (GRAPH_RED).
// Sig unique in tf/bin/x64/client.dll. Scan once; 2 s retries only re-read ptr.
static void* FindMatSystemSurface()
{
    static uint8_t* s_sig     = nullptr;
    static bool     s_scanned = false;
    if (!s_scanned) {
        if (HMODULE hClient = GetModuleHandleA("client.dll")) {
            s_scanned = true;
            s_sig = PatternScan(hClient,
                "48 8B 0D ? ? ? ? 48 8D 55 ? 48 89 54 24 ? BF E5 00 00 00");
            if (!s_sig) DebugLog("netgraph: g_pMatSystemSurface sig NOT FOUND");
        }
    }
    if (uint8_t* a = s_sig) {
        __try {
            void* surf = *reinterpret_cast<void**>(a + 7 + *reinterpret_cast<int32_t*>(a + 3));
            if (reinterpret_cast<uintptr_t>(surf) >= 0x10000) {
                DebugLog("netgraph: g_pMatSystemSurface=%p (client.dll sig)", surf);
                return surf;
            }
            DebugLog("netgraph: g_pMatSystemSurface not set yet");
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            DebugLog("netgraph: SEH reading g_pMatSystemSurface");
        }
    }

    // Fallback: CMatSystemSurface has one vtable (IMatSystemSurface : vgui::ISurface) -> same slot.
    const char* names[] = { "MatSystemSurface008", "MatSystemSurface007",
                            "VGUI_Surface031", "VGUI_Surface030", nullptr };
    for (int i = 0; names[i]; ++i) {
        if (void* s = GetInterface("vguimatsurface.dll", names[i])) {
            DebugLog("netgraph: surface via %s", names[i]);
            return s;
        }
    }
    return nullptr;
}

static void* s_hookAddr = nullptr;
static bool  s_hookOn   = false;

void NetGraph_Install()
{
    if (o_DrawColoredText) return;

    void* surf = FindMatSystemSurface();
    if (!surf) { DebugLog("netgraph: surface NOT found"); return; }

    __try {
        void** vt = *(void***)surf;
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000 && vt[k_drawColoredTextSlot]) {
            MH_STATUS st = MH_CreateHook(vt[k_drawColoredTextSlot], (LPVOID)h_DrawColoredText,
                                         (LPVOID*)&o_DrawColoredText);
            if (st == MH_OK) {
                // created NOT enabled; NetGraph_Tick arms it
                s_hookAddr = vt[k_drawColoredTextSlot];
                DebugLog("netgraph: DrawColoredText hook created (slot 162) fn=%p",
                         vt[k_drawColoredTextSlot]);
            }
            else DebugLog("netgraph: MH_CreateHook failed st=%d", (int)st);
        }
        else DebugLog("netgraph: bad vtable/slot vt=%p", (void*)vt);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("netgraph: SEH during DrawColoredText install");
    }
}

void NetGraph_Tick()
{
    if (!s_hookAddr) {
        // surface ptr live only after vguimatsurface init, may postdate poll thread pass -> retry
        static DWORD s_nextRetry = 0;
        if (g_ngColorEnable && !g_eject) {
            DWORD now = GetTickCount();
            if (now >= s_nextRetry) { s_nextRetry = now + 2000; NetGraph_Install(); }
        }
        return;
    }

    const bool want = g_ngColorEnable && !g_eject;
    if (want == s_hookOn) return;
    if (want) MH_EnableHook((LPVOID)s_hookAddr);
    else      MH_DisableHook((LPVOID)s_hookAddr);
    s_hookOn = want;
    DebugLog("netgraph: recolor hook %s", want ? "enabled" : "disabled");
}

void NetGraph_Shutdown()
{
    if (!s_hookAddr) return;
    if (s_hookOn) { MH_DisableHook((LPVOID)s_hookAddr); s_hookOn = false; }
    MH_RemoveHook((LPVOID)s_hookAddr);
    s_hookAddr        = nullptr;
    o_DrawColoredText = nullptr;
}
