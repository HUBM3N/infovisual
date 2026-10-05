#pragma once
#include "../globals.h"

// Screen effects: shared depth source (own INTZ via RESZ amd/intel or NvAPI StretchRectEx nvidia,
// msaa ok) + passes reading it. Ported from dih (SimpleVisual) <- coldtires.
// Draws at DoPostScreenSpaceEffects (hooked in skins.cpp): 3D done, no viewmodel/hud yet.
// Multicore -> queued to render thread at same pos. Health probe keeps passes off till real depth.

void ScreenFx_OnPostScreenSpaceEffects(const void* setup);   // from h_DoPostSSE

void ScreenFx_NewFrame();   // h_Present: frame latch + learns render thread

// mat_queue_mode fallback (only when call queue refused), 100ms tick
void ScreenFx_Tick();
void ScreenFx_RestoreQueueMode();   // eject
void ScreenFx_LevelShutdown();      // map change: release mat_queue_mode before load

const char* ScreenFx_Status();      // menu line, "" when all off

void ScreenFx_Destroy();            // all D3DPOOL_DEFAULT: h_Reset + eject

IDirect3DTexture9* ScreenFx_DepthTexture();   // null when no route

// View setup from game post pass. False = never delivered / implausible: no pass may run.
struct FxViewSetup {
    float fov;      // horizontal, degrees
    float zNear;
    float zFar;
    int   width;
    int   height;
};
bool ScreenFx_View(FxViewSetup& out);

struct FxVec3 {
    float x = 0.f, y = 0.f, z = 0.f;
    FxVec3() {}
    FxVec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    FxVec3 operator+(const FxVec3& v) const { return { x + v.x, y + v.y, z + v.z }; }
    FxVec3 operator-(const FxVec3& v) const { return { x - v.x, y - v.y, z - v.z }; }
    FxVec3 operator*(float f)         const { return { x * f, y * f, z * f }; }
};

// Fullscreen quad: ps_3_0 won't pair w/ fixed-function VP -> one passthrough vs_3_0 + decl.
// Clip-space pos; target size does half-texel shift.
struct FxQuadVert { float x, y, z, w, u, v; };

bool ScreenFx_QuadReady(IDirect3DDevice9* dev);
void ScreenFx_BindQuad(IDirect3DDevice9* dev);
void ScreenFx_DrawQuad(IDirect3DDevice9* dev, int targetW, int targetH);

// ssao.cpp
void Ssao_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
               const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
               const FxViewSetup& view);
void Ssao_Destroy();

// dof.cpp. Focus trace = main thread, same post slot; draw half only reads its result.
void Dof_UpdateFocus(const FxVec3& origin, const FxVec3& angles);

void Dof_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
              const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
              const FxViewSetup& view);
void Dof_Destroy();

// frameblend.cpp: motion blur (velocity buffer, tile chain, gather). Runs TWICE a frame:
//   gunPass false  post slot: world + players, no gun yet
//   gunPass true   ViewDrawFade (gun drawn, no hud): gun only, velocity from BONES
void FrameBlend_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* target,
                     const D3DSURFACE_DESC& desc, IDirect3DTexture9* depth,
                     const FxViewSetup& view, bool gunPass = false);

// Main thread, post slot: camera history, reprojection, player boxes. Draw half only reads.
void FrameBlend_UpdateView(const float origin[3], const float angles[3],
                           float fov, float fovVm, float zNearVm, float zFarVm);

// Draw thread, DrawModelExecute detour, one vm part per call. `weapon` splits the 16 bone slots
// between c_ weapon and arms, else hands take them all.
void FrameBlend_CollectBones(const void* pState, const void* pInfo, const void* pBones, bool weapon);

void FrameBlend_NewFrame();   // h_Present: frame id bones + camera pair on
void FrameBlend_Reset();      // map change
void FrameBlend_Destroy();

// ViewDrawFade hook (gun window: after DrawViewModels, before hud); also arms ScreenFx_OnPush2DView.
// Installed from PollThread.
void ScreenFx_InstallViewmodelPass();

// From flipworld.cpp's Push2DView hook, before original: bloom + cc under hud. Only first push
// after a ViewDrawFade draws (escape menu's later push doesn't).
void ScreenFx_OnPush2DView();
