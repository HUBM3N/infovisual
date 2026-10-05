#include "globals.h"
#include <tlhelp32.h>

PersistCrashData* g_crashData = nullptr;
HANDLE             g_hVEH     = nullptr;

// ---- Crash handler -----------------------------------------
// VEH is removed on eject (thread.cpp). VirtualAlloc page holds PersistCrashData.

// ---- First-chance fault throttle ---------------------------
// This VEH sees EVERY exception in the process -- including the first-chance
// access violations our own SEH-guarded probes raise on purpose (stale
// IMaterial*, half-built entities, pattern scans over unmapped pages).
//
// The old handler answered each one with a Toolhelp module snapshot (~200
// WriteFile calls), SymInitialize(invade=TRUE) (loads symbols for every module
// in the process), a 32-frame StackWalk64 and a FlushFileBuffers: 100 ms+ per
// fault. A map load raises those faults in bursts, so joining a server sat on
// the loading screen for a minute.
//
// Now a repeat fault address costs one interlocked increment. A new address
// writes a dump built with RtlVirtualUnwind (no dbghelp, no symbol load) into
// one buffer and one WriteFile. The module list is written once, in the first
// dump. crash.log holds every distinct fault of this session, appended (it
// used to be overwritten, which lost the module list and all but the newest
// stack); the previous session's file is kept as crash_prev.log.
std::atomic<uint32_t>  g_vehFaultCount{0};   // total first-chance faults seen
std::atomic<uintptr_t> g_vehLastAddr {0};    // newest fault address
std::atomic<uint32_t>  g_vehDumpCount{0};    // dumps actually written

static const int     kSeenMax = 64;
static uintptr_t     s_seenAddr[kSeenMax];
static volatile LONG s_seenHits[kSeenMax];
static volatile LONG s_seenCount    = 0;
static volatile LONG s_lastDumpTick = 0;

// True when this fault address has not been dumped yet.
static bool VehFirstSight(uintptr_t addr)
{
    LONG n = s_seenCount;
    if (n > kSeenMax) n = kSeenMax;
    for (LONG i = 0; i < n; ++i) {
        if (s_seenAddr[i] == addr) { InterlockedIncrement(&s_seenHits[i]); return false; }
    }
    LONG slot = InterlockedIncrement(&s_seenCount) - 1;
    if (slot >= kSeenMax) {
        // Table full -- still dump, but no more than one every 5 s.
        LONG now  = (LONG)GetTickCount();
        LONG last = s_lastDumpTick;
        if ((now - last) < 5000) return false;
        InterlockedExchange(&s_lastDumpTick, now);
        return true;
    }
    s_seenAddr[slot] = addr;
    s_seenHits[slot] = 1;
    return true;
}

