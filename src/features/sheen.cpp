#include "globals.h"
#include "config.h"

// Equip region hook typedefs and original function pointers (TU-local)
using VerifyChangedLoadoutsAreValid_t   = void(__fastcall*)(void*);
using GenerateEquipRegionConflictMask_t = uint32_t(__fastcall*)(int, int, int);
using GetItemInLoadoutForClass_t        = void*(__fastcall*)(void*, int, int, void*);
static VerifyChangedLoadoutsAreValid_t   o_VerifyChangedLoadoutsAreValid   = nullptr;
static GenerateEquipRegionConflictMask_t o_GenerateEquipRegionConflictMask  = nullptr;
static GetItemInLoadoutForClass_t        o_GetItemInLoadoutForClass          = nullptr;
uintptr_t g_addrGetItemInLoadoutCall = 0;

void GetAnimatedSheenColor(float out[3])
{
    // Cache result per millisecond -- called multiple times per frame
    // (SheenOnBind per weapon + UpdateMaterialsRender + UI preview). Game time (AnimTimeMs): a
    // host_framerate recording cycles at playback speed, host_timescale / pause slow it with the game.
    static ULONGLONG s_lastTick = ~0ull;
    static float     s_cache[3] = {1.f, 1.f, 1.f};
    ULONGLONG now = AnimTimeMs();
    if (now != s_lastTick)
    {
        s_lastTick = now;
        int n = g_sheenColorCount;
        if (n < 2) n = 2;

        int   segs     = (n - 1) * 2;
        // Guard against divide-by-zero: clamp speed to 0.1 s minimum.
        float speed    = (g_sheenAnimSpeed < 0.1f) ? 0.1f : g_sheenAnimSpeed;
        float cycleSec = speed * segs;
        float t        = (float)(now % (ULONGLONG)(cycleSec * 1000.0f))
                         / (cycleSec * 1000.0f) * segs;

        int   seg  = (int)t;
        float frac = t - (float)seg;
        if (seg >= segs) seg = segs - 1;

        int idxA, idxB;
        if (seg < n - 1) {
            idxA = seg; idxB = seg + 1;
        } else {
            int back = seg - (n - 1);
            idxA = (n - 1) - back; idxB = idxA - 1;
        }
        idxA = std::clamp(idxA, 0, n - 1);
        idxB = std::clamp(idxB, 0, n - 1);

        s_cache[0] = g_sheenColors[idxA][0] + (g_sheenColors[idxB][0] - g_sheenColors[idxA][0]) * frac;
        s_cache[1] = g_sheenColors[idxA][1] + (g_sheenColors[idxB][1] - g_sheenColors[idxA][1]) * frac;
        s_cache[2] = g_sheenColors[idxA][2] + (g_sheenColors[idxB][2] - g_sheenColors[idxA][2]) * frac;
    }
    out[0] = s_cache[0]; out[1] = s_cache[1]; out[2] = s_cache[2];
}


// CProxyAnimatedWeaponSheen offsets (x64)
static constexpr ptrdiff_t kSheenIndexVarOffset = 0x20;
static constexpr ptrdiff_t kTintVarOffset       = 0x28;

using SheenOnBind_t = void(__fastcall*)(void*, void*);
SheenOnBind_t o_SheenOnBind = nullptr;

// ---- FakeSheenIndexVar -------------------------------------
// Replaces m_pSheenIndexVar during OnBind to force alpha=1, bypassing the
// early-exit that would prevent tint injection on non-killstreak weapons.

void*  FakeSheenIndexVar::s_vtable[31] = {};
bool   FakeSheenIndexVar::s_inited     = false;
FakeSheenIndexVar g_fakeSheenIndexVar;

