#pragma once
#include "globals.h"
#include "drawutils.h"

// --- Footstep hooks ----------------------------------------------------------
extern float s_hwStepAccum;
using TF_IsHolidayActive_t = bool(__fastcall*)(int);
extern TF_IsHolidayActive_t o_TF_IsHolidayActive;
using AttribHookInt_t = int(__fastcall*)(int, const char*, void*, void*, bool);
extern AttribHookInt_t o_AttribHookInt;
void HwFoot_Tick();
__declspec(noinline) bool __fastcall h_TF_IsHolidayActive(int eHoliday);
__declspec(noinline) int  __fastcall h_AttribHookInt(int value, const char* name, void* econent, void* buffer, bool isGlobalConstString);

// --- Speedometer / menu snow --------------------------------------------------
void MenuSectionLabel(const char* label);
void RenderMenuSnow(float alpha);
void RenderSpeedometer();

// --- Minesweeper -------------------------------------------------------------
void RenderMinesweeper();

// --- Media Player (SMTC) -----------------------------------------------------
DWORD WINAPI MediaPlayerThread(LPVOID);
void MediaPlayer_Invalidate();
void MediaPlayer_Drain();   // release thumbs parked by the poll thread -- runs with the player off too
void RenderMediaPlayer();
