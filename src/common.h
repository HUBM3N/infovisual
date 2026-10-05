#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <psapi.h>
#include <d3d9.h>
#include <atomic>
#include <thread>
#include <cmath>
#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <string>
#include <vector>
#include <ctime>
#include <fstream>
#include <mutex>
#include <array>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <intrin.h>
#include <direct.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <d3dx9tex.h>
#pragma comment(lib, "d3dx9.lib")

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"
#include "minhook/MinHook.h"

#include "include/nanosvg.h"
#include "include/nanosvgrast.h"
#include "giflib-6.1.3/gif_lib.h"
#include "data/skindata.h"

// ---- ConVar / ICvar ----------------------------------------
// x64 ConVar object layout:
//   +0x00  ConCommandBase vtptr
//   +0x08  m_pNext
//   +0x10  m_bRegistered (bool, 7 bytes pad)
//   +0x18  m_pszName
//   +0x20  m_pszHelpString
//   +0x28  m_nFlags (int, 4 bytes pad)
//   +0x30  IConVar vtptr
//   +0x38  m_pParent
//   +0x40  m_pszDefaultValue
//   +0x48  m_pszString
//   +0x50  m_StringLength (int)
//   +0x54  m_fValue (float)
//   +0x58  m_nValue (int)
//
// ICvar vtable (absolute slots, IAppSystem has 5):
//   12 = FindVar,  16 = GetCommands

