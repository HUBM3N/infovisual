#include "globals.h"
#include "config.h"
#include "names.h"
#include "killfeed.h"
#include "playerlist.h"
#include "skins.h"   // Skins_OnLocalKill / Skins_ResetStreak -- the killstreak count

// Streamer mode: 0=off, 1=class names, 2=team role, 3=everyone--label
int         g_streamerMode          = 0;
std::string g_streamerEveryoneLabel = "player";
std::string g_streamerTeamLabel     = "teammate";
std::string g_streamerEnemyLabel    = "enemy";
bool        g_useCustomUsername     = false;
std::string g_customUsername        = "";

// Dynamic name cycler (requires g_useCustomUsername)
NameEntry_t g_nameList[k_maxNames] = {};
int         g_nameCount         = 0;
bool        g_enableNameCycle   = false;
int         g_nameTrigger       = 0;     // 0=timer, 1=on kill
float       g_nameInterval      = 5.0f;  // seconds
int         g_nameIdx           = 0;
ULONGLONG   g_nameLastChange    = 0;

// Streamer label cyclers (mode 2: team+enemy, mode 3: everyone) — share g_nameTrigger/g_nameInterval
bool        g_enableTeamLabelCycle    = false;
NameEntry_t g_teamLabelList[k_maxNames] = {};
int         g_teamLabelCount          = 0;
int         g_teamLabelIdx            = 0;
ULONGLONG   g_teamLabelLastChange     = 0;

bool        g_enableEnemyLabelCycle   = false;
NameEntry_t g_enemyLabelList[k_maxNames] = {};
int         g_enemyLabelCount         = 0;
int         g_enemyLabelIdx           = 0;
ULONGLONG   g_enemyLabelLastChange    = 0;

bool        g_enableEveryoneLabelCycle = false;
NameEntry_t g_everyoneLabelList[k_maxNames] = {};
int         g_everyoneLabelCount      = 0;
int         g_everyoneLabelIdx        = 0;
ULONGLONG   g_everyoneLabelLastChange = 0;
static const char* const k_ClassNames[10] = {
    "unknown", "scout", "sniper", "soldier", "demoman",
    "medic",   "heavy", "pyro",   "spy",     "engineer"
};
// IGameResources thisptr -- stored on first h_GetPlayerName call
static void* s_gameResources = nullptr;

// ---- Killfeed filter -- FireEventClientSide hook -----------
// CGameEventManager vtable, read out of TF2 x64 engine.dll (RTTI .?AVCGameEventManager@@),
// same order as sdk igameevents.h -- there is NO extra slot:
//   0 ~dtor  1 LoadEventsFromFile  2 Reset  3 AddListener  4 FindListener  5 RemoveListener
//   6 CreateEvent  7 FireEvent (xor r9d,r9d; jmp FireEventIntern)
//   8 FireEventClientSide (mov r9b,1; jmp FireEventIntern)  9 DuplicateEvent  10 FreeEvent
//   11 SerializeEvent  12 UnserializeEvent
// Events the server sends reach the client HUD listeners (CHudBaseDeathNotice) through slot 8,
// which is what this hooks. The bool arg is unused by that slot and just passed through.
FireEvent_t o_FireEvent = nullptr;

