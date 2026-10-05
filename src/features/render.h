#pragma once
#include "../globals.h"

// --- SetCursorPos hook -------------------------------------------------------
using SetCursorPos_t = BOOL(WINAPI*)(int, int);
extern SetCursorPos_t o_SetCursorPos;
BOOL WINAPI h_SetCursorPos(int x, int y);

// --- DX9 present / reset hooks ---------------------------------------
using Present_t = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset_t   = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
extern Present_t    o_Present;
extern Reset_t      o_Reset;
HRESULT __stdcall h_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND win, const RGNDATA* dirty);
HRESULT __stdcall h_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp);

// --- Render helpers (render.cpp) ---------------------------------------------
std::string GetWmFontPath();
void Logo_InvalidateTexture();
void Logo_CreateTexture(IDirect3DDevice9* dev);
void RebuildWatermarkFont();   // queues an atlas build on a Jobs worker
void Font_Pump();              // render thread, before NewFrame: swaps a finished build in
void Font_DropPending();       // eject, after Jobs::Stop
void BlurRT_Init(IDirect3DDevice9* dev);
void BlurRT_Destroy();
void BlurFrame_Run(IDirect3DDevice9* dev, float sigma, bool widen = false);
void ColorCorr_Init(IDirect3DDevice9* dev, IDirect3DSurface9* bb);
void ColorCorr_Destroy();
bool ColorCorr_Apply(IDirect3DDevice9* dev, IDirect3DSurface9* bb);   // false = no pass ran this frame
// Bloom + CC under the hud: render thread, at Push2DView (queued/forked by screenfx.cpp).
void SceneFx_PreHud(IDirect3DDevice9* dev);
void SceneFx_PresentFallback(IDirect3DDevice9* dev);   // h_Present: only if PreHud missed the frame
void Render_ReleaseAllTextures();   // full D3D9 teardown, for eject
LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void DLightTick();
bool GetDX9VTable(void** vt, int n);
