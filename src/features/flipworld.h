#pragma once
#include "flipmath.h"

// Flip World: mirror rendered 3D frame in screen space (Aletherium CFlipWorld copy + mirrored
// DrawScreenSpaceRectangle); everything else made to agree:
//   WHERE  end of h_DoPostSSE, right before DrawViewModels (viewrender.cpp:2157/2160): world +
//          particles flip inside CViewSetup rect; vm, its particles, 2D HUD stay unmirrored.
//   HAND   cl_flipviewmodels string inverted via real SetValue (-> m_bFlipViewModels,
//          tf_gamerules.cpp:10281) so projectiles leave mirrored side; client int poked back to own
//          hand (FlipClientHand) so gun draws unflipped. config.cfg keeps own hand (FlipCfgWrite).
//   SWAY   CalcViewModelLag offset reflected across view right (FlipMirrorSway).
//   HUD    W2S-placed elements get mirrored matrix outside 3D pass; angle-placed ones byte-patched
//          (k_flipHudPatches).
//   INPUT  A/D (before ClientMode CreateMove: taunt turn + kart see it) + m_yaw sign mirrored.
// Non-frame parts follow mirror only while frame on screen went through it (FlipFrameTrack); one
// owner DLL per process (shared mapping). Decisions = FlipCoreTick, tested by tests/flipworld_test.cpp.

void FlipWorld_Install();                // PollThread, once: config writer, Push2D, sway hooks + HUD patch sites
bool FlipWorld_NeedsRT();                // LevelInit would create the RT (none yet, or backbuffer resized)
void FlipWorld_LevelInit();              // create RT + material once, reopen the gate (CViewRender::LevelInit hook)
void FlipWorld_LevelUnload();            // release RT + material
void FlipWorld_LevelShutdown();          // IBaseClientDLL::LevelShutdown: stop, hand everything back
bool FlipWorld_Mirroring();              // the frame on screen went through this DLL's mirror (any thread)
void FlipWorld_RenderStart();            // FRAME_RENDER_START: the 3D pass begins (W2S stays true)
void FlipWorld_WorldPass();              // DoPostScreenSpaceEffects: this frame draws the world
void FlipWorld_Render(const void* pViewSetup);  // mirror the frame drawn so far (end of DoPostSSE)
void FlipWorld_Tick();                   // game thread: m_yaw, HUD patches, cl_flipviewmodels
void FlipWorld_Release();                // eject: all back (the cvar write only on the game thread)
void FlipWorld_Unload();                 // after the eject drain: give the owner token back

// CClientModeShared::CreateMove hook (slot 21): command mirror + Tick
using FlipCreateMove_t = bool(__fastcall*)(void* rcx, float flInputSampleTime, void* pCmd);
extern FlipCreateMove_t o_FlipCreateMove;
bool __fastcall h_FlipCreateMove(void* rcx, float flInputSampleTime, void* pCmd);

// IVEngineClient::WorldToScreenMatrix hook (slot 36), returns const VMatrix& = float[4][4]
using FlipW2S_t = const float*(__fastcall*)(void* rcx);
extern FlipW2S_t o_FlipW2S;
const float* __fastcall h_FlipW2S(void* rcx);
