#pragma once
#include "../globals.h"

// Web on surface: browser (WebView2, Steam ISteamHTMLSurface fallback) as panel and/or on a wall.
// Steam limits: no volume API (JS .volume), no extensions (CSS + JS + nav refusal only),
// audio from steamwebhelper (not positional), no Widevine.
// Draws in DoPostScreenSpaceEffects (viewrender.cpp:2157: after world, before viewmodels + HUD);
// on-top mode in Present. Post slot = main thread under mat_queue_mode -> queued like screenfx.

void Web_Present(IDirect3DDevice9* dev);             // render thread, inside the ImGui frame
void Web_MainTick();                                  // main thread every frame (FSN FRAME_START), menu too
void Web_OnPostScreenSpaceEffects(const void* setup); // the game's post slot
void Web_LevelShutdown();                             // map change: the placed screen goes away
void Web_Destroy();                                   // eject
void Web_InvalidateTexture();                         // h_Reset: DEFAULT pool texture is lost
bool Web_WantsSingleThreadedMat();                    // the mat_queue_mode fallback, shared with screenfx
bool Web_CaptureKey(int vk);                          // WndProcHook: a bind is being rebound
bool Web_OnInput(unsigned msg, uintptr_t wParam);     // WndProcHook, menu closed: wheel/arrows to the page
void Web_PlaceAtCrosshair();                          // menu button -- same thing the bind does
void Web_Navigate(const char* url);                   // menu / panel address bar, any thread
void Web_Unplace();                                   // take the screen off the wall
bool Web_BlockFire();                                 // CreateMove: crosshair on the screen, drop IN_ATTACK/2
void Web_RestoreOverColorCorr(IDirect3DDevice9* dev, IDirect3DTexture9* preCC, int w, int h); // render thread, after CC
void Web_ClearCache();                                // all browsing data (cookies, cache, history), fresh browser
void Web_ClearMemory();                               // restart the browser processes: RAM back, logins kept
const char* Web_Status();                             // one line for the menu
