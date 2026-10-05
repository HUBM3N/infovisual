#include "globals.h"

IClientEntityList* g_entityList = nullptr;

// IClientEntityList is looked up once during MainThread init (thread.cpp). If
// client.dll was not loaded yet, or the interface version there is not one of
// the two names it tries, g_entityList stays null for the whole session and
// every GetClientEntity call silently returns nullptr -- which is exactly what
// happened: player chams attachments logged "entList=0000000000000000" forever.
// Retry from the render thread's 100 ms tick and probe the full version range.
void EnsureEntityList()
{
    if (g_entityList) return;

    static int  s_tries    = 0;
    static bool s_loggedOk = false;
    if (s_tries > 200) return;          // ~20 s of retries, then give up quietly
    ++s_tries;

    char name[32];
    for (int v = 1; v <= 10 && !g_entityList; ++v) {
        sprintf(name, "VClientEntityList%03d", v);
        g_entityList = (IClientEntityList*)GetInterface("client.dll", name);
        if (g_entityList && !s_loggedOk) {
            s_loggedOk = true;
            DebugLog("entlist: resolved %s -> %p (try %d)", name, (void*)g_entityList, s_tries);
        }
    }
    if (!g_entityList && s_tries == 200)
        DebugLog("entlist: NOT resolved after %d tries -- client.dll=%p",
                 s_tries, (void*)GetModuleHandleA("client.dll"));
}

