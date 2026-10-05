#pragma once
#include "globals.h"
#include "drawutils.h"

void EnumerateFonts();
void ResolveFontIndex();
void RenderLoadingCircle();
void DrawLogo(ImDrawList* dl, float x, float y, float w, float h, ImU32 tint);
void DrawLogoImage(ImDrawList* dl, IDirect3DTexture9* tex, float x, float y, float w, float h, ImU32 tint, float angle = 0.f);
void RenderWatermarkAndOverlay();
void RenderOverlayImages();

// Called from render.cpp h_Present
void LogoIrisAnimTick();

// Called from menu.cpp. FilePick_Start opens the file dialog on its own thread; target is an
// overlay image slot (0..k_imgMaxSlots-1) or one of the FILEPICK_ ids. FilePick_Service runs
// every frame from h_Present and applies the picked path.
enum : int { FILEPICK_LOGO = 100, FILEPICK_MEDIA_THUMB = 101 };
void FilePick_Start(int target);
void FilePick_Service();
void WmLogoImg_ReleaseFrames();

// Called from watermark.cpp RenderWatermarkAndOverlay
const char* GetTitleLabel(float delta);
const char* GetUsernameLabel();
