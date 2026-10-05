#include "playerlist.h"
#include "../globals.h"  // GetPlayerInfo, g_engine, g_netvarsReady, g_gameEventManager, etc.
#include "../common.h"   // IGameEventVtbl

std::unordered_map<uint64_t, AliasEntry> g_aliases;
SRWLOCK                                   g_aliasLock = SRWLOCK_INIT;
std::vector<std::string>                  g_playerGroups;

// steam64s in the server at the last scan, for join edge-detect
static uint64_t s_presence[64];
static int      s_presenceN = 0;

char  g_playerJoinNotifyMsg[192]  = {};
float g_playerJoinNotifyTimer     = 0.f;

char               g_pendingChatNotifyMsg[192] = {};
char               g_pendingChatGroupMsg[128]  = {};
std::atomic<bool>  g_hasPendingChatNotify      { false };

void PlayerList_ScanServer()
{
    if (!g_engine || !g_netvarsReady) return;

    // 64 GetPlayerInfo calls every 100 ms buy nothing while nobody is on notify.
    {
        AliasLockGuard lk;
        bool anyNotify = false;
        for (auto& [_, ae] : g_aliases) if (ae.notifyOnServer) { anyNotify = true; break; }
        if (!anyNotify) { s_presenceN = 0; return; }
    }

    uint64_t present[64];
    int      presentN = 0;
    PlayerInfo_t pi{};
    for (int i = 1; i <= 64; ++i)
    {
        if (!GetPlayerInfo(i, &pi)) continue;
        if (pi.fakeplayer || pi.ishltv || pi.friendsID == 0) continue;
        present[presentN++] = FriendsIDToSteam64(pi.friendsID);
    }

    AliasLockGuard lk;
    // Edge-detect: joined this scan but not last
    for (int k = 0; k < presentN; ++k)
    {
        const uint64_t s64 = present[k];
        if (std::find(s_presence, s_presence + s_presenceN, s64) != s_presence + s_presenceN) continue;
        auto it = g_aliases.find(s64);
        if (it == g_aliases.end() || !it->second.notifyOnServer) continue;

        const char* alias = it->second.displayName[0]
            ? it->second.displayName : "a watched player";
        snprintf(g_playerJoinNotifyMsg, sizeof(g_playerJoinNotifyMsg),
            "[infovisual] %s is in the server", alias);
        g_playerJoinNotifyTimer = k_playerJoinNotifyDur;

        snprintf(g_pendingChatNotifyMsg, sizeof(g_pendingChatNotifyMsg),
            "infovisual - %s is in the server", alias);

        uint32_t mask = it->second.groupMask;
        if (mask) {
            char groupList[128] = {};
            bool first = true;
            for (int b = 0; b < 32 && b < (int)g_playerGroups.size(); ++b) {
                if (!(mask & (1u << b))) continue;
                if (!first) strncat_s(groupList, ", ", _TRUNCATE);
                strncat_s(groupList, g_playerGroups[b].c_str(), _TRUNCATE);
                first = false;
            }
            if (!first)
                snprintf(g_pendingChatGroupMsg, sizeof(g_pendingChatGroupMsg),
                    "%s is marked as %s", alias, groupList);
            else
                g_pendingChatGroupMsg[0] = '\0';
        } else {
            g_pendingChatGroupMsg[0] = '\0';
        }

        g_hasPendingChatNotify.store(true, std::memory_order_relaxed);
    }

    std::copy(present, present + presentN, s_presence);
    s_presenceN = presentN;
}

void PlayerList_RenderNotify(ImDrawList* dl, float scrW, float scrH)
{
    if (g_playerJoinNotifyTimer <= 0.f) return;

    float alpha = (g_playerJoinNotifyTimer > 0.5f)
        ? 1.f : g_playerJoinNotifyTimer / 0.5f;

    ImVec2 ts = ImGui::CalcTextSize(g_playerJoinNotifyMsg);
    float x = (scrW - ts.x) * 0.5f;
    float y = scrH * 0.12f;

    dl->AddRectFilled(
        ImVec2(x - 8.f, y - 4.f),
        ImVec2(x + ts.x + 8.f, y + ts.y + 4.f),
        IM_COL32(10, 10, 15, (int)(180 * alpha)), 4.f);
    dl->AddText(ImVec2(x, y),
        IM_COL32(255, 220, 80, (int)(255 * alpha)),
        g_playerJoinNotifyMsg);
}

void PlayerList_FireChatNotify()
{
    if (!g_hasPendingChatNotify.exchange(false, std::memory_order_relaxed)) return;
    if (!g_gameEventManager) return;

    __try {
        void** vgem = *(void***)g_gameEventManager;
        if (!vgem) return;

        // CGameEventManager slots from engine.dll (table in names.cpp): 6 CreateEvent,
        // 8 FireEventClientSide. This used to call 7 as CreateEvent -- that is FireEvent,
        // handed a string as the event -- and 9 (DuplicateEvent) to fire it.
        // "hltv_chat" because it is the one chat event the client HUD listens to
        // (CBaseHudChat::FireGameEvent prints "(SourceTV) <text>"); the old "server_say" has
        // no client listener, so CreateEvent returned null and nothing ever showed.
        using CreateEvent_t = void*(__fastcall*)(void*, const char*, bool);
        void* pEvent = ((CreateEvent_t)vgem[6])(g_gameEventManager, "hltv_chat", false);
        if (!pEvent) return;

        IGameEventVtbl::SetString(pEvent, "text", g_pendingChatNotifyMsg);

        using FireClientSide_t = bool(__fastcall*)(void*, void*);
        ((FireClientSide_t)vgem[8])(g_gameEventManager, pEvent);   // frees the event

        if (g_pendingChatGroupMsg[0] != '\0') {
            void* pEvent2 = ((CreateEvent_t)vgem[6])(g_gameEventManager, "hltv_chat", false);
            if (pEvent2) {
                IGameEventVtbl::SetString(pEvent2, "text", g_pendingChatGroupMsg);
                ((FireClientSide_t)vgem[8])(g_gameEventManager, pEvent2);
            }
            g_pendingChatGroupMsg[0] = '\0';
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