// ---- NetVar resolver ---------------------------------------
ClientClass* GetAllClasses()
{
    HMODULE client = GetModuleHandleA("client.dll");
    if (!client) return nullptr;
    static void* g_clientDLL = nullptr;
    if (!g_clientDLL)
    {
        using CreateInterface_t = void*(*)(const char*, int*);
        auto ci = (CreateInterface_t)GetProcAddress(client, "CreateInterface");
        if (!ci) return nullptr;
        const char* names[] = {"VClient018","VClient017","VClient016",nullptr};
        for (int i = 0; names[i] && !g_clientDLL; ++i)
            g_clientDLL = ci(names[i], nullptr);
    }
    if (!g_clientDLL) return nullptr;
    ClientClass* result = nullptr;
    __try {
        using GetAllClasses_t = ClientClass*(__thiscall*)(void*);
        void** vtbl = *(void***)g_clientDLL;
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            result = ((GetAllClasses_t)vtbl[8])(g_clientDLL);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

int FindPropOffset(RecvTable* pTable, const char* name, int baseOffset, int depth)
{
    if (!pTable || depth > 6) return -1;
    for (int i = 0; i < pTable->m_nProps; ++i)
    {
        RecvProp& prop = pTable->m_pProps[i];
        if (!prop.m_pVarName) continue;
        if (strcmp(prop.m_pVarName, name) == 0)
            return baseOffset + prop.m_Offset;
        if (prop.m_pDataTable)
        {
            int result = FindPropOffset(prop.m_pDataTable, name, baseOffset + prop.m_Offset, depth + 1);
            if (result >= 0) return result;
        }
    }
    return -1;
}

RecvTable* FindNetworkTable(const char* netTableName)
{
    for (auto* cls = GetAllClasses(); cls; cls = cls->m_pNext)
        if (cls->m_pRecvTable && strcmp(cls->m_pRecvTable->m_pNetTableName, netTableName) == 0)
            return cls->m_pRecvTable;
    return nullptr;
}

int  g_offClass     = 0;
int  g_offTeam      = 0;
int  g_offVelocity  = 0;
int  g_offLifeState = 0;
int  g_offOrigin    = 0;
int  g_offFlags     = 0;
int  g_offMaxspeed  = 0;   // m_flMaxspeed (DT_LocalPlayerExclusive) -- csgo crosshair
int  g_offRagdollTeam       = 0; // CTFRagdoll::m_iTeam
int  g_offRagdollClass      = 0; // CTFRagdoll::m_iClass
int  g_offRagdollPlayer     = 0; // CTFRagdoll::m_hPlayer (EHANDLE -- entity index in low 14 bits)
int  g_offRagdollGib        = 0; // CTFRagdoll::m_bGib
int  g_offRagdollBurning    = 0; // CTFRagdoll::m_bBurning
int  g_offRagdollElectro    = 0; // CTFRagdoll::m_bElectrocuted
int  g_offRagdollFeignDeath = 0; // CTFRagdoll::m_bFeignDeath (m_bDissolving = feignDeath-1)
int  g_offRagdollAsh        = 0; // CTFRagdoll::m_bBecomeAsh
int  g_offRagdollGold       = 0; // CTFRagdoll::m_bGoldRagdoll
int  g_offRagdollIce        = 0; // CTFRagdoll::m_bIceRagdoll
int  g_offMoveParent   = 0; // offset of m_hNetworkMoveParent ("moveparent" netvar)
int  g_offModelIndex   = 0;
int  g_offActiveWeapon = 0; // offset of m_hActiveWeapon in DT_BaseCombatCharacter
int  g_offMyWeapons    = 0; // offset of m_hMyWeapons[0] in DT_BaseCombatCharacter (48 EHANDLEs)
int  g_offViewModelIndex = 0; // CBaseViewModel::m_nViewModelIndex (0 = weapon, 1 = offhand: spy watch etc)
int  g_offPlayerCond    = 0; // m_nPlayerCond bitfield (ETFCond enum value = bit index: DISGUISING=2, DISGUISED=3, STEALTHED=4, STEALTHED_BLINK=9)
bool g_netvarsReady = false; // set true once all lookups have been attempted

void ResolveNetvars()
{
    if (g_netvarsReady) return;

    RecvTable* pTFPlayer = FindNetworkTable("DT_TFPlayer");
    if (!pTFPlayer) return; // tables not yet available -- try again next tick

    if (g_offClass == 0) {
        int off = FindPropOffset(pTFPlayer, "m_iClass");
        g_offClass = (off > 0) ? off : -1;
    }
    if (g_offTeam == 0) {
        int off = FindPropOffset(pTFPlayer, "m_iTeamNum");
        g_offTeam = (off > 0) ? off : -1;
    }
    if (g_offVelocity == 0) {
        int off = FindPropOffset(pTFPlayer, "m_vecVelocity[0]");
        if (off <= 0) off = FindPropOffset(pTFPlayer, "m_vecVelocity");
        g_offVelocity = (off > 0) ? off : -1;
    }
    if (g_offLifeState == 0) {
        int off = FindPropOffset(pTFPlayer, "m_lifeState");
        g_offLifeState = (off > 0) ? off : -1;
    }
    if (g_offOrigin == 0) {
        RecvTable* pBase = FindNetworkTable("DT_BaseEntity");
        int off = pBase ? FindPropOffset(pBase, "m_vecOrigin") : -1;
        g_offOrigin = (off > 0) ? off : -1;
    }
    if (g_offFlags == 0) {
        int off = FindPropOffset(pTFPlayer, "m_fFlags");
        g_offFlags = (off > 0) ? off : -1;
        DebugLog("hwfoot: g_offFlags=%d", g_offFlags);
    }
    if (g_offMaxspeed == 0) {
        int off = FindPropOffset(pTFPlayer, "m_flMaxspeed");
        g_offMaxspeed = (off > 0) ? off : -1;
    }

    if (g_offModelIndex == 0) {
        RecvTable* pBase = FindNetworkTable("DT_BaseEntity");
        int off = pBase ? FindPropOffset(pBase, "m_nModelIndex") : -1;
        g_offModelIndex = (off > 0) ? off : -1;
    }
    if (g_offMoveParent == 0) {
        // m_hNetworkMoveParent; "moveparent" is its netvar name
        RecvTable* pBase = FindNetworkTable("DT_BaseEntity");
        int off = pBase ? FindPropOffset(pBase, "moveparent") : -1;
        g_offMoveParent = (off > 0) ? off : -1;
    }
    if (g_offActiveWeapon == 0) {
        int off = FindPropOffset(pTFPlayer, "m_hActiveWeapon");
        g_offActiveWeapon = (off > 0) ? off : -1;
    }
    if (g_offMyWeapons == 0) {
        // RecvPropArray3 names the array prop itself "m_hMyWeapons" and its
        // elements "000".."047", so this offset is element 0; stride is 4.
        int off = FindPropOffset(pTFPlayer, "m_hMyWeapons");
        if (off <= 0) off = FindPropOffset(pTFPlayer, "m_hMyWeapons[0]");
        g_offMyWeapons = (off > 0) ? off : -1;
        DebugLog("plchams: m_hMyWeapons=%d", g_offMyWeapons);
    }

    if (g_offRagdollTeam == 0 || g_offRagdollClass == 0 || g_offRagdollPlayer == 0) {
        RecvTable* pRag = FindNetworkTable("DT_TFRagdoll");
        if (pRag) {
            if (g_offRagdollTeam == 0) {
                int off = FindPropOffset(pRag, "m_iTeam");
                g_offRagdollTeam = (off > 0) ? off : -1;
            }
            if (g_offRagdollClass == 0) {
                int off = FindPropOffset(pRag, "m_iClass");
                g_offRagdollClass = (off > 0) ? off : -1;
            }
            if (g_offRagdollPlayer == 0) {
                int off = FindPropOffset(pRag, "m_hPlayer");
                g_offRagdollPlayer = (off > 0) ? off : -1;
            }
            DebugLog("ragdoll netvars: m_iTeam=%d m_iClass=%d m_hPlayer=%d", g_offRagdollTeam, g_offRagdollClass, g_offRagdollPlayer);
            auto resolveRagBool = [&](int& dest, const char* name) {
                if (dest == 0) { int o = FindPropOffset(pRag, name); dest = (o > 0) ? o : -1; }
            };
            resolveRagBool(g_offRagdollGib,        "m_bGib");
            resolveRagBool(g_offRagdollBurning,    "m_bBurning");
            resolveRagBool(g_offRagdollElectro,    "m_bElectrocuted");
            resolveRagBool(g_offRagdollFeignDeath, "m_bFeignDeath");
            resolveRagBool(g_offRagdollAsh,        "m_bBecomeAsh");
            resolveRagBool(g_offRagdollGold,       "m_bGoldRagdoll");
            resolveRagBool(g_offRagdollIce,        "m_bIceRagdoll");
            DebugLog("ragdollFX: gib=%d burning=%d electro=%d feignDeath=%d ash=%d gold=%d ice=%d",
                g_offRagdollGib, g_offRagdollBurning, g_offRagdollElectro,
                g_offRagdollFeignDeath, g_offRagdollAsh, g_offRagdollGold, g_offRagdollIce);
        }
    }

    if (g_offPlayerCond == 0) {
        int off = FindPropOffset(pTFPlayer, "m_nPlayerCond");
        g_offPlayerCond = (off > 0) ? off : -1;
        DebugLog("hwfoot: m_nPlayerCond=%d", g_offPlayerCond);
    }
    if (g_offViewModelIndex == 0) {
        RecvTable* pVm = FindNetworkTable("DT_BaseViewModel");
        int off = pVm ? FindPropOffset(pVm, "m_nViewModelIndex") : -1;
        g_offViewModelIndex = (off > 0) ? off : -1;
        DebugLog("vmoffset: m_nViewModelIndex=%d", g_offViewModelIndex);
    }

    if (g_offClass != 0 && g_offTeam != 0 && g_offVelocity != 0 && g_offLifeState != 0)
        g_netvarsReady = true;
}