class ConVar
{
public:
    void ForceValue(int value)
    {
        __try {
            uintptr_t base   = reinterpret_cast<uintptr_t>(this);
            uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
            if (!parent) parent = base;
            *reinterpret_cast<float*>(parent + 0x54) = static_cast<float>(value);
            *reinterpret_cast<int*>  (parent + 0x58) = value;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    float GetFloat()
    {
        float v = 0.f;
        __try {
            uintptr_t base   = reinterpret_cast<uintptr_t>(this);
            uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
            if (!parent) parent = base;
            v = *reinterpret_cast<float*>(parent + 0x54);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return v;
    }

    void ForceFloat(float value)
    {
        __try {
            uintptr_t base   = reinterpret_cast<uintptr_t>(this);
            uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
            if (!parent) parent = base;
            *reinterpret_cast<float*>(parent + 0x54) = value;
            *reinterpret_cast<int*>  (parent + 0x58) = static_cast<int>(value);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // The real SetValue path -- it fires the ConVar change callback, which
    // ForceValue (a raw memory write) does not. Needed for cvars the engine
    // acts on in a callback rather than polling, e.g. mat_queue_mode.
    //
    // A ConVar has TWO vtables. The primary one is ConCommandBase's and holds
    // no SetValue at all: 0=dtor 1=IsCommand 2=IsFlagSet 3=AddFlags 4=GetName
    // 5=GetHelpText 6=IsRegistered 7=GetDLLIdentifier 8=CreateBase 9=Init.
    // (Slot 6 is IsRegistered -- it returns true for every live cvar, which is
    // why the old "6=IsCommand" check made this always fail; slot 9 is Init,
    // so the old "9=SetValue" would have been a wild call.)
    //
    // SetValue lives in the IConVar vtable, whose vptr sits at this+0x30, just
    // before m_pParent at 0x38 (the offset GetFloat already relies on).
    // MSVC emits the three SetValue overloads in REVERSE declaration order:
    // 0=SetValue(int) 1=SetValue(float) 2=SetValue(char*) 3=GetName 4=IsFlagSet.
    // Read off client.dll's ConVar vtable (slot 0 -> InternalSetIntValue, 2 ->
    // InternalSetValue) and engine SetDirect, which calls slot 2 with a string;
    // tools/check_vtables.py asserts it. The old 0=char*/2=int count sent
    // SetValueInt(x) into InternalSetValue((char*)x): 0 became a NULL string (the
    // cvar read "" afterwards), anything else faulted inside the __try and was lost.
    // Name is checked through primary slot 4 first and the write is read back,
    // so a layout mismatch is a no-op, not a wild call.
    bool SetValueInt(const char* expectName, int value)
    {
        bool ok = false;
        __try {
            if (void* p = IConVarFor(expectName)) {
                using SetInt_t = void(__fastcall*)(void*, int);
                ((SetInt_t)(*reinterpret_cast<void***>(p))[0])(p, value);
                ok = (static_cast<int>(GetFloat()) == value);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return ok;
    }

    bool SetValueStr(const char* expectName, const char* value)
    {
        bool ok = false;
        __try {
            if (void* p = IConVarFor(expectName)) {
                using SetStr_t = void(__fastcall*)(void*, const char*);
                ((SetStr_t)(*reinterpret_cast<void***>(p))[2])(p, value);
                const char* now = GetString();
                ok = now && strcmp(now, value) == 0;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return ok;
    }

    // m_pszString of the parent (+0x48). Engine-owned, valid until the next SetValue.
    const char* GetString()
    {
        const char* s = nullptr;
        __try {
            uintptr_t base   = reinterpret_cast<uintptr_t>(this);
            uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
            if (!parent) parent = base;
            s = *reinterpret_cast<const char**>(parent + 0x48);
        } __except(EXCEPTION_EXECUTE_HANDLER) { s = nullptr; }
        return s;
    }

    // m_nFlags of the parent (+0x28) -- the word ConVar::IsFlagSet tests and AddFlags ORs
    // (client.dll ConVar primary slots 2/3: `mov rax,[rcx+38h]; test/or [rax+28h],edx`).
    // A raw write fires nothing; callers use it for FCVAR_ARCHIVE, which is only read when
    // config.cfg is written. Returns -1 when unreadable.
    int GetFlags()
    {
        int f = -1;
        __try {
            uintptr_t base   = reinterpret_cast<uintptr_t>(this);
            uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
            if (!parent) parent = base;
            f = *reinterpret_cast<int*>(parent + 0x28);
        } __except(EXCEPTION_EXECUTE_HANDLER) { f = -1; }
        return f;
    }

    void SetFlags(int flags)
    {
        __try {
            uintptr_t base   = reinterpret_cast<uintptr_t>(this);
            uintptr_t parent = *reinterpret_cast<uintptr_t*>(base + 0x38);
            if (!parent) parent = base;
            *reinterpret_cast<int*>(parent + 0x28) = flags;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

private:
    // IConVar sub-object (+0x30) once the cvar's name matches, else null. Caller holds the __try.
    void* IConVarFor(const char* expectName)
    {
        void** vtbl = *reinterpret_cast<void***>(this);
        using GetName_t   = const char*(__fastcall*)(void*);
        using IsCommand_t = bool(__fastcall*)(void*);
        const char* name = ((GetName_t)vtbl[4])(this);
        if (!name || ((IsCommand_t)vtbl[1])(this) || strcmp(name, expectName) != 0) return nullptr;
        void* p = reinterpret_cast<char*>(this) + 0x30;
        return *reinterpret_cast<void***>(p) ? p : nullptr;
    }
};

class ICvar
{
public:
    ConVar* FindVar(const char* name)
    {
        ConVar* r = nullptr;
        __try {
            using Fn = ConVar*(__thiscall*)(void*, const char*);
            r = ((Fn)(*(void***)(this))[12])(this, name);
        } __except(EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        return r;
    }

    void* GetCommandsRaw()
    {
        void* r = nullptr;
        __try {
            using Fn = void*(__thiscall*)(void*);
            r = ((Fn)(*(void***)(this))[16])(this);
        } __except(EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        return r;
    }

    ConVar* FindVarWalk(const char* name)
    {
        ConVar* r = FindVar(name);
        if (r) return r;

        uintptr_t pBase = reinterpret_cast<uintptr_t>(GetCommandsRaw());
        int safety = 0;
        while (pBase != 0 && ++safety < 16384)
        {
            const char* pName = nullptr;
            uintptr_t   pNext = 0;
            __try {
                void** vtbl = *reinterpret_cast<void***>(pBase);
                using GetName_t = const char*(__thiscall*)(void*);
                pName = ((GetName_t)vtbl[4])(reinterpret_cast<void*>(pBase));
                pNext = *reinterpret_cast<uintptr_t*>(pBase + 0x08);
            } __except(EXCEPTION_EXECUTE_HANDLER) { break; }

            if (pName && strcmp(pName, name) == 0)
                return reinterpret_cast<ConVar*>(pBase);
            pBase = pNext;
        }
        return nullptr;
    }
};

// Safe vtable call -- validates the vtable pointer looks like a real user-mode
// address before dereferencing, and wraps the actual call in SEH so a stale
// or dangling pointer during map transitions can't reach address 0x0 and
// smash RSP before the VEH gets a chance to run.
template<typename Ret, typename Self, typename... Args>
inline Ret VCall(Self* self, int idx, Args... args) {
    using Fn = Ret(__fastcall*)(Self*, Args...);
    Ret result{};
    __try {
        void** vtbl = *(void***)self;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return result;
        Fn fn = reinterpret_cast<Fn>(vtbl[idx]);
        if (reinterpret_cast<uintptr_t>(fn) < 0x10000) return result;
        result = fn(self, args...);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

struct IEngineClient   {};

// Minimal IGameEvent vtable wrapper (matches Amalgam IGameEvents.h -- no GetUint64 shift)
// TF2 IGameEvent vtable (confirmed via slot probe: slot 8 = GetString, slot 12 = SetString):
//   0: ~dtor  1: GetName  2: IsReliable  3: IsLocal  4: IsEmpty
//   5: GetBool  6: GetInt  7: GetFloat  8: GetString
//   9: SetBool  10: SetInt  11: SetFloat  12: SetString
struct IGameEventVtbl {
    static inline const char* GetName(void* pEvent)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return nullptr;
        if (reinterpret_cast<uintptr_t>(vtbl[1]) < 0x10000) return nullptr;
        using Fn = const char*(__fastcall*)(void*);
        return ((Fn)vtbl[1])(pEvent);
    }
    static inline int GetInt(void* pEvent, const char* key, int def = 0)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return def;
        if (reinterpret_cast<uintptr_t>(vtbl[6]) < 0x10000) return def;
        using Fn = int(__fastcall*)(void*, const char*, int);
        return ((Fn)vtbl[6])(pEvent, key, def);
    }
    static inline const char* GetString(void* pEvent, const char* key, const char* def = "")
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return def;
        if (reinterpret_cast<uintptr_t>(vtbl[8]) < 0x10000) return def;
        using Fn = const char*(__fastcall*)(void*, const char*, const char*);
        return ((Fn)vtbl[8])(pEvent, key, def);
    }
    // Slot 10. The kill notice's streak banner reads kill_streak_total / kill_streak_wep off the
    // event, so a faked killstreak count has to correct them here as well as on the entity.
    static inline void SetInt(void* pEvent, const char* key, int val)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return;
        if (reinterpret_cast<uintptr_t>(vtbl[10]) < 0x10000) return;
        using Fn = void(__fastcall*)(void*, const char*, int);
        ((Fn)vtbl[10])(pEvent, key, val);
    }
    static inline void SetString(void* pEvent, const char* key, const char* val)
    {
        void** vtbl = *(void***)pEvent;
        if (reinterpret_cast<uintptr_t>(vtbl) < 0x10000) return;
        if (reinterpret_cast<uintptr_t>(vtbl[12]) < 0x10000) return;
        using Fn = void(__fastcall*)(void*, const char*, const char*);
        ((Fn)vtbl[12])(pEvent, key, val);
    }
};
struct INetChannelInfo {};

// ---- Pattern scanner (IDA-style: "40 56 ? 83 EC ?" -- '?' = wildcard byte) ----
// PatternScan split into noinline SEH helper to satisfy C2712:
// __try cannot live in a function that requires object unwinding (std::vector).
struct PatternScanToken { uint8_t byte; bool wild; };

__declspec(noinline) static uint8_t* PatternScan_SEH(
    uint8_t* base, size_t sz,
    const PatternScanToken* pat, size_t plen)
{
    // Walk committed, readable pages only -- SizeOfImage includes non-committed
    // sections (.reloc, resource, padding) that AV on a raw byte scan.
    uint8_t* result = nullptr;
    __try {
        MEMORY_BASIC_INFORMATION mbi{};
        size_t off = 0;
        while (off + plen <= sz)
        {
            if (!VirtualQuery(base + off, &mbi, sizeof(mbi)) || mbi.RegionSize == 0)
                break;
            size_t regionEnd = (size_t)((uint8_t*)mbi.BaseAddress - base) + mbi.RegionSize;
            if (regionEnd > sz) regionEnd = sz;
            constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE |
                                        PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            if (mbi.State == MEM_COMMIT && (mbi.Protect & kReadable) &&
                !(mbi.Protect & PAGE_GUARD))
            {
                size_t scanEnd = (regionEnd >= plen) ? regionEnd - plen + 1 : 0;
                for (size_t i = off; i < scanEnd; ++i)
                {
                    bool found = true;
                    for (size_t j = 0; j < plen; ++j)
                        if (!pat[j].wild && base[i + j] != pat[j].byte) { found = false; break; }
                    if (found) { result = base + i; break; }
                }
                if (result) break;
            }
            off = regionEnd;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

static uint8_t* PatternScan(HMODULE mod, const char* ida_pattern)
{
    MODULEINFO mi{};
    GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi));
    auto base = (uint8_t*)mi.lpBaseOfDll;
    size_t sz  = mi.SizeOfImage;

    std::vector<PatternScanToken> pat;
    const char* p = ida_pattern;
    while (*p)
    {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '?') {
            pat.push_back({0, true});
            ++p;
            if (*p == '?') ++p;
        } else {
            pat.push_back({(uint8_t)strtoul(p, nullptr, 16), false});
            while (*p && *p != ' ') ++p;
        }
    }

    if (pat.empty()) return nullptr;
    return PatternScan_SEH(base, sz, pat.data(), pat.size());
}

// ---- IMaterialVar ------------------------------------------
// Vtable slots confirmed from SDK: SetVecValue(float*,int)=9, GetVecValueInternal()=28
class IMaterialVar
{
public:
    virtual void*        GetTextureValue() = 0;                             // 0
    virtual const char*  GetName() const = 0;                              // 1
    virtual unsigned short GetNameAsSymbol() const = 0;                    // 2
    virtual void         SetFloatValue(float val) = 0;                     // 3
    virtual void         SetIntValue(int val) = 0;                         // 4
    virtual void         SetStringValue(const char* val) = 0;              // 5
    virtual const char*  GetStringValue() const = 0;                       // 6
    virtual void         SetFourCCValue(unsigned long type, void* pData) = 0; // 7
    virtual void         GetFourCCValue(unsigned long* type, void** ppData) = 0; // 8
    virtual void         SetVecValue(float const* val, int numcomps) = 0;  // 9
    virtual void         SetVecValue(float x, float y) = 0;                // 10
    virtual void         SetVecValue(float x, float y, float z) = 0;       // 11
    virtual void         SetVecValue(float x, float y, float z, float w) = 0; // 12
    virtual void         GetLinearVecValue(float* val, int numcomps) const = 0; // 13
    virtual void         SetTextureValue(void*) = 0;                       // 14
    virtual void*        GetMaterialValue() = 0;                           // 15
    virtual void         SetMaterialValue(void*) = 0;                      // 16
    virtual bool         IsDefined() const = 0;                            // 17
    virtual void         SetUndefined() = 0;                               // 18
    virtual void         SetMatrixValue(const void* matrix) = 0;           // 19
    virtual const void*  GetMatrixValue() = 0;                             // 20
    virtual bool         MatrixIsIdentity() const = 0;                     // 21
    virtual void         CopyFrom(IMaterialVar* pMaterialVar) = 0;         // 22
    virtual void         SetValueAutodetectType(const char* val) = 0;      // 23
    virtual void*        GetOwningMaterial() = 0;                          // 24
    virtual void         SetVecComponentValue(float fVal, int nComponent) = 0; // 25
    virtual int          GetIntValueInternal() const = 0;                  // 26
    virtual float        GetFloatValueInternal() const = 0;                // 27
    virtual float const* GetVecValueInternal() const = 0;                  // 28
    virtual void         GetVecValueInternal(float* val, int numcomps) const = 0; // 29
    virtual int          VectorSizeInternal() const = 0;                   // 30
};
