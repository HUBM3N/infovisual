#pragma once
#include "globals.h"
#include "drawutils.h"
#include "config.h"
#include "watermark.h"
#include "crosshair.h"
#include "misc.h"

int  Skin_FindSpecificDef(int weaponRedir, int pkId);
void SetupImGuiStyle();

void RenderMenu();
float Menu_Alpha();     // 0..1 open/close fade
bool  Menu_Visible();   // open, or still fading out
void  Menu_RenderDrawData(ImDrawData* dd);   // ImGui_ImplDX9_RenderDrawData + menu fade layer
void  Menu_ReleaseLayer();                   // D3DPOOL_DEFAULT: call on reset and eject
