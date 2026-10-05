#include "misc.h"
// Media player (Windows SMTC / WinRT) and waveform (WASAPI loopback): this file only
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "menu.h"
#include "minesweeper.h"
#include <sstream>
#include <winhttp.h>

struct SnowFlake {
    float x, y, startX;
    float size, opacity;
    float fallSpeed;
    float hPhase, hSpeed, hAmp;
    float vPhase, vSpeed, vAmp;
};
static SnowFlake s_snowFlakes[160] = {};
static bool      s_snowInit        = false;

void RenderMenuSnow(float alpha)
{
    if (!g_menuSnow) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float sw = (float)ImGui::GetIO().DisplaySize.x;
    float sh = (float)ImGui::GetIO().DisplaySize.y;

    if (!s_snowInit || sw < 1.f) {
        s_snowInit = true;
        srand(42);
        auto rf = [](float lo, float hi) { return lo + (hi-lo) * (rand() / (float)RAND_MAX); };
        for (auto& f : s_snowFlakes) {
            f.startX    = rf(0.f, sw);
            f.x         = f.startX;
            f.y         = rf(-sh, 0.f);
            f.size      = rf(1.f, 3.5f);
            f.opacity   = rf(0.25f, 0.85f);
            f.fallSpeed = rf(120.f, 320.f);   // px/sec base fall
            f.hPhase    = rf(0.f, 6.28318f);
            f.hSpeed    = rf(0.3f, 0.9f);     // oscillations/sec horizontal
            f.hAmp      = rf(15.f, 40.f);     // px horizontal sway
            f.vPhase    = rf(0.f, 6.28318f);
            f.vSpeed    = rf(0.5f, 1.3f);     // oscillations/sec vertical
            f.vAmp      = rf(20.f, 50.f);     // px/sec vertical wobble
        }
    }

    static uint64_t s_lastSnowTick = 0;
    static float    s_snowTime     = 0.f;
    uint64_t now = GetTickCount64();
    float dt = (s_lastSnowTick == 0) ? (1.f/60.f) : std::min((now - s_lastSnowTick) * 0.001f, 0.05f);
    s_lastSnowTick = now;
    s_snowTime    += dt;
    float t = s_snowTime;

    for (auto& f : s_snowFlakes) {
        // vertical: base fall + sinusoidal wobble, both time-based
        float vy = f.fallSpeed + sinf(t * f.vSpeed + f.vPhase) * f.vAmp;
        f.y += vy * dt;
        // horizontal: pure time-based oscillation around start position
        f.x  = f.startX + sinf(t * f.hSpeed + f.hPhase) * f.hAmp;

        if (f.y > sh + f.size || f.x < -60.f || f.x > sw + 60.f) {
            f.startX = (float)(rand() % (int)sw);
            f.x      = f.startX;
            f.y      = -f.size;
        }

        // Simulated soft blur: 4 concentric rings with Gaussian-like falloff
        float baseA = f.opacity * 255.f * alpha;
        dl->AddCircleFilled(ImVec2(f.x, f.y), f.size * 3.0f, IM_COL32(220,235,255, (int)(baseA * 0.04f)));
        dl->AddCircleFilled(ImVec2(f.x, f.y), f.size * 2.0f, IM_COL32(220,235,255, (int)(baseA * 0.12f)));
        dl->AddCircleFilled(ImVec2(f.x, f.y), f.size * 1.3f, IM_COL32(220,235,255, (int)(baseA * 0.30f)));
        dl->AddCircleFilled(ImVec2(f.x, f.y), f.size * 0.7f, IM_COL32(220,235,255, (int)(baseA * 0.70f)));
    }
}

// ---- Speedometer -------------------------------------------
void RenderSpeedometer()
{
    ImGuiIO& io = ImGui::GetIO();
    float dt = std::min(AnimDt(), 0.1f);   // game time (animclock.h)
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    bool  alive    = IsLocalPlayerAlive();
    float rawSpeed = alive ? GetLocalPlayerSpeed() : -1.f;

    // ---- Ground/air state + pre-speed (jump only) --------------
    // We read vertical velocity to distinguish a jump (vz > 0) from walking off
    // a ledge (vz == 0 or slightly negative at the first frame of freefall).
    // TF2 gives players ~+vz when they jump; falling off a ledge is vz~0.
    {
        bool onGround = false;
        float vz = 0.f;
        if (g_offFlags > 0 && g_offVelocity > 0) {
            int idx = GetLocalPlayerIndex();
            if (idx > 0) {
                void* pEnt = GetClientEntity(idx);
                if (pEnt) {
                    __try {
                        int flags = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + g_offFlags);
                        onGround = (flags & 1) != 0;
                        // velocity is [vx, vy, vz] floats
                        float* vel = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pEnt) + g_offVelocity);
                        vz = vel[2];
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
        }

        if (alive) {
            // Only record pre-speed when transitioning ground->air AND vz > 0
            // (genuine jump, not a ledge walk-off or crouching).
            if (g_speedoWasOnGround && !onGround && vz > 10.f) {
                g_speedoPreVal   = rawSpeed;
                g_speedoPreTimer = g_speedoPreHoldTime;
                g_speedoCenterDelay = 0.f;
                if (g_speedoPreAlpha < 0.05f) {
                    // New jump: start collapsed (center = -1 normalized, slide = off-screen right)
                    g_speedoPreXOff   = g_speedoPreAnimCenter ? -1.f : 150.f;
                    g_speedoPreXOffV  = 0.f;
                    g_speedoPreSlotW  = 0.f;
                    g_speedoPreSlotWV = 0.f;
                }
            }
        } else {
            // Reset everything on death/disconnect
            g_speedoPreTimer    = 0.f;
            g_speedoPreAlpha    = 0.f;
            g_speedoPreAlphaV   = 0.f;
            g_speedoPreXOff     = 0.f;
            g_speedoPreXOffV    = 0.f;
            g_speedoPreSlotW    = 0.f;
            g_speedoPreSlotWV   = 0.f;
            g_speedoMainSlotW   = 0.f;  // 0 triggers first-frame snap in draw block
            g_speedoMainSlotWV  = 0.f;
            g_speedoMainPosX    = -1.f; // -1 triggers snap on next draw
            g_speedoCenterDelay = 0.f;
            g_speedoDisplayVal  = -1.f;
            g_speedoDisplayV    = 0.f;
        }

        g_speedoWasOnGround = onGround;
    }

    // ---- Pre-speed alpha + slide springs -----------------------
    // XOff: large positive = off-screen right (slide-in start), 0 = docked.
    // Pre-speed fades in place (XOff frozen at 0 after hold expires).
    // g_speedoCenterDelay: 200ms window after hold expires before main re-centers.
    if (g_speedoShowPre) {
        float naturalW = 0.f;
        {
            ImFont* preFont = (g_speedoFont && g_speedoFont->IsLoaded()) ? g_speedoFont : ImGui::GetFont();
            if (preFont) {
                float fs = preFont->FontSize;
                if (g_speedoPreDoctrineParens && g_doctrineFont && g_doctrineFont->IsLoaded()) {
                    char numTmp[16];
                    snprintf(numTmp, sizeof(numTmp), "%d", (int)roundf(g_speedoPreVal));
                    naturalW = g_doctrineFont->CalcTextSizeA(fs, FLT_MAX, 0.f, "(").x
                             + preFont->CalcTextSizeA(fs, FLT_MAX, 0.f, numTmp).x
                             + g_doctrineFont->CalcTextSizeA(fs, FLT_MAX, 0.f, ")").x + 6.f;
                } else {
                    char tmpBuf[32];
                    snprintf(tmpBuf, sizeof(tmpBuf), "(%d)", (int)roundf(g_speedoPreVal));
                    naturalW = preFont->CalcTextSizeA(fs, FLT_MAX, 0.f, tmpBuf).x + 6.f;
                }
            }
        }

        if (!g_speedoPreAnimCenter) {
            // Slide-from-right mode: XOff is pixels (large positive = off-screen right, 0 = docked)
            if (g_speedoPreTimer > 0.f) {
                g_speedoPreTimer -= dt;
                SpringToward(g_speedoPreAlpha, g_speedoPreAlphaV, 1.f, dt);
                SpringToward(g_speedoPreXOff,  g_speedoPreXOffV,  0.f, dt);
                if (naturalW > 0.f)
                    SpringToward(g_speedoPreSlotW, g_speedoPreSlotWV, naturalW, dt);
                if (g_speedoPreTimer <= 0.f)
                    g_speedoCenterDelay = 0.2f;
            } else {
                SpringToward(g_speedoPreAlpha, g_speedoPreAlphaV, 0.f, dt);
                g_speedoPreXOff  = 0.f;
                g_speedoPreXOffV = 0.f;
                if (g_speedoCenterDelay > 0.f) {
                    g_speedoCenterDelay -= dt;
                } else {
                    SpringToward(g_speedoPreSlotW, g_speedoPreSlotWV, 0.f, dt);
                }
            }
            g_speedoPreAlpha = std::clamp(g_speedoPreAlpha, 0.f, 1.f);
            g_speedoPreXOff  = std::max(0.f, g_speedoPreXOff);
            g_speedoPreSlotW = std::max(0.f, g_speedoPreSlotW);
        } else {
            // Expand-from-center mode: XOff normalized [-1, 0]; -1=collapsed, 0=expanded
            if (g_speedoPreTimer > 0.f) {
                g_speedoPreTimer -= dt;
                SpringToward(g_speedoPreAlpha, g_speedoPreAlphaV, 1.f, dt);
                SpringToward(g_speedoPreXOff,  g_speedoPreXOffV,  0.f, dt);  // expand toward 0
                if (naturalW > 0.f)
                    SpringToward(g_speedoPreSlotW, g_speedoPreSlotWV, naturalW, dt);
                if (g_speedoPreTimer <= 0.f)
                    g_speedoCenterDelay = 0.2f;
            } else {
                SpringToward(g_speedoPreAlpha, g_speedoPreAlphaV, 0.f, dt);
                SpringToward(g_speedoPreXOff,  g_speedoPreXOffV,  -1.f, dt); // collapse back
                if (g_speedoCenterDelay > 0.f) {
                    g_speedoCenterDelay -= dt;
                } else {
                    SpringToward(g_speedoPreSlotW, g_speedoPreSlotWV, 0.f, dt);
                }
            }
            g_speedoPreAlpha = std::clamp(g_speedoPreAlpha, 0.f, 1.f);
            g_speedoPreXOff  = std::clamp(g_speedoPreXOff, -1.f, 0.f);
            g_speedoPreSlotW = std::max(0.f, g_speedoPreSlotW);
        }
    } else {
        g_speedoPreAlpha    = 0.f;
        g_speedoPreAlphaV   = 0.f;
        g_speedoPreXOff     = 0.f;
        g_speedoPreXOffV    = 0.f;
        g_speedoPreSlotW    = 0.f;
        g_speedoPreSlotWV   = 0.f;
        g_speedoCenterDelay = 0.f;
    }

    // ---- Collect graph history ---------------------------------
    if (g_speedoGraph && alive) {
        g_speedoHistory.push_front(rawSpeed);
        if ((int)g_speedoHistory.size() > k_speedoHistMax)
            g_speedoHistory.pop_back();
    } else if (!alive) {
        if (!g_speedoHistory.empty()) g_speedoHistory.clear();
    }

    // ---- Number fade alpha -------------------------------------
    // Target: 1 when alive and speed >= threshold; 0 otherwise.
    bool speedoVisible = alive && g_speedoEnable &&
        (g_speedoFadeThreshold <= 0.f || rawSpeed >= g_speedoFadeThreshold);
    float targetAlpha = speedoVisible ? 1.f : 0.f;
    if (!g_speedoFadeIn) {
        g_speedoAlpha  = targetAlpha;
        g_speedoAlphaV = 0.f;
    } else {
        SpringToward(g_speedoAlpha, g_speedoAlphaV, targetAlpha, dt);
        g_speedoAlpha = std::clamp(g_speedoAlpha, 0.f, 1.f);
    }

    // ---- Graph fade alpha (independent) ------------------------
    bool graphVisible = alive && g_speedoGraph;
    float targetGraphAlpha = graphVisible ? 1.f : 0.f;
    if (!g_speedoFadeIn) {
        g_speedoGraphAlpha  = targetGraphAlpha;
        g_speedoGraphAlphaV = 0.f;
    } else {
        SpringToward(g_speedoGraphAlpha, g_speedoGraphAlphaV, targetGraphAlpha, dt);
        g_speedoGraphAlpha = std::clamp(g_speedoGraphAlpha, 0.f, 1.f);
    }

    // ---- Shared color/font helpers -----------------------------
    auto lerpRGBA = [](const float a[4], const float b[4], float t, float alpha) -> ImU32 {
        t = std::clamp(t, 0.f, 1.f);
        int r  = (int)((a[0] + (b[0]-a[0])*t) * 255.f);
        int g  = (int)((a[1] + (b[1]-a[1])*t) * 255.f);
        int bl = (int)((a[2] + (b[2]-a[2])*t) * 255.f);
        int al = (int)(std::clamp(alpha, 0.f, 1.f) * 255.f);
        return IM_COL32(r,g,bl,al);
    };

    auto resolveSpeedColor = [&](float spd, float masterAlpha) -> ImU32 {
        if (!g_speedoColorBySpeed)
            return lerpRGBA(g_speedoColorLow, g_speedoColorLow, 0.f, masterAlpha * g_speedoColorLow[3]);
        float lo = g_speedoSpeedLow, hi = g_speedoSpeedHigh;
        if (hi <= lo) hi = lo + 1.f;
        if (spd <= lo) return lerpRGBA(g_speedoColorLow, g_speedoColorLow,   0.f, masterAlpha * g_speedoColorLow[3]);
        if (spd >= hi) return lerpRGBA(g_speedoColorHigh, g_speedoColorHigh, 0.f, masterAlpha * g_speedoColorHigh[3]);
        float t = (spd - lo) / (hi - lo);
        if (t < 0.5f) return lerpRGBA(g_speedoColorLow,  g_speedoColorMid,  t*2.f,       masterAlpha * (g_speedoColorLow[3]  + (g_speedoColorMid[3] -g_speedoColorLow[3]) *t*2.f));
        else          return lerpRGBA(g_speedoColorMid,  g_speedoColorHigh, (t-0.5f)*2.f, masterAlpha * (g_speedoColorMid[3]  + (g_speedoColorHigh[3]-g_speedoColorMid[3]) *(t-0.5f)*2.f));
    };

    ImFont* font = (g_speedoFont && g_speedoFont->IsLoaded()) ? g_speedoFont : ImGui::GetFont();
    float fontSize = font->FontSize;

    // Helper: draw text with optional drop-shadow + outline at given alpha
    auto drawWithEffects = [&](const char* str, float px, float py, ImU32 col, float masterAlpha) {
        if (g_speedoOutline) {
            float ot = g_speedoOutlineThick;
            ImU32 oc = IM_COL32(
                (int)(g_speedoOutlineColor[0]*255),
                (int)(g_speedoOutlineColor[1]*255),
                (int)(g_speedoOutlineColor[2]*255),
                (int)(g_speedoOutlineColor[3]*255*masterAlpha));
            dl->AddText(font, fontSize, ImVec2(px-ot, py),    oc, str);
            dl->AddText(font, fontSize, ImVec2(px+ot, py),    oc, str);
            dl->AddText(font, fontSize, ImVec2(px,    py-ot), oc, str);
            dl->AddText(font, fontSize, ImVec2(px,    py+ot), oc, str);
            dl->AddText(font, fontSize, ImVec2(px-ot, py-ot), oc, str);
            dl->AddText(font, fontSize, ImVec2(px+ot, py-ot), oc, str);
            dl->AddText(font, fontSize, ImVec2(px-ot, py+ot), oc, str);
            dl->AddText(font, fontSize, ImVec2(px+ot, py+ot), oc, str);
        }
        if (g_speedoShadow) {
            float ox = g_speedoShadowOffX, oy = g_speedoShadowOffY;
            if (g_speedoShadowBlur) {
                const float bO[][2] = {
                    {ox-1,oy-1},{ox,oy-1},{ox+1,oy-1},
                    {ox-1,oy  },{ox,oy  },{ox+1,oy  },
                    {ox-1,oy+1},{ox,oy+1},{ox+1,oy+1}
                };
                ImU32 sc = IM_COL32(
                    (int)(g_speedoShadowColor[0]*255),
                    (int)(g_speedoShadowColor[1]*255),
                    (int)(g_speedoShadowColor[2]*255),
                    (int)(g_speedoShadowColor[3]*255*masterAlpha*0.35f));
                for (auto& b : bO)
                    dl->AddText(font, fontSize, ImVec2(px+b[0], py+b[1]), sc, str);
            } else {
                ImU32 sc = IM_COL32(
                    (int)(g_speedoShadowColor[0]*255),
                    (int)(g_speedoShadowColor[1]*255),
                    (int)(g_speedoShadowColor[2]*255),
                    (int)(g_speedoShadowColor[3]*255*masterAlpha));
                dl->AddText(font, fontSize, ImVec2(px+ox, py+oy), sc, str);
            }
        }
        dl->AddText(font, fontSize, ImVec2(px, py), col, str);
    };

    // ---- Draw numbers ------------------------------------------
    if (g_speedoEnable && g_speedoAlpha > 0.004f) {
        float speed = alive ? rawSpeed : 0.f;

        // Smooth displayed number (spring toward real speed)
        if (g_speedoDisplayVal < 0.f) { g_speedoDisplayVal = speed; g_speedoDisplayV = 0.f; }
        SpringToward(g_speedoDisplayVal, g_speedoDisplayV, speed, dt);
        g_speedoDisplayVal = std::max(0.f, g_speedoDisplayVal);

        float sx = io.DisplaySize.x * g_speedoPosX;
        float sy = io.DisplaySize.y * g_speedoPosY;

        ImU32 textCol = resolveSpeedColor(speed, g_speedoAlpha);

        // Measure actual rendered string (rounded display value).
        char mainBuf[32];
        snprintf(mainBuf, sizeof(mainBuf), "%d", (int)roundf(g_speedoDisplayVal));
        ImVec2 mainSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, mainBuf);

        // Animate main slot width: spring toward natural pixel width so digit-count
        // changes (99->100, 299->300) don't snap the block sideways.
        if (g_speedoMainSlotW < 1.f) { g_speedoMainSlotW = mainSz.x; g_speedoMainSlotWV = 0.f; }
        SpringToward(g_speedoMainSlotW, g_speedoMainSlotWV, mainSz.x, dt);
        g_speedoMainSlotW = std::max(1.f, g_speedoMainSlotW);

        const float gap = 6.f;
        bool showPre = g_speedoShowPre && g_speedoPreAlpha > 0.004f;

        char preBuf[32] = {};
        char numBuf[16] = {};
        ImVec2 preSz = {};
        if (showPre || g_speedoPreAlpha > 0.f) {
            snprintf(numBuf, sizeof(numBuf), "%d", (int)roundf(g_speedoPreVal));
            if (g_speedoPreDoctrineParens && g_doctrineFont && g_doctrineFont->IsLoaded()) {
                float lw = g_doctrineFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, "(").x;
                float nw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, numBuf).x;
                float rw = g_doctrineFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, ")").x;
                preSz = ImVec2(lw + nw + rw, fontSize);
            } else {
                snprintf(preBuf, sizeof(preBuf), "(%d)", (int)roundf(g_speedoPreVal));
                preSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, preBuf);
            }
        }

        float tx, ty;
        ty = sy - mainSz.y * 0.5f;

        if (!g_speedoPreAnimCenter) {
            // Slide-from-right: main number springs to grouped/solo center position
            bool groupMode = showPre || (g_speedoCenterDelay > 0.f);
            float groupW = g_speedoMainSlotW + (groupMode ? g_speedoPreSlotW : 0.f);
            float targetMainX = sx - groupW * 0.5f;
            if (g_speedoMainPosX < 0.f) g_speedoMainPosX = targetMainX;
            g_speedoMainPosX += (targetMainX - g_speedoMainPosX) * (1.f - expf(-12.f * dt));
            tx = g_speedoMainPosX;
            float mainX = tx + (g_speedoMainSlotW - mainSz.x) * 0.5f;
            drawWithEffects(mainBuf, mainX, ty, textCol, g_speedoAlpha);
            if (showPre) {
                float preX = tx + g_speedoMainSlotW + gap + g_speedoPreXOff;
                float pAlpha = g_speedoPreAlpha * g_speedoAlpha;
                ImU32 preCol = resolveSpeedColor(g_speedoPreVal, pAlpha);
                if (g_speedoPreDoctrineParens && g_doctrineFont && g_doctrineFont->IsLoaded()) {
                    float lw = g_doctrineFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, "(").x;
                    float nw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, numBuf).x;
                    ImFont* saved = font; font = g_doctrineFont;
                    drawWithEffects("(", preX, ty, preCol, pAlpha);
                    font = saved;
                    drawWithEffects(numBuf, preX + lw, ty, preCol, pAlpha);
                    font = g_doctrineFont;
                    drawWithEffects(")", preX + lw + nw, ty, preCol, pAlpha);
                    font = saved;
                } else {
                    drawWithEffects(preBuf, preX, ty, preCol, pAlpha);
                }
            }
        } else {
            // Expand-from-center: expansion = slotW * (1 + XOff) where XOff  --  [-1, 0]
            float expansion = g_speedoPreSlotW * (1.f + g_speedoPreXOff);
            float totalW = showPre ? g_speedoMainSlotW + expansion : g_speedoMainSlotW;
            tx = sx - totalW * 0.5f;
            float mainX = tx + (g_speedoMainSlotW - mainSz.x) * 0.5f;
            drawWithEffects(mainBuf, mainX, ty, textCol, g_speedoAlpha);
            if (showPre) {
                float preX = tx + g_speedoMainSlotW + expansion - preSz.x;
                float pAlpha = g_speedoPreAlpha * g_speedoAlpha;
                ImU32 preCol = resolveSpeedColor(g_speedoPreVal, pAlpha);
                if (g_speedoPreDoctrineParens && g_doctrineFont && g_doctrineFont->IsLoaded()) {
                    float lw = g_doctrineFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, "(").x;
                    float nw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, numBuf).x;
                    ImFont* saved = font; font = g_doctrineFont;
                    drawWithEffects("(", preX, ty, preCol, pAlpha);
                    font = saved;
                    drawWithEffects(numBuf, preX + lw, ty, preCol, pAlpha);
                    font = g_doctrineFont;
                    drawWithEffects(")", preX + lw + nw, ty, preCol, pAlpha);
                    font = saved;
                } else {
                    drawWithEffects(preBuf, preX, ty, preCol, pAlpha);
                }
            }
        }
    }

    // ---- Draw graph --------------------------------------------
    if (g_speedoGraph && g_speedoGraphAlpha > 0.004f && (int)g_speedoHistory.size() >= 2) {
        int N = (int)g_speedoHistory.size();
        float gw = g_speedoGraphW * g_speedoScale;
        float gh = g_speedoGraphH * g_speedoScale;
        float gsx = io.DisplaySize.x * g_speedoGraphPosX;
        float gsy = io.DisplaySize.y * g_speedoGraphPosY;
        float gLeft = gsx - gw * 0.5f;
        float gBot  = gsy;
        float maxSpd = g_speedoGraphMaxSpeed > 1.f ? g_speedoGraphMaxSpeed : 1.f;
        float lineW  = gw / (float)(k_speedoHistMax - 1);

        for (int i = 0; i < N - 1; ++i) {
            float cur  = g_speedoHistory[i];
            float next = g_speedoHistory[i + 1];

            int fadeMult = 255;
            switch (g_speedoGraphFade) {
            case 1:
                if (i < 60) fadeMult = (int)(((i+1)/60.f)*255.f);
                else if (i > N-61) fadeMult = (int)(((N-i)/60.f)*255.f);
                break;
            case 2:
                if (i < 60) fadeMult = (int)(((i+1)/60.f)*255.f);
                else if (i > N-61) fadeMult = (int)(((N-i)/60.f)*255.f);
                fadeMult = (int)(fadeMult * std::clamp(((cur+next)*0.5f)/maxSpd, 0.f, 1.f));
                break;
            case 3:
                if (i > N-61) fadeMult = (int)(((N-i)/60.f)*255.f);
                break;
            case 4:
                if (i < 60) fadeMult = (int)(((i+1)/60.f)*255.f);
                break;
            default: break;
            }
            fadeMult = std::clamp(fadeMult, 0, 255);

            float fa = g_speedoGraphColor[3] * g_speedoGraphAlpha * (fadeMult/255.f);
            ImU32 lc = IM_COL32(
                (int)(g_speedoGraphColor[0]*255),
                (int)(g_speedoGraphColor[1]*255),
                (int)(g_speedoGraphColor[2]*255),
                (int)(fa*255));

            // Each sample maps to one pixel column; use filled rects so each
            // column is exactly 1px wide. Height (thickness) is user-controlled.
            float x1 = gLeft + gw - (float)i       * lineW;
            float x2 = gLeft + gw - (float)(i + 1) * lineW;
            float y1 = gBot  - std::clamp(cur  / maxSpd, 0.f, 1.f) * gh;
            float y2 = gBot  - std::clamp(next / maxSpd, 0.f, 1.f) * gh;
            dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), lc, g_speedoGraphLineH);
        }
    }
}
// ---- end speedometer ---------------------------------------