// Walk the faulting context with the x64 unwind tables. dbghelp is not used:
// the dump prints module + offset, which needs no symbols, and SymInitialize
// was the single most expensive call in the old fault path.
__declspec(noinline) static int VehCaptureStack(CONTEXT* src, uintptr_t* out, int maxFrames)
{
    int n = 0;
    __try {
        CONTEXT c = *src;
        UNWIND_HISTORY_TABLE hist;
        memset(&hist, 0, sizeof(hist));
        while (n < maxFrames && c.Rip) {
            out[n++] = (uintptr_t)c.Rip;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(c.Rip, &imageBase, &hist);
            if (!rf) {
                // Leaf frame -- return address sits on top of the stack.
                if (!c.Rsp) break;
                c.Rip  = *reinterpret_cast<DWORD64*>(c.Rsp);
                c.Rsp += 8;
                continue;
            }
            PVOID   handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, rf, &c,
                             &handlerData, &establisher, nullptr);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// "C:\infovisual" + "C:\infovisual\crash.log" from logPath, which holds the
// config file path (e.g. "C:\infovisual\default.cfg"). Both buffers MAX_PATH.
static void CrashLogPaths(char* crashDir, char* crashPath)
{
    strcpy(crashDir,  "C:\\infovisual");
    strcpy(crashPath, "C:\\infovisual\\crash.log");
    const char* src   = g_crashData->logPath;
    const char* slash = nullptr;
    for (const char* p = src; *p; ++p)
        if (*p == '\\' || *p == '/') slash = p;
    if (slash && (size_t)(slash - src) + sizeof("\\crash.log") <= MAX_PATH)
    {
        size_t dirLen = (size_t)(slash - src);
        memcpy(crashDir, src, dirLen);
        crashDir[dirLen] = '\0';
        memcpy(crashPath, src, dirLen);
        memcpy(crashPath + dirLen, "\\crash.log", sizeof("\\crash.log"));
    }
}

static LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    // Skip C++ exceptions, breakpoints, single-step, and debug print events.
    if (code == 0xE06D7363 || code == EXCEPTION_BREAKPOINT ||
        code == EXCEPTION_SINGLE_STEP ||
        code == 0x40010006 || code == 0x4001000A)
        return EXCEPTION_CONTINUE_SEARCH;

    // Only genuine fatal CPU/hardware faults carry STATUS_SEVERITY_ERROR
    // (0xCxxxxxxx). Everything else is first-chance software-exception control
    // flow the raising module handles itself -- notably RPC codes raised
    // constantly on steamclient/RPCRT4/wlanapi threads.
    if ((code & 0xC0000000) != 0xC0000000)
        return EXCEPTION_CONTINUE_SEARCH;

    if (!g_crashData) return EXCEPTION_CONTINUE_SEARCH;
    if (!g_crashData->active) return EXCEPTION_CONTINUE_SEARCH;

    void*     fault = ep->ExceptionRecord->ExceptionAddress;
    uintptr_t addr  = (uintptr_t)fault;

    g_vehFaultCount.fetch_add(1, std::memory_order_relaxed);
    g_vehLastAddr.store(addr, std::memory_order_relaxed);

    // Site already dumped -- counted, nothing else. This is the path a map load
    // takes thousands of times.
    if (!VehFirstSight(addr)) return EXCEPTION_CONTINUE_SEARCH;

    // Re-entrancy guard only -- reset on exit, so the handler stays armed for
    // the whole session.
    if (InterlockedExchange(&g_crashData->writing, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    char crashDir[MAX_PATH], crashPath[MAX_PATH];
    CrashLogPaths(crashDir, crashPath);
    CreateDirectoryA(crashDir, nullptr);

    // One static buffer, one WriteFile -- guarded by ->writing above.
    static const size_t kBufSize = 64 * 1024;
    static char s_buf[kBufSize];
    size_t used = 0;

    auto Append = [&](const char* fmt, ...) {
        if (used + 1024 >= kBufSize) return;
        va_list ap; va_start(ap, fmt);
        int n = _vsnprintf(s_buf + used, kBufSize - used - 1, fmt, ap);
        va_end(ap);
        if (n > 0) used += (size_t)n;
    };

    SYSTEMTIME st;
    GetSystemTime(&st);
    Append("\n=== FAULT @ %04d-%02d-%02d %02d:%02d:%02d UTC ===\n",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    if (g_crashData->ejectNote[0])
        Append("Eject context: %s\n", g_crashData->ejectNote);

    Append("Code:    0x%08X\n", (unsigned)code);
    Append("Address: 0x%IX\n",  addr);
    // What the faulting instruction was reaching for. Without it a vtable call that faults
    // reads the same as a null this, and the two have different causes.
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == 0xC0000006) &&
        ep->ExceptionRecord->NumberParameters >= 2)
    {
        const ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
        Append("Access:  %s 0x%IX\n",
               op == 0 ? "read" : (op == 1 ? "write" : "execute"),
               (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    if (const CONTEXT* c = ep->ContextRecord)
        Append("Regs:    rax=0x%IX rcx=0x%IX rdx=0x%IX r8=0x%IX rsp=0x%IX rbp=0x%IX\n",
               (uintptr_t)c->Rax, (uintptr_t)c->Rcx, (uintptr_t)c->Rdx,
               (uintptr_t)c->R8,  (uintptr_t)c->Rsp, (uintptr_t)c->Rbp);
    Append("Thread:  %u\n",     (unsigned)GetCurrentThreadId());
    Append("Phase:   \"%s\"\n", g_phase);
    Append("Faults this session: %u, distinct sites: %d\n",
           g_vehFaultCount.load(), (int)s_seenCount);

    HMODULE hFaultMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)fault, &hFaultMod) && hFaultMod)
    {
        char modName[MAX_PATH] = {};
        GetModuleFileNameA(hFaultMod, modName, MAX_PATH);
        Append("Module:  %s + 0x%IX\n", modName, addr - (uintptr_t)hFaultMod);
    }
    else
    {
        Append("Module:  <unknown>\n");
    }

    // Module list is only useful once, and Module32Next over ~200 modules is
    // the second most expensive thing this handler can do.
    static bool s_modulesWritten = false;
    if (!s_modulesWritten)
    {
        s_modulesWritten = true;
        Append("\nLoaded modules:\n");
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
        if (hSnap != INVALID_HANDLE_VALUE)
        {
            MODULEENTRY32 me{}; me.dwSize = sizeof(me);
            if (Module32First(hSnap, &me)) {
                do {
                    Append("  0x%IX  %s\n", (uintptr_t)me.modBaseAddr, me.szModule);
                } while (Module32Next(hSnap, &me));
            }
            CloseHandle(hSnap);
        }
    }

    Append("\nStack trace:\n");
    {
        uintptr_t frames[32];
        int nf = VehCaptureStack(ep->ContextRecord, frames, 32);
        for (int i = 0; i < nf; ++i)
        {
            HMODULE   hFm  = nullptr;
            char      fmod[MAX_PATH] = "<unknown>";
            uintptr_t offset = 0;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)frames[i], &hFm) && hFm)
            {
                GetModuleFileNameA(hFm, fmod, MAX_PATH);
                const char* sl = strrchr(fmod, '\\');
                if (sl) memmove(fmod, sl + 1, strlen(sl));
                offset = frames[i] - (uintptr_t)hFm;
            }
            Append("  [%2d] 0x%IX  %s + 0x%IX\n", i, frames[i], fmod, offset);
        }
    }

    // Repeat counts per site -- shows at a glance which SEH-guarded probe is
    // faulting in a loop.
    Append("\nFault sites:\n");
    {
        LONG n = s_seenCount;
        if (n > kSeenMax) n = kSeenMax;
        for (LONG i = 0; i < n; ++i)
            Append("  0x%IX  x%d\n", s_seenAddr[i], (int)s_seenHits[i]);
    }

    // Append (InstallPersistentCrashHandler rotated last session's file away).
    // The size cap is checked on the file itself, so a failed rotation cannot let
    // it grow across sessions; it only matters once the site table is full (one
    // dump per 5 s from then on). CreateFileA/GetFileSizeEx/WriteFile do not
    // touch the process heap, which may be what just faulted.
    HANDLE hFile = CreateFileA(crashPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(hFile, &size) && size.QuadPart < 2 * 1024 * 1024) {
            DWORD written = 0;
            WriteFile(hFile, s_buf, (DWORD)used, &written, nullptr);
            g_vehDumpCount.fetch_add(1, std::memory_order_relaxed);
        }
        // No FlushFileBuffers -- the OS write cache survives a process crash,
        // and the flush is a synchronous disk hit inside the fault path.
        CloseHandle(hFile);
    }

    InterlockedExchange(&g_crashData->writing, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallPersistentCrashHandler()
{
    const size_t kPageSize = 0x4000;
    uint8_t* page = (uint8_t*)VirtualAlloc(nullptr, kPageSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!page) return;

    memset(page, 0, kPageSize);
    g_crashData = reinterpret_cast<PersistCrashData*>(page);
    strcpy(g_crashData->logPath, CONFIG_PATH);
    g_crashData->active = 1;

    CreateDirectoryA("C:\\infovisual", nullptr);

    // crash.log is this session's; keep the last one as crash_prev.log. Done here,
    // not in the handler: MoveFileExA allocates, and the handler may be running
    // because the heap is corrupt.
    {
        char crashDir[MAX_PATH], crashPath[MAX_PATH], prevPath[MAX_PATH];
        CrashLogPaths(crashDir, crashPath);
        memcpy(prevPath, crashPath, sizeof(prevPath));
        char* dot = strrchr(prevPath, '.');   // crashPath always ends in "crash.log"
        if (dot && (size_t)(dot - prevPath) + sizeof("_prev.log") <= sizeof(prevPath)) {
            memcpy(dot, "_prev.log", sizeof("_prev.log"));
            MoveFileExA(crashPath, prevPath, MOVEFILE_REPLACE_EXISTING);
        }
    }

    g_hVEH = AddVectoredExceptionHandler(0, CrashHandler);
}

void WriteEjectLog(const char* phase)
{
    if (!g_crashData) return;

    static char s_phases[256] = {};
    if (s_phases[0] != '\0')
        strncat(s_phases, ",", sizeof(s_phases) - strlen(s_phases) - 1);
    strncat(s_phases, phase, sizeof(s_phases) - strlen(s_phases) - 1);

    snprintf(g_crashData->ejectNote, sizeof(g_crashData->ejectNote),
        "phases=%s imguiInit=%d sheen=%d sheenAnim=%d crit=%d dr=%d",
        s_phases, (int)g_imguiInit, (int)g_enableSheen,
        (int)g_enableSheenAnim, (int)g_enableCritColor, (int)g_enableDRColor);
}
