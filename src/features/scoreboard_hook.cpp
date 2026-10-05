#include "../common.h"
#include "../globals.h"
#include "playerlist.h"
#include <intrin.h>

// return addrs inside OnScoreBoardMouseRightRelease
static uintptr_t s_addrIsFakePlayer_Call      = 0;
static uintptr_t s_addrAddMenuItem_CallProfile = 0;

// per right-click state, shared across the 3 hooks
static int      s_iPlayerIndex = 0;
static uint32_t s_uAccountID   = 0;
static char     s_sPlayerName[128] = {};

using IsFakePlayer_t = bool(__fastcall*)(void*, int);
using AddMenuItem_t  = void*(__fastcall*)(void*, const char*, const char*, const char*);
using OnCommand_t    = void(__fastcall*)(void*, const char*);

static IsFakePlayer_t o_IsFakePlayer = nullptr;
static AddMenuItem_t  o_AddMenuItem  = nullptr;
static OnCommand_t    o_OnCommand    = nullptr;

static bool __fastcall h_IsFakePlayer(void* rcx, int index)
{
    const uintptr_t retAddr = uintptr_t(_ReturnAddress());

    if (s_addrIsFakePlayer_Call && retAddr == s_addrIsFakePlayer_Call)
        s_iPlayerIndex = index;

    return o_IsFakePlayer(rcx, index);
}

static void* __fastcall h_AddMenuItem(void* rcx,
    const char* pszButtonText, const char* pszCommand, const char* pszCategory)
{
    const uintptr_t retAddr = uintptr_t(_ReturnAddress());

    if (s_addrAddMenuItem_CallProfile && retAddr == s_addrAddMenuItem_CallProfile)
    {
        // SDK struct (name[32]) -> friendsID at right offset; name from big-buffer struct
        s_uAccountID = GetPlayerFriendsID(s_iPlayerIndex);
        PlayerInfo_t pi = {};
        if (g_engine && GetPlayerInfo(s_iPlayerIndex, &pi))
            strncpy_s(s_sPlayerName, pi.name, _TRUNCATE);
        else
            s_sPlayerName[0] = '\0';
        DebugLog("sbhook: profile call idx=%d acct=%u name='%s'",
            s_iPlayerIndex, s_uAccountID, s_sPlayerName);

        void* ret = o_AddMenuItem(rcx, pszButtonText, pszCommand, pszCategory);

        o_AddMenuItem(rcx, "Steam History",     "iv_history", "profile");

        uint64_t steam64 = FriendsIDToSteam64(s_uAccountID);
        // labels built under lock, items added after: AddMenuItem = VGUI code
        bool alreadyAliased = false;
        std::vector<std::string> markLabels;
        {
            AliasLockGuard lk;
            auto it = g_aliases.find(steam64);
            alreadyAliased = it != g_aliases.end();
            for (size_t i = 0; i < g_playerGroups.size() && i < 32; ++i)
            {
                bool inGroup = alreadyAliased && (it->second.groupMask & (1u << (int)i)) != 0;
                markLabels.push_back((inGroup ? "Remove: " : "Mark: ") + g_playerGroups[i]);
            }
        }
        o_AddMenuItem(rcx, alreadyAliased ? "Edit Alias" : "Add to Alias List", "iv_alias", "profile");

        for (size_t i = 0; i < markLabels.size(); ++i)
        {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "iv_mark%zu", i);
            o_AddMenuItem(rcx, markLabels[i].c_str(), cmd, "mark");
        }

        DebugLog("sbhook: injected items for idx=%d acct=%u name='%s'",
            s_iPlayerIndex, s_uAccountID, s_sPlayerName);
        return ret;
    }

    return o_AddMenuItem(rcx, pszButtonText, pszCommand, pszCategory);
}

static void __fastcall h_OnCommand(void* rcx, const char* command)
{
    if (!command)
    {
        o_OnCommand(rcx, command);
        return;
    }

    if (strcmp(command, "iv_history") == 0)
    {
        if (s_uAccountID)
        {
            uint64_t steam64 = FriendsIDToSteam64(s_uAccountID);
            char url[256];
            snprintf(url, sizeof(url), "https://steamhistory.net/id/%llu", (unsigned long long)steam64);
            SteamOverlay_OpenURL(url);
        }
        return;
    }

    if (strcmp(command, "iv_alias") == 0)
    {
        if (s_uAccountID) {
            AliasLockGuard lk;
            PlayerList_AddOrMark(FriendsIDToSteam64(s_uAccountID), s_sPlayerName[0] ? s_sPlayerName : nullptr, -1);
        }
        return;
    }

    if (strncmp(command, "iv_mark", 7) == 0)
    {
        int groupID = atoi(command + 7);
        if (s_uAccountID && groupID >= 0 && groupID < 32)
        {
            uint64_t steam64 = FriendsIDToSteam64(s_uAccountID);
            AliasLockGuard lk;
            auto it = g_aliases.find(steam64);
            if (it != g_aliases.end() && (it->second.groupMask & (1u << groupID)) != 0)
                it->second.groupMask &= ~(1u << groupID);
            else
                PlayerList_AddOrMark(steam64, s_sPlayerName[0] ? s_sPlayerName : nullptr, groupID);
        }
        return;
    }

    o_OnCommand(rcx, command);
}

