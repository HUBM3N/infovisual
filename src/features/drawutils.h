#pragma once
#include "globals.h"

// GammaToLinear: sRGB -> linear (needed by mediaplayer and sheen color)
inline float GammaToLinear(float f)
{
    return (f <= 0.04045f) ? (f / 12.92f) : powf((f + 0.055f) / 1.055f, 2.4f);
}

// Shared drawing utilities
void GetAnimatedSheenColor(float out[3]);
ImDrawFlags CornerFlagsToImDraw(int cf);
void DrawBgBlurRect(ImDrawList* dl, float x0, float y0, float x1, float y1, float rounding, ImDrawFlags cf);
void DrawBoxShadow(ImDrawList* dl, float x0, float y0, float x1, float y1, float offX, float offY, float blur, const float color[4], float alphaScale, float rounding, ImDrawFlags cf);
void DrawOutlineSides(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col, float thick, int sf, float rounding, ImDrawFlags cornerFlags);
void DrawOutlineLayers(ImDrawList* dl, float x0, float y0, float x1, float y1, const OutlineLayer* layers, int count, float sc, float rounding, ImDrawFlags cornerFlags, float alphaScale = 1.f);
void DrawBlobGradient(ImDrawList* dl, float rx, float ry, float rw, float rh, const float blobColor[3], float alpha, float size, float speed, float timeOffset = 0.f, float rounding = 0.f);
void DrawPrecipitation(ImDrawList* dl, float bx, float by, float bw, float bh, int mode, int count, float speed, float size, float wind, const float col4[4], float dt, int slot);
void SpringToward(float& val, float& vel, float target, float dt, float omega = 22.f);
// Appends one texture + delay (seconds) per GIF frame; render thread only. False = nothing decoded.
bool LoadGifFrames(IDirect3DDevice9* dev, const char* path,
                   std::vector<IDirect3DTexture9*>& frames, std::vector<float>& delays, int& outW, int& outH);
void DrawBgSideGradients(ImDrawList* dl, float x0, float y0, float x1, float y1, const BgSideGrad grads[4], ImU32 solidColor, float rounding = 0.f, ImDrawFlags cf = 0, bool overlayOnly = false);

// Returns alpha fade factor [0,1] at point (px,py) based on BgSideGrad settings.
// 0 = fully transparent (at/beyond outer boundary), 1 = opaque (inside inner boundary).
inline float ComputeBgFadeFactor(float px, float py, float x0, float y0, float x1, float y1, const BgSideGrad grads[4])
{
    float fade = 1.f;
    const float W = x1 - x0, H = y1 - y0;
    if (W <= 0.f || H <= 0.f) return 1.f;
    auto side = [](float t, float s, float e) -> float {
        if (t <= e) return 0.f;
        if (t >= s) return 1.f;
        return (s > e) ? (t - e) / (s - e) : 1.f;
    };
    if (grads[0].enable) fade = std::min(fade, side((px - x0) / W, grads[0].start, grads[0].end));
    if (grads[1].enable) fade = std::min(fade, side((x1 - px) / W, grads[1].start, grads[1].end));
    if (grads[2].enable) fade = std::min(fade, side((py - y0) / H, grads[2].start, grads[2].end));
    if (grads[3].enable) fade = std::min(fade, side((y1 - py) / H, grads[3].start, grads[3].end));
    return fade;
}
