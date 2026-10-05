#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <windows.h>
#include "../imgui/imgui.h"

// ---- Alias list data ---------------------------------------
struct AliasEntry
{
    uint64_t steam64;
    char     displayName[64];            // label shown in alias list
    bool     notifyOnServer;             // pop HUD message when they join
    bool     changeNameInScoreboard;     // override GetPlayerName return value
    uint32_t groupMask;                  // bitmask: bit i set → member of g_playerGroups[i]
};

// AliasEntry map: steam64 → entry
extern std::unordered_map<uint64_t, AliasEntry> g_aliases;

// g_aliases + g_playerGroups are written by the menu/config (render thread) and the scoreboard
// hooks (game thread), and read by GetPlayerName (game thread). A rehash under a concurrent
// find() is a crash, so every touch holds this. Never call into the engine while holding it.
extern SRWLOCK g_aliasLock;
struct AliasLockGuard {
    AliasLockGuard()  { AcquireSRWLockExclusive(&g_aliasLock); }
    ~AliasLockGuard() { ReleaseSRWLockExclusive(&g_aliasLock); }
    AliasLockGuard(const AliasLockGuard&) = delete;
    AliasLockGuard& operator=(const AliasLockGuard&) = delete;
};

// User-defined groups, max 32 (AliasEntry::groupMask bits)
extern std::vector<std::string> g_playerGroups;

// ---- Notification ------------------------------------------
// Shown in overlay for k_playerJoinNotifyDur seconds, then fades.
extern char  g_playerJoinNotifyMsg[192];
extern float g_playerJoinNotifyTimer;   // counts down from k_playerJoinNotifyDur
static constexpr float k_playerJoinNotifyDur = 4.f;

// Pending chat notification (set from D3D thread, fired from FSN game thread).
extern char               g_pendingChatNotifyMsg[192];
extern char               g_pendingChatGroupMsg[128]; // optional second line (group mark), empty = none
extern std::atomic<bool>  g_hasPendingChatNotify;

// ---- Steam overlay helpers ---------------------------------
// friendsID (uint32 from PlayerInfo_t) → steam64
inline uint64_t FriendsIDToSteam64(uint32_t accountID)
{
    return 0x0110000100000000ULL | (uint64_t)accountID;
}

// Get ISteamFriends pointer. TF2 x64 loads steam_api64.dll (not steam_api.dll).
// Tries flat API, legacy export, then SteamInternal_FindOrCreateUserInterface.
inline void* GetSteamFriends()
{
    // TF2 x64 uses steam_api64.dll; fall back to steam_api.dll for older builds
    HMODULE h = GetModuleHandle("steam_api64.dll");
    if (!h) h = GetModuleHandle("steam_api.dll");
    if (!h) return nullptr;

    // Flat API export (modern Steam SDK)
    auto pfnFlat = (void*(*)())GetProcAddress(h, "SteamAPI_SteamFriends_v017");
    if (pfnFlat) { void* p = pfnFlat(); if (p) return p; }

    // Legacy direct export (older Steam SDK)
    auto pfnLegacy = (void*(*)())GetProcAddress(h, "SteamFriends");
    if (pfnLegacy) { void* p = pfnLegacy(); if (p) return p; }

    // SteamInternal_FindOrCreateUserInterface path (works across SDK versions)
    using FindOrCreate_t = void*(*)(int, const char*);
    auto pfnFOC = (FindOrCreate_t)GetProcAddress(h, "SteamInternal_FindOrCreateUserInterface");
    if (pfnFOC) {
        using GetHUser_t = int(*)();
        auto pfnUser = (GetHUser_t)GetProcAddress(h, "SteamAPI_GetHSteamUser");
        if (pfnUser) {
            void* p = pfnFOC(pfnUser(), "SteamFriends017");
            if (p) return p;
        }
    }

    return nullptr;
}

// slot 30: ActivateGameOverlayToWebPage(url, 0)
inline void SteamOverlay_OpenURL(const char* url)
{
    __try {
        void* friends = GetSteamFriends();
        if (!friends) return;
        using Fn = void(__fastcall*)(void*, const char*, int);
        ((Fn)(*(void***)friends)[30])(friends, url, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Caller holds g_aliasLock.
inline void PlayerList_AddOrMark(uint64_t steam64, const char* nameHint, int groupID)
{
    auto& e = g_aliases[steam64];
    e.steam64 = steam64;
    if (e.displayName[0] == '\0' && nameHint)
        strncpy_s(e.displayName, nameHint, _TRUNCATE);
    if (groupID >= 0 && groupID < 32)
        e.groupMask |= (1u << groupID);
}

// Check current server for notifyOnServer entries; edge-detects joins.
// Called from the h_Present 100 ms tick.
void PlayerList_ScanServer();

// Render the "notify" HUD text; call from Present hook.
void PlayerList_RenderNotify(ImDrawList* dl, float scrW, float scrH);

// Fire a pending "hltv_chat" notification via IGameEventManager2.
// Must be called from the game thread (e.g. h_FrameStageNotify).
void PlayerList_FireChatNotify();