void MenuSectionLabel(const char* label)
{
    ImGui::Spacing();
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.37f, 0.37f, 0.37f, 1.f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImVec2 lp = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.f);
    ImGui::GetWindowDrawList()->AddRectFilled(lp,
        ImVec2(lp.x + avail, lp.y + 1.f), IM_COL32(20, 20, 20, 255));
    ImGui::Dummy(ImVec2(0.f, 3.f));
}

// ---- Minesweeper -------------------------------------------
// minesweeper.online look (night skin, classic skin when Night theme is off) inside a
// panel styled like the menu. Rules live in minesweeper.h. The grid is one
// InvisibleButton hit-tested by math: 480 per-cell buttons + snprintf ids per frame
// was the Expert lag.
namespace Ms {

using MsGame::IDLE; using MsGame::PLAYING; using MsGame::WIN; using MsGame::LOSE;

static constexpr float kCell = 24.f;
static constexpr const char* kWindow = "Minesweeper##ms";

static MsGame::Board s_b;
static bool          s_inited = false;
static uint64_t      s_t0 = 0, s_t1 = 0;   // GetTickCount64 at first reveal / game end
enum Hold : uint8_t { HOLD_NONE, HOLD_OPEN, HOLD_CHORD };
static Hold          s_hold = HOLD_NONE;   // button held on the grid, released = act

static constexpr ImU32 Hex(uint32_t rgb, int a = 255) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
}

struct Skin {
    ImU32 body, lt, dk;                   // frame + bevels
    ImU32 closedLt, closedDk, closedIn;   // unopened tile
    ImU32 open, line;                     // opened tile, its top-left line
    ImU32 mineBg, boom;                   // revealed mine, the mine that ended it
    ImU32 flag, pole;
    ImU32 wrongLt, wrongDk, wrongIn, wrongFlag;  // flag on a safe cell after a loss
    bool  shadedMine;                     // night: grey radial ball, classic: black + white dot
    ImU32 num[9];
};

// Colors lifted from minesweeper.online's skin SVGs (night) and the classic XP skin.
static const Skin kNight = {
    Hex(0x40484F), Hex(0x707880), Hex(0x1E262E),
    Hex(0x707880), Hex(0x222A32), Hex(0x4C545C),
    Hex(0x384048), Hex(0x1E262E),
    Hex(0x464E56), Hex(0xEE6666),
    Hex(0xF77070), Hex(0xD8E0E8),
    Hex(0xBB6666), Hex(0x661111), Hex(0x883333), Hex(0xFF8888),
    true,
    { 0, Hex(0x7CC7FF), Hex(0x66C266), Hex(0xFF7788), Hex(0xEE88FF),
         Hex(0xDDAA22), Hex(0x66CCCC), Hex(0x999999), Hex(0xD0D8E0) },
};
static const Skin kClassic = {
    Hex(0xC0C0C0), Hex(0xFFFFFF), Hex(0x808080),
    Hex(0xFFFFFF), Hex(0x808080), Hex(0xC0C0C0),
    Hex(0xC0C0C0), Hex(0x808080),
    Hex(0xC0C0C0), Hex(0xFF0000),
    Hex(0xFF0000), Hex(0x000000),
    Hex(0xFFD0D0), Hex(0xA04040), Hex(0xE89090), Hex(0xFF0000),
    false,
    { 0, Hex(0x0000FF), Hex(0x008000), Hex(0xFF0000), Hex(0x000080),
         Hex(0x800000), Hex(0x008080), Hex(0x000000), Hex(0x808080) },
};

static void Init(int diff) { s_b.Init(diff); s_hold = HOLD_NONE; s_inited = true; }

// Timer edges: starts on the action that leaves IDLE, stops on the one that ends it.
static void After(MsGame::State prev) {
    const uint64_t now = GetTickCount64();
    if (prev == IDLE && s_b.state != IDLE) s_t0 = now;
    if (prev != WIN && prev != LOSE && s_b.Over()) s_t1 = now;
}
static uint32_t Seed() { return (uint32_t)(__rdtsc() ^ GetTickCount64()); }

// Rect frame with mitred corners: tl color on top/left, br color on bottom/right.
static void Bevel(ImDrawList* dl, ImVec2 a, ImVec2 b, float t, ImU32 tl, ImU32 br) {
    const ImDrawListFlags f = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;   // pixel-aligned, AA would soften the edges
    dl->AddQuadFilled(a, {b.x, a.y}, {b.x - t, a.y + t}, {a.x + t, a.y + t}, tl);
    dl->AddQuadFilled(a, {a.x + t, a.y + t}, {a.x + t, b.y - t}, {a.x, b.y}, tl);
    dl->AddQuadFilled({a.x, b.y}, {a.x + t, b.y - t}, {b.x - t, b.y - t}, b, br);
    dl->AddQuadFilled({b.x, a.y}, b, {b.x - t, b.y - t}, {b.x - t, a.y + t}, br);
    dl->Flags = f;
}

// Unopened tile: light top-left half, dark bottom-right half, inset face (skin closed.svg).
static void ClosedTile(ImDrawList* dl, ImVec2 a, ImU32 lt, ImU32 dk, ImU32 in) {
    const float s = kCell, t = 3.f;
    dl->AddRectFilled(a, {a.x + s, a.y + s}, dk);
    const ImDrawListFlags f = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;   // AA fringe bleeds into the next tile
    dl->AddTriangleFilled(a, {a.x + s, a.y}, {a.x, a.y + s}, lt);
    dl->Flags = f;
    dl->AddRectFilled({a.x + t, a.y + t}, {a.x + s - t, a.y + s - t}, in);
}

static void OpenTile(ImDrawList* dl, ImVec2 a, ImU32 fill, ImU32 line) {
    dl->AddRectFilled(a, {a.x + kCell, a.y + kCell}, line);
    dl->AddRectFilled({a.x + 1.f, a.y + 1.f}, {a.x + kCell, a.y + kCell}, fill);
}

// Shapes in tile units (0..1), taken from flag.svg / mine.svg.
static void DrawFlag(ImDrawList* dl, ImVec2 a, ImU32 flag, ImU32 pole) {
    const float s = kCell;
    auto P = [&](float x, float y) { return ImVec2(a.x + x * s, a.y + y * s); };
    dl->AddRectFilled(P(0.50f, 0.45f), P(0.58f, 0.72f), pole);
    dl->AddRectFilled(P(0.28f, 0.70f), P(0.78f, 0.82f), pole, 1.f);
    dl->AddTriangleFilled(P(0.58f, 0.18f), P(0.58f, 0.53f), P(0.22f, 0.26f), flag);
}

static void DrawMine(ImDrawList* dl, ImVec2 a, bool shaded) {
    const float s = kCell;
    const ImVec2 c = {a.x + s * 0.52f, a.y + s * 0.52f};
    const ImU32 blk = IM_COL32(0, 0, 0, 255);
    const float arm = s * 0.36f, diag = s * 0.25f, th = s * 0.075f;
    dl->AddLine({c.x - arm, c.y}, {c.x + arm, c.y}, blk, th);
    dl->AddLine({c.x, c.y - arm}, {c.x, c.y + arm}, blk, th);
    dl->AddLine({c.x - diag, c.y - diag}, {c.x + diag, c.y + diag}, blk, th);
    dl->AddLine({c.x + diag, c.y - diag}, {c.x - diag, c.y + diag}, blk, th);
    const float r = s * 0.27f;
    dl->AddCircleFilled(c, r, blk, 20);
    if (shaded) {   // stepped stand-in for the SVG's #000 -> #ccc radial gradient
        dl->AddCircleFilled(c, r * 0.72f, Hex(0x333333), 16);
        dl->AddCircleFilled(c, r * 0.48f, Hex(0x777777), 16);
        dl->AddCircleFilled(c, r * 0.26f, Hex(0xAAAAAA), 12);
        dl->AddCircleFilled(c, r * 0.10f, Hex(0xCCCCCC), 8);
    } else {
        dl->AddRectFilled({c.x - r * 0.45f, c.y - r * 0.45f}, {c.x - r * 0.1f, c.y - r * 0.1f},
                          IM_COL32(255, 255, 255, 255));
    }
}

// Digit centered on its glyph box, not the line box, so every number sits dead center.
static void DrawNum(ImDrawList* dl, ImFont* f, float fsz, ImVec2 a, int n, ImU32 col) {
    const char s[2] = { (char)('0' + n), 0 };
    const ImFontGlyph* g = f->FindGlyph((ImWchar)s[0]);
    const float sc = fsz / f->FontSize;
    float x = a.x + kCell * 0.5f, y = a.y + kCell * 0.5f;
    if (g) { x -= (g->X0 + g->X1) * 0.5f * sc; y -= (g->Y0 + g->Y1) * 0.5f * sc; }
    dl->AddText(f, fsz, {std::floor(x + 0.5f), std::floor(y + 0.5f)}, col, s);
}

// 7-segment digit with hex segments (skin d0..d9.svg). bits: 0 top, 1 top-right,
// 2 bottom-right, 3 bottom, 4 bottom-left, 5 top-left, 6 middle.
static void DrawSeg(ImDrawList* dl, float x, float y, float dw, float dh, uint8_t bits) {
    const ImU32 on = Hex(0xEE1111), off = Hex(0x3A0000);
    const float t = dw * 0.22f, h = t * 0.5f, g = 0.7f;
    auto Hz = [&](float yc, bool lit) {
        const ImVec2 p[6] = { {x + h + g, yc}, {x + t + g, yc - h}, {x + dw - t - g, yc - h},
                              {x + dw - h - g, yc}, {x + dw - t - g, yc + h}, {x + t + g, yc + h} };
        dl->AddConvexPolyFilled(p, 6, lit ? on : off);
    };
    auto Vt = [&](float xc, float y0, float y1, bool lit) {
        const ImVec2 p[6] = { {xc, y0 + h + g}, {xc + h, y0 + t + g}, {xc + h, y1 - t - g},
                              {xc, y1 - h - g}, {xc - h, y1 - t - g}, {xc - h, y0 + t + g} };
        dl->AddConvexPolyFilled(p, 6, lit ? on : off);
    };
    const float ym = y + dh * 0.5f;
    Hz(y + h,        bits & 1);
    Vt(x + dw - h, y, ym,      bits & 2);
    Vt(x + dw - h, ym, y + dh, bits & 4);
    Hz(y + dh - h,   bits & 8);
    Vt(x + h, ym, y + dh,      bits & 16);
    Vt(x + h, y, ym,           bits & 32);
    Hz(ym,           bits & 64);
}

static constexpr float kLedDh = 26.f, kLedDw = 14.f, kLedGap = 2.f, kLedPad = 3.f;
static constexpr float kLedW = 3 * kLedDw + 2 * kLedGap + 2 * kLedPad, kLedH = kLedDh + 2 * kLedPad;

