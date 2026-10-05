#pragma once
#include "../globals.h"

// Bloom. Once per frame from SceneFx_PreHud (render.cpp), before colour grade (lens before film).
// Pre-hud, so hud doesn't glow; missed frame falls back to h_Present, hud included.
bool Bloom_Frame(IDirect3DDevice9* dev);
void Bloom_Destroy();   // device Reset, device change, eject
