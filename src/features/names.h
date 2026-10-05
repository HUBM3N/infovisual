#pragma once
#include "globals.h"
#include "config.h"

using FireEvent_t           = bool(__fastcall*)(void*, void*, bool);
extern FireEvent_t           o_FireEvent;
bool __fastcall h_FireEvent(void* thisptr, void* pEvent, bool bDontBroadcast);

// streamer name masking
using GetPlayerName_t = const char*(__fastcall*)(void*, int);
extern GetPlayerName_t o_GetPlayerName;
const char* __fastcall h_GetPlayerName(void* rcx, int iIndex);