static void DrawLed(ImDrawList* dl, ImVec2 a, int v) {
    static const uint8_t kSegs[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    dl->AddRectFilled(a, {a.x + kLedW, a.y + kLedH}, IM_COL32(0, 0, 0, 255));
    v = v < -99 ? -99 : (v > 999 ? 999 : v);
    const int m = v < 0 ? -v : v;
    const uint8_t d[3] = { v < 0 ? (uint8_t)0x40 : kSegs[m / 100], kSegs[(m / 10) % 10], kSegs[m % 10] };
    for (int i = 0; i < 3; i++)
        DrawSeg(dl, a.x + kLedPad + i * (kLedDw + kLedGap), a.y + kLedPad, kLedDw, kLedDh, d[i]);
}

enum Face : uint8_t { FACE_SMILE, FACE_WOW, FACE_WIN, FACE_DEAD };

static void DrawFace(ImDrawList* dl, ImVec2 a, float S, bool pressed, Face face, const Skin& k) {
    const ImVec2 b = {a.x + S, a.y + S};
    dl->AddRectFilled(a, b, k.dk);
    if (pressed) dl->AddRectFilled({a.x + 2.f, a.y + 2.f}, b, k.body);
    else {
        dl->AddRectFilled({a.x + 1.f, a.y + 1.f}, {b.x - 1.f, b.y - 1.f}, k.body);
        Bevel(dl, {a.x + 1.f, a.y + 1.f}, {b.x - 1.f, b.y - 1.f}, 3.f, k.lt, k.dk);
    }
    const float r = S * 0.31f, o = pressed ? 1.f : 0.f;
    const ImVec2 c = {a.x + S * 0.5f + o, a.y + S * 0.5f + o};
    const ImVec2 hc = {c.x - r * 0.12f, c.y - r * 0.12f};   // gradient center sits up-left
    const ImU32 blk = IM_COL32(0, 0, 0, 255);
    dl->AddCircleFilled(c,  r,         Hex(0xF5A83C), 32);
    dl->AddCircleFilled(hc, r * 0.80f, Hex(0xFACB0A), 32);
    dl->AddCircleFilled(hc, r * 0.52f, Hex(0xF3EE2A), 24);
    dl->AddCircleFilled(hc, r * 0.18f, Hex(0xFBF9C0), 12);
    dl->AddCircle(c, r, blk, 32, 1.3f);

    const float ex = r * 0.34f, ey = c.y - r * 0.24f;
    auto Arc = [&](ImVec2 ctr, float rad, float a0, float a1) {
        dl->PathArcTo(ctr, rad, a0 * 3.14159265f, a1 * 3.14159265f, 12);
        dl->PathStroke(blk, 0, 1.5f);
    };
    switch (face) {
    case FACE_DEAD:
        for (int s = -1; s <= 1; s += 2) {
            const float x = c.x + s * ex, e = r * 0.12f;
            dl->AddLine({x - e, ey - e}, {x + e, ey + e}, blk, 1.4f);
            dl->AddLine({x + e, ey - e}, {x - e, ey + e}, blk, 1.4f);
        }
        Arc({c.x, c.y + r * 0.78f}, r * 0.40f, 1.2f, 1.8f);
        break;
    case FACE_WIN:
        for (int s = -1; s <= 1; s += 2) {
            const float x = c.x + s * ex;
            dl->AddRectFilled({x - r * 0.24f, ey - r * 0.15f}, {x + r * 0.24f, ey + r * 0.14f}, blk, r * 0.1f);
            dl->AddLine({c.x + s * (ex + r * 0.2f), ey - r * 0.08f}, {c.x + s * r * 0.92f, ey - r * 0.22f}, blk, 1.2f);
        }
        dl->AddLine({c.x - ex, ey - r * 0.08f}, {c.x + ex, ey - r * 0.08f}, blk, 1.4f);
        Arc(c, r * 0.55f, 0.2f, 0.8f);
        break;
    case FACE_WOW:
        dl->AddCircleFilled({c.x - ex, ey}, r * 0.11f, blk, 8);
        dl->AddCircleFilled({c.x + ex, ey}, r * 0.11f, blk, 8);
        dl->AddCircle({c.x, c.y + r * 0.40f}, r * 0.18f, blk, 12, 1.5f);
        break;
    default:
        dl->AddCircleFilled({c.x - ex, ey}, r * 0.11f, blk, 8);
        dl->AddCircleFilled({c.x + ex, ey}, r * 0.11f, blk, 8);
        Arc(c, r * 0.55f, 0.2f, 0.8f);
        break;
    }
}

static void Render() {
    if (!s_inited) Init(1);
    if (!g_minesweeperOpen || !Menu_Visible()) { s_hold = HOLD_NONE; return; }
    const bool input = g_showMenu;   // fading out with the menu: drawn, not clickable
    const MsGame::Board& b = s_b;
    const Skin& k = g_msDarkMode ? kNight : kClassic;
    ImGuiIO& io = ImGui::GetIO();

    const bool pushedFont = g_menuFont && g_menuFont->IsLoaded();
    if (pushedFont) ImGui::PushFont(g_menuFont);

    // ---- Layout ------------------------------------------------
    const float pad = 12.f, titleH = 34.f, bev = 3.f, gap = 6.f, hdrIn = 42.f, faceS = 34.f;
    const float boardW = b.w * kCell, boardH = b.h * kCell;
    const float bodyW  = 2 * bev + 2 * gap + 2 * bev + boardW;
    const float bodyH  = 2 * bev + 3 * gap + (2 * bev + hdrIn) + (2 * bev + boardH);

    static const char* const kDiffs[3] = { "Beginner", "Intermediate", "Expert" };
    const float tabH = 22.f, tabPadX = 8.f, closeS = 20.f;
    const char* title = "Minesweeper";
    float tabsW = 0.f;
    for (const char* d : kDiffs) tabsW += ImGui::CalcTextSize(d).x + 2 * tabPadX + 2.f;
    const float titleW = pad + ImGui::CalcTextSize(title).x + 20.f + tabsW + 6.f + closeS + pad;
    const float W = std::floor(std::max(bodyW + 2 * pad, titleW));
    const float H = titleH + 1.f + pad + bodyH + pad;

    // Default spot: right of the menu (752 wide, centered, 620 tall), tops aligned.
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f + 376.f + 10.f, io.DisplaySize.y * 0.5f - 310.f},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({W, H}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin(kWindow, nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        (input ? 0 : ImGuiWindowFlags_NoInputs));
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = {std::floor(ImGui::GetWindowPos().x), std::floor(ImGui::GetWindowPos().y)};

    // ---- Panel, same recipe as the menu ------------------------
    const ImU32 colBg      = IM_COL32((int)(g_menuBgColor[0] * 255), (int)(g_menuBgColor[1] * 255), (int)(g_menuBgColor[2] * 255), 245);
    const ImU32 colLine    = IM_COL32(255, 255, 255, 15);
    const ImU32 colAccent  = IM_COL32((int)(g_accentColor[0] * 255), (int)(g_accentColor[1] * 255), (int)(g_accentColor[2] * 255), 255);
    auto Tone = [](float m) {
        auto ch = [&](float v) { v *= m; return (int)((v > 1.f ? 1.f : v) * 255); };
        return IM_COL32(ch(g_menuTextColor[0]), ch(g_menuTextColor[1]), ch(g_menuTextColor[2]), 255);
    };
    const ImU32 colDim = Tone(0.582f), colHover = Tone(0.859f);

    dl->AddRectFilled(wp, {wp.x + W, wp.y + H}, colBg, 6.f);
    dl->AddRect(wp, {wp.x + W, wp.y + H}, colLine, 6.f, 0, 1.f);
    dl->AddRectFilled({wp.x, wp.y + titleH}, {wp.x + W, wp.y + titleH + 1.f}, colLine);

    const float textH = ImGui::GetTextLineHeight();
    dl->AddText({wp.x + pad, wp.y + std::floor((titleH - textH) * 0.5f)}, colAccent, title);

    // Close X, far right
    {
        const ImVec2 a = {wp.x + W - pad - closeS, wp.y + std::floor((titleH - closeS) * 0.5f)};
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton("##msclose", {closeS, closeS});
        const bool hov = ImGui::IsItemHovered();
        if (hov) dl->AddRectFilled(a, {a.x + closeS, a.y + closeS}, IM_COL32(255, 255, 255, 6), 4.f);
        const float e = 5.f;
        const ImVec2 c = {a.x + closeS * 0.5f, a.y + closeS * 0.5f};
        const ImU32 col = hov ? colHover : colDim;
        dl->AddLine({c.x - e, c.y - e}, {c.x + e, c.y + e}, col, 1.5f);
        dl->AddLine({c.x + e, c.y - e}, {c.x - e, c.y + e}, col, 1.5f);
        if (hit) g_minesweeperOpen = false;
    }
    // Difficulty tabs, right-aligned before the X (menu sidebar tab look)
    {
        float x = wp.x + W - pad - closeS - 6.f - tabsW;
        const float y = wp.y + std::floor((titleH - tabH) * 0.5f);
        for (int i = 0; i < 3; i++) {
            const float tw = ImGui::CalcTextSize(kDiffs[i]).x + 2 * tabPadX;
            ImGui::SetCursorScreenPos({x, y});
            ImGui::PushID(i);
            const bool hit = ImGui::InvisibleButton("##msdiff", {tw, tabH});
            ImGui::PopID();
            const bool hov = ImGui::IsItemHovered(), act = b.diff == i;
            if (act)      dl->AddRectFilled({x, y}, {x + tw, y + tabH}, IM_COL32(255, 255, 255, 10), 4.f);
            else if (hov) dl->AddRectFilled({x, y}, {x + tw, y + tabH}, IM_COL32(255, 255, 255, 6), 4.f);
            dl->AddText({x + tabPadX, y + std::floor((tabH - textH) * 0.5f)},
                        act ? colAccent : (hov ? colHover : colDim), kDiffs[i]);
            if (hit) Init(i);
            x += tw + 2.f;
        }
    }

    // ---- Game body ---------------------------------------------
    const ImVec2 bA = {std::floor(wp.x + (W - bodyW) * 0.5f), wp.y + titleH + 1.f + pad};
    const ImVec2 bB = {bA.x + bodyW, bA.y + bodyH};
    dl->AddRectFilled(bA, bB, k.body);
    Bevel(dl, bA, bB, bev, k.lt, k.dk);

    const ImVec2 hA = {bA.x + bev + gap, bA.y + bev + gap};
    const ImVec2 hB = {bB.x - bev - gap, hA.y + 2 * bev + hdrIn};
    Bevel(dl, hA, hB, bev, k.dk, k.lt);
    const ImVec2 gA = {hA.x, hB.y + gap};
    const ImVec2 gB = {hB.x, gA.y + 2 * bev + boardH};
    Bevel(dl, gA, gB, bev, k.dk, k.lt);
    const ImVec2 g0 = {gA.x + bev, gA.y + bev};

    // ---- Grid input --------------------------------------------
    ImGui::SetCursorScreenPos(g0);
    ImGui::InvisibleButton("##msgrid", {boardW, boardH}, ImGuiButtonFlags_MouseButtonMask_);
    const bool gridHov = ImGui::IsItemHovered();
    int hr = -1, hc = -1;
    if ((gridHov || ImGui::IsItemActive()) &&
        io.MousePos.x >= g0.x && io.MousePos.y >= g0.y &&
        io.MousePos.x < g0.x + boardW && io.MousePos.y < g0.y + boardH) {
        hc = (int)((io.MousePos.x - g0.x) / kCell);
        hr = (int)((io.MousePos.y - g0.y) / kCell);
        if (hc >= b.w) hc = b.w - 1;
        if (hr >= b.h) hr = b.h - 1;
    }
    if (input && !b.Over()) {
        // Right press flags now; left/middle press previews, release acts (like the site).
        // Left+right together or left on a number = chord.
        if (gridHov && hr >= 0) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (s_hold != HOLD_NONE || ImGui::IsMouseDown(ImGuiMouseButton_Left)) s_hold = HOLD_CHORD;
                else { const MsGame::State p = s_b.state; s_b.Flag(hr, hc); After(p); }
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                s_hold = (ImGui::IsMouseDown(ImGuiMouseButton_Right) || b.c[hr][hc].revealed) ? HOLD_CHORD : HOLD_OPEN;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) s_hold = HOLD_CHORD;
        }
        if (s_hold != HOLD_NONE && (ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                                    ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
                                    ImGui::IsMouseReleased(ImGuiMouseButton_Middle))) {
            if (hr >= 0) {
                const MsGame::State p = s_b.state;
                if (s_hold == HOLD_OPEN) s_b.Reveal(hr, hc, Seed());
                else                     s_b.Chord(hr, hc, Seed());
                After(p);
            }
            s_hold = HOLD_NONE;
        }
    } else s_hold = HOLD_NONE;

    // ---- Header: mines left, face, timer -----------------------
    const float hy = std::floor((hA.y + hB.y - kLedH) * 0.5f);
    DrawLed(dl, {hA.x + bev + 6.f, hy}, b.mines - b.flags);
    const uint64_t now = GetTickCount64();
    const int secs = b.state == IDLE ? 0 : (int)(((b.Over() ? s_t1 : now) - s_t0) / 1000);
    DrawLed(dl, {hB.x - bev - 6.f - kLedW, hy}, secs);

    const ImVec2 fA = {std::floor((hA.x + hB.x - faceS) * 0.5f), std::floor((hA.y + hB.y - faceS) * 0.5f)};
    ImGui::SetCursorScreenPos(fA);
    if (ImGui::InvisibleButton("##msface", {faceS, faceS})) Init(b.diff);
    const bool facePressed = ImGui::IsItemActive() && ImGui::IsItemHovered();
    const Face face = b.state == LOSE ? FACE_DEAD : b.state == WIN ? FACE_WIN
                    : s_hold != HOLD_NONE ? FACE_WOW : FACE_SMILE;
    DrawFace(dl, fA, faceS, facePressed, face, k);

    // ---- Tiles -------------------------------------------------
    ImFont* numFont = g_msFont ? g_msFont : ImGui::GetFont();
    const float numSz = g_msFont ? g_msFont->FontSize : ImGui::GetFontSize();
    for (int r = 0; r < b.h; r++) for (int c = 0; c < b.w; c++) {
        const MsGame::Cell& e = b.c[r][c];
        const ImVec2 a = {g0.x + c * kCell, g0.y + r * kCell};
        if (e.revealed) {
            if (e.mine) {
                OpenTile(dl, a, (r == b.boomR && c == b.boomC) ? k.boom : k.mineBg, k.line);
                DrawMine(dl, a, k.shadedMine);
            } else {
                OpenTile(dl, a, k.open, k.line);
                if (e.adj) DrawNum(dl, numFont, numSz, a, e.adj, k.num[e.adj]);
            }
            continue;
        }
        const bool pressed = s_hold != HOLD_NONE && hr >= 0 && !e.flagged &&
            (s_hold == HOLD_OPEN ? (r == hr && c == hc) : (abs(r - hr) <= 1 && abs(c - hc) <= 1));
        if (pressed) OpenTile(dl, a, k.open, k.line);
        else if (e.flagged && b.state == LOSE && !e.mine) {
            ClosedTile(dl, a, k.wrongLt, k.wrongDk, k.wrongIn);
            DrawFlag(dl, a, k.wrongFlag, k.pole);
        } else {
            ClosedTile(dl, a, k.closedLt, k.closedDk, k.closedIn);
            if (e.flagged) DrawFlag(dl, a, k.flag, k.pole);
        }
    }

    ImGui::End();
    if (pushedFont) ImGui::PopFont();
}

} // namespace Ms

void RenderMinesweeper() { Ms::Render(); }
// ---- end minesweeper ---------------------------------------
// ---- Media Player (SMTC) -----------------------------------
using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;


MediaPlayerState                 g_mpState;
std::mutex                       g_mpMutex;
std::atomic<bool>                g_mpRunning{ false };
HANDLE                           g_mpThread      = nullptr;
// Raw thumbnail bytes (JPEG/PNG) for the render thread to decode and upload.
// Non-empty = upload needed. Protected by g_mpMutex.
std::vector<uint8_t>             g_mpPendingPixels;

static std::string WideToUtf8(const wchar_t* w, size_t len)
{
    if (!w || len == 0) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, w, (int)len, nullptr, 0, nullptr, nullptr);
    std::string out(need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, (int)len, &out[0], need, nullptr, nullptr);
    return out;
}

// D3D9 is not thread-safe, so the poll thread never releases a texture itself: it parks the
// pointer here and the render thread lets go of it. Protected by g_mpMutex.
static std::vector<IDirect3DTexture9*> s_mpTexToRelease;

// g_mpMutex held.
static void MpDropThumb_Locked()
{
    if (g_mpState.thumbTex) { s_mpTexToRelease.push_back(g_mpState.thumbTex); g_mpState.thumbTex = nullptr; }
    g_mpState.hasThumb = false;
}

// Render thread only. Called unconditionally each frame (see render.cpp): the poll thread keeps
// parking thumbs after the media player is switched OFF, and the drain used to live only inside
// RenderMediaPlayer -- so the texture live at the moment of the switch was never released, once
// per on/off cycle, for the rest of the session.
static void MpReleaseParkedTextures()
{
    std::vector<IDirect3DTexture9*> dead;
    { std::lock_guard<std::mutex> lk(g_mpMutex); dead.swap(s_mpTexToRelease); }
    for (IDirect3DTexture9* t : dead) t->Release();
}

void MediaPlayer_Drain() { MpReleaseParkedTextures(); }

// Clears g_mpState safely -- must be called with g_mpMutex held.
static void MpStateClear_Locked()
{
    MpDropThumb_Locked();
    g_mpState.title.clear();
    g_mpState.artist.clear();
    g_mpState.isPlaying   = false;
    g_mpState.totalTimeMs = 0;
    g_mpState.positionMs   = 0;
    g_mpState.posGeneration = 0;
    g_mpState.hasThumb     = false;
}

// ---- Lyrics (LRCLib HTTPS fetch + LRC parse) ----------------------------------------
struct LyricLine { double startSec; std::string text; };
struct LyricsFetchArgs { std::string title, artist; int totalMs; };

