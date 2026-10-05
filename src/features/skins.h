#pragma once
#include "globals.h"
#include "config.h"

// Skin_FindSpecificDef declared in menu.h; Skin_FindWeaponRedir in globals.h

using SetSequence_t = void(__fastcall*)(void*, int);
extern SetSequence_t o_CBaseAnimating_SetSequence;
void __fastcall h_CBaseAnimating_SetSequence(void* rcx, int nSequence);

using DoPostSSE_t = bool(__fastcall*)(void*, const void*);
extern DoPostSSE_t o_DoPostSSE;
bool __fastcall h_DoPostSSE(void* rcx, const void* pSetup);

// held reskin has set_turn_to_gold (Saxxy, Golden Wrench, Gold Pan). Attr is server-side ->
// ragdoll hook gilds corpse itself.
bool Skins_HeldWeaponTurnsToGold();
// Own killstreak count. Sheen paces off NETWORKED streak, eye glow needs >= tf_killstreakeyes_minkills (5),
// so no server ks item = both dead whatever attrs written. Stamp count into both client arrays.
// serverKs = event kill_streak_wep > 0. Ret new total (0 = leave event); *outWep = per-weapon count (0 = keep server's).
int  Skins_OnLocalKill(bool serverKs, int* outWep);
// death / spawn / class or team change / round. In demo only death = true ends it (demostreak.h).
void Skins_ResetStreak(const char* why, bool death = false);
using FrameStageNotify_t = void(__fastcall*)(int);
void __fastcall h_FrameStageNotify(int stage);
extern FrameStageNotify_t o_FrameStageNotify;

