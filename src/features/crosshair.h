#pragma once
#include "globals.h"
#include "drawutils.h"

void RenderCrosshair();

// CS:GO sniper scope (scope_csgo.inl): hooks CHudScope::Paint + the sniper dot. Active = painted
// on the last HUD pass (visuals holds "crosshair" 0 on it); Restore = unhook (eject).
void ScopeCsgo_Install(HMODULE client);
bool ScopeCsgo_Active();
void ScopeCsgo_Restore();
void ScopeCsgo_ReleaseTextures();

// Menu preview: whichever crosshairs are enabled, drawn into [min,max] with their own
// animation state. testSpeed drives the dynamic gap (< 0 = standing still).
void Crosshair_DrawPreview(ImDrawList* dl, ImVec2 min, ImVec2 max, float testSpeed);

// csgo reticle art + the custom crosshair's blend-mode shaders and frame copy (DEFAULT
// pool) -- drop them on device reset and on eject.
void CsgoCrosshair_ReleaseTextures();

// Dropdown of the custom crosshair's blend modes (ch_blend_modes.h), one pick at a time.
// Returns true while the closed combo is hovered, for the caller's tooltip.
bool Crosshair_BlendCombo(const char* id);