static std::mutex             s_lyricsMutex;
static std::vector<LyricLine> s_lyricsSync;    // timestamp-sorted lines
static std::string            s_lyricsTrack;   // "title|artist" key we last fetched
static int                    s_lyricsState = 0; // 0=idle,1=fetching,2=synced,4=notfound
static int                    s_lyricsGeneration = 0; // bumped each time s_lyricsSync is replaced
static std::wstring LyrUrlEncode(const std::string& s) {
    std::wstring out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (wchar_t)c;
        else { wchar_t b[8]; swprintf_s(b, L"%%%02X", c); out += b; }
    }
    return out;
}

static std::string LyrJsonStr(const std::string& json, const char* key) {
    std::string needle = std::string("\"") + key + "\":";
    size_t p = json.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    while (p < json.size() && json[p] == ' ') ++p;
    if (p >= json.size() || json[p] == 'n') return {};
    if (json[p] != '"') return {};
    ++p;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            char c = json[++p];
            if (c == 'n') out += '\n'; else if (c == 'r') out += '\r';
            else if (c == 't') out += '\t'; else out += c;
        } else { out += json[p]; }
        ++p;
    }
    return out;
}

static std::vector<LyricLine> LyrParseLrc(const std::string& lrc) {
    std::vector<LyricLine> out;
    std::istringstream ss(lrc);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 5 || line[0] != '[') continue;
        size_t rb = line.find(']');
        if (rb == std::string::npos) continue;
        std::string ts = line.substr(1, rb - 1);
        size_t col = ts.find(':');
        if (col == std::string::npos) continue;
        int min = 0; double sec = 0.0;
        try {
            min = std::stoi(ts.substr(0, col));
            std::string rest = ts.substr(col + 1);
            size_t dot = rest.find('.');
            if (dot != std::string::npos) {
                int s = std::stoi(rest.substr(0, dot));
                std::string ms = rest.substr(dot + 1);
                while (ms.size() < 3) ms += '0';
                sec = s + std::stoi(ms.substr(0, 3)) / 1000.0;
            } else { sec = std::stod(rest); }
        } catch (...) { continue; }
        std::string text = (rb + 1 < line.size()) ? line.substr(rb + 1) : "";
        if (!text.empty() && text[0] == ' ') text = text.substr(1);
        out.push_back({ min * 60.0 + sec, text });
    }
    std::sort(out.begin(), out.end(), [](const LyricLine& a, const LyricLine& b) {
        return a.startSec < b.startSec; });
    return out;
}

// hConnect is lazy-opened on first use and reused across calls within the same thread.
static std::string LyrHttpGet(HINTERNET hSession, HINTERNET& hConnect, const std::wstring& path) {
    std::string result;
    if (!hConnect) {
        hConnect = WinHttpConnect(hSession, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) return result;
    }
    HINTERNET hR = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hConnect); hConnect = nullptr; return result; }
    bool ok = WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(hR, nullptr);
    if (ok) {
        DWORD code = 0, csz = sizeof(code);
        WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &csz, WINHTTP_NO_HEADER_INDEX);
        if (code == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
                std::vector<char> buf(avail + 1, 0); DWORD rd = 0;
                if (WinHttpReadData(hR, buf.data(), avail, &rd) && rd > 0)
                    result.append(buf.data(), rd);
            }
        }
    } else {
        WinHttpCloseHandle(hConnect); hConnect = nullptr;
    }
    WinHttpCloseHandle(hR);
    return result;
}

static DWORD WINAPI FetchLyricsThread(LPVOID param) {
    auto* a = reinterpret_cast<LyricsFetchArgs*>(param);
    std::string title = a->title, artist = a->artist; int totalMs = a->totalMs;
    delete a;

    std::wstring ae = LyrUrlEncode(artist), te = LyrUrlEncode(title);
    int sec = (totalMs + 500) / 1000;
    std::wstring base = L"/api/get?artist_name=" + ae + L"&track_name=" + te;
    std::wstring path = base;
    if (sec > 0) { wchar_t d[32]; swprintf_s(d, L"&duration=%d", sec); path += d; }

    // Create session and connection here; they live only for the duration of this fetch.
    HINTERNET hSession = WinHttpOpen(L"fpsoverlay/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET hConnect = nullptr;
    std::string json;
    if (hSession) {
        json = LyrHttpGet(hSession, hConnect, path);
        if (json.empty() && sec > 0) json = LyrHttpGet(hSession, hConnect, base);
        if (hConnect) { WinHttpCloseHandle(hConnect); hConnect = nullptr; }
        WinHttpCloseHandle(hSession);
    }

    std::vector<LyricLine> synced; int state = 4;
    if (!json.empty()) {
        std::string lrc = LyrJsonStr(json, "syncedLyrics");
        if (!lrc.empty()) { synced = LyrParseLrc(lrc); if (!synced.empty()) state = 2; }
    }
    { std::lock_guard<std::mutex> lk(s_lyricsMutex);
      s_lyricsSync  = std::move(synced);
      s_lyricsState = state;
      ++s_lyricsGeneration; }
    return 0;
}

// Cached session manager -- RequestAsync is expensive, only call it once or on failure.
// Poll thread only; released before the thread leaves its apartment.
static GlobalSystemMediaTransportControlsSessionManager s_mgr{ nullptr };

static void MediaPlayer_Update()
{
    try {
        if (!s_mgr) {
            s_mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            if (!s_mgr) { std::lock_guard<std::mutex> lk(g_mpMutex); MpStateClear_Locked(); return; }
        }

        auto session = s_mgr.GetCurrentSession();
        if (!session) { std::lock_guard<std::mutex> lk(g_mpMutex); MpStateClear_Locked(); return; }

        auto info = session.TryGetMediaPropertiesAsync().get();

        std::string newTitle  = WideToUtf8(info.Title().c_str(),  info.Title().size());
        std::string newArtist = WideToUtf8(info.Artist().c_str(), info.Artist().size());

        bool isPlaying = false;
        int  totalMs   = 0;
        auto pbInfo = session.GetPlaybackInfo();
        if (pbInfo)
            isPlaying = (pbInfo.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

        auto tl = session.GetTimelineProperties();
        totalMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                      tl.EndTime() - tl.StartTime()).count();
        int posMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                        tl.Position() - tl.StartTime()).count();
        auto posTs = std::chrono::steady_clock::now();

        // Album art -- read raw image bytes here on the poll thread and hand them off to
        // the render thread for decode + GPU upload. D3D9 is not thread-safe so we must
        // not call any device methods (including D3DXCreateTextureFromFileInMemory) here.
        bool titleChanged = false, hadThumb = false;
        {
            // the render thread clears these under the lock (device reset), so read them under it
            std::lock_guard<std::mutex> lk(g_mpMutex);
            titleChanged = (newTitle != g_mpState.title);
            hadThumb     = g_mpState.hasThumb;
        }
        if (info.Thumbnail() && (titleChanged || !hadThumb)) {
            std::vector<uint8_t> artBytes;
            auto TryReadThumb = [&](uint32_t delayMs) -> bool {
                if (!info.Thumbnail()) return false;
                try {
                    auto stream = info.Thumbnail().OpenReadAsync().get();
                    uint64_t streamSz = stream.Size();
                    if (streamSz > 0 && streamSz <= 4 * 1024 * 1024) {
                        winrt::Windows::Storage::Streams::Buffer buf(streamSz);
                        stream.ReadAsync(buf, buf.Capacity(),
                                         winrt::Windows::Storage::Streams::InputStreamOptions::ReadAhead).get();
                        uint32_t sz = buf.Length();
                        if (sz > 0) { artBytes.assign(buf.data(), buf.data() + sz); return true; }
                    }
                } catch (...) {}
                return false;
            };
            // Try immediately, then retry once after 100ms (thumbnail may load async from Windows)
            if (!TryReadThumb(0)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                TryReadThumb(100);
            }
            if (!artBytes.empty()) {
                std::lock_guard<std::mutex> lk(g_mpMutex);
                g_mpPendingPixels = std::move(artBytes);
            }
        }

        std::lock_guard<std::mutex> lk(g_mpMutex);
        // Texture is created on the Present thread from g_mpPendingPixels -- don't touch thumbTex here.
        // Clear stale thumb when there's no thumbnail available for the current track.
        if (!info.Thumbnail() && g_mpState.thumbTex)
            MpDropThumb_Locked();
        g_mpState.title        = newTitle;
        g_mpState.artist       = newArtist;
        g_mpState.isPlaying    = isPlaying;
        g_mpState.totalTimeMs  = totalMs;
        g_mpState.positionMs   = posMs;
        g_mpState.posTimestamp = posTs;
        ++g_mpState.posGeneration;
    }
    catch (...) {
        s_mgr = nullptr; // force re-request next poll -- session may have died
        std::lock_guard<std::mutex> lk(g_mpMutex);
        MpStateClear_Locked();
    }
}

// Started when the player is switched on, stopped (g_mpRunning = false) when it is switched
// off or on eject; render.cpp closes the handle once the thread has gone.
DWORD WINAPI MediaPlayerThread(LPVOID)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // auto-reset, start signaled so we run once on load. Shared with the event handlers: removing
    // a handler does not wait for one already running, so the last owner closes the handle.
    HANDLE rawEvent = CreateEventW(nullptr, FALSE, TRUE, nullptr);
    if (!rawEvent) { winrt::uninit_apartment(); return 0; }
    const std::shared_ptr<void> wake(rawEvent, CloseHandle);
    const HANDLE hEvent = rawEvent;

    // Subscribe to SMTC session/property changes so we only wake on actual track changes
    GlobalSystemMediaTransportControlsSessionManager s_evtMgr{ nullptr };
    winrt::event_token s_sessionToken{};
    winrt::event_token s_propsToken{};
    winrt::event_token s_pbToken{};    // PlaybackInfoChanged -- fires on pause/resume/seek
    GlobalSystemMediaTransportControlsSession s_evtSession{ nullptr };

    auto resubscribe = [&]() {
        // Unsubscribe old handlers
        if (s_evtSession && s_propsToken.value) { s_evtSession.MediaPropertiesChanged(s_propsToken);  s_propsToken = {}; }
        if (s_evtSession && s_pbToken.value)    { s_evtSession.PlaybackInfoChanged(s_pbToken);         s_pbToken    = {}; }
        if (s_evtMgr    && s_sessionToken.value){ s_evtMgr.CurrentSessionChanged(s_sessionToken);     s_sessionToken = {}; }
        s_evtSession = nullptr;

        if (!s_evtMgr) return;
        // Watch for session switches (e.g. switching media app)
        s_sessionToken = s_evtMgr.CurrentSessionChanged([wake](auto&&, auto&&) { SetEvent(wake.get()); });
        s_evtSession = s_evtMgr.GetCurrentSession();
        if (s_evtSession) {
            // Force immediate update on track change (thumbnail loads async from Windows)
            s_propsToken = s_evtSession.MediaPropertiesChanged([wake](auto&&, auto&&) { SetEvent(wake.get()); });
            s_pbToken    = s_evtSession.PlaybackInfoChanged(   [wake](auto&&, auto&&) { SetEvent(wake.get()); });
        }
    };

    while (g_mpRunning) {
        // Poll every 1s for fast updates
        WaitForSingleObject(hEvent, 1000);
        if (!g_mpRunning) break;

        // Re-acquire manager if needed
        if (!s_evtMgr) {
            try {
                s_evtMgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                resubscribe();
            } catch (...) { s_evtMgr = nullptr; }
        }

        MediaPlayer_Update();
    }

    // Cleanup event subscriptions
    if (s_evtSession && s_propsToken.value)  s_evtSession.MediaPropertiesChanged(s_propsToken);
    if (s_evtSession && s_pbToken.value)     s_evtSession.PlaybackInfoChanged(s_pbToken);
    if (s_evtMgr    && s_sessionToken.value) s_evtMgr.CurrentSessionChanged(s_sessionToken);
    s_evtSession = nullptr;
    s_evtMgr     = nullptr;
    s_mgr        = nullptr;
    {
        // the thumb texture is parked for the render thread's drain, never released here
        std::lock_guard<std::mutex> lk(g_mpMutex);
        MpStateClear_Locked();
        g_mpPendingPixels.clear();
    }
    winrt::uninit_apartment();
    return 0;
}

static DWORD WINAPI WaveformThread(LPVOID)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 0;

    static const GUID kCLSID_MMDevEnum =
        {0xBCDE0395,0xE52F,0x467C,{0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E}};
    IMMDeviceEnumerator* pEnum = nullptr;
    CoCreateInstance(kCLSID_MMDevEnum, nullptr, CLSCTX_ALL,
                     __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
    if (!pEnum) { CoUninitialize(); return 0; }

    IMMDevice* pDevice = nullptr;
    pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnum->Release();
    if (!pDevice) { CoUninitialize(); return 0; }

    IAudioClient* pClient = nullptr;
    pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pClient);
    pDevice->Release();
    if (!pClient) { CoUninitialize(); return 0; }

    WAVEFORMATEX* pwfx = nullptr;
    pClient->GetMixFormat(&pwfx);
    if (!pwfx) { pClient->Release(); CoUninitialize(); return 0; }

    bool isFloat = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (!isFloat && pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        static const GUID kGuidFloat =
            {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
        isFloat = (reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx)->SubFormat == kGuidFloat);
    }
    int nChannels = (int)pwfx->nChannels;

    hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                             200000, 0, pwfx, nullptr);
    CoTaskMemFree(pwfx);
    if (FAILED(hr)) { pClient->Release(); CoUninitialize(); return 0; }

    IAudioCaptureClient* pCapture = nullptr;
    pClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCapture);
    if (!pCapture) { pClient->Release(); CoUninitialize(); return 0; }

    pClient->Start();

    float smooth[5] = {};
    // band 0 = bass-like (slow rise/fall), band 4 = treble-like (fast/punchy)
    const float kAttack[5]  = { 0.90f, 0.75f, 0.60f, 0.45f, 0.28f };
    const float kRelease[5] = { 0.03f, 0.06f, 0.11f, 0.17f, 0.25f };

    while (g_wfRunning) {
        Sleep(16);

        if (!isFloat) {
            for (int i = 0; i < 5; i++) {
                smooth[i] += (0.f - smooth[i]) * kRelease[i];
                g_wfBands[i] = smooth[i];
            }
            continue;
        }

        float  sum[5] = {};
        int    cnt[5] = {};
        int    globalFrame = 0;
        UINT32 packetSize  = 0;
        pCapture->GetNextPacketSize(&packetSize);

        while (packetSize > 0) {
            BYTE*  pData     = nullptr;
            UINT32 numFrames = 0;
            DWORD  flags     = 0;
            if (FAILED(pCapture->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr)))
                break;

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData && numFrames > 0) {
                const float* fData = reinterpret_cast<const float*>(pData);
                for (UINT32 f = 0; f < numFrames; f++, globalFrame++) {
                    float s = 0.f;
                    for (int c = 0; c < nChannels; c++)
                        s += fData[f * nChannels + c];
                    s /= (float)nChannels;
                    int b = globalFrame % 5;
                    sum[b] += s * s;
                    cnt[b]++;
                }
            } else {
                globalFrame += (int)numFrames;
            }

            pCapture->ReleaseBuffer(numFrames);
            pCapture->GetNextPacketSize(&packetSize);
        }

        for (int i = 0; i < 5; i++) {
            float rms = (cnt[i] > 0) ? std::min(sqrtf(sum[i] / (float)cnt[i]) * g_wfSensitivity, 1.f) : 0.f;
            float k   = (rms > smooth[i]) ? kAttack[i] : kRelease[i];
            smooth[i] += (rms - smooth[i]) * k;
            g_wfBands[i] = smooth[i];
        }
    }

    pClient->Stop();
    pCapture->Release();
    pClient->Release();
    CoUninitialize();
    return 0;
}

static std::vector<IDirect3DTexture9*> s_customThumbFrames;
static std::vector<float>              s_customThumbDelays;
static int                             s_customThumbCurFrame = 0;
static float                           s_customThumbFrameTimer = 0.f;
static std::string                     s_customThumbLoadedPath;
static int                             s_customThumbW = 0;
static int                             s_customThumbH = 0;

static void CustomThumb_Release()
{
    for (auto* t : s_customThumbFrames) if (t) t->Release();
    s_customThumbFrames.clear();
    s_customThumbDelays.clear();
    s_customThumbCurFrame = 0;
    s_customThumbFrameTimer = 0.f;
    s_customThumbLoadedPath.clear();
    s_customThumbW = s_customThumbH = 0;
}

static void CustomThumb_LoadGIF(IDirect3DDevice9* dev, const char* path)
{
    LoadGifFrames(dev, path, s_customThumbFrames, s_customThumbDelays, s_customThumbW, s_customThumbH);
}

void MediaPlayer_Invalidate()
{
    MpReleaseParkedTextures();
    std::lock_guard<std::mutex> lk(g_mpMutex);
    if (g_mpState.thumbTex) { g_mpState.thumbTex->Release(); g_mpState.thumbTex = nullptr; }
    g_mpState.hasThumb = false;
    g_mpPendingPixels.clear();
    // Force re-decode next update so the new device gets a fresh D3DPOOL_DEFAULT texture
    g_mpState.title.clear();
    // Custom thumb is also D3DPOOL_DEFAULT -- release so it reloads after device reset
    CustomThumb_Release();
}

