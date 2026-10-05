#pragma once
#include "../common.h"

extern bool g_glowRTsReady;

using LevelInit_t = void(__fastcall*)(void*);
extern LevelInit_t o_LevelInit;
void __fastcall h_LevelInit(void* rcx);

bool GlowVM_NeedsRT();      // LevelInit would create RTs (none yet, or backbuffer resized)
void GlowVM_LevelInit();
void GlowVM_LevelUnload();

// col[4] = RGBA, sigma = blur radius, stencil = pixel-shift halo thickness (0=off).
void GlowVM_DrawWeapon(void* rcx_ent, int flags, float col[4], float sigma, int stencil);
void GlowVM_DrawArms(void* rcx, const void* pState, const void* pInfo, const void* pBones,
                     float col[4], float sigma, int stencil);
