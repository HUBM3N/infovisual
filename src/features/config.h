#pragma once
#include "globals.h"

void SaveConfig(const std::string& path = CONFIG_PATH);
void LoadConfig(const std::string& path = CONFIG_PATH);

// Defined in watermark.cpp
void EnumerateFonts();
void ResolveFontIndex();
int  FontIndexForFile(const std::string& file);

void Skin_RedirectIndex(int& d);

// SEH helpers: noinline so the __try frames stay out of the callers (C2712)
__declspec(noinline) bool         FSN_IsInGame();
__declspec(noinline) unsigned int FSN_ReadWeaponHandle(void* pLocal, int offset, int i);
__declspec(noinline) bool         FSN_VtableSane(void* pEnt);
__declspec(noinline) void*        FSN_GetEntityFromHandle(unsigned int hWeapon);
__declspec(noinline) void         FSN_InjectAttribs(CAttributeList* pAttrList);
__declspec(noinline) int          FSN_ReadItemDefIndex(void* pWeapon, int offset);
__declspec(noinline) void         FSN_WriteItemDefIndex(void* pWeapon, int offset, int val);