void RenderMediaPlayer()
{
    MpReleaseParkedTextures();

    // Decode + upload album art on the render thread -- all D3D9 calls must happen here.
    {
        std::vector<uint8_t> pending;
        {
            std::lock_guard<std::mutex> lk(g_mpMutex);
            if (!g_mpPendingPixels.empty())
                pending = std::move(g_mpPendingPixels);
        }
        if (!pending.empty() && g_d3dDev) {
            // Decode at original size to preserve aspect ratio
            IDirect3DTexture9* stage = nullptr;
            HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
                g_d3dDev, pending.data(), (UINT)pending.size(),
                D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                D3DX_FILTER_LINEAR, D3DX_FILTER_LINEAR, 0, nullptr, nullptr, &stage);
            if (SUCCEEDED(hr) && stage) {
                D3DLOCKED_RECT lr{};
                if (SUCCEEDED(stage->LockRect(0, &lr, nullptr, 0))) {
                    D3DSURFACE_DESC desc;
                    stage->GetLevelDesc(0, &desc);
                    int imgW = desc.Width;
                    int imgH = desc.Height;
                    auto* px = reinterpret_cast<uint32_t*>(lr.pBits);
                    int rowPitch = lr.Pitch / 4;
                    
                    // Gamma correction
                    for (int y = 0; y < imgH; ++y)
                    for (int x = 0; x < imgW; ++x) {
                        uint32_t p = px[y * rowPitch + x];
                        auto lin = [](uint8_t v) -> uint8_t {
                            return (uint8_t)(GammaToLinear(v / 255.f) * 255.f + 0.5f);
                        };
                        px[y * rowPitch + x] = (0xFF000000u)
                            | ((uint32_t)lin((p >> 16) & 0xFF) << 16)
                            | ((uint32_t)lin((p >>  8) & 0xFF) <<  8)
                            |  (uint32_t)lin( p        & 0xFF);
                    }
                    stage->UnlockRect(0);
                    
                    // Copy to GPU texture (keep original size for aspect)
                    IDirect3DTexture9* t = nullptr;
                    if (SUCCEEDED(g_d3dDev->CreateTexture(imgW, imgH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t, nullptr)) && t) {
                        if (SUCCEEDED(g_d3dDev->UpdateTexture(stage, t))) {
                            std::lock_guard<std::mutex> lk(g_mpMutex);
                            if (g_mpState.thumbTex) g_mpState.thumbTex->Release();
                            g_mpState.thumbTex = t;
                            g_mpState.thumbW = imgW;
                            g_mpState.thumbH = imgH;
                            g_mpState.hasThumb = true;
                        } else {
                            t->Release();
                        }
                    }
                }
                stage->Release();
            }
        }
    }

    // Custom album cover: load/reload on path change (render thread, D3DPOOL_DEFAULT)
    // Supports static images (PNG/JPG/BMP/TGA/DDS) and animated GIFs.
    if (g_mpCustomThumb && g_d3dDev) {
        if (g_mpCustomThumbPath != s_customThumbLoadedPath) {
            CustomThumb_Release();
            s_customThumbLoadedPath = g_mpCustomThumbPath;
            if (!g_mpCustomThumbPath.empty()) {
                const char* ext = strrchr(g_mpCustomThumbPath.c_str(), '.');
                if (ext && _stricmp(ext, ".gif") == 0) {
                    CustomThumb_LoadGIF(g_d3dDev, g_mpCustomThumbPath.c_str());
                } else {
                    IDirect3DTexture9* ct = nullptr;
                    if (SUCCEEDED(D3DXCreateTextureFromFileA(g_d3dDev, g_mpCustomThumbPath.c_str(), &ct)) && ct) {
                        D3DSURFACE_DESC cdesc{};
                        if (SUCCEEDED(ct->GetLevelDesc(0, &cdesc))) {
                            s_customThumbW = cdesc.Width;
                            s_customThumbH = cdesc.Height;
                        }
                        s_customThumbFrames.push_back(ct);
                        s_customThumbDelays.push_back(0.f);
                    }
                }
            }
        }
        // Advance GIF animation
        if (s_customThumbFrames.size() > 1) {
            s_customThumbFrameTimer += std::min(ImGui::GetIO().DeltaTime, 0.1f);
            float fd = s_customThumbDelays[s_customThumbCurFrame];
            if (fd <= 0.f) fd = 0.1f;
            while (s_customThumbFrameTimer >= fd) {
                s_customThumbFrameTimer -= fd;
                s_customThumbCurFrame = (s_customThumbCurFrame + 1) % (int)s_customThumbFrames.size();
                fd = s_customThumbDelays[s_customThumbCurFrame]; if (fd <= 0.f) fd = 0.1f;
            }
        }
    } else if (!g_mpCustomThumb && !s_customThumbFrames.empty()) {
        CustomThumb_Release();
    }

    // static: copy-assigning into the same strings every frame reuses their buffers.
    // thumbTex is AddRef'd here and released on every exit below, as before.
    static MediaPlayerState snap;
    {
        std::lock_guard<std::mutex> lk(g_mpMutex);
        snap = g_mpState;
        if (snap.thumbTex) snap.thumbTex->AddRef();
    }

    // Lazy-start waveform capture only when user enables it -- avoids loading WASAPI
    // audio DLLs at startup, which destabilises TF2's material system job thread.
    if (g_mpWaveform && !g_wfRunning) {
        g_wfRunning = true;
        g_wfThread  = CreateThread(nullptr, 0, WaveformThread, nullptr, 0, nullptr);
        if (g_wfThread) SetThreadPriority(g_wfThread, THREAD_PRIORITY_BELOW_NORMAL);
    }

    if (snap.title.empty() && snap.artist.empty()) {
        if (snap.thumbTex) snap.thumbTex->Release();
        g_mpLastBoxH   = 0.f;
        g_mpLastBoxY   = 0.f;
        g_mpLastBoxPos = -1;
        return;
    }

    // Trigger lyrics fetch when track changes or feature is first enabled
    if (g_mpShowLyrics) {
        static std::string trackKey;
        trackKey.assign(snap.title).append("|").append(snap.artist);
        bool need = false;
        { std::lock_guard<std::mutex> lk(s_lyricsMutex);
          need = (s_lyricsTrack != trackKey && s_lyricsState != 1); }
        if (need) {
            { std::lock_guard<std::mutex> lk(s_lyricsMutex);
              s_lyricsState = 1; s_lyricsTrack = trackKey; }
            auto* fa = new LyricsFetchArgs{ snap.title, snap.artist, snap.totalTimeMs };
            HANDLE h = CreateThread(nullptr, 0, FetchLyricsThread, fa, 0, nullptr);
            if (h) { SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(h); }
            else delete fa;
        }
    }

    // Resolve style -- copy-from-watermark or own settings
    const float*  bgCol      = g_mpCopyWmStyle ? g_wmBgColor : g_mpBgColor;
    const float*  titleCol   = g_mpCopyWmStyle ? g_wmTitleColor      : g_mpTitleColor;
    const float*  artistCol  = g_mpCopyWmStyle ? g_wmDimColor        : g_mpArtistColor;
    const float   rounding     = g_mpCopyWmStyle ? g_wmBgRounding      : g_mpBgRounding;
    const int     cornerFlags  = g_mpCopyWmStyle ? g_wmBgCornerFlags   : g_mpBgCornerFlags;
    const bool    drawBg       = g_mpCopyWmStyle ? g_drawWatermarkBg   : g_mpBgDraw;
    const bool    blurBg       = g_bgBlur;
    const OutlineLayer* outlineLayers = g_mpCopyWmStyle ? g_wmOutlineLayers : g_mpOutlineLayers;
    const int           outlineCount  = g_mpCopyWmStyle ? g_wmOutlineLayerCount : g_mpOutlineLayerCount;
    const ImDrawFlags mpCF     = CornerFlagsToImDraw(cornerFlags);
    const bool*   fflags     = g_mpCopyWmStyle ? g_wmFontFlags       : g_mpFontFlags;
    const float   sc         = g_mpCopyWmStyle ? g_watermarkScale    : g_mpScale;
    ImFont*       font       = (g_mpFont && g_mpFont->IsLoaded()) ? g_mpFont : ImGui::GetFont();
    // Blob settings -- copy from wm when copy-style is on
    const bool    blobEnable    = g_mpCopyWmStyle ? g_wmBlobEnable    : g_mpBlobEnable;
    const bool    blobUseSheen  = g_mpCopyWmStyle ? g_wmBlobUseSheen  : g_mpBlobUseSheen;
    const float*  blobColorSrc  = g_mpCopyWmStyle ? g_wmBlobColor     : g_mpBlobColor;
    const float   blobAlphaSrc  = g_mpCopyWmStyle ? g_wmBlobAlpha     : g_mpBlobAlpha;
    const float   blobSpeed     = g_mpCopyWmStyle ? g_wmBlobSpeed     : g_mpBlobSpeed;
    const float   blobSize      = g_mpCopyWmStyle ? g_wmBlobSize      : g_mpBlobSize;
    // Precipitation settings -- copy from wm when copy-style is on
    const bool    precipEnable  = g_mpCopyWmStyle ? g_wmPrecipEnable  : g_mpPrecipEnable;
    const int     precipMode    = g_mpCopyWmStyle ? g_wmPrecipMode    : g_mpPrecipMode;
    const int     precipCount   = g_mpCopyWmStyle ? g_wmPrecipCount   : g_mpPrecipCount;
    const float   precipSpeed   = g_mpCopyWmStyle ? g_wmPrecipSpeed   : g_mpPrecipSpeed;
    const float   precipSize    = g_mpCopyWmStyle ? g_wmPrecipSize    : g_mpPrecipSize;
    const float   precipWind    = g_mpCopyWmStyle ? g_wmPrecipWind    : g_mpPrecipWind;
    const float*  precipColor   = g_mpCopyWmStyle ? g_wmPrecipColor   : g_mpPrecipColor;

    const float fontSize = font->FontSize;
    const float PAD_EDGE = 14.f;
    const float H_PAD    = 10.f * sc;

    // Progress tracking -- DNA style: accumulate wall-clock delta per frame, hard-snap
    // whenever SMTC delivers a new positionMs value (no backward-jump guard).
    static float        s_smoothProg  = 0.f;
    static double       s_calcPosMs   = 0.0;  // running position estimate (ms)
    static int          s_lastPosMs   = -1;   // last positionMs received from SMTC
    static auto         s_lastTime    = std::chrono::steady_clock::now();
    static std::string  s_lastTitle;

    if (snap.title != s_lastTitle) {
        // New track -- seed directly from SMTC, no guard
        s_lastTitle  = snap.title;
        s_calcPosMs  = (double)snap.positionMs;
        s_lastPosMs  = snap.positionMs;
        s_lastTime   = std::chrono::steady_clock::now();
        s_smoothProg = (snap.totalTimeMs > 0)
                       ? std::clamp((float)s_calcPosMs / (float)snap.totalTimeMs, 0.f, 1.f)
                       : 0.f;
    }

    if (snap.isPlaying && snap.totalTimeMs > 0) {
        auto   now   = std::chrono::steady_clock::now();
        double delta = std::chrono::duration<double, std::milli>(now - s_lastTime).count();
        s_lastTime   = now;
        if (snap.positionMs != s_lastPosMs) {
            // SMTC delivered a fresh position -- hard-snap
            s_calcPosMs = (double)snap.positionMs;
            s_lastPosMs = snap.positionMs;
        } else {
            // No new sample -- dead-reckon forward
            s_calcPosMs += delta;
        }
        s_smoothProg = std::clamp((float)s_calcPosMs / (float)snap.totalTimeMs, 0.f, 1.f);
    }

    // Scroll state (title marquee -- shared between modes)
    // Ping-pong: scrolls right until overflow is consumed, pauses, scrolls back, pauses.
    static float       s_scrollOff    = 0.f;  // current pixel offset (0 = start visible)
    static float       s_scrollPause  = 3.0f; // seconds to pause at each end
    static bool        s_scrollFwd    = true; // true = scrolling forward (revealing end)
    static float       s_scrollPauseT = 3.0f; // countdown timer for current pause
    static std::string s_scrollKey;           // reset when track changes
    if (snap.title != s_scrollKey) {
        s_scrollOff    = 0.f;
        s_scrollFwd    = true;
        s_scrollPauseT = s_scrollPause;
        s_scrollKey    = snap.title;
    }
    // Separate artist scroll state (card mode only)
    static float       s_aScrollOff    = 0.f;
    static bool        s_aScrollFwd    = true;
    static float       s_aScrollPauseT = 3.0f;
    static std::string s_aScrollKey;
    if (snap.artist != s_aScrollKey) {
        s_aScrollOff    = 0.f;
        s_aScrollFwd    = true;
        s_aScrollPauseT = s_scrollPause;
        s_aScrollKey    = snap.artist;
    }
    ImGuiIO&    io = ImGui::GetIO();
    const float scrollDelta = std::min(io.DeltaTime, 0.1f);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float sw = io.DisplaySize.x;
    const float sh = io.DisplaySize.y;

    // Max card width: 360px base, scaled -- prevents long titles from filling the screen
    const float MAX_TEXT_W = 280.f * sc;

    // Resolve animated blob color once per frame
    float resolvedBlobCol[3];
    float resolvedBlobAlpha = blobAlphaSrc;
    if (blobUseSheen) {
        GetAnimatedSheenColor(resolvedBlobCol);
    } else {
        resolvedBlobCol[0] = blobColorSrc[0];
        resolvedBlobCol[1] = blobColorSrc[1];
        resolvedBlobCol[2] = blobColorSrc[2];
    }

    auto drawBlob = [&](float bx, float by, float bw, float bh) {
        if (blobEnable && drawBg)
            DrawBlobGradient(dl, bx, by, bw, bh,
                             resolvedBlobCol, resolvedBlobAlpha, blobSize, blobSpeed, 0.f, rounding);
    };

    const bool dropShadow = fflags[4];
    const bool outlineText = fflags[5];
    const ImU32 shadowCol  = IM_COL32(0, 0, 0, 140);
    const ImU32 olineCol   = IM_COL32(0, 0, 0, 255);
    const float shadowOff  = 1.5f * sc;

    auto drawText = [&](ImVec2 pos, ImU32 col, const char* str) {
        if (outlineText) {
            dl->AddText(font, fontSize, ImVec2(pos.x-1,pos.y),   olineCol, str);
            dl->AddText(font, fontSize, ImVec2(pos.x+1,pos.y),   olineCol, str);
            dl->AddText(font, fontSize, ImVec2(pos.x,  pos.y-1), olineCol, str);
            dl->AddText(font, fontSize, ImVec2(pos.x,  pos.y+1), olineCol, str);
        }
        if (dropShadow)
            dl->AddText(font, fontSize, ImVec2(pos.x+shadowOff, pos.y+shadowOff), shadowCol, str);
        dl->AddText(font, fontSize, pos, col, str);
    };

    auto mkCol3 = [](const float* c, int a) -> ImU32 {
        return IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),a);
    };
    auto mkCol4 = [](const float* c) -> ImU32 {
        return IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),(int)(c[3]*255));
    };

    // Smooth-scroll + animated-size state for lyrics
    static float       s_lyricScrollTop = 0.f; // fractional index of line at top of box
    static float       s_lyricDisplayW  = 0.f; // animated box width
    static float       s_lyricDisplayH  = 0.f; // animated box height
    static std::string s_lyricAnimTrack;
    static bool        s_lyricWasClamp  = false;

    // Draws synced-lyrics lines with smooth vertical scroll and animated box size
    auto drawLyricsOverlay = [&](float bx, float by, float bw, float bh) {
        if (!g_mpShowLyrics) { s_lyricDisplayH = s_lyricDisplayW = 0.f; return; }
        static std::vector<LyricLine> s_lyrCached;
        static int                    s_lyrCachedGen = -1;
        int lyrState, lyrGen;
        { std::lock_guard<std::mutex> lk(s_lyricsMutex);
          lyrState = s_lyricsState; lyrGen = s_lyricsGeneration; }
        if (lyrState != 2) { s_lyricDisplayH = s_lyricDisplayW = 0.f; return; }
        if (lyrGen != s_lyrCachedGen) {
            std::lock_guard<std::mutex> lk(s_lyricsMutex);
            s_lyrCached    = s_lyricsSync;
            s_lyrCachedGen = lyrGen;
        }
        if (s_lyrCached.empty()) { s_lyricDisplayH = s_lyricDisplayW = 0.f; return; }
        const std::vector<LyricLine>& lyr = s_lyrCached;

        double curSec = s_calcPosMs / 1000.0 + (double)g_mpLyricsOffset;
        int ai = 0;
        const int n = (int)lyr.size();
        for (int i = 1; i < n; ++i) {
            if (lyr[i].startSec <= curSec) ai = i; else break;
        }

        // Use lyrics font built at the exact scale to avoid upscale blur
        ImFont* lyrFont = (g_mpLyricsFont && g_mpLyricsFont->IsLoaded()) ? g_mpLyricsFont : font;
        const float lfs      = lyrFont->FontSize;
        const float LG       = 6.f * sc;
        const float LS       = 4.f * sc;
        const float VP       = 7.f * sc;
        const float HP       = 12.f * sc;
        const float lineStep = lfs + LS;

        // Build visual line array; when clamping, word-wrap long lines at the independent clamp width.
        // Cache across frames -- rebuilding involves CalcTextSizeA per line and is expensive.
        // Pointers in VisLine point into s_lyrCached strings; cache is invalidated whenever
        // s_lyrCached is refreshed (lyrGen changes) or layout parameters change.
        struct VisLine { int srcIdx; const char* beg; const char* end; };
        const float clampW   = g_mpLyricsClampW * sc;
        const float maxTextW = g_mpLyricsClampWidth ? clampW : bw - HP * 2.f;
        static std::vector<VisLine> s_visCache;
        static int    s_visCachedGen    = -1;
        static bool   s_visWasClamp     = false;
        static bool   s_visWasEllipsis  = false;
        static float  s_visCachedClampW = 0.f;
        static float  s_visCachedBw     = 0.f;
        static float  s_visCachedLfs    = 0.f;
        static ImFont* s_visCachedFont  = nullptr;

        if (lyrGen != s_visCachedGen
            || g_mpLyricsClampWidth  != s_visWasClamp
            || g_mpLyricsEllipsis    != s_visWasEllipsis
            || clampW != s_visCachedClampW
            || bw     != s_visCachedBw
            || lfs    != s_visCachedLfs
            || lyrFont != s_visCachedFont) {
            s_visCache.clear();
            s_visCache.reserve(n);
            // When ellipsis mode is on, don't word-wrap — one vis line per source line.
            // Truncation with "..." is applied at draw time.
            const bool doWrap = g_mpLyricsClampWidth && !g_mpLyricsEllipsis;
            for (int k = 0; k < n; ++k) {
                const char* str = lyr[k].text.c_str();
                if (!doWrap || str[0] == '\0') {
                    s_visCache.push_back({ k, str, nullptr });
                    continue;
                }
                const char* lineStart = str;
                const char* p = str;
                while (*p) {
                    const char* wEnd = p;
                    while (*wEnd && *wEnd != ' ') ++wEnd;
                    float w = lyrFont->CalcTextSizeA(lfs, FLT_MAX, 0.f, lineStart, wEnd).x;
                    if (w > maxTextW && p > lineStart) {
                        const char* prevEnd = p;
                        if (prevEnd > lineStart && *(prevEnd - 1) == ' ') --prevEnd;
                        s_visCache.push_back({ k, lineStart, prevEnd });
                        lineStart = p;
                    } else {
                        p = wEnd;
                        if (*p == ' ') ++p;
                        if (!*p) s_visCache.push_back({ k, lineStart, nullptr });
                    }
                }
            }
            s_visCachedGen    = lyrGen;
            s_visWasClamp     = g_mpLyricsClampWidth;
            s_visWasEllipsis  = g_mpLyricsEllipsis;
            s_visCachedClampW = clampW;
            s_visCachedBw     = bw;
            s_visCachedLfs    = lfs;
            s_visCachedFont   = lyrFont;
        }
        const std::vector<VisLine>& vis = s_visCache;
        if (vis.empty()) { s_lyricDisplayH = s_lyricDisplayW = 0.f; return; }

        // Find first and last visual line index for the active source lyric
        const int viN = (int)vis.size();
        int viAI = 0, viAIEnd = 0;
        for (int i = 0; i < viN; ++i) { if (vis[i].srcIdx >= ai) { viAI = i; break; } }
        viAIEnd = viAI;
        while (viAIEnd + 1 < viN && vis[viAIEnd + 1].srcIdx == ai) ++viAIEnd;

        // Window: (rows-1)/2 prev + all active sub-lines + rows/2 next
        int numActive  = viAIEnd - viAI + 1;
        int extra      = std::max(0, g_mpLyricsRows - numActive);
        int prevLines  = extra / 2;
        int nextLines  = extra - prevLines;
        int topLine    = std::max(0,     viAI    - prevLines);
        int botLine    = std::min(viN-1, viAIEnd + nextLines);
        int numVisible = botLine - topLine + 1;

        // Target width: expand to fit text, capped at clamp limit (or screen edge)
        float targetW = bw;
        for (int k = topLine; k <= botLine; ++k) {
            float w = lyrFont->CalcTextSizeA(lfs, FLT_MAX, 0.f, vis[k].beg, vis[k].end).x + HP * 2.f;
            if (w > targetW) targetW = w;
        }
        const float maxW = g_mpLyricsClampWidth ? std::min(clampW + HP * 2.f, sw - 2.f * PAD_EDGE)
                                                : (sw - 2.f * PAD_EDGE);
        targetW = std::min(targetW, maxW);
        float targetH = VP * 2.f + numVisible * lfs + (numVisible - 1) * LS;

        // Snap immediately on track change or clamp toggle
        bool clampChanged = (s_lyricWasClamp != g_mpLyricsClampWidth);
        if (s_lyricAnimTrack != snap.title || clampChanged) {
            s_lyricAnimTrack = snap.title;
            s_lyricWasClamp  = g_mpLyricsClampWidth;
            s_lyricScrollTop = (float)topLine;
            s_lyricDisplayW  = targetW;
            s_lyricDisplayH  = targetH;
        }

        // Animate scroll, width, height with the same easing (~0.25s)
        const float t = 1.f - expf(-scrollDelta * 10.f);
        s_lyricScrollTop += ((float)topLine - s_lyricScrollTop) * t;
        s_lyricDisplayW  += (targetW        - s_lyricDisplayW)  * t;
        s_lyricDisplayH  += (targetH        - s_lyricDisplayH)  * t;
        if (fabsf((float)topLine - s_lyricScrollTop) < 0.001f) s_lyricScrollTop = (float)topLine;
        if (fabsf(targetW - s_lyricDisplayW)         < 0.5f)   s_lyricDisplayW  = targetW;
        if (fabsf(targetH - s_lyricDisplayH)         < 0.5f)   s_lyricDisplayH  = targetH;

        float lyrW = s_lyricDisplayW;
        float lyrH = s_lyricDisplayH;

        bool  posRight = (g_mpPos == WM_TOP_RIGHT  || g_mpPos == WM_BOTTOM_RIGHT);
        float lyrX, lyrY;
        if (g_mpLyricsSide) {
            lyrX = posRight ? (bx - LG - lyrW) : (bx + bw + LG);
            lyrY = by + (bh - lyrH) * 0.5f;
        } else {
            // Anchor based on lyrics text alignment so the text origin stays fixed as the box grows wider than the player.
            if      (g_mpLyricsAlign == 2) lyrX = bx + bw - lyrW;          // right-align: right edge pinned to player right
            else if (g_mpLyricsAlign == 1) lyrX = bx + (bw - lyrW) * 0.5f; // center: midpoint pinned to player center
            else                           lyrX = bx;                        // left-align: left edge pinned to player left
            lyrY = g_mpLyricsAbove ? (by - LG - lyrH) : (by + bh + LG);
        }
        lyrX += g_mpLyricsXOff * sc;
        lyrY += g_mpLyricsYOff * sc;
        lyrX = std::min(lyrX, sw - PAD_EDGE - lyrW);
        lyrY = std::clamp(lyrY, PAD_EDGE, sh - PAD_EDGE - lyrH);

        // Avoid watermark overlap
        if (g_mpLyricsAvoidWm && g_wmLastBoxH2 > 0.f) {
            const float GAP  = 4.f;
            float wmX  = g_wmLastBoxX;
            float wmY  = g_wmLastBoxY2;
            float wmX2 = wmX + g_wmLastBoxW;
            float wmY2 = wmY + g_wmLastBoxH2;
            bool xOvlp = (lyrX < wmX2 + GAP) && (lyrX + lyrW > wmX - GAP);
            bool yOvlp = (lyrY < wmY2 + GAP) && (lyrY + lyrH > wmY - GAP);
            if (xOvlp && yOvlp) {
                float pushUp   = fabsf(lyrY - (wmY  - GAP - lyrH));
                float pushDown = fabsf(lyrY - (wmY2 + GAP));
                if (pushUp < pushDown)
                    lyrY = wmY  - GAP - lyrH;
                else
                    lyrY = wmY2 + GAP;
                lyrY = std::clamp(lyrY, PAD_EDGE, sh - PAD_EDGE - lyrH);
            }
        }

        // Full background treatment — same as the player card
        if (drawBg && g_mpLyricsBgDraw) {
            if (blurBg && g_d3dDev)
                DrawBgBlurRect(dl, lyrX, lyrY, lyrX+lyrW, lyrY+lyrH, rounding, mpCF);
            const bool useShadow = g_mpCopyWmStyle ? g_wmBoxShadow : g_mpBoxShadow;
            if (useShadow) {
                float olExt = 0.f;
                for (int _i = 0; _i < outlineCount; ++_i)
                    if (outlineLayers[_i].enable) olExt += outlineLayers[_i].thick * sc;
                const float* shCol  = g_mpCopyWmStyle ? g_wmBoxShadowColor : g_mpBoxShadowColor;
                const float  shBlur = g_mpCopyWmStyle ? g_wmBoxShadowBlur  : g_mpBoxShadowBlur;
                const float  shOffX = (g_mpCopyWmStyle ? g_wmBoxShadowOffX : g_mpBoxShadowOffX) * sc;
                const float  shOffY = (g_mpCopyWmStyle ? g_wmBoxShadowOffY : g_mpBoxShadowOffY) * sc;
                DrawBoxShadow(dl,
                    lyrX - olExt, lyrY - olExt, lyrX+lyrW + olExt, lyrY+lyrH + olExt,
                    shOffX, shOffY, shBlur*sc, shCol, 1.f, rounding + olExt, mpCF);
            }
            dl->AddRectFilled(ImVec2(lyrX, lyrY), ImVec2(lyrX+lyrW, lyrY+lyrH),
                              mkCol4(bgCol), rounding, mpCF);
            if (g_mpLyricsBlob) drawBlob(lyrX, lyrY, lyrW, lyrH);
            DrawOutlineLayers(dl, lyrX, lyrY, lyrX+lyrW, lyrY+lyrH, outlineLayers, outlineCount, sc, rounding, mpCF);
        }
        if (precipEnable && g_mpLyricsPrecip && drawBg && g_mpLyricsBgDraw) {
            float pc[4] = { precipColor[0], precipColor[1], precipColor[2], precipColor[3] };
            DrawPrecipitation(dl, lyrX, lyrY, lyrW, lyrH,
                precipMode, precipCount, precipSpeed, precipSize, precipWind, pc, scrollDelta, 2);
        }

        // Single clip rect; lines scroll in/out via the boundary
        dl->PushClipRect(ImVec2(lyrX + HP, lyrY + VP),
                         ImVec2(lyrX + lyrW - HP, lyrY + lyrH - VP), true);

        // Draw stable window ± 1 to cover lines mid-scroll
        int kDraw0 = std::max(0, topLine - 1);
        int kDraw1 = std::min(viN - 1, botLine + 1);
        char s_ellBuf[512]; // scratch buffer for ellipsis truncation, reused per line
        for (int k = kDraw0; k <= kDraw1; ++k) {
            const char* beg = vis[k].beg;
            const char* end = vis[k].end;
            if (!beg || !beg[0]) continue;

            // Ellipsis truncation: when clamp+ellipsis is on and text overflows, append "..."
            if (g_mpLyricsEllipsis && g_mpLyricsClampWidth) {
                float fullW = lyrFont->CalcTextSizeA(lfs, FLT_MAX, 0.f, beg, end).x;
                if (fullW > maxTextW) {
                    const char* ellipsis = "...";
                    float ellW = lyrFont->CalcTextSizeA(lfs, FLT_MAX, 0.f, ellipsis).x;
                    float avail = maxTextW - ellW;
                    const char* e = end ? end : beg + strlen(beg);
                    const char* p = beg;
                    while (p < e) {
                        unsigned char c = (unsigned char)*p;
                        int step = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                        const char* next = p + step;
                        if (next > e) next = e;
                        if (lyrFont->CalcTextSizeA(lfs, FLT_MAX, 0.f, beg, next).x > avail) break;
                        p = next;
                    }
                    snprintf(s_ellBuf, sizeof(s_ellBuf), "%.*s...", (int)(p - beg), beg);
                    beg = s_ellBuf;
                    end = nullptr;
                }
            }

            // Active source lyric brightest; dimmer by source distance
            float dist   = fabsf((float)(vis[k].srcIdx - ai));
            float alphaF = std::max(0.f, 1.f - dist * 0.55f) * g_mpLyricsColor[3];
            ImU32 col    = IM_COL32((int)(g_mpLyricsColor[0]*255),(int)(g_mpLyricsColor[1]*255),
                                    (int)(g_mpLyricsColor[2]*255),(int)(alphaF*255));

            float lineY = lyrY + VP + ((float)k - s_lyricScrollTop) * lineStep;
            ImVec2 tsz  = lyrFont->CalcTextSizeA(lfs, FLT_MAX, 0.f, beg, end);
            float tx;
            if (g_mpLyricsAlign == 1)
                tx = lyrX + (lyrW - tsz.x) * 0.5f;  // center
            else if (g_mpLyricsAlign == 2)
                tx = lyrX + lyrW - HP - tsz.x;        // right
            else
                tx = lyrX + HP;                        // left (default)

            if (outlineText) {
                ImU32 oc = IM_COL32(0, 0, 0, (int)(alphaF * 255));
                dl->AddText(lyrFont, lfs, ImVec2(tx-1, lineY),   oc, beg, end);
                dl->AddText(lyrFont, lfs, ImVec2(tx+1, lineY),   oc, beg, end);
                dl->AddText(lyrFont, lfs, ImVec2(tx,   lineY-1), oc, beg, end);
                dl->AddText(lyrFont, lfs, ImVec2(tx,   lineY+1), oc, beg, end);
            }
            if (dropShadow) {
                ImU32 shCol = IM_COL32(0, 0, 0, (int)(alphaF * 140));
                dl->AddText(lyrFont, lfs, ImVec2(tx + shadowOff, lineY + shadowOff), shCol, beg, end);
            }
            dl->AddText(lyrFont, lfs, ImVec2(tx, lineY), col, beg, end);
        }
        dl->PopClipRect();
    };

    // ---- Lyrics-only mode (no card, no strip) ------------------
    if (g_mpLyricsOnly && g_mpShowLyrics)
    {
        // Anchor lyrics box to the same corner as the media player position
        float lox = PAD_EDGE + g_mpXOffset;
        float loy = PAD_EDGE + g_mpYOffset * (sh * 0.84f);
        switch (g_mpPos) {
        case WM_TOP_RIGHT:     lox = sw - PAD_EDGE + g_mpXOffset; loy = PAD_EDGE + g_mpYOffset*(sh*0.84f); break;
        case WM_TOP_CENTER:    lox = sw * 0.5f    + g_mpXOffset; loy = PAD_EDGE + g_mpYOffset*(sh*0.84f); break;
        case WM_BOTTOM_LEFT:   lox = PAD_EDGE     + g_mpXOffset; loy = sh - PAD_EDGE - g_mpYOffset*(sh*0.84f); break;
        case WM_BOTTOM_CENTER: lox = sw * 0.5f    + g_mpXOffset; loy = sh - PAD_EDGE - g_mpYOffset*(sh*0.84f); break;
        case WM_BOTTOM_RIGHT:  lox = sw - PAD_EDGE + g_mpXOffset; loy = sh - PAD_EDGE - g_mpYOffset*(sh*0.84f); break;
        default: break;
        }
        drawLyricsOverlay(lox, loy, 0.f, 0.f);
        if (snap.thumbTex) snap.thumbTex->Release();
        return;
    }

    // ---- Mode 1: plain text strip ------------------------------
    if (g_mpMode == 1)
    {
        char textBuf[512];
        snprintf(textBuf, sizeof(textBuf), "%s - %s", snap.title.c_str(), snap.artist.c_str());

        const float DOT_R    = 1.4f * sc;
        const float DOT_STEP = 4.0f * sc;
        const float WF_GAP   = 3.f  * sc;
        // Free position: dots float outside the box, don't reserve space inside
        const float wfW      = (g_mpWaveform && g_mpWaveformPos != 2) ? (2.f * DOT_R + 4.f * DOT_STEP + WF_GAP) : 0.f;
        const float BOX_H    = 22.f * sc;
        float fullTextW  = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, textBuf).x;
        float availTextW = MAX_TEXT_W;
        float clipW      = std::min(fullTextW, availTextW);
        float boxW       = H_PAD * 2.f + clipW + wfW;
        // Apply max / static width cap
        if (g_mpStaticWidth && g_mpMaxWidth > 0.f) {
            boxW  = g_mpMaxWidth * sc;
            clipW = std::max(0.f, boxW - H_PAD * 2.f - wfW);
        } else if (g_mpMaxWidth > 0.f && boxW > g_mpMaxWidth * sc) {
            boxW  = g_mpMaxWidth * sc;
            clipW = std::max(0.f, boxW - H_PAD * 2.f - wfW);
        }

        // Advance scroll if text overflows
        float overflow = fullTextW - clipW;
        if (overflow > 0.f) {
            const float SCROLL_SPD = 20.f * sc;
            if (s_scrollPauseT > 0.f) {
                s_scrollPauseT -= scrollDelta;
            } else {
                if (s_scrollFwd) {
                    s_scrollOff += SCROLL_SPD * scrollDelta;
                    if (s_scrollOff >= overflow) {
                        s_scrollOff    = overflow;
                        s_scrollFwd    = false;
                        s_scrollPauseT = s_scrollPause;
                    }
                } else {
                    s_scrollOff -= SCROLL_SPD * scrollDelta;
                    if (s_scrollOff <= 0.f) {
                        s_scrollOff    = 0.f;
                        s_scrollFwd    = true;
                        s_scrollPauseT = s_scrollPause;
                    }
                }
            }
        } else {
            s_scrollOff = 0.f;
        }

        float boxX, boxY;
        switch (g_mpPos) {
        default:
        case WM_TOP_LEFT:      boxX = PAD_EDGE          + g_mpXOffset; boxY = PAD_EDGE + g_mpYOffset*(sh*0.84f-BOX_H); break;
        case WM_TOP_CENTER:    boxX = (sw-boxW)*0.5f    + g_mpXOffset; boxY = PAD_EDGE + g_mpYOffset*(sh*0.84f-BOX_H); break;
        case WM_TOP_RIGHT:     boxX = sw-boxW-PAD_EDGE  + g_mpXOffset; boxY = PAD_EDGE + g_mpYOffset*(sh*0.84f-BOX_H); break;
        case WM_BOTTOM_LEFT:   boxX = PAD_EDGE          + g_mpXOffset; boxY = sh-BOX_H-PAD_EDGE-g_mpYOffset*(sh*0.84f-BOX_H); break;
        case WM_BOTTOM_CENTER: boxX = (sw-boxW)*0.5f    + g_mpXOffset; boxY = sh-BOX_H-PAD_EDGE-g_mpYOffset*(sh*0.84f-BOX_H); break;
        case WM_BOTTOM_RIGHT:  boxX = sw-boxW-PAD_EDGE  + g_mpXOffset; boxY = sh-BOX_H-PAD_EDGE-g_mpYOffset*(sh*0.84f-BOX_H); break;
        }

        // Shift box so lyrics stay on screen
        if (g_mpLyricsShiftBoxes && g_mpShowLyrics && !g_mpLyricsSide && s_lyricDisplayH > 0.f) {
            const float LG1 = 6.f * sc;
            if (g_mpLyricsAbove) {
                float lyrTop = boxY - LG1 - s_lyricDisplayH + g_mpLyricsYOff * sc;
                if (lyrTop < PAD_EDGE)
                    boxY = std::min(boxY + (PAD_EDGE - lyrTop), sh - BOX_H - PAD_EDGE);
            } else {
                float lyrBot = boxY + BOX_H + LG1 + s_lyricDisplayH + g_mpLyricsYOff * sc;
                if (lyrBot > sh - PAD_EDGE)
                    boxY = std::max(boxY - (lyrBot - (sh - PAD_EDGE)), PAD_EDGE);
            }
        }

        const float bgX0 = boxX      + g_mpBgInsetL * sc;
        const float bgY0 = boxY      + g_mpBgInsetT * sc;
        const float bgX1 = boxX+boxW - g_mpBgInsetR * sc;
        const float bgY1 = boxY+BOX_H - g_mpBgInsetB * sc;
        const float bgW  = std::max(0.f, bgX1 - bgX0);
        const float bgH  = std::max(0.f, bgY1 - bgY0);
        if (drawBg) {
            if (blurBg && g_d3dDev)
                DrawBgBlurRect(dl, bgX0, bgY0, bgX1, bgY1, rounding, mpCF);
            // ---- MP box shadow (mode 1) --------------------------------
            const bool useMpShadow = g_mpCopyWmStyle ? g_wmBoxShadow : g_mpBoxShadow;
            if (useMpShadow) {
                float mpOlExt = 0.f;
                for (int _i = 0; _i < outlineCount; ++_i)
                    if (outlineLayers[_i].enable) mpOlExt += outlineLayers[_i].thick * sc;
                const float*  shCol   = g_mpCopyWmStyle ? g_wmBoxShadowColor : g_mpBoxShadowColor;
                const float   shBlur  = g_mpCopyWmStyle ? g_wmBoxShadowBlur  : g_mpBoxShadowBlur;
                const float   shOffX  = (g_mpCopyWmStyle ? g_wmBoxShadowOffX : g_mpBoxShadowOffX) * sc;
                const float   shOffY  = (g_mpCopyWmStyle ? g_wmBoxShadowOffY : g_mpBoxShadowOffY) * sc;
                DrawBoxShadow(dl,
                    bgX0 - mpOlExt, bgY0 - mpOlExt,
                    bgX1 + mpOlExt, bgY1 + mpOlExt,
                    shOffX, shOffY, shBlur*sc, shCol, 1.f, rounding + mpOlExt, mpCF);
            }
            {
                ImU32 mpFill1 = mkCol4(bgCol);
                const BgSideGrad* grads1 = g_mpCopyWmStyle ? g_wmBgGrad : g_mpBgGrad;
                if (grads1[0].enable || grads1[1].enable || grads1[2].enable || grads1[3].enable)
                    DrawBgSideGradients(dl, bgX0, bgY0, bgX1, bgY1, grads1, mpFill1, rounding, mpCF);
                else
                    dl->AddRectFilled(ImVec2(bgX0,bgY0), ImVec2(bgX1,bgY1), mpFill1, rounding, mpCF);
            }
        }
        {
            const BgSideGrad* mpGrads1 = g_mpCopyWmStyle ? g_wmBgGrad : g_mpBgGrad;
            const float gradFade = ComputeBgFadeFactor(
                (bgX0 + bgX1) * 0.5f, (bgY0 + bgY1) * 0.5f,
                bgX0, bgY0, bgX1, bgY1, mpGrads1);
            if (blobEnable && drawBg)
                DrawBlobGradient(dl, bgX0, bgY0, bgW, bgH, resolvedBlobCol, resolvedBlobAlpha * gradFade, blobSize, blobSpeed, 0.f, rounding);
            DrawOutlineLayers(dl, bgX0, bgY0, bgX1, bgY1, outlineLayers, outlineCount, sc, rounding, mpCF, gradFade);
            if (precipEnable && drawBg) {
                float pc[4] = { precipColor[0], precipColor[1], precipColor[2], precipColor[3] * gradFade };
                DrawPrecipitation(dl, bgX0, bgY0, bgW, bgH,
                    precipMode, precipCount, precipSpeed, precipSize, precipWind, pc, scrollDelta, 1);
            }
        }

        float textY  = boxY + (BOX_H - font->Ascent + font->Descent) * 0.5f;
        // Shift clip right when waveform is "before" to reserve space for dots
        float wfBeforeShift = (g_mpWaveform && g_mpWaveformPos == 0) ? wfW : 0.f;
        float clipX0 = boxX + H_PAD + wfBeforeShift + g_mpTitleXOff * sc;
        float clipX1 = clipX0 + clipW;
        float textX;
        if (fullTextW <= clipW) {
            if (g_mpTitleAlign == 1)
                textX = clipX0 + (clipW - fullTextW) * 0.5f;
            else if (g_mpTitleAlign == 2)
                textX = clipX0 + clipW - fullTextW;
            else
                textX = clipX0;
        } else {
            textX = clipX0 - s_scrollOff;
        }
        float textYOff = textY + g_mpTitleYOff * sc;
        dl->PushClipRect(ImVec2(clipX0, boxY), ImVec2(clipX1, boxY+BOX_H), true);
        drawText(ImVec2(textX, textYOff), mkCol3(titleCol, 230), textBuf);
        dl->PopClipRect();
        // Waveform dots: before title, after title, or free screen position
        if (g_mpWaveform) {
            const float dotCenterY = boxY + BOX_H * 0.5f;
            const float maxExtend  = std::max(BOX_H * 0.5f - DOT_R - sc, 0.f);
            const ImU32 dotCol     = mkCol3(titleCol, 210);
            static const int kPyramid[5] = {0, 1, 4, 3, 2};
            float dotStartX;
            float dotCY = dotCenterY;
            if (g_mpWaveformPos == 2) {
                dotStartX = g_mpWfFreeX;
                dotCY     = g_mpWfFreeY;
            } else if (g_mpWaveformPos == 0) {
                dotStartX = boxX + H_PAD; // before title clip
            } else {
                dotStartX = clipX1 + WF_GAP; // after title clip
            }
            for (int i = 0; i < 5; i++) {
                float dotX  = dotStartX + DOT_R + (float)i * DOT_STEP;
                float halfH = DOT_R + g_wfBands[kPyramid[i]] * maxExtend;
                dl->AddRectFilled(ImVec2(dotX-DOT_R, dotCY-halfH),
                                  ImVec2(dotX+DOT_R, dotCY+halfH), dotCol, DOT_R);
            }
        }

        drawLyricsOverlay(boxX, boxY, boxW, BOX_H);
        g_mpLastBoxH   = BOX_H;
        g_mpLastBoxY   = boxY;
        g_mpLastBoxPos = g_mpPos;
        if (snap.thumbTex) snap.thumbTex->Release();
        return;
    }

    // ---- Mode 0: card (thumb + title + artist + progress bar) ----
    const float THUMB_SZ = g_mpThumbSize * sc;
    const float PAD      = 8.f  * sc;
    const float PROG_H   = g_mpProgHeight * sc;
    const float LINE_GAP = 4.f  * sc;

    const bool showThumb    = (g_mpCustomThumb && !s_customThumbFrames.empty()) || (g_mpShowThumb && snap.hasThumb && snap.thumbTex);
    const bool showProgress = g_mpShowProgress;

    // Build display artist string (with optional duration suffix)
    char displayArtist[640];
    if (g_mpShowDuration && snap.totalTimeMs > 0) {
        int totalSec = snap.totalTimeMs / 1000;
        snprintf(displayArtist, sizeof(displayArtist), "%s / %d:%02d",
                 snap.artist.c_str(), totalSec / 60, totalSec % 60);
    } else {
        snprintf(displayArtist, sizeof(displayArtist), "%s", snap.artist.c_str());
    }

    // Measure two text lines; cap at MAX_TEXT_W so long titles don't fill the screen
    ImVec2 titleSzFull  = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, snap.title.c_str());
    ImVec2 artistSzFull = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, displayArtist);
    float  titleFullW   = titleSzFull.x;
    float  artistFullW  = artistSzFull.x;
    float  titleClipW   = std::min(titleFullW, MAX_TEXT_W);
    float  artistClipW  = std::min(artistFullW, MAX_TEXT_W);
    float  textH        = fontSize * 2.f + LINE_GAP;
    float  innerH       = showThumb ? std::max(THUMB_SZ, textH) : textH;
    float  boxH         = PAD + innerH + PAD + (showProgress ? PROG_H + PAD : 0.f);
    // In circle mode, thumb is always THUMB_SZ wide (cropped). Otherwise use actual display size.
    float thumbSlotW = THUMB_SZ;
    float thumbDisplayW = THUMB_SZ;
    float thumbDisplayH = THUMB_SZ;
    if (!g_mpThumbCircle && g_mpThumbPreserveAspect && snap.thumbW > 0 && snap.thumbH > 0) {
        float aspect = (float)snap.thumbW / (float)snap.thumbH;
        if (aspect > 1.f) {
            // Landscape: width grows
            thumbDisplayW = THUMB_SZ * aspect;
            thumbSlotW = thumbDisplayW;
        }
        // Portrait/square: stays THUMB_SZ in slot, height shrinks (handled in render)
    }
    const float DOT_R    = 1.4f * sc;
    const float DOT_STEP = 4.0f * sc;
    const float WF_GAP   = 3.f  * sc;
    // Free position: dots float outside the box, don't reserve space inside
    const float wfW      = (g_mpWaveform && g_mpWaveformPos != 2) ? (2.f * DOT_R + 4.f * DOT_STEP + WF_GAP) : 0.f;

    // Title row: title gets full MAX_TEXT_W; wfW reserved for dots (before or after)
    const float availTitleW     = MAX_TEXT_W;
    float effectiveTitleW = std::min(titleFullW, availTitleW);
    // textW: title + dots vs artist (whichever is wider); free-pos has no reserved wfW
    const float textW = (g_mpWaveform && g_mpWaveformPos != 2)
                            ? std::max(effectiveTitleW + wfW, artistClipW)
                            : std::max(titleClipW, artistClipW);
    float  boxW       = PAD + (showThumb ? thumbSlotW + PAD : 0.f) + textW + PAD;
    boxW = std::max(boxW, 120.f * sc);
    // Apply max / static width cap (card mode)
    if (g_mpStaticWidth && g_mpMaxWidth > 0.f) {
        boxW = std::max(120.f * sc, g_mpMaxWidth * sc);
        float thumbArea = showThumb ? thumbSlotW + PAD : 0.f;
        float newTextW  = std::max(40.f * sc, boxW - PAD - thumbArea - PAD);
        titleClipW       = std::min(titleFullW,  newTextW);
        artistClipW      = std::min(artistFullW, newTextW);
        effectiveTitleW  = std::min(titleFullW,  newTextW);
    } else if (g_mpMaxWidth > 0.f && boxW > g_mpMaxWidth * sc) {
        boxW = std::max(120.f * sc, g_mpMaxWidth * sc);
        float thumbArea = showThumb ? thumbSlotW + PAD : 0.f;
        float newTextW  = std::max(40.f * sc, boxW - PAD - thumbArea - PAD);
        titleClipW       = std::min(titleFullW,  newTextW);
        artistClipW      = std::min(artistFullW, newTextW);
        effectiveTitleW  = std::min(titleFullW,  newTextW);
    }
    // Per-element max width narrowing (applied after global/static width cap)
    if (g_mpTitleMaxW > 0.f) {
        float cap = g_mpTitleMaxW * sc;
        titleClipW      = std::min(titleClipW,      cap);
        effectiveTitleW = std::min(effectiveTitleW,  cap);
    }
    if (g_mpArtistMaxW > 0.f) {
        float cap = g_mpArtistMaxW * sc;
        artistClipW = std::min(artistClipW, cap);
    }
    // Advance title scroll only when the title is longer than the available space
    float titleOverflow = titleFullW - effectiveTitleW;
    if (titleOverflow > 0.f && !g_mpTitleEllipsis) {
        const float SCROLL_SPD = 40.f * sc;
        if (s_scrollPauseT > 0.f) {
            s_scrollPauseT -= scrollDelta;
        } else {
            if (s_scrollFwd) {
                s_scrollOff += SCROLL_SPD * scrollDelta;
                if (s_scrollOff >= titleOverflow) {
                    s_scrollOff    = titleOverflow;
                    s_scrollFwd    = false;
                    s_scrollPauseT = s_scrollPause;
                }
            } else {
                s_scrollOff -= SCROLL_SPD * scrollDelta;
                if (s_scrollOff <= 0.f) {
                    s_scrollOff    = 0.f;
                    s_scrollFwd    = true;
                    s_scrollPauseT = s_scrollPause;
                }
            }
        }
    } else {
        s_scrollOff = 0.f;
    }

    s_aScrollOff = 0.f;

    float boxX, boxY;
    switch (g_mpPos) {
    default:
    case WM_TOP_LEFT:      boxX = PAD_EDGE          + g_mpXOffset; boxY = PAD_EDGE + g_mpYOffset*(sh*0.84f-boxH); break;
    case WM_TOP_CENTER:    boxX = (sw-boxW)*0.5f    + g_mpXOffset; boxY = PAD_EDGE + g_mpYOffset*(sh*0.84f-boxH); break;
    case WM_TOP_RIGHT:     boxX = sw-boxW-PAD_EDGE  + g_mpXOffset; boxY = PAD_EDGE + g_mpYOffset*(sh*0.84f-boxH); break;
    case WM_BOTTOM_LEFT:   boxX = PAD_EDGE          + g_mpXOffset; boxY = sh-boxH-PAD_EDGE-g_mpYOffset*(sh*0.84f-boxH); break;
    case WM_BOTTOM_CENTER: boxX = (sw-boxW)*0.5f    + g_mpXOffset; boxY = sh-boxH-PAD_EDGE-g_mpYOffset*(sh*0.84f-boxH); break;
    case WM_BOTTOM_RIGHT:  boxX = sw-boxW-PAD_EDGE  + g_mpXOffset; boxY = sh-boxH-PAD_EDGE-g_mpYOffset*(sh*0.84f-boxH); break;
    }

    // Shift box so lyrics stay on screen
    if (g_mpLyricsShiftBoxes && g_mpShowLyrics && !g_mpLyricsSide && s_lyricDisplayH > 0.f) {
        const float LG0 = 6.f * sc;
        if (g_mpLyricsAbove) {
            float lyrTop = boxY - LG0 - s_lyricDisplayH + g_mpLyricsYOff * sc;
            if (lyrTop < PAD_EDGE)
                boxY = std::min(boxY + (PAD_EDGE - lyrTop), sh - boxH - PAD_EDGE);
        } else {
            float lyrBot = boxY + boxH + LG0 + s_lyricDisplayH + g_mpLyricsYOff * sc;
            if (lyrBot > sh - PAD_EDGE)
                boxY = std::max(boxY - (lyrBot - (sh - PAD_EDGE)), PAD_EDGE);
        }
    }

    const float bgX0 = boxX      + g_mpBgInsetL * sc;
    const float bgY0 = boxY      + g_mpBgInsetT * sc;
    const float bgX1 = boxX+boxW - g_mpBgInsetR * sc;
    const float bgY1 = boxY+boxH - g_mpBgInsetB * sc;
    const float bgW  = std::max(0.f, bgX1 - bgX0);
    const float bgH  = std::max(0.f, bgY1 - bgY0);
    if (drawBg) {
        if (blurBg && g_d3dDev)
            DrawBgBlurRect(dl, bgX0, bgY0, bgX1, bgY1, rounding, mpCF);
        // ---- MP box shadow (mode 0) --------------------------------
        const bool useMpShadow0 = g_mpCopyWmStyle ? g_wmBoxShadow : g_mpBoxShadow;
        if (useMpShadow0) {
            float mpOlExt0 = 0.f;
            for (int _i = 0; _i < outlineCount; ++_i)
                if (outlineLayers[_i].enable) mpOlExt0 += outlineLayers[_i].thick * sc;
            const float*  shCol0  = g_mpCopyWmStyle ? g_wmBoxShadowColor : g_mpBoxShadowColor;
            const float   shBlur0 = g_mpCopyWmStyle ? g_wmBoxShadowBlur  : g_mpBoxShadowBlur;
            const float   shOffX0 = (g_mpCopyWmStyle ? g_wmBoxShadowOffX : g_mpBoxShadowOffX) * sc;
            const float   shOffY0 = (g_mpCopyWmStyle ? g_wmBoxShadowOffY : g_mpBoxShadowOffY) * sc;
            DrawBoxShadow(dl,
                bgX0 - mpOlExt0, bgY0 - mpOlExt0,
                bgX1 + mpOlExt0, bgY1 + mpOlExt0,
                shOffX0, shOffY0, shBlur0*sc, shCol0, 1.f, rounding + mpOlExt0, mpCF);
        }
        {
            ImU32 mpFill0 = mkCol4(bgCol);
            const BgSideGrad* grads0 = g_mpCopyWmStyle ? g_wmBgGrad : g_mpBgGrad;
            if (grads0[0].enable || grads0[1].enable || grads0[2].enable || grads0[3].enable)
                DrawBgSideGradients(dl, bgX0, bgY0, bgX1, bgY1, grads0, mpFill0, rounding, mpCF);
            else
                dl->AddRectFilled(ImVec2(bgX0,bgY0), ImVec2(bgX1,bgY1), mpFill0, rounding, mpCF);
        }
    }
    {
        const BgSideGrad* mpGrads0 = g_mpCopyWmStyle ? g_wmBgGrad : g_mpBgGrad;
        const float gradFade = ComputeBgFadeFactor(
            (bgX0 + bgX1) * 0.5f, (bgY0 + bgY1) * 0.5f,
            bgX0, bgY0, bgX1, bgY1, mpGrads0);
        if (blobEnable && drawBg)
            DrawBlobGradient(dl, bgX0, bgY0, bgW, bgH, resolvedBlobCol, resolvedBlobAlpha * gradFade, blobSize, blobSpeed, 0.f, rounding);
        DrawOutlineLayers(dl, bgX0, bgY0, bgX1, bgY1, outlineLayers, outlineCount, sc, rounding, mpCF, gradFade);
        if (precipEnable && drawBg) {
            float pc[4] = { precipColor[0], precipColor[1], precipColor[2], precipColor[3] * gradFade };
            DrawPrecipitation(dl, bgX0, bgY0, bgW, bgH,
                precipMode, precipCount, precipSpeed, precipSize, precipWind, pc, scrollDelta, 1);
        }
    }

    float cx = boxX + PAD;
    float cy = boxY + PAD;

    if (showThumb) {
        float thumbSlot = THUMB_SZ; // fixed slot size
        float radius = thumbSlot * 0.5f; // circle radius

        // Circle mode: always fixed circle, crop image to fit
        // Non-circle: use aspect ratio, box resizes
        float thumbDisplayW = thumbSlot;
        float thumbDisplayH = thumbSlot;
        float offsetX = 0.f;
        float offsetY = 0.f;

        IDirect3DTexture9* drawThumbTex = (g_mpCustomThumb && !s_customThumbFrames.empty())
                                         ? s_customThumbFrames[s_customThumbCurFrame] : snap.thumbTex;
        int effThumbW = (g_mpCustomThumb && !s_customThumbFrames.empty()) ? s_customThumbW : snap.thumbW;
        int effThumbH = (g_mpCustomThumb && !s_customThumbFrames.empty()) ? s_customThumbH : snap.thumbH;
        if (!g_mpThumbCircle && g_mpThumbPreserveAspect && effThumbW > 0 && effThumbH > 0) {
            float aspect = (float)effThumbW / (float)effThumbH;
            if (aspect > 1.f) {
                // Landscape: height stays same, width grows
                thumbDisplayW = thumbSlot * aspect;
            } else {
                // Portrait/square: both scale
                thumbDisplayW = thumbSlot * aspect;
                offsetX = (thumbSlot - thumbDisplayW) * 0.5f;
                thumbDisplayH = thumbSlot * aspect;
                offsetY = (thumbSlot - thumbDisplayH) * 0.5f;
            }
        }

        float tx0 = cx + offsetX + g_mpAlbumXOff * sc;
        float ty0 = cy + offsetY + g_mpAlbumYOff * sc;
        float tx1 = tx0 + thumbDisplayW;
        float ty1 = ty0 + thumbDisplayH;
        float effRad = g_mpThumbCircle ? (thumbDisplayW * 0.5f) : (g_mpThumbRounding * sc);
        float outlineThick = g_mpThumbOutlineThick * sc;

        // Outline behind the thumb
        if (g_mpThumbOutline && outlineThick > 0.f) {
            ImU32 outlineCol = IM_COL32(
                (int)(g_mpThumbOutlineColor[0] * 255),
                (int)(g_mpThumbOutlineColor[1] * 255),
                (int)(g_mpThumbOutlineColor[2] * 255),
                (int)(g_mpThumbOutlineColor[3] * 255));
            if (g_mpThumbCircle) {
                // Draw circular outline: push clip for circle, draw outer ring, pop, then cover inner
                float ccx = (tx0 + tx1) * 0.5f;
                float ccy = (ty0 + ty1) * 0.5f;
                dl->PushClipRect(ImVec2(ccx - effRad - outlineThick, ccy - effRad - outlineThick), ImVec2(ccx + effRad + outlineThick, ccy + effRad + outlineThick), true);
                dl->AddCircleFilled(ImVec2(ccx, ccy), effRad + outlineThick * 0.5f, outlineCol);
                dl->PopClipRect();
                // Cover inner black circle
                dl->AddCircleFilled(ImVec2(ccx, ccy), effRad, IM_COL32(0, 0, 0, 255));
            } else {
                // Square/rounded outline
                dl->AddRectFilled(ImVec2(tx0 - outlineThick, ty0 - outlineThick), ImVec2(tx1 + outlineThick, ty1 + outlineThick), outlineCol, effRad + outlineThick);
                dl->AddRectFilled(ImVec2(tx0, ty0), ImVec2(tx1, ty1), IM_COL32(0, 0, 0, 255), effRad);
            }
        }

        // Draw thumb background (black)
        if (g_mpThumbCircle) {
            float ccx = (tx0 + tx1) * 0.5f;
            float ccy = (ty0 + ty1) * 0.5f;
            dl->AddCircleFilled(ImVec2(ccx, ccy), effRad, IM_COL32(0, 0, 0, 255));
        } else {
            dl->AddRectFilled(ImVec2(tx0, ty0), ImVec2(tx1, ty1), IM_COL32(0, 0, 0, 255), effRad);
        }

        // Draw the image
        if (g_mpThumbCircle) {
            float ccx = (tx0 + tx1) * 0.5f;
            float ccy = (ty0 + ty1) * 0.5f;
            dl->PushClipRect(ImVec2(ccx - effRad, ccy - effRad), ImVec2(ccx + effRad, ccy + effRad), true);
            dl->AddImageRounded((ImTextureID)(uintptr_t)drawThumbTex,
                ImVec2(tx0, ty0), ImVec2(tx1, ty1),
                ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), effRad);
            dl->PopClipRect();
        } else {
            dl->AddImageRounded((ImTextureID)(uintptr_t)drawThumbTex,
                ImVec2(tx0, ty0), ImVec2(tx1, ty1),
                ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), effRad);
        }

        cx += thumbSlotW + PAD;
    }

    float midY   = cy + (innerH - textH) * 0.5f;
    float cxText = cx;  // artist always starts here
    // Title: L/C/R alignment within clip, per-element offset, waveform dots before/after/free
    {
        // "Before" mode: shift clip right to leave room for dots to the left
        float wfBeforeShift = (g_mpWaveform && g_mpWaveformPos == 0) ? wfW : 0.f;
        float clipX0 = cx + wfBeforeShift + g_mpTitleXOff * sc;
        float clipX1 = clipX0 + effectiveTitleW;
        float tX;
        if (titleFullW <= effectiveTitleW) {
            if (g_mpTitleAlign == 1)
                tX = clipX0 + (effectiveTitleW - titleFullW) * 0.5f; // center
            else if (g_mpTitleAlign == 2)
                tX = clipX0 + effectiveTitleW - titleFullW;           // right
            else
                tX = clipX0;                                           // left
        } else {
            tX = clipX0 - s_scrollOff; // scrolling — always left-origin
        }
        float tY = midY + g_mpTitleYOff * sc;
        char titleEllipsisBuf[512];
        const char* titleStr = snap.title.c_str();
        if (g_mpTitleEllipsis && titleFullW > effectiveTitleW) {
            float dotW = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, "...").x;
            float maxW = effectiveTitleW - dotW;
            const char* p = snap.title.c_str();
            const char* end = p;
            float accW = 0.f;
            while (*end) {
                unsigned char c0 = (unsigned char)*end;
                const char* nxt = end + 1;
                if (c0 >= 0x80) while ((*nxt & 0xC0) == 0x80) nxt++;
                float cw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, end, nxt).x;
                if (accW + cw > maxW) break;
                accW += cw; end = nxt;
            }
            snprintf(titleEllipsisBuf, sizeof(titleEllipsisBuf), "%.*s...", (int)(end - p), p);
            titleStr = titleEllipsisBuf;
            tX = clipX0;
        }
        dl->PushClipRect(ImVec2(clipX0, tY), ImVec2(clipX1, tY + fontSize), true);
        drawText(ImVec2(tX, tY), mkCol3(titleCol, 230), titleStr);
        dl->PopClipRect();
        // Waveform dots: before title, after title, or free screen position
        if (g_mpWaveform) {
            const float dotCenterY = tY + fontSize * 0.5f;
            const float maxExtend  = fontSize * 0.40f;
            const ImU32 dotCol     = mkCol3(titleCol, 210);
            static const int kPyramid[5] = {0, 1, 4, 3, 2};
            float dotStartX;
            float dotCY = dotCenterY;
            if (g_mpWaveformPos == 2) {
                dotStartX = g_mpWfFreeX;
                dotCY     = g_mpWfFreeY;
            } else if (g_mpWaveformPos == 0) {
                dotStartX = cx; // left of title clip column
            } else {
                dotStartX = clipX1 + WF_GAP; // after title clip
            }
            for (int i = 0; i < 5; i++) {
                float dotX  = dotStartX + DOT_R + (float)i * DOT_STEP;
                float halfH = DOT_R + g_wfBands[kPyramid[i]] * maxExtend;
                dl->AddRectFilled(ImVec2(dotX-DOT_R, dotCY-halfH),
                                  ImVec2(dotX+DOT_R, dotCY+halfH), dotCol, DOT_R);
            }
        }
    }
    // Artist: L/C/R alignment within clip, per-element offset
    {
        float ay     = midY + fontSize + LINE_GAP + g_mpArtistYOff * sc;
        float clipX0 = cxText + g_mpArtistXOff * sc;
        float clipX1 = clipX0 + artistClipW;
        float aX;
        if (artistFullW <= artistClipW) {
            if (g_mpArtistAlign == 1)
                aX = clipX0 + (artistClipW - artistFullW) * 0.5f;
            else if (g_mpArtistAlign == 2)
                aX = clipX0 + artistClipW - artistFullW;
            else
                aX = clipX0;
        } else {
            aX = clipX0;
        }
        char artistEllipsisBuf[640];
        const char* artistStr = displayArtist;
        if (g_mpArtistEllipsis && artistFullW > artistClipW) {
            float dotW = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, "...").x;
            float maxW = artistClipW - dotW;
            const char* p = displayArtist;
            const char* end = p;
            float accW = 0.f;
            while (*end) {
                unsigned char c0 = (unsigned char)*end;
                const char* nxt = end + 1;
                if (c0 >= 0x80) while ((*nxt & 0xC0) == 0x80) nxt++;
                float cw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, end, nxt).x;
                if (accW + cw > maxW) break;
                accW += cw; end = nxt;
            }
            snprintf(artistEllipsisBuf, sizeof(artistEllipsisBuf), "%.*s...", (int)(end - p), p);
            artistStr = artistEllipsisBuf;
            aX = clipX0;
        }
        dl->PushClipRect(ImVec2(clipX0, ay), ImVec2(clipX1, ay + fontSize), true);
        drawText(ImVec2(aX, ay), mkCol3(artistCol, 200), artistStr);
        dl->PopClipRect();
    }

    // Progress bar
    if (showProgress) {
        float halfExtra = g_mpProgBarW * sc * 0.5f;
        float progY  = boxY + boxH - PAD - PROG_H + g_mpProgYOff * sc;
        float progX1 = (g_mpStaticWidth && g_mpMaxWidth > 0.f ? cx : boxX + PAD) - halfExtra + g_mpProgXOff * sc;
        float progX2 = boxX + boxW - PAD + halfExtra + g_mpProgXOff * sc;
        dl->AddRectFilled(ImVec2(progX1, progY), ImVec2(progX2, progY+PROG_H),
                          IM_COL32(60,60,60,200), 1.f);
        float fillX = progX1 + (progX2-progX1) * s_smoothProg;
        if (fillX > progX1)
            dl->AddRectFilled(ImVec2(progX1, progY), ImVec2(fillX, progY+PROG_H),
                              mkCol3(titleCol, 200), 1.f);
    }

    drawLyricsOverlay(boxX, boxY, boxW, boxH);
    g_mpLastBoxH   = boxH;
    g_mpLastBoxY   = boxY;
    g_mpLastBoxPos = g_mpPos;
    if (snap.thumbTex) snap.thumbTex->Release();
}

