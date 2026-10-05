#pragma once
#include "globals.h"
#include "drawutils.h"

// ImGui frame (h_Present); lazily builds icon sheets
void KillfeedTick();

// h_Reset + eject: release textures, restore hud_deathnotice_time
void KillfeedUnload();

// raw write of saved hud_deathnotice_time (float+int fields); render.cpp on runtime disable
void KillfeedRestoreNoticeTime();

// names.cpp h_FireEvent (game thread). team: 2=RED 3=BLU 0=unknown.
// weaponDefIdx: 0 = unknown; picks per-reskin SVG for weapons sharing event weapon (AWPer Hand).
// iconIsPlainWeapon: icon from weapon name (SVG override ok), false = customkill icon (headshot etc).
void KillfeedPushEntry(const char* attackerName, const char* victimName,
                       const char* iconName, bool localIsAttacker,
                       int attackerTeam, int victimTeam, int weaponDefIdx = 0,
                       bool iconIsPlainWeapon = false);