// SEH helpers for h_SheenOnBind (C2712: __try cannot be in a function with object unwinding)
__declspec(noinline) static bool SheenOnBind_SwapIn(uintptr_t base, IMaterialVar** pOrig)
{
    __try {
        IMaterialVar** ppSheenIndex = reinterpret_cast<IMaterialVar**>(base + kSheenIndexVarOffset);
        *pOrig = *ppSheenIndex;
        *ppSheenIndex = reinterpret_cast<IMaterialVar*>(&g_fakeSheenIndexVar);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) static void SheenOnBind_SwapOut(uintptr_t base, IMaterialVar* orig)
{
    __try {
        IMaterialVar** ppSheenIndex = reinterpret_cast<IMaterialVar**>(base + kSheenIndexVarOffset);
        *ppSheenIndex = orig;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void SheenOnBind_SetTint(uintptr_t base)
{
    __try {
        IMaterialVar* pTint = *reinterpret_cast<IMaterialVar**>(base + kTintVarOffset);
        if (pTint) {
            float activeColor[3];
            if (g_enableSheenAnim)
                GetAnimatedSheenColor(activeColor);
            else {
                activeColor[0] = g_sheenColor[0];
                activeColor[1] = g_sheenColor[1];
                activeColor[2] = g_sheenColor[2];
            }
            float colorArray[4] = {
                activeColor[0] * 2.55f,
                activeColor[1] * 2.55f,
                activeColor[2] * 2.55f,
                1.f
            };
            pTint->SetVecValue(colorArray, 4);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void __fastcall h_SheenOnBind(void* rcx, void* pEntity)
{
    if (g_eject) { o_SheenOnBind(rcx, pEntity); return; }
    ++g_miscHookDepth;
    struct Dec { ~Dec() { --g_miscHookDepth; } } _dec;

    if (!rcx || !g_enableSheen)
    {
        o_SheenOnBind(rcx, pEntity);
        return;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(rcx);

    IMaterialVar* origSheenIndexVar = nullptr;
    bool swapped = SheenOnBind_SwapIn(base, &origSheenIndexVar);

    if (g_attrLockInit) EnterCriticalSection(&g_attrLock);
    o_SheenOnBind(rcx, pEntity);
    if (g_attrLockInit) LeaveCriticalSection(&g_attrLock);

    if (swapped)
        SheenOnBind_SwapOut(base, origSheenIndexVar);

    SheenOnBind_SetTint(base);
}


// ---- Static amby crosshair hook ----------------------------
using CTFRevolver_GetWeaponCrosshairScale_t = void(__fastcall*)(void*, float&);
CTFRevolver_GetWeaponCrosshairScale_t o_CTFRevolver_GetWeaponCrosshairScale = nullptr;
bool  g_sheenSigsResolved = false;

// ---- Engine attribute API (proper path, same as SkinChanger) ----
// Exact signatures from Amalgam SkinChanger.cpp (confirmed working):

GetItemSchema_t             g_fnGetItemSchema            = nullptr;
GetAttributeDefinition_t    g_fnGetAttributeDefinition   = nullptr;
SetRuntimeAttributeValue_t  g_fnSetRuntimeAttributeValue = nullptr;

// Call through the engine's schema API -- this is the only path the proxy/sheen
// system recognises. Falls back to raw SetOrAdd when a sig did not resolve.
// (A vtable fallback used to call unknown CEconItemSchema slots 3-7 with guessed
// args; a wrong slot can mutate schema state, which __try cannot undo.)
__declspec(noinline) static void* SchemaGetAttrDef(void* pSchema, int index)
{
    void* pDef = nullptr;
    __try { pDef = g_fnGetAttributeDefinition(pSchema, index); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return pDef;
}

__declspec(noinline) bool EngineSetAttribute(CAttributeList* pList,
                                                     uint32_t index, float value)
{
    __try {
        if (g_fnGetItemSchema && g_fnGetAttributeDefinition && g_fnSetRuntimeAttributeValue)
        {
            void* pSchema = g_fnGetItemSchema();
            if (!pSchema) goto fallback;

            void* pDef = SchemaGetAttrDef(pSchema, (int)index);
            if (!pDef) goto fallback;

            g_fnSetRuntimeAttributeValue(pList, pDef, value);
            return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

fallback:
    pList->SetOrAdd((uint16_t)index, value);
    return false;
}

// FSN injection state -- file-scope so they're reset when sheen is re-enabled
// after an eject/reinject without the statics surviving from the last session.
bool     g_fsn_wasEnabled      = false;
void*    g_fsn_lastLocalEnt    = nullptr;
uint32_t g_fsn_entityChangeAge = 0;
int      g_fsn_offMyWeapons    = 0;
int      g_fsn_offAttrList     = 0;
int      g_fsn_offItemDefIndex = 0;
int      g_offHViewModel0      = 0;
bool     g_fsn_skinWasEnabled  = false;
bool     g_setSeqHookInstalled = false;

// ---- Skinchanger -------------------------------------------
std::unordered_map<int,SkinCfg> g_skinCfgs;  // keyed by redirected def idx

// Every weapon the menu can show gets its entry up front, before any hook runs. From then on the
// map never changes shape: the menu and LoadConfig (render thread) only write fields while the FSN
// and game-event hooks iterate it on the game thread. An insert used to rehash under them.
void SkinCfgs_Init()
{
    if (!g_skinCfgs.empty()) return;
    g_skinCfgs.reserve(k_wpnCount);
    for (int i = 0; i < k_wpnCount; ++i)
        g_skinCfgs.emplace(k_wpnList[i].redir, SkinCfg{});
}
bool g_enableSkins     = false;
bool g_skinNeedsUpdate = false;

// ---- Equip Region Unlock hooks (Amalgam-based) -------------
// Hook 1: skip inventory validation -- lets invalid/conflicting loadouts through
static void __fastcall h_VerifyChangedLoadoutsAreValid(void* rcx)
{
    if (!g_enableEquipRegionUnlock)
        o_VerifyChangedLoadoutsAreValid(rcx);
    // when unlocked: just return without calling original
}

// Hook 2: zero the conflict mask -- every item slot always appears conflict-free
static uint32_t __fastcall h_GenerateEquipRegionConflictMask(int iClass, int iUpToSlot, int iIgnoreSlot)
{
    if (g_enableEquipRegionUnlock)
        return 0;
    return o_GenerateEquipRegionConflictMask(iClass, iUpToSlot, iIgnoreSlot);
}

// Hook 3: when called from CEquipSlotItemSelectionPanel::UpdateModelPanelsForSelection,
// return nullptr so already-equipped items don't block selection of a new item.
static void* __fastcall h_GetItemInLoadoutForClass(void* rcx, int iClass, int iSlot, void* pID)
{
    if (g_enableEquipRegionUnlock && g_addrGetItemInLoadoutCall)
    {
        uintptr_t retAddr = (uintptr_t)_ReturnAddress();
        if (retAddr == g_addrGetItemInLoadoutCall)
            return nullptr;
    }
    return o_GetItemInLoadoutForClass(rcx, iClass, iSlot, pID);
}

bool g_equipRegionHooksInstalled = false;

void InstallEquipRegionHook()
{
    if (g_equipRegionHooksInstalled) return;
    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) return;

    struct { const char* sig; void* hook; void** orig; } hooks[] = {
        // Amalgam sigs from CTFPlayerInventory_VerifyChangedLoadoutsAreValid.cpp
        {
            "41 56 48 83 EC ? 48 8B 05 ? ? ? ? 48 8D 54 24",
            (void*)h_VerifyChangedLoadoutsAreValid,
            (void**)&o_VerifyChangedLoadoutsAreValid
        },
        {
            "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 33 FF 41 8B E8",
            (void*)h_GenerateEquipRegionConflictMask,
            (void**)&o_GenerateEquipRegionConflictMask
        },
        {
            "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 83 EC ? 81 60",
            (void*)h_GetItemInLoadoutForClass,
            (void**)&o_GetItemInLoadoutForClass
        },
    };

    bool allOk = true;
    for (auto& h : hooks)
    {
        uint8_t* addr = PatternScan(client, h.sig);
        if (!addr) { allOk = false; continue; }
        MH_CreateHook((LPVOID)addr, h.hook, h.orig);
        MH_EnableHook((LPVOID)addr);
    }

    // Resolve the return-address sentinel for hook 3 (Amalgam sig)
    {
        uint8_t* call = PatternScan(client,
            "48 85 C0 74 ? 48 8D 48 ? 48 8B 40 ? FF 50 ? 44 0B A0");
        if (call)
            g_addrGetItemInLoadoutCall = reinterpret_cast<uintptr_t>(call);
    }

    if (allOk)
        g_equipRegionHooksInstalled = true;
}

void ResolveSheenSigs()
{
    if (g_sheenSigsResolved) return;

    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) hClient = GetModuleHandleA("tf.dll");
    if (!hClient) return;

    // Exact patterns from Amalgam SkinChanger.cpp (confirmed working in x64 TF2)
    if (uint8_t* addr = PatternScan(hClient, "48 83 EC ? E8 ? ? ? ? 48 83 C0 ? 48 83 C4 ? C3 CC CC CC"))
        g_fnGetItemSchema = reinterpret_cast<GetItemSchema_t>(addr);

    // CEconItemSchema::GetAttributeDefinitionByIndex; two fallbacks for minor build variation
    {
        uint8_t* addr = PatternScan(hClient,
            "89 54 24 ? 53 48 83 EC ? 48 8B D9 48 8D 54 24 ? 48 81 C1 ? ? ? ? E8 ? ? ? ? 8B D0 3B 83 ? ? ? ? 73 ? 8B 83 ? ? ? ? 83 F8 ? 74 ? 3B D0 7F ? 48 81 C3 ? ? ? ? 44 8B C2 83 FA ? 74 ? 48 8B 03 8B CA");
        if (!addr)
            addr = PatternScan(hClient, "48 89 5C 24 ? 57 48 83 EC ? 8B FA 48 8B D9 E8 ? ? ? ? 48 85 C0");
        if (!addr)
            addr = PatternScan(hClient, "48 8B C4 48 89 58 ? 48 89 70 ? 57 48 83 EC ? 41 8B F0 48 8B D9");
        if (addr)
            g_fnGetAttributeDefinition = reinterpret_cast<GetAttributeDefinition_t>(addr);
        else
            DebugLog("sheen: GetAttributeDefinitionByIndex sig not found -- raw attribute path");
    }

    // CAttributeList::SetRuntimeAttributeValue
    {
        uint8_t* addr = PatternScan(hClient, "48 89 5C 24 10 55 56 57 48 8B EC 48 83 EC 50 44");
        if (!addr)
            addr = PatternScan(hClient,
                "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05");
        if (addr) g_fnSetRuntimeAttributeValue = reinterpret_cast<SetRuntimeAttributeValue_t>(addr);
    }

    g_sheenSigsResolved = true;
}

void TriggerForceFullUpdate(const char* reason)
{
    DebugLog("ForceFullUpdate called: %s", reason);
    if (!g_engine) return;
    __try {
        void** vtbl = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vtbl[106]) > 0x10000) {
            using Fn = void(__thiscall*)(void*, const char*);
            ((Fn)vtbl[106])(g_engine, "cl_fullupdate");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void __fastcall h_CTFRevolver_GetWeaponCrosshairScale(void* rcx, float& flScale)
{
    if (!g_staticAmbyCrosshair)
        o_CTFRevolver_GetWeaponCrosshairScale(rcx, flScale);
}