// ---- end Media Player --------------------------------------
// ---- footstep hooks ----------------------------------------
TF_IsHolidayActive_t o_TF_IsHolidayActive = nullptr;
AttribHookInt_t      o_AttribHookInt       = nullptr;
static float s_hwLastPos[3]   = {};
// ---- Amalgam-style footstep hooks --------------------------
// Hook TF_IsHolidayActive: return true when called from CTFPlayer::FireEvent so
// TF2 always proceeds to query the footstep spell attribute regardless of map.
__declspec(noinline) bool __fastcall h_TF_IsHolidayActive(int eHoliday)
{
    if (g_addrFireEvent_IsHolidayCall &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_addrFireEvent_IsHolidayCall)
        return true;
    // Spells (pumpkin bombs, Halloween fire) also check TF_IsHolidayActive(9) before reading
    // the attribute.  Spoof it on any map so client-side spell visuals fire regardless of season.
    if (eHoliday == 9 /*kHoliday_HalloweenOrFullMoon*/ && g_enableSkins)
    {
        for (auto& [id, cfg] : g_skinCfgs)
            if (cfg.enabled && cfg.spellFlags) return true;
    }
    return o_TF_IsHolidayActive(eHoliday);
}

// Hook CAttributeManager::AttribHookInt: override "halloween_footstep_type" for
// the local player entity when called from CTFPlayer::FireEvent, returning our
// packed RGB color so TF2 dispatches halloween_boss_foot_impact_customcolor.
__declspec(noinline) int __fastcall h_AttribHookInt(
    int value, const char* name, void* econent, void* buffer, bool isGlobalConstString)
{
    if (g_hwFootsteps && g_addrFireEvent_AttribHookCall &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_addrFireEvent_AttribHookCall &&
        name && strcmp(name, "halloween_footstep_type") == 0)
    {
        int li = GetLocalPlayerIndex();
        if (li >= 1 && econent == GetClientEntity(li))
        {
            float sheenCol[3];
            if (g_enableSheenAnim) GetAnimatedSheenColor(sheenCol);
            else { sheenCol[0] = g_sheenColor[0]; sheenCol[1] = g_sheenColor[1]; sheenCol[2] = g_sheenColor[2]; }
            const float* col = g_hwFootUseSheenColor ? sheenCol : g_hwFootColor;
            int r = (int)(col[0] * 255.f + 0.5f);
            int g = (int)(col[1] * 255.f + 0.5f);
            int b = (int)(col[2] * 255.f + 0.5f);
            return (r << 16) | (g << 8) | b;
        }
    }
    return o_AttribHookInt(value, name, econent, buffer, isGlobalConstString);
}