// FireEvent owns the event: every path through the engine's FireEventIntern ends in FreeEvent
// (igameevents.h: "if an event was created but not fired ... it has to be freed"). Dropping an
// event without calling the original used to leak it, one per filtered kill. FreeEvent = slot 10
// (table above; its body is `test rdx,rdx; ... jmp [vtable+0]`, the event's deleting dtor).
__declspec(noinline) static bool DropEvent(void* mgr, void* pEvent)
{
    __try {
        using FreeEvent_t = void(__fastcall*)(void*, void*);
        ((FreeEvent_t)(*(void***)mgr)[10])(mgr, pEvent);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}


bool __fastcall h_FireEvent(void* thisptr, void* pEvent, bool bDontBroadcast)
{
    // ---- Kill tracking. UNCONDITIONAL ----------------------------------------------------------
    // This used to live inside the g_selfKillfeedOnly block below, which meant the ragdoll
    // effects, the "only my kills" filter and the gold statue all silently did nothing unless
    // that unrelated killfeed toggle happened to be on. It also feeds the killstreak count.
    // fish_notice / slap_notice are here because the Holy Mackerel and the Hot Hand announce
    // their kills through those instead of player_death.
    if (pEvent)
    {
        __try
        {
            const char* kname = IGameEventVtbl::GetName(pEvent);
            if (kname && (strcmp(kname, "player_death")     == 0 ||
                          strcmp(kname, "fish_notice")      == 0 ||
                          strcmp(kname, "fish_notice__arm") == 0 ||
                          strcmp(kname, "slap_notice")      == 0))
            {
                const int localIdx = GetLocalPlayerIndex();
                if (localIdx >= 1) {
                    const int attackerUid = IGameEventVtbl::GetInt(pEvent, "attacker", 0);
                    const int victimUid   = IGameEventVtbl::GetInt(pEvent, "userid",   0);
                    const int attackerIdx = (attackerUid > 0) ? GetPlayerForUserID(attackerUid) : 0;
                    const int victimIdx   = (victimUid   > 0) ? GetPlayerForUserID(victimUid)   : 0;

                    if (victimIdx == localIdx) {
                        // Not a Dead Ringer feign: same event, death_flags carries
                        // TF_DEATH_FEIGN_DEATH (tf_shareddefs.h:1811), and we are alive.
                        if (IGameEventVtbl::GetInt(pEvent, "death_flags", 0) & 0x0020)
                            DebugLog("skins: feign death, streak kept");
                        else
                            Skins_ResetStreak("death", true);
                    } else if (attackerIdx == localIdx && victimIdx > 0) {
                        g_ragdollKillSet.insert(victimIdx);
                        // The event's own counts feed the kill notice's streak banner, so they
                        // are corrected alongside the entity fields. kill_streak_wep > 0 = the
                        // server counted this kill (real killstreak weapon); read before the write.
                        // Total = every weapon; wep = this weapon only (killfeed number).
                        int wep = 0;
                        const int streak = Skins_OnLocalKill(
                            IGameEventVtbl::GetInt(pEvent, "kill_streak_wep", 0) > 0, &wep);
                        if (streak > 0) IGameEventVtbl::SetInt(pEvent, "kill_streak_total", streak);
                        if (wep > 0)    IGameEventVtbl::SetInt(pEvent, "kill_streak_wep",   wep);
                    }
                }
            }
            // Every other way a streak ends.
            else if (kname && (strcmp(kname, "player_spawn")       == 0 ||
                               strcmp(kname, "player_changeclass") == 0))
            {
                const int uid = IGameEventVtbl::GetInt(pEvent, "userid", 0);
                if (uid > 0 && GetPlayerForUserID(uid) == GetLocalPlayerIndex())
                    Skins_ResetStreak(kname);
            }
            else if (kname && (strcmp(kname, "teamplay_round_start")    == 0 ||
                               strcmp(kname, "teamplay_round_active")   == 0 ||
                               strcmp(kname, "teamplay_win_panel")      == 0 ||
                               strcmp(kname, "localplayer_changeclass") == 0 ||
                               strcmp(kname, "localplayer_changeteam")  == 0))
            {
                Skins_ResetStreak(kname);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (pEvent && g_selfKillfeedOnly)
    {
        __try
        {
            const char* name = IGameEventVtbl::GetName(pEvent);
            if (name)
            {
                const int localIdx = GetLocalPlayerIndex();
                if (localIdx >= 1)
                {
                    // Convert userid field value -> entity index
                    auto uid2idx = [](int uid) -> int {
                        return (uid > 0) ? GetPlayerForUserID(uid) : 0;
                    };

                    // ---- player_death / fish humiliation / slap humiliation ----
                    if (strcmp(name, "player_death")     == 0 ||
                        strcmp(name, "fish_notice")      == 0 ||
                        strcmp(name, "fish_notice__arm") == 0 ||
                        strcmp(name, "slap_notice")      == 0)
                    {
                        int attackerIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
                        int victimIdx   = uid2idx(IGameEventVtbl::GetInt(pEvent, "userid",   0));
                        int assisterIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "assister", 0));
                        bool involved = (attackerIdx == localIdx) ||
                                        (victimIdx   == localIdx) ||
                                        (assisterIdx == localIdx && assisterIdx > 0);
                        if (!involved)
                            return DropEvent(thisptr, pEvent);
                        // Kill tracking (ragdoll effects, gold statue, killstreak) is done
                        // unconditionally at the top of this hook -- it must not depend on this
                        // toggle.
                        // On-kill name cycle: advance when local player gets a kill
                        if (attackerIdx == localIdx && victimIdx != localIdx && g_nameTrigger == 1)
                        {
                            if (g_useCustomUsername && g_enableNameCycle && g_nameCount > 0)
                                g_nameIdx = (g_nameIdx + 1) % g_nameCount;
                            if (g_streamerMode == 2) {
                                if (g_enableTeamLabelCycle  && g_teamLabelCount  > 0) g_teamLabelIdx  = (g_teamLabelIdx  + 1) % g_teamLabelCount;
                                if (g_enableEnemyLabelCycle && g_enemyLabelCount > 0) g_enemyLabelIdx = (g_enemyLabelIdx + 1) % g_enemyLabelCount;
                            }
                            if (g_streamerMode == 3 && g_enableEveryoneLabelCycle && g_everyoneLabelCount > 0)
                                g_everyoneLabelIdx = (g_everyoneLabelIdx + 1) % g_everyoneLabelCount;
                        }
                    }
                    // ==== object_destroyed: show when local player destroys OR
                    //    owns the destroyed building ========================================================
                    else if (strcmp(name, "object_destroyed") == 0)
                    {
                        int attackerIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
                        int ownerIdx    = uid2idx(IGameEventVtbl::GetInt(pEvent, "userid",   0));
                        if (attackerIdx != localIdx && ownerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ---- point captures / start-captures: cappers byte string ----
                    else if (strcmp(name, "teamplay_point_captured")    == 0 ||
                             strcmp(name, "teamplay_point_startcapture") == 0)
                    {
                        // "cappers" is raw bytes where each byte IS the entity index
                        const char* cappers = IGameEventVtbl::GetString(pEvent, "cappers", "");
                        bool involved = false;
                        if (cappers)
                        {
                            for (int i = 0; cappers[i] != '\0'; ++i)
                            {
                                if ((int)(unsigned char)cappers[i] == localIdx)
                                {
                                    involved = true;
                                    break;
                                }
                            }
                        }
                        if (!involved)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ---- teamplay_capture_blocked: blocker is entity index -----
                    else if (strcmp(name, "teamplay_capture_blocked") == 0)
                    {
                        // "blocker" is an entity index directly
                        int blockerIdx = IGameEventVtbl::GetInt(pEvent, "blocker", 0);
                        if (blockerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ==== teamplay_flag_event (pickup/capture/defend/drop/return)
                    //    "player" is an entity index directly ========================================
                    else if (strcmp(name, "teamplay_flag_event") == 0)
                    {
                        int playerIdx = IGameEventVtbl::GetInt(pEvent, "player", 0);
                        if (playerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ---- rd_robot_killed: "attacker" is a userid ---------------
                    else if (strcmp(name, "rd_robot_killed") == 0)
                    {
                        int attackerIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
                        if (attackerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ---- special_score: "player" is an entity index ------------
                    else if (strcmp(name, "special_score") == 0)
                    {
                        int playerIdx = IGameEventVtbl::GetInt(pEvent, "player", 0);
                        if (playerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ---- duck_xp_level_up: "player" is an entity index ---------
                    else if (strcmp(name, "duck_xp_level_up") == 0)
                    {
                        int playerIdx = IGameEventVtbl::GetInt(pEvent, "player", 0);
                        if (playerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ---- team_leader_killed: entity indices --------------------
                    else if (strcmp(name, "team_leader_killed") == 0)
                    {
                        int killerIdx = IGameEventVtbl::GetInt(pEvent, "killer", 0);
                        int victimIdx = IGameEventVtbl::GetInt(pEvent, "victim", 0);
                        if (killerIdx != localIdx && victimIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ---- Passtime events (all use entity indices) --------------
                    else if (strcmp(name, "passtime_ball_get") == 0)
                    {
                        int ownerIdx = IGameEventVtbl::GetInt(pEvent, "owner", 0);
                        if (ownerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    else if (strcmp(name, "passtime_ball_stolen") == 0)
                    {
                        int victimIdx   = IGameEventVtbl::GetInt(pEvent, "victim",   0);
                        int attackerIdx = IGameEventVtbl::GetInt(pEvent, "attacker", 0);
                        if (victimIdx != localIdx && attackerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    else if (strcmp(name, "passtime_score") == 0)
                    {
                        int scorerIdx = IGameEventVtbl::GetInt(pEvent, "scorer", 0);
                        if (scorerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    else if (strcmp(name, "passtime_pass_caught") == 0)
                    {
                        int passerIdx  = IGameEventVtbl::GetInt(pEvent, "passer",  0);
                        int catcherIdx = IGameEventVtbl::GetInt(pEvent, "catcher", 0);
                        if (passerIdx != localIdx && catcherIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    else if (strcmp(name, "passtime_ball_blocked") == 0)
                    {
                        int ownerIdx   = IGameEventVtbl::GetInt(pEvent, "owner",   0);
                        int blockerIdx = IGameEventVtbl::GetInt(pEvent, "blocker", 0);
                        if (ownerIdx != localIdx && blockerIdx != localIdx)
                            return DropEvent(thisptr, pEvent);
                    }
                    // ---- suppress non-player-specific killfeed noise -----------
                    else if (strcmp(name, "teamplay_round_win")       == 0 ||
                             strcmp(name, "teamplay_round_stalemate") == 0 ||
                             strcmp(name, "controlpoint_updateowner") == 0 ||
                             strcmp(name, "ctf_flag_captured")        == 0)
                    {
                        return DropEvent(thisptr, pEvent);
                    }
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    // ---- Exorcism spell -- ghost particle on kill (unconditional) ----
    // Runs regardless of g_selfKillfeedOnly. Exorcism (halloween_death_ghosts,
    // attr 1009) is server-side so attribute injection alone doesn't show ghosts.
    // We manually dispatch the particle on every player_death where the local
    // player is the attacker and the active weapon has Exorcism selected.
    if (pEvent && g_enableSkins && g_fnDispatchEffect && g_fnGetParticleIndex &&
        g_offOrigin > 0 && g_offTeam > 0)
    {
        __try {
            const char* ename = IGameEventVtbl::GetName(pEvent);
            if (ename && strcmp(ename, "player_death") == 0)
            {
                const int localIdx  = GetLocalPlayerIndex();
                int attackerUid     = IGameEventVtbl::GetInt(pEvent, "attacker", 0);
                int victimUid       = IGameEventVtbl::GetInt(pEvent, "userid",   0);
                int attackerIdx     = (attackerUid > 0) ? GetPlayerForUserID(attackerUid) : 0;
                int victimIdx       = (victimUid   > 0) ? GetPlayerForUserID(victimUid)   : 0;

                if (localIdx >= 1 && attackerIdx == localIdx &&
                    victimIdx > 0 && victimIdx != localIdx)
                {
                    // Exorcism fires when the kill weapon has the flag configured
                    // (custom skin) OR has the vanilla halloween_death_ghosts attr (1009).
                    // Read the local player's active weapon at the moment the event fires.
                    bool doGhosts = false;
                    if (g_offActiveWeapon > 0 && g_fsn_offItemDefIndex > 0) {
                        __try {
                            void* pLocal2 = GetClientEntity(localIdx);
                            if (pLocal2) {
                                unsigned int hActive = *reinterpret_cast<unsigned int*>(
                                    reinterpret_cast<uintptr_t>(pLocal2) + g_offActiveWeapon);
                                int wpnIdx = (int)(hActive & 0x3FFF);
                                void* pWpn = (wpnIdx > 0) ? GetClientEntity(wpnIdx) : nullptr;
                                if (pWpn) {
                                    int def = *reinterpret_cast<int*>(
                                        reinterpret_cast<uintptr_t>(pWpn) + g_fsn_offItemDefIndex);
                                    if (def > 0) {
                                        Skin_RedirectIndex(def);
                                        // If already at a specific 15xxx skin def, reverse-map
                                        if (def >= 15000 && def < 16000) {
                                            int redir = Skin_FindWeaponRedir(def);
                                            if (redir > 0) def = redir;
                                        }
                                        auto it = g_skinCfgs.find(def);
                                        if (it != g_skinCfgs.end() && it->second.enabled
                                            && (it->second.spellFlags & kCfgSpell_Exorcism))
                                            doGhosts = true;
                                    }
                                    // Vanilla weapon with Exorcism spell (attr 1009) --
                                    // server won't dispatch ghosts on non-Halloween servers,
                                    // so check the weapon's attribute list directly and
                                    // dispatch ourselves regardless of holiday state.
                                    if (!doGhosts && g_fsn_offAttrList > 0) {
                                        __try {
                                            CAttributeList* pList = reinterpret_cast<CAttributeList*>(
                                                reinterpret_cast<uintptr_t>(pWpn) + g_fsn_offAttrList);
                                            int sz = pList->m_Attributes.m_Size;
                                            auto* mem = pList->m_Attributes.m_Memory.m_pMemory;
                                            if (mem && sz > 0 && sz < 48 &&
                                                reinterpret_cast<uintptr_t>(mem) >= 0x10000)
                                            {
                                                for (int ai = 0; ai < sz; ++ai) {
                                                    if (mem[ai].m_iAttributeDefinitionIndex == 1009) {
                                                        doGhosts = true;
                                                        break;
                                                    }
                                                }
                                            }
                                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                    }
                                }
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }

                    if (doGhosts) {
                        __try {
                            void* pVictim = GetClientEntity(victimIdx);
                            if (pVictim) {
                                float* pos = reinterpret_cast<float*>(
                                    reinterpret_cast<uintptr_t>(pVictim) + g_offOrigin);
                                int teamNum = *reinterpret_cast<int*>(
                                    reinterpret_cast<uintptr_t>(pVictim) + g_offTeam);
                                // TF_TEAM_BLU=3, TF_TEAM_RED=2
                                const char* pname = (teamNum == 3)
                                    ? "halloween_player_death_blue"
                                    : "halloween_player_death";
                                int pidx = 0;
                                __try { pidx = g_fnGetParticleIndex(pname); }
                                __except(EXCEPTION_EXECUTE_HANDLER) {}
                                if (pidx > 0) {
                                    CEffectData_Weather data = {};
                                    data.m_vOrigin[0] = pos[0];
                                    data.m_vOrigin[1] = pos[1];
                                    data.m_vOrigin[2] = pos[2] + 32.f;
                                    data.m_nHitBox    = pidx;
                                    data.m_flScale    = 1.f;
                                    __try { g_fnDispatchEffect("ParticleEffect", data); }
                                    __except(EXCEPTION_EXECUTE_HANDLER) {}
                                }
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ---- Watermark kill glow trigger ---------------------------
    // Unconditional -- not gated on g_selfKillfeedOnly.
    if (pEvent && g_wmKillGlowEnable)
    {
        __try {
            const char* name = IGameEventVtbl::GetName(pEvent);
            if (name && strcmp(name, "player_death") == 0)
            {
                const int localIdx = GetLocalPlayerIndex();
                if (localIdx >= 1)
                {
                    int attackerUid = IGameEventVtbl::GetInt(pEvent, "attacker", 0);
                    int victimUid   = IGameEventVtbl::GetInt(pEvent, "userid",   0);
                    int attackerIdx = (attackerUid > 0) ? GetPlayerForUserID(attackerUid) : 0;
                    int victimIdx   = (victimUid   > 0) ? GetPlayerForUserID(victimUid)   : 0;
                    if (attackerIdx == localIdx && victimIdx > 0 && victimIdx != localIdx)
                        g_wmKillGlowRising = true;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ---- Killfeed icon: rewrite "weapon" field to show active reskin icon ----
    // Runs unconditionally (not gated on g_selfKillfeedOnly) so the icon is
    // correct even when killfeed-only mode is off.
    if (pEvent && g_enableSkins &&
        g_offActiveWeapon > 0 && g_fsn_offItemDefIndex > 0)
    {
        __try {
            const char* name = IGameEventVtbl::GetName(pEvent);
            if (name && strcmp(name, "player_death") == 0)
            {
                const int localIdx = GetLocalPlayerIndex();
                auto uid2idx = [](int uid) -> int {
                    return (uid > 0) ? GetPlayerForUserID(uid) : 0;
                };
                int attackerIdx = uid2idx(IGameEventVtbl::GetInt(pEvent, "attacker", 0));
                if (attackerIdx == localIdx && localIdx >= 1)
                {
                    void* pLocal2 = GetClientEntity(localIdx);
                    if (pLocal2) {
                        unsigned int hActive = *reinterpret_cast<unsigned int*>(
                            reinterpret_cast<uintptr_t>(pLocal2) + g_offActiveWeapon);
                        int wpnEnt = (int)(hActive & 0x3FFF);
                        void* pWpn = (wpnEnt > 0) ? GetClientEntity(wpnEnt) : nullptr;
                        if (pWpn) {
                            int def = *reinterpret_cast<int*>(
                                reinterpret_cast<uintptr_t>(pWpn) + g_fsn_offItemDefIndex);
                            // A kill icon is literally "d_" + this string
                            // (hud_basedeathnotice.cpp:515), resolved against the entries in
                            // tf/scripts/mod_textures.txt. k_reskinInfo carries the item's own
                            // item_logname, and NULL for the five reskins mod_textures has no
                            // entry for (skullbat, prinny_machete, awper_hand, giger_counter,
                            // shooting_star) -- writing one of those makes the HUD fall back to
                            // the SKULL, so the event is left alone instead.
                            const ReskinInfo* ri = Skin_FindReskinInfo(def);
                            const char* newLogname = ri ? ri->logname : nullptr;
                            if (newLogname) {
                                IGameEventVtbl::SetString(pEvent, "weapon", newLogname);
                                IGameEventVtbl::SetString(pEvent, "weapon_logclassname", newLogname);
                            }
                        }
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ---- Custom killfeed push (data collection) ----------------
    // Collect AFTER the reskin icon block (weapon field may have been overridden above).
    // The actual KillfeedPushEntry call happens AFTER o_FireEvent so that TF2 naturally
    // calls CHudBaseDeathNotice::GetIcon first -- our hook on that function captures
    // the real 'this' pointer needed to resolve icons.
    struct KfData { char att[128]; char vic[128]; char icon[64]; bool localIsAtt; int attTeam; int vicTeam; int defIdx; bool iconIsPlainWeapon; };
    KfData kf = {};
    bool kfPush = false;
    if (pEvent && g_customKillfeedEnable && g_selfKillfeedOnly)
    {
        __try {
            const char* ename = IGameEventVtbl::GetName(pEvent);
            if (ename && strcmp(ename, "player_death") == 0)
            {
                const int localIdx = GetLocalPlayerIndex();
                if (localIdx >= 1)
                {
                    int attackerUid = IGameEventVtbl::GetInt(pEvent, "attacker", 0);
                    int victimUid   = IGameEventVtbl::GetInt(pEvent, "userid",   0);
                    int attackerIdx = (attackerUid > 0) ? GetPlayerForUserID(attackerUid) : 0;
                    int victimIdx   = (victimUid   > 0) ? GetPlayerForUserID(victimUid)   : 0;

                    bool iKilled   = (attackerIdx == localIdx && victimIdx > 0 && victimIdx != localIdx);
                    bool iDied     = (victimIdx   == localIdx && attackerIdx > 0 && attackerIdx != localIdx);
                    bool iSuicided = (victimIdx   == localIdx && (attackerIdx == 0 || attackerIdx == localIdx));

                    if (iKilled || iDied || iSuicided)
                    {
                        // Skip feigned Dead Ringer deaths — spy didn't actually die.
                        int deathFlags = IGameEventVtbl::GetInt(pEvent, "death_flags", 0);
                        if (deathFlags & 0x0020) { /* feign death — do nothing */ }
                        else
                        {

                        const char* weapon    = IGameEventVtbl::GetString(pEvent, "weapon", "");
                        int customkill        = IGameEventVtbl::GetInt(pEvent, "customkill", 0);
                        // CHudBaseDeathNotice::GetIcon keys always use "d_" prefix.
                        // customkill overrides take priority over the raw weapon name
                        // unless g_kfPlainWeaponIcons is set (always use plain weapon icon).
                        static char s_iconBuf[80];
                        const char* iconName = nullptr;
                        if (!g_kfPlainWeaponIcons) {
                            switch (customkill) {
                                case 51: // TF_DMG_CUSTOM_HEADSHOT_DECAPITATION
                                case 1:  // TF_DMG_CUSTOM_HEADSHOT
                                    if (strcmp(weapon, "huntsman") == 0)
                                        iconName = "d_huntsman_headshot";
                                    else if (strcmp(weapon, "ambassador") == 0)
                                        iconName = "d_ambassador_headshot";
                                    else if (strcmp(weapon, "sniperrifle")    == 0 ||
                                             strcmp(weapon, "machina")         == 0 ||
                                             strcmp(weapon, "shooting_star")   == 0 ||
                                             strcmp(weapon, "bazaar_bargain")  == 0 ||
                                             strcmp(weapon, "classic")         == 0 ||
                                             strcmp(weapon, "hitman_heatmaker") == 0)
                                        iconName = "d_headshot";
                                    // else: non-sniper weapon with customkill=1 (custom server quirk)
                                    // — leave iconName null so the plain weapon icon is used instead
                                    break;
                                case 2:  iconName = "d_backstab"; break;        // TF_DMG_CUSTOM_BACKSTAB
                                case 17: iconName = "d_huntsman_burning"; break; // TF_DMG_CUSTOM_BURNING_ARROW
                                case 18: iconName = "d_huntsman_flyingburn"; break; // TF_DMG_CUSTOM_FLYINGBURN
                                case 19: iconName = "d_pumpkindeath"; break;    // TF_DMG_CUSTOM_PUMPKIN_BOMB
                                default: break;
                            }
                        }
                        if (!iconName && (!weapon || !weapon[0] ||
                            strcmp(weapon, "world")   == 0 ||
                            strcmp(weapon, "fall")    == 0 ||
                            strcmp(weapon, "suicide") == 0))
                            iconName = "d_skull";
                        bool iconIsPlainWeapon = (iconName == nullptr);
                        if (!iconName && weapon && weapon[0]) {
                            // Prepend "d_" to raw weapon name (e.g. "knife" -> "d_knife")
                            snprintf(s_iconBuf, sizeof(s_iconBuf), "d_%s", weapon);
                            iconName = s_iconBuf;
                        }
                        strncpy_s(kf.icon, iconName ? iconName : "", _TRUNCATE);
                        kf.iconIsPlainWeapon = iconIsPlainWeapon;
                        kf.localIsAtt = iKilled;

                        // Read the kill weapon's item definition index so the killfeed can
                        // distinguish reskins that share the same event weapon field (e.g. AWPer Hand).
                        kf.defIdx = 0;
                        if (g_offActiveWeapon > 0 && g_fsn_offItemDefIndex > 0) {
                            int attEntIdx = iDied ? attackerIdx : localIdx;
                            if (attEntIdx > 0) {
                                __try {
                                    void* pAtt = GetClientEntity(attEntIdx);
                                    if (pAtt) {
                                        unsigned int hActive = *reinterpret_cast<unsigned int*>(
                                            reinterpret_cast<uintptr_t>(pAtt) + g_offActiveWeapon);
                                        int wpnIdx = (int)(hActive & 0x3FFF);
                                        void* pWpn = (wpnIdx > 0) ? GetClientEntity(wpnIdx) : nullptr;
                                        if (pWpn)
                                            kf.defIdx = *reinterpret_cast<int*>(
                                                reinterpret_cast<uintptr_t>(pWpn) + g_fsn_offItemDefIndex);
                                    }
                                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            }
                        }

                        // Helper: get player name -- cache first (has streamer mask), on-demand fallback
                        auto getName = [&](int idx) -> const char* {
                            // Class-name mode: bypass stale cache and read current class directly.
                            // Cache only updates on GetPlayerName calls, which can lag class changes.
                            if (g_streamerMode == 1 && idx >= 1 && idx <= 64) {
                                int cls = GetPlayerClass(idx);
                                return k_ClassNames[(cls >= 0 && cls <= 9) ? cls : 0];
                            }
                            if (idx >= 1 && idx <= 64 && g_nameCache[idx][0] != '\0')
                                return g_nameCache[idx];
                            if (s_gameResources && o_GetPlayerName) {
                                __try {
                                    const char* n = o_GetPlayerName(s_gameResources, idx);
                                    if (n && n[0]) return n;
                                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            }
                            return "?";
                        };

                        // Local player display name.
                        // Priority: custom username → name cache (may have streamer mask)
                        //   → localRealName → on-demand IGameResources lookup → "me"
                        const char* localName = nullptr;
                        if (g_useCustomUsername) {
                            // Name cycler: pick current cycle entry.
                            // g_nameIdx was already advanced at the top of h_FireEvent for kill-trigger
                            // mode, so step back one to read the name that was current at kill time.
                            if (g_enableNameCycle && g_nameCount > 0) {
                                int cycleIdx = (g_nameTrigger == 1 && iKilled)
                                    ? ((g_nameIdx + g_nameCount - 1) % g_nameCount)
                                    : ((g_nameIdx < g_nameCount) ? g_nameIdx : 0);
                                if (g_nameList[cycleIdx].text[0]) localName = g_nameList[cycleIdx].text;
                            }
                            if (!localName && !g_customUsername.empty())
                                localName = g_customUsername.c_str();
                        }
                        if (!localName && localIdx >= 1 && localIdx <= 64 && g_nameCache[localIdx][0])
                            localName = g_nameCache[localIdx];
                        if (!localName && g_localRealName[0])
                            localName = g_localRealName;
                        if (!localName && s_gameResources && o_GetPlayerName) {
                            __try {
                                const char* n = o_GetPlayerName(s_gameResources, localIdx);
                                if (n && n[0]) localName = n;
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        }
                        if (!localName) localName = "me";

                        if (iKilled) {
                            strncpy_s(kf.att, localName, _TRUNCATE);
                            strncpy_s(kf.vic, getName(victimIdx), _TRUNCATE);
                        } else if (iDied) {
                            strncpy_s(kf.att, getName(attackerIdx), _TRUNCATE);
                            strncpy_s(kf.vic, localName, _TRUNCATE);
                        } else {
                            // iSuicided: world/fall → no attacker; self-damage → name on both sides
                            if (attackerIdx == 0)
                                kf.att[0] = '\0';
                            else
                                strncpy_s(kf.att, localName, _TRUNCATE);
                            strncpy_s(kf.vic, localName, _TRUNCATE);
                        }

                        if (g_offTeam > 0) {
                            __try {
                                int aIdx = iKilled ? localIdx   : (iDied ? attackerIdx : localIdx);
                                int vIdx = iKilled ? victimIdx  : localIdx;
                                void* pA = (attackerIdx > 0) ? GetClientEntity(aIdx) : nullptr;
                                void* pV = GetClientEntity(vIdx);
                                if (pA) kf.attTeam = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pA) + g_offTeam);
                                if (pV) kf.vicTeam = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pV) + g_offTeam);
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        }
                        kfPush = true;
                        DebugLog("KF collect: att='%s' vic='%s' icon='%s' local_is_att=%d", kf.att, kf.vic, kf.icon, (int)kf.localIsAtt);

                        } // end else (not feign death)
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool result = o_FireEvent(thisptr, pEvent, bDontBroadcast);

    // Push AFTER o_FireEvent: by now TF2 has called CHudBaseDeathNotice::GetIcon
    // internally (our hook captured 'this'), so icon lookups will succeed.
    if (kfPush)
        KillfeedPushEntry(kf.att, kf.vic, kf.icon, kf.localIsAtt, kf.attTeam, kf.vicTeam, kf.defIdx, kf.iconIsPlainWeapon);

    return result;
}

// ---- Streamer name masking ---------------------------------
char g_nameCache[65][128]  = {};
char g_localRealName[128]  = {}; // real Steam name before killfeed masking

using GetPlayerName_t = const char*(__fastcall*)(void*, int);
GetPlayerName_t o_GetPlayerName = nullptr;

static const char* GetStreamedPlayerName(int iIndex, int localIdx, const char* sRealName);

// h_GetPlayerName: HUD panels call it many times a frame, so no heap work in here.
// The __try lives one frame above in GetPlayerName_SEH (AliasLockGuard unwinds, C2712).
__declspec(noinline) static const char* GetPlayerName_Masked(int iIndex, const char* sOriginal)
{
    if (iIndex < 1 || iIndex > 64) return sOriginal;
    const int localIdx = GetLocalPlayerIndex();

    // Always cache real name for every player so killfeed lookups work
    // even when streamer mode is off.
    if (sOriginal && sOriginal[0])
    {
        strncpy(g_nameCache[iIndex], sOriginal, 127);
        g_nameCache[iIndex][127] = '\0';
    }
    if (iIndex == localIdx && sOriginal)
    {
        strncpy(g_localRealName, sOriginal, 127);
        g_localRealName[127] = '\0';
    }

    // Alias list: if a custom scoreboard name is set, return it
    {
        uint32_t acctID = GetPlayerFriendsID(iIndex);
        if (acctID != 0) {
            uint64_t s64 = FriendsIDToSteam64(acctID);
            AliasLockGuard lk;
            auto it = g_aliases.find(s64);
            if (it != g_aliases.end() &&
                it->second.changeNameInScoreboard &&
                it->second.displayName[0] != '\0')
            {
                strncpy(g_nameCache[iIndex], it->second.displayName, 127);
                g_nameCache[iIndex][127] = '\0';
                return g_nameCache[iIndex];
            }
        }
    }

    const char* masked = GetStreamedPlayerName(iIndex, localIdx, sOriginal ? sOriginal : "");
    if (!masked || !masked[0] || (sOriginal && strcmp(masked, sOriginal) == 0)) return sOriginal;
    strncpy(g_nameCache[iIndex], masked, 127);
    g_nameCache[iIndex][127] = '\0';
    return g_nameCache[iIndex];
}

__declspec(noinline) static const char* GetPlayerName_SEH(int iIndex, const char* sOriginal)
{
    __try { return GetPlayerName_Masked(iIndex, sOriginal); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return sOriginal; }
}

const char* __fastcall h_GetPlayerName(void* rcx, int iIndex)
{
    if (!s_gameResources) s_gameResources = rcx;
    const char* sOriginal = o_GetPlayerName(rcx, iIndex);
    return GetPlayerName_SEH(iIndex, sOriginal);
}

// Returns a pointer into a label/name list or sRealName; the caller copies it at once.
static const char* GetStreamedPlayerName(int iIndex, int localIdx, const char* sRealName)
{
    // Local player always shows their real Steam name when any streamer mode
    // is active -- modes 1/2/3 only mask other players, never yourself.
    // The custom username override still takes priority if explicitly set.
    if (iIndex == localIdx)
    {
        if (g_useCustomUsername)
        {
            if (g_enableNameCycle && g_nameCount > 0)
            {
                // Timer trigger: advance when interval has elapsed
                if (g_nameTrigger == 0)
                {
                    ULONGLONG now = AnimTimeMs();   // game time (animclock.h)
                    ULONGLONG ms  = (ULONGLONG)(g_nameInterval * 1000.0f);
                    if (g_nameLastChange == 0) g_nameLastChange = now;
                    if (now - g_nameLastChange >= ms)
                    {
                        g_nameIdx = (g_nameIdx + 1) % g_nameCount;
                        g_nameLastChange = now;
                    }
                }
                int idx = (g_nameIdx < g_nameCount) ? g_nameIdx : 0;
                if (g_nameList[idx].text[0] != '\0')
                    return g_nameList[idx].text;
            }
            if (!g_customUsername.empty())
                return g_customUsername.c_str();
        }
        // For modes 1 (class), 2 (team), 3 (everyone): return real name.
        return sRealName ? sRealName : "";
    }

    if (g_streamerMode == 0)
        return sRealName ? sRealName : "";

    switch (g_streamerMode)
    {
    case 1: {
        int cls = GetPlayerClass(iIndex);
        return k_ClassNames[(cls >= 0 && cls <= 9) ? cls : 0];
    }
    case 2: {
        // localIdx can be -1 when not in-game; passing a negative index to
        // GetClientEntity crashes inside client.dll (vtable slot 3 has no
        // bounds check for negative indices).
        if (localIdx < 1) return "player";
        int localTeam  = GetPlayerTeam(localIdx);
        int playerTeam = GetPlayerTeam(iIndex);
        if (localTeam == 0 || playerTeam == 0) return "player";
        if (playerTeam == localTeam) {
            if (g_enableTeamLabelCycle && g_teamLabelCount > 0) {
                if (g_nameTrigger == 0) {
                    ULONGLONG now = AnimTimeMs();   // game time (animclock.h)
                    ULONGLONG ms  = (ULONGLONG)(g_nameInterval * 1000.0f);
                    if (g_teamLabelLastChange == 0) g_teamLabelLastChange = now;
                    if (now - g_teamLabelLastChange >= ms) {
                        g_teamLabelIdx = (g_teamLabelIdx + 1) % g_teamLabelCount;
                        g_teamLabelLastChange = now;
                    }
                }
                int idx = (g_teamLabelIdx < g_teamLabelCount) ? g_teamLabelIdx : 0;
                if (g_teamLabelList[idx].text[0] != '\0')
                    return g_teamLabelList[idx].text;
            }
            return g_streamerTeamLabel.c_str();
        } else {
            if (g_enableEnemyLabelCycle && g_enemyLabelCount > 0) {
                if (g_nameTrigger == 0) {
                    ULONGLONG now = AnimTimeMs();   // game time (animclock.h)
                    ULONGLONG ms  = (ULONGLONG)(g_nameInterval * 1000.0f);
                    if (g_enemyLabelLastChange == 0) g_enemyLabelLastChange = now;
                    if (now - g_enemyLabelLastChange >= ms) {
                        g_enemyLabelIdx = (g_enemyLabelIdx + 1) % g_enemyLabelCount;
                        g_enemyLabelLastChange = now;
                    }
                }
                int idx = (g_enemyLabelIdx < g_enemyLabelCount) ? g_enemyLabelIdx : 0;
                if (g_enemyLabelList[idx].text[0] != '\0')
                    return g_enemyLabelList[idx].text;
            }
            return g_streamerEnemyLabel.c_str();
        }
    }
    case 3: {
        if (g_enableEveryoneLabelCycle && g_everyoneLabelCount > 0) {
            if (g_nameTrigger == 0) {
                ULONGLONG now = AnimTimeMs();   // game time (animclock.h)
                ULONGLONG ms  = (ULONGLONG)(g_nameInterval * 1000.0f);
                if (g_everyoneLabelLastChange == 0) g_everyoneLabelLastChange = now;
                if (now - g_everyoneLabelLastChange >= ms) {
                    g_everyoneLabelIdx = (g_everyoneLabelIdx + 1) % g_everyoneLabelCount;
                    g_everyoneLabelLastChange = now;
                }
            }
            int idx = (g_everyoneLabelIdx < g_everyoneLabelCount) ? g_everyoneLabelIdx : 0;
            if (g_everyoneLabelList[idx].text[0] != '\0')
                return g_everyoneLabelList[idx].text;
        }
        return g_streamerEveryoneLabel.c_str();
    }
    default:
        return sRealName ? sRealName : "";
    }
}
