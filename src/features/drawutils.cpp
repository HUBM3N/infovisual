#include "drawutils.h"
#include "gifdecode.h"

// Blur runs entirely pre-frame (see BlurFrame_Run in render.cpp).
// DrawBgBlurRect just samples the already-blurred texture at the panel's UV region.
void DrawBgBlurRect(ImDrawList* dl, float x0, float y0, float x1, float y1, float rounding, ImDrawFlags cf)
{
    if (!g_blurTexA || g_blurShaderW <= 0 || g_blurShaderH <= 0) return;
    float u0 = x0 / (float)g_blurShaderW, v0 = y0 / (float)g_blurShaderH;
    float u1 = x1 / (float)g_blurShaderW, v1 = y1 / (float)g_blurShaderH;
    dl->AddImageRounded((ImTextureID)g_blurTexA,
        ImVec2(x0, y0), ImVec2(x1, y1),
        ImVec2(u0, v0), ImVec2(u1, v1),
        IM_COL32_WHITE, rounding, cf);
}

void DrawBoxShadow(ImDrawList* dl,
    float x0, float y0, float x1, float y1,
    float offX, float offY, float blur,
    const float color[4], float alphaScale,
    float rounding, ImDrawFlags cf)
{
    float sx0 = x0 + offX, sy0 = y0 + offY;
    float sx1 = x1 + offX, sy1 = y1 + offY;
    int R = (int)(color[0]*255), G = (int)(color[1]*255), B = (int)(color[2]*255);
    float baseA = std::clamp(color[3] * alphaScale, 0.f, 1.f);
    if (blur > 0.01f) {
        const int N = 8;
        for (int i = N-1; i >= 0; --i) {
            float t = (float)(i+1) / N;
            float expand = t * blur;
            float a = baseA * (1.f - t) * 0.8f;
            int ai = (int)(a * 255.f);
            if (ai <= 0) continue;
            dl->AddRectFilled(
                ImVec2(sx0 - expand, sy0 - expand),
                ImVec2(sx1 + expand, sy1 + expand),
                IM_COL32(R,G,B,ai), rounding + expand, cf);
        }
    }
    dl->AddRectFilled(ImVec2(sx0,sy0), ImVec2(sx1,sy1),
        IM_COL32(R,G,B,(int)(baseA*255.f)), rounding, cf);
}

// sideFlags: 0=all, bit0=top, bit1=right, bit2=bottom, bit3=left
// Draws the outline as up to 4 separate lines so individual sides can be hidden.
void DrawOutlineSides(ImDrawList* dl, float x0, float y0, float x1, float y1,
                             ImU32 col, float thick, int sf,
                             float rounding, ImDrawFlags cornerFlags)
{
    if (sf == 0) {
        dl->AddRect(ImVec2(x0,y0), ImVec2(x1,y1), col, rounding, cornerFlags, thick);
        return;
    }
    ImVec2 pts[4] = { {x0,y0}, {x1,y0}, {x1,y1}, {x0,y1} };
    int sideBit[4] = { 1, 2, 4, 8 };
    for (int i = 0; i < 4; ++i) {
        if (!(sf & sideBit[i])) continue;
        dl->AddLine(pts[i], pts[(i+1)%4], col, thick);
    }
}