// ---- Halloween footsteps -- per-frame distance-based step detection ----
// Animation events (event 7001) don't fire for the local player in first-person
// because ShouldDraw() returns false -- no SetupBones -- no DispatchAnimEvents.
// Instead, accumulate horizontal distance (g_offOrigin) and dispatch per step.

void HwFoot_Tick()
{
    // Bypass TF2's FireEvent(7001) entirely -- that path fails in first-person because
    // TF2 checks velocity/surface conditions we can't guarantee. Instead dispatch the
    // particle directly via CTE_DispatchEffect("ParticleEffect", data) which bypasses
    // all holiday/velocity/ShouldDraw checks and works in both FP and TP.
    if (!g_hwFootsteps || !g_fnDispatchEffect || !g_fnGetParticleIndex)
    {
        s_hwLastPosValid = false; s_hwStepAccum = 0.f; return;
    }
    if (!IsInGame()) { s_hwLastPosValid = false; s_hwStepAccum = 0.f; return; }

    int li = GetLocalPlayerIndex();
    if (li < 1) { s_hwLastPosValid = false; return; }
    void* ent = GetClientEntity(li);
    if (!ent) { s_hwLastPosValid = false; return; }

    // Suppress footsteps when spy is cloaked or disguised (optional)
    if (g_hwFootHideStealth && GetPlayerClass(li) == 8) {
        static constexpr int kStealthMask =
            (1 << 2) | (1 << 3) | (1 << 4) | (1 << 9);
        bool hidden = false;
        if (g_offPlayerCond > 0) {
            int cond = 0;
            __try { cond = *reinterpret_cast<const int*>(reinterpret_cast<uintptr_t>(ent) + g_offPlayerCond); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
            if (cond & kStealthMask) hidden = true;
        }
        if (hidden) { s_hwStepAccum = 0.f; return; }
    }

    // Read m_vecOrigin via the resolved DT_BaseEntity netvar offset
    if (g_offOrigin <= 0) { s_hwLastPosValid = false; return; }
    float safePos[3];
    __try {
        const float* raw = reinterpret_cast<const float*>(
            reinterpret_cast<uintptr_t>(ent) + g_offOrigin);
        safePos[0] = raw[0]; safePos[1] = raw[1]; safePos[2] = raw[2];
    } __except(EXCEPTION_EXECUTE_HANDLER) { s_hwLastPosValid = false; return; }

    if (!s_hwLastPosValid)
    {
        s_hwLastPos[0] = safePos[0]; s_hwLastPos[1] = safePos[1]; s_hwLastPos[2] = safePos[2];
        s_hwLastPosValid = true;
        return;
    }

    float dx = safePos[0] - s_hwLastPos[0];
    float dy = safePos[1] - s_hwLastPos[1];
    s_hwLastPos[0] = safePos[0]; s_hwLastPos[1] = safePos[1]; s_hwLastPos[2] = safePos[2];
    s_hwStepAccum += sqrtf(dx*dx + dy*dy);

    // Only fire when on the ground (FL_ONGROUND = bit 0 of m_fFlags)
    if (g_offFlags <= 0) { s_hwStepAccum = 0.f; return; }
    int flags = 0;
    __try { flags = *reinterpret_cast<const int*>(reinterpret_cast<uintptr_t>(ent) + g_offFlags); }
    __except(EXCEPTION_EXECUTE_HANDLER) { s_hwLastPosValid = false; return; }
    if (!(flags & 1)) { s_hwStepAccum = 0.f; return; }

    if (s_hwStepAccum < 90.f) return;
    s_hwStepAccum -= 90.f;

    // Always do a fresh index lookup -- unusual-particle registration can shift indices,
    // which would make the cached value point at the wrong (e.g. unusual) particle.
    int footIdx = 0;
    __try { footIdx = g_fnGetParticleIndex("halloween_boss_foot_impact_customcolor"); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (footIdx <= 0) return;

    float sheenCol[3];
    if (g_enableSheenAnim) GetAnimatedSheenColor(sheenCol);
    else { sheenCol[0] = g_sheenColor[0]; sheenCol[1] = g_sheenColor[1]; sheenCol[2] = g_sheenColor[2]; }
    const float* col = g_hwFootUseSheenColor ? sheenCol : g_hwFootColor;

    CEffectData_Weather data = {};
    data.m_vOrigin[0]     = safePos[0];
    data.m_vOrigin[1]     = safePos[1];
    data.m_vOrigin[2]     = safePos[2];
    data.m_nHitBox        = footIdx;
    data.m_flScale        = 1.f;
    data.m_bControlPoint1 = true;
    data.m_vecOffset[0]   = col[0];
    data.m_vecOffset[1]   = col[1];
    data.m_vecOffset[2]   = col[2];
    __try { g_fnDispatchEffect("ParticleEffect", data); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