void ScoreboardHook_Install()
{
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;

    uint8_t* fnIsFakePlayer = PatternScan(hClient,
        "48 89 5C 24 ? 57 48 81 EC ? ? ? ? 48 8B 01 48 8B F9 48 63 DA");

    uint8_t* fnAddMenuItem = PatternScan(hClient,
        "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B EA 49 8B F9 48 8B 51 ? "
        "49 8B F0 48 8B D9 48 85 D2 74 ? 49 8B C9 E8 ? ? ? ? 85 C0 74 ? 48 8B 0B 48 8B 01 "
        "FF 90 ? ? ? ? 48 8B 0B 4C 8B C6 4C 8B 4B ? 48 8B D5 48 89 7B ? 48 C7 44 24 ? ? ? ? ? "
        "48 8B 01 FF 90 ? ? ? ? 48 8B 13 3B 82 ? ? ? ? 73 ? 3B 82 ? ? ? ? 7F ? 48 8B 92 ? ? ? ? "
        "8B C8 48 03 C9 39 44 CA ? 75 ? 39 44 CA ? 75 ? 48 8B 04 CA EB ? 33 C0 "
        "48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 CC CC CC CC 48 89 5C 24");

    uint8_t* fnOnCommand = PatternScan(hClient,
        "48 89 5C 24 ? 57 48 83 EC ? 48 8B DA 48 8B F9 48 8B CB 48 8D 15 ? ? ? ? "
        "E8 ? ? ? ? 48 85 C0 74 ? 48 8B 0D");

    // call-site sigs: bytes start AT return addr in OnScoreBoardMouseRightRelease
    uint8_t* siteIsFakePlayer = PatternScan(hClient,
        "48 8B 0D ? ? ? ? 41 8B D6 48 81 C1 ? ? ? ? 44 0F B6 E8");

    uint8_t* siteAddMenuProfile = PatternScan(hClient,
        "48 8B 0D ? ? ? ? 4C 8D 85 ? ? ? ? 48 8D 95 ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 44 8B 85");

    DebugLog("sbhook: fn_IsFP=%p fn_AM=%p fn_OC=%p site_IsFP=%p site_Prof=%p",
        (void*)fnIsFakePlayer, (void*)fnAddMenuItem, (void*)fnOnCommand,
        (void*)siteIsFakePlayer, (void*)siteAddMenuProfile);

    if (siteIsFakePlayer)   s_addrIsFakePlayer_Call       = uintptr_t(siteIsFakePlayer);
    if (siteAddMenuProfile) s_addrAddMenuItem_CallProfile  = uintptr_t(siteAddMenuProfile);

    if (fnIsFakePlayer)
    {
        if (MH_CreateHook(fnIsFakePlayer, (LPVOID)h_IsFakePlayer, (LPVOID*)&o_IsFakePlayer) == MH_OK)
        {
            MH_EnableHook(fnIsFakePlayer);
            DebugLog("sbhook: IsFakePlayer hook OK");
        }
        else DebugLog("sbhook: IsFakePlayer hook FAILED");
    }

    if (fnAddMenuItem)
    {
        if (MH_CreateHook(fnAddMenuItem, (LPVOID)h_AddMenuItem, (LPVOID*)&o_AddMenuItem) == MH_OK)
        {
            MH_EnableHook(fnAddMenuItem);
            DebugLog("sbhook: AddMenuItem hook OK");
        }
        else DebugLog("sbhook: AddMenuItem hook FAILED");
    }

    if (fnOnCommand)
    {
        if (MH_CreateHook(fnOnCommand, (LPVOID)h_OnCommand, (LPVOID*)&o_OnCommand) == MH_OK)
        {
            MH_EnableHook(fnOnCommand);
            DebugLog("sbhook: OnCommand hook OK");
        }
        else DebugLog("sbhook: OnCommand hook FAILED");
    }
}