// Draw all enabled outline layers for a box, each one starting right outside the previous.
// alphaScale multiplies the alpha of every layer (used by watermark for fade-in/out).
void DrawOutlineLayers(ImDrawList* dl,
                              float x0, float y0, float x1, float y1,
                              const OutlineLayer* layers, int count,
                              float sc, float rounding, ImDrawFlags cornerFlags,
                              float alphaScale)
{
    float offset = 0.f;
    for (int i = 0; i < count; ++i)
    {
        const OutlineLayer& L = layers[i];
        if (!L.enable) continue;
        float t      = L.thick * sc;
        float halfT  = t * 0.5f;
        float expand = offset + halfT;
        float lx0    = x0 - expand, ly0 = y0 - expand;
        float lx1    = x1 + expand, ly1 = y1 + expand;
        float layerRounding = rounding > 0.f ? rounding + expand : 0.f;
        ImU32 col = IM_COL32(
            (int)(L.color[0]*255),
            (int)(L.color[1]*255),
            (int)(L.color[2]*255),
            (int)(L.color[3]*255 * alphaScale));
        if (L.blur > 0.f) {
            const int   kBN     = 8;
            const float blurPx  = L.blur * sc;
            for (int bi = kBN; bi >= 1; --bi) {
                float bt   = (float)bi / (float)kBN;
                float bexp = expand + blurPx * bt;
                int   bai  = (int)((1.0f - bt) * L.color[3] * alphaScale * 200.f);
                if (bai <= 0) continue;
                float blr  = rounding > 0.f ? rounding + bexp : 0.f;
                ImU32 bcol = IM_COL32(
                    (int)(L.color[0]*255),
                    (int)(L.color[1]*255),
                    (int)(L.color[2]*255), bai);
                DrawOutlineSides(dl, x0-bexp, y0-bexp, x1+bexp, y1+bexp,
                                 bcol, t, L.sideFlags, blr, cornerFlags);
            }
        }
        DrawOutlineSides(dl, lx0, ly0, lx1, ly1, col, t, L.sideFlags, layerRounding, cornerFlags);
        offset += t;
    }
}

void DrawBlobGradient(ImDrawList* dl,
                             float rx, float ry, float rw, float rh,
                             const float blobColor[3], float alpha,
                             float size, float speed,
                             float timeOffset,
                             float rounding)
{
    if (alpha <= 0.f || rw <= 0.f || rh <= 0.f) return;

    float t = (float)(AnimTimeMs() % 1000000u) * 0.001f * speed + timeOffset;   // game time (animclock.h)

    float nx = 0.38f*sinf(t*0.19f) + 0.27f*sinf(t*0.31f + 1.1f) + 0.19f*sinf(t*0.53f + 2.7f);
    float ny = 0.38f*sinf(t*0.23f + 0.5f) + 0.27f*sinf(t*0.37f + 2.3f) + 0.19f*sinf(t*0.47f + 0.9f);
    nx /= 0.84f * 2.f;
    ny /= 0.84f * 2.f;
    float cx = rx + rw * (0.5f + nx * 0.55f);
    float cy = ry + rh * (0.5f + ny * 0.55f);

    float sigma  = size * std::min(rw, rh);
    float sigma2 = sigma * sigma * 2.f;
    if (sigma2 <= 0.f) return;

    float warpAmp1 = sigma * 0.45f;
    float warpAmp2 = sigma * 0.18f;
    float fx1 = 2.8f / std::max(rw, rh);
    float fy1 = 2.8f / std::max(rw, rh);
    float fx2 = 5.6f / std::max(rw, rh);
    float fy2 = 5.6f / std::max(rw, rh);
    float ts1 = t * 0.27f;
    float ts2 = t * 0.61f;

    float r = std::min(rounding, std::min(rw, rh) * 0.5f);

    auto cornerMask = [&](float px, float py) -> float {
        if (r <= 0.f) return 1.f;
        bool inCornerX = (px < rx + r) || (px > rx + rw - r);
        bool inCornerY = (py < ry + r) || (py > ry + rh - r);
        if (inCornerX && inCornerY) {
            float qx = (px < rx + r) ? (rx + r) : (rx + rw - r);
            float qy = (py < ry + r) ? (ry + r) : (ry + rh - r);
            float dx = px - qx, dy = py - qy;
            float dist = sqrtf(dx*dx + dy*dy);
            return std::clamp(r - dist + 1.f, 0.f, 1.f);
        }
        return 1.f;
    };

    const int COLS = 32, ROWS = 24;
    float stepX = rw / COLS;
    float stepY = rh / ROWS;

    float bright[ROWS+1][COLS+1];
    for (int row = 0; row <= ROWS; row++) {
        float vy = ry + row * stepY;
        for (int col = 0; col <= COLS; col++) {
            float vx = rx + col * stepX;

            float wx = warpAmp1 * sinf(vx*fx1 + ts1        ) * cosf(vy*fy1 + ts1*0.7f    )
                     + warpAmp2 * sinf(vx*fx2 + ts2 + 1.3f ) * cosf(vy*fy2 + ts2*1.4f + 2.1f);
            float wy = warpAmp1 * cosf(vx*fx1 + ts1*1.3f   ) * sinf(vy*fy1 + ts1 + 0.9f  )
                     + warpAmp2 * cosf(vx*fx2 + ts2*0.8f+0.7f) * sinf(vy*fy2 + ts2 + 3.7f);

            float wx2 = vx + wx - cx;
            float wy2 = vy + wy - cy;
            float gauss = expf(-(wx2*wx2 + wy2*wy2) / sigma2);

            bright[row][col] = gauss * cornerMask(vx, vy);
        }
    }

    int ir = (int)(blobColor[0]*255);
    int ig = (int)(blobColor[1]*255);
    int ib = (int)(blobColor[2]*255);

    auto mkV = [&](float b) -> ImU32 {
        int a = (int)(b * alpha * 255.f);
        return IM_COL32(ir, ig, ib, a < 0 ? 0 : a);
    };

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            float b00 = bright[row  ][col  ];
            float b10 = bright[row  ][col+1];
            float b01 = bright[row+1][col  ];
            float b11 = bright[row+1][col+1];
            if (b00 < 0.003f && b10 < 0.003f && b01 < 0.003f && b11 < 0.003f) continue;

            float x0 = rx + col   * stepX, y0 = ry + row   * stepY;
            float x1 = rx +(col+1)* stepX, y1 = ry +(row+1)* stepY;
            dl->AddRectFilledMultiColor(
                ImVec2(x0,y0), ImVec2(x1,y1),
                mkV(b00), mkV(b10), mkV(b11), mkV(b01));
        }
    }
}

