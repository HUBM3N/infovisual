#pragma once
#include <d3d9.h>

void WeaponHud_Tick(IDirect3DDevice9* dev);
void WeaponHud_InvalidateTextures();

// rasterize defIdx SVG at heightPx -> D3DPOOL_DEFAULT tex, *outW = px width. null = no SVG.
IDirect3DTexture9* WpnHud_RasterizeForKf(IDirect3DDevice9* dev, int defIdx, int heightPx, int* outW);