void DrawPrecipitation(ImDrawList* dl,
                              float bx, float by, float bw, float bh,
                              int mode, int count, float speed, float size,
                              float wind, const float col4[4], float dt, int slot)
{
    if (!dl || bw <= 0.f || bh <= 0.f || count <= 0 || col4[3] <= 0.f) return;

    static constexpr int k_maxSlots     = 11;
    static constexpr int k_maxParticles = 200;

    struct Flake {
        float x, y;
        float startX;
        float sz;
        float opacity;
        float fallSpeed;
        float hPhase, hSpeed, hAmp;
        float vPhase, vSpeed, vAmp;
    };
    static Flake s_flakes[k_maxSlots][k_maxParticles] = {};
    static bool  s_init[k_maxSlots]  = {};
    static float s_bw[k_maxSlots]    = {};
    static float s_bh[k_maxSlots]    = {};
    static float s_t[k_maxSlots]     = {};

    const int N = std::min(count, k_maxParticles);
    slot = std::clamp(slot, 0, k_maxSlots - 1);

    bool sizeChanged = fabsf(s_bw[slot] - bw) > 4.f || fabsf(s_bh[slot] - bh) > 4.f;
    if (!s_init[slot] || sizeChanged) {
        s_bw[slot] = bw; s_bh[slot] = bh;
        s_init[slot] = true;
        auto rf = [](float lo, float hi) {
            return lo + (hi - lo) * ((rand() % 10000) / 10000.f);
        };
        for (int i = 0; i < k_maxParticles; ++i) {
            Flake& f       = s_flakes[slot][i];
            f.startX       = rf(0.f, bw);
            f.x            = f.startX;
            f.y            = rf(0.f, bh);
            f.sz           = rf(0.5f, 1.5f);
            f.opacity      = rf(0.25f, 0.85f);
            f.fallSpeed    = rf(0.8f, 2.2f);
            f.hPhase       = rf(0.f, 6.28318f);
            f.hSpeed       = rf(0.3f, 0.9f);
            f.hAmp         = rf(8.f, 20.f);
            f.vPhase       = rf(0.f, 6.28318f);
            f.vSpeed       = rf(0.5f, 1.3f);
            f.vAmp         = rf(10.f, 30.f);
        }
    }

    s_t[slot] += dt;
    float t = s_t[slot];

    const float kWindSnowDrift = 25.f;
    const float kWindRainSlant = 0.6f;

    const float cr = col4[0], cg = col4[1], cb = col4[2];
    const float baseAlpha = col4[3];

    dl->PushClipRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), true);

    for (int i = 0; i < N; ++i) {
        Flake& f = s_flakes[slot][i];

        if (mode == 0) {
            float fallPx  = f.fallSpeed * bh * speed * dt;
            float slantX  = wind * kWindRainSlant * fallPx;
            f.y += fallPx;
            f.x += slantX;

            if (f.y > bh + 2.f || f.x < -4.f || f.x > bw + 4.f) {
                float spread = bw + fabsf(wind) * 10.f;
                f.x = ((rand() % 10000) / 10000.f) * spread - fabsf(wind) * 5.f;
                f.y = -f.sz * size * 12.f;
            }

            float wx  = bx + f.x;
            float wy  = by + f.y;
            float len = f.sz * size * 10.f;
            float dx  = wind * kWindRainSlant * len;
            float dy  = len;
            float a   = baseAlpha * f.opacity;

            int aHead = (int)(a * 255.f);
            int aMid  = (int)(a * 0.45f * 255.f);
            int aTail = (int)(a * 0.12f * 255.f);
            ImU32 cHead = IM_COL32((int)(cr*255),(int)(cg*255),(int)(cb*255), aHead);
            ImU32 cMid  = IM_COL32((int)(cr*255),(int)(cg*255),(int)(cb*255), aMid);
            ImU32 cTail = IM_COL32((int)(cr*255),(int)(cg*255),(int)(cb*255), aTail);
            dl->AddLine(ImVec2(wx - dx, wy - dy), ImVec2(wx, wy), cTail, 1.0f);
            dl->AddLine(ImVec2(wx - dx*0.6f, wy - dy*0.6f), ImVec2(wx, wy), cMid, 1.0f);
            dl->AddLine(ImVec2(wx - dx*0.2f, wy - dy*0.2f), ImVec2(wx, wy), cHead, 1.2f);

        } else {
            float vy = (f.fallSpeed * bh + sinf(t * f.vSpeed + f.vPhase) * f.vAmp) * speed;
            f.y += vy * dt;
            f.startX += wind * kWindSnowDrift * dt;
            f.x = f.startX + sinf(t * f.hSpeed + f.hPhase) * f.hAmp;

            if (f.y > bh + f.sz || f.x < -40.f || f.x > bw + 40.f) {
                f.startX = ((rand() % 10000) / 10000.f) * bw;
                f.x      = f.startX;
                f.y      = -f.sz * size * 3.f;
            }

            float wx = bx + f.x;
            float wy = by + f.y;
            float r  = f.sz * size * 2.2f;
            float a  = baseAlpha * f.opacity;

            int baseA = (int)(a * 255.f);
            ImU32 c0 = IM_COL32((int)(cr*255),(int)(cg*255),(int)(cb*255),(int)(baseA*0.04f));
            ImU32 c1 = IM_COL32((int)(cr*255),(int)(cg*255),(int)(cb*255),(int)(baseA*0.12f));
            ImU32 c2 = IM_COL32((int)(cr*255),(int)(cg*255),(int)(cb*255),(int)(baseA*0.30f));
            ImU32 c3 = IM_COL32((int)(cr*255),(int)(cg*255),(int)(cb*255),(int)(baseA*0.70f));
            dl->AddCircleFilled(ImVec2(wx, wy), r * 3.0f, c0);
            dl->AddCircleFilled(ImVec2(wx, wy), r * 2.0f, c1);
            dl->AddCircleFilled(ImVec2(wx, wy), r * 1.3f, c2);
            dl->AddCircleFilled(ImVec2(wx, wy), r * 0.7f, c3);
        }
    }

    dl->PopClipRect();
}

// Critically-damped spring: drives `val` toward `target` with `vel` (px/s).
// omega=~22 gives fast, tight response with zero overshoot.
void SpringToward(float& val, float& vel, float target, float dt, float omega)
{
    constexpr float zeta  = 1.0f;
    float err  = val - target;
    float damp = 2.f * zeta * omega;
    vel = vel - (damp * vel + omega * omega * err) * dt;
    val = val + vel * dt;
    if (fabsf(val - target) < 0.15f && fabsf(vel) < 0.5f) { val = target; vel = 0.f; }
}

void DrawBgSideGradients(ImDrawList* dl, float x0, float y0, float x1, float y1,
    const BgSideGrad grads[4], ImU32 solidColor, float rounding, ImDrawFlags cf, bool overlayOnly)
{
    const float W = x1 - x0, H = y1 - y0;
    if (W <= 0.f || H <= 0.f || !dl) return;

    const auto& gL = grads[0]; const auto& gR = grads[1];
    const auto& gT = grads[2]; const auto& gB = grads[3];

    // Transparent version of solidColor (same RGB, alpha=0)
    ImU32 transp = solidColor & ~(0xFFu << IM_COL32_A_SHIFT);

    // Normal (bg fill): fade from transparent at outer edge to solidColor at start.
    // Overlay (drawn on top of decorative elements): solid at outer edge, transparent at start.
    ImU32 edgeC   = overlayOnly ? solidColor : transp;
    ImU32 centerC = overlayOnly ? transp      : solidColor;

    float lD = gL.enable ? std::clamp(gL.start, 0.f, 1.f) * W : 0.f;
    float rD = gR.enable ? std::clamp(gR.start, 0.f, 1.f) * W : 0.f;
    float tD = gT.enable ? std::clamp(gT.start, 0.f, 1.f) * H : 0.f;
    float bD = gB.enable ? std::clamp(gB.start, 0.f, 1.f) * H : 0.f;
    float lE = gL.enable ? std::clamp(gL.end, 0.f, gL.start) * W : 0.f;
    float rE = gR.enable ? std::clamp(gR.end, 0.f, gR.start) * W : 0.f;
    float tE = gT.enable ? std::clamp(gT.end, 0.f, gT.start) * H : 0.f;
    float bE = gB.enable ? std::clamp(gB.end, 0.f, gB.start) * H : 0.f;

    float sx0 = x0 + lD, sy0 = y0 + tD;
    float sx1 = x1 - rD, sy1 = y1 - bD;

    if (!overlayOnly && sx1 > sx0 && sy1 > sy0)
        dl->AddRectFilled(ImVec2(sx0, sy0), ImVec2(sx1, sy1), solidColor, rounding, cf);

    // Edge gradient strips: edgeC at outer boundary, centerC at start boundary
    if (gL.enable && lD > lE)
        dl->AddRectFilledMultiColor(ImVec2(x0+lE,sy0), ImVec2(x0+lD,sy1), edgeC,centerC,centerC,edgeC);
    if (gR.enable && rD > rE)
        dl->AddRectFilledMultiColor(ImVec2(x1-rD,sy0), ImVec2(x1-rE,sy1), centerC,edgeC,edgeC,centerC);
    if (gT.enable && tD > tE)
        dl->AddRectFilledMultiColor(ImVec2(sx0,y0+tE), ImVec2(sx1,y0+tD), edgeC,edgeC,centerC,centerC);
    if (gB.enable && bD > bE)
        dl->AddRectFilledMultiColor(ImVec2(sx0,y1-bD), ImVec2(sx1,y1-bE), centerC,centerC,edgeC,edgeC);

    // Solid outer zones (end > 0): fully edgeC beyond the gradient
    if (gL.enable && lE > 0.f) dl->AddRectFilled(ImVec2(x0,    sy0), ImVec2(x0+lE,sy1), edgeC);
    if (gR.enable && rE > 0.f) dl->AddRectFilled(ImVec2(x1-rE, sy0), ImVec2(x1,   sy1), edgeC);
    if (gT.enable && tE > 0.f) dl->AddRectFilled(ImVec2(sx0, y0),    ImVec2(sx1,y0+tE), edgeC);
    if (gB.enable && bE > 0.f) dl->AddRectFilled(ImVec2(sx0, y1-bE), ImVec2(sx1,y1),    edgeC);

    // Corner quads: edgeC at the outer corner, centerC at the inner corner
    if (gL.enable && gT.enable && lD>0.f && tD>0.f)
        dl->AddRectFilledMultiColor(ImVec2(x0+lE,y0+tE), ImVec2(x0+lD,y0+tD), edgeC,edgeC,centerC,edgeC);
    if (gR.enable && gT.enable && rD>0.f && tD>0.f)
        dl->AddRectFilledMultiColor(ImVec2(x1-rD,y0+tE), ImVec2(x1-rE,y0+tD), edgeC,edgeC,edgeC,centerC);
    if (gL.enable && gB.enable && lD>0.f && bD>0.f)
        dl->AddRectFilledMultiColor(ImVec2(x0+lE,y1-bD), ImVec2(x0+lD,y1-bE), edgeC,centerC,edgeC,edgeC);
    if (gR.enable && gB.enable && rD>0.f && bD>0.f)
        dl->AddRectFilledMultiColor(ImVec2(x1-rD,y1-bD), ImVec2(x1-rE,y1-bE), centerC,edgeC,edgeC,edgeC);
}

// Animated GIF -> one D3DPOOL_DEFAULT dynamic texture per composited frame. Render thread only.
// Compositing and its caps live in gifdecode.h (tested); this only opens and uploads.
bool LoadGifFrames(IDirect3DDevice9* dev, const char* path,
                   std::vector<IDirect3DTexture9*>& frames, std::vector<float>& delays, int& outW, int& outH)
{
    outW = outH = 0;
    if (!dev || !path || !path[0]) return false;
    int err = 0;
    GifFileType* gif = DGifOpenFileName(path, &err);
    if (!gif) { DebugLog("gif: open failed err=%d '%s'", err, path); return false; }
    if (DGifSlurp(gif) != GIF_OK) {
        DebugLog("gif: slurp failed err=%d '%s'", gif->Error, path);
        DGifCloseFile(gif, &err);
        return false;
    }

    const size_t before = frames.size();
    const int emitted = GifCompositeFrames(gif, kGifMaxBytes,
        [&](const uint32_t* argb, int W, int H, float delay) {
            // D3DPOOL_DEFAULT + D3DUSAGE_DYNAMIC: MANAGED is invalid on TF2's D3D9Ex device.
            IDirect3DTexture9* tex = nullptr;
            if (SUCCEEDED(dev->CreateTexture(W, H, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                             D3DPOOL_DEFAULT, &tex, nullptr)) && tex) {
                D3DLOCKED_RECT lr{};
                if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
                    for (int y = 0; y < H; ++y)
                        memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch,
                               argb + (size_t)y * W, (size_t)W * sizeof(uint32_t));
                    tex->UnlockRect(0);
                    frames.push_back(tex);
                    delays.push_back(delay);
                    outW = W; outH = H;
                } else {
                    tex->Release();
                }
            }
            return true;
        });
    if (emitted == 0)
        DebugLog("gif: rejected %dx%d frames=%d '%s'", gif->SWidth, gif->SHeight, gif->ImageCount, path);
    else if (emitted < gif->ImageCount)
        DebugLog("gif: stopped at %d/%d frames (512 MB cap) '%s'", emitted, gif->ImageCount, path);
    DGifCloseFile(gif, &err);
    return frames.size() > before;
}
