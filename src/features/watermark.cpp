#include "watermark.h"

// Iris glance animation state (TU-local)
enum IrisPhase { kIrisIdle, kIrisMoving, kIrisHolding, kIrisReturning };
static IrisPhase s_irisPhase   = kIrisIdle;
static float     s_irisOffFrac = 0.f;
static float     s_irisVel     = 0.f;
static float     s_irisTarget  = 0.f;
static DWORD     s_irisNextMs  = 0;
static DWORD     s_irisHoldEnd = 0;

// ---- Injection loading circle ------------------------------
// Shown top-left after imgui inits. Arc fills clockwise in the accent colour and
// fades out (0.4 s) once g_fullyInjected is set.
void RenderLoadingCircle()
{
    if (!g_loadingCircleStartMs) return;

    static bool  s_done      = false;
    static DWORD s_fadeStart = 0;
    if (s_done) return;

    const float kFadeMs = 400.f;
    // tau = 1.0 s: arc fills fast early, decelerates and asymptotically approaches
    // 1.0 without ever reaching it -- so it can only complete when g_fullyInjected
    // fires.  At the typical 3.3 s injection window the arc sits at ~96 %, so the
    // snap to 100 % on the flag is imperceptible.
    const float kTau    = 1.0f;

    DWORD now     = GetTickCount();
    float elapsed = (float)(now - g_loadingCircleStartMs) * 0.001f; // ms -> s

    float t = 1.f - expf(-elapsed / kTau); // exponential approach, never reaches 1

    bool fullyReady = g_fullyInjected.load();
    if (fullyReady) t = 1.f; // complete the arc the instant injection finishes

    if (fullyReady && !s_fadeStart)
        s_fadeStart = now;

    float alpha = 1.f;
    if (s_fadeStart)
    {
        float fadeT = (float)(now - s_fadeStart) / kFadeMs;
        if (fadeT >= 1.f) { s_done = true; return; }
        alpha = 1.f - fadeT;
    }

    const float cx    = 28.f;
    const float cy    = 28.f;
    const float R     = 16.f;
    const float thick = 3.0f;
    const int   segs  = 64;
    const float kPi   = 3.14159265f;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Translucent dark backdrop so the arc is readable over any background
    dl->AddCircleFilled(ImVec2(cx, cy), R + 5.f, IM_COL32(0, 0, 0, (int)(130.f * alpha)));

    // Dim track ring
    dl->AddCircle(ImVec2(cx, cy), R, IM_COL32(255, 255, 255, (int)(45.f * alpha)), segs, thick);

    // Accent-coloured arc, fills clockwise from the top
    const float startA = -kPi * 0.5f;
    const float endA   = startA + t * 2.f * kPi;

    ImU32 arcCol = IM_COL32(
        (int)(g_accentColor[0] * 255.f),
        (int)(g_accentColor[1] * 255.f),
        (int)(g_accentColor[2] * 255.f),
        (int)(255.f * alpha));

    if (t > 0.f)
    {
        dl->PathArcTo(ImVec2(cx, cy), R, startA, endA, segs);
        dl->PathStroke(arcCol, false, thick);
    }

    // Small dot at the arc tip
    if (t > 0.01f)
    {
        float tipX = cx + R * cosf(endA);
        float tipY = cy + R * sinf(endA);
        dl->AddCircleFilled(ImVec2(tipX, tipY), thick * 1.6f, arcCol);
    }
}

// ---- Logo iris glance animation ----------------------------
// Called once per frame from h_Present.  Drives s_irisOffFrac via a spring so
// the iris smoothly drifts left or right every 10-15 s then returns to centre.
void LogoIrisAnimTick()
{
    float dt = AnimDt();              // game time: 0 while paused, the spring just holds
    if (dt > 0.1f) dt = 0.1f;

    DWORD now = (DWORD)AnimTimeMs();

    // Seed the first interval on first call
    if (!s_irisNextMs)
        s_irisNextMs = now + 10000 + (DWORD)(rand() % 5001);

    switch (s_irisPhase)
    {
    case kIrisIdle:
        if ((long)(now - s_irisNextMs) >= 0)
        {
            s_irisTarget = ((rand() & 1) ? 1.f : -1.f) * 0.045f; // 4.5 % of logo width
            s_irisPhase  = kIrisMoving;
        }
        break;

    case kIrisMoving:
    {
        float err = s_irisTarget - s_irisOffFrac;
        s_irisVel    += (80.f * err - 14.f * s_irisVel) * dt;
        s_irisOffFrac += s_irisVel * dt;
        if (fabsf(err) < 0.002f && fabsf(s_irisVel) < 0.02f)
        {
            s_irisOffFrac = s_irisTarget;
            s_irisVel     = 0.f;
            s_irisHoldEnd = now + 200; // hold 200 ms at peak
            s_irisPhase   = kIrisHolding;
        }
        break;
    }

    case kIrisHolding:
        if ((long)(now - s_irisHoldEnd) >= 0)
        {
            s_irisTarget = 0.f;
            s_irisPhase  = kIrisReturning;
        }
        break;

    case kIrisReturning:
    {
        float err = -s_irisOffFrac;
        s_irisVel    += (80.f * err - 14.f * s_irisVel) * dt;
        s_irisOffFrac += s_irisVel * dt;
        if (fabsf(s_irisOffFrac) < 0.001f && fabsf(s_irisVel) < 0.01f)
        {
            s_irisOffFrac = 0.f;
            s_irisVel     = 0.f;
            s_irisNextMs  = now + 10000 + (DWORD)(rand() % 5001); // next in 10-15 s
            s_irisPhase   = kIrisIdle;
        }
        break;
    }
    }
}

// Draw the eye ring and animated iris at (x,y)-(x+w,y+h) with the given tint.
// Both layers use the same screen rect; the iris is shifted by s_irisOffFrac*w.
void DrawLogo(ImDrawList* dl, float x, float y, float w, float h, ImU32 tint)
{
    if (!g_logoTex) return;
    dl->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        auto* dev = static_cast<IDirect3DDevice9*>(cmd->UserCallbackData);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER,  D3DTEXF_LINEAR);
    }, g_d3dDev);
    dl->AddImage((ImTextureID)g_logoTex,
        ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0,0), ImVec2(1,1), tint);
    if (g_logoIrisTex) {
        float off = s_irisOffFrac * w;
        dl->AddImage((ImTextureID)g_logoIrisTex,
            ImVec2(x + off, y), ImVec2(x + w + off, y + h), ImVec2(0,0), ImVec2(1,1), tint);
    }
    dl->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
        auto* dev = static_cast<IDirect3DDevice9*>(cmd->UserCallbackData);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER,  D3DTEXF_LINEAR);
    }, g_d3dDev);
}

void DrawLogoImage(ImDrawList* dl, IDirect3DTexture9* tex,
                          float x, float y, float w, float h, ImU32 tint, float angle)
{
    if (!tex) return;
    if (angle == 0.f) {
        dl->AddImage((ImTextureID)tex,
            ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0,0), ImVec2(1,1), tint);
    } else {
        float cx = x + w * 0.5f, cy = y + h * 0.5f;
        float cosA = cosf(angle), sinA = sinf(angle);
        float hw = w * 0.5f, hh = h * 0.5f;
        ImVec2 p[4] = {
            { cx + cosA*(-hw) - sinA*(-hh), cy + sinA*(-hw) + cosA*(-hh) },
            { cx + cosA*( hw) - sinA*(-hh), cy + sinA*( hw) + cosA*(-hh) },
            { cx + cosA*( hw) - sinA*( hh), cy + sinA*( hw) + cosA*( hh) },
            { cx + cosA*(-hw) - sinA*( hh), cy + sinA*(-hw) + cosA*( hh) },
        };
        dl->AddImageQuad((ImTextureID)tex,
            p[0], p[1], p[2], p[3],
            ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1), tint);
    }
}

void RenderWatermarkAndOverlay()
{
    ImGuiIO& io = ImGui::GetIO();
    // Clamp delta: a map-load stall produces a huge DeltaTime on the first frame back,
    // which sends the spring smoother wildly past the target and stretches the box.
    float delta = std::min(AnimDt(), 0.1f);

    const bool anythingVisible =
        g_drawWatermarkBg || g_drawWatermarkText ||
        g_drawFPS || g_drawPing || g_drawLoss || g_drawTime;

    g_watermarkFade = anythingVisible
        ? std::min(1.0f, g_watermarkFade + delta * 7.5f)
        : std::max(0.0f, g_watermarkFade - delta * 7.5f);

    if (g_watermarkFade < 0.01f) {
        g_wmLastBoxH2 = 0.f;
        return;
    }

    const int alpha = static_cast<int>(255.0f * g_watermarkFade);

    static time_t s_timeAt = 0;
    static char   timebuf[16]{};
    if (time_t now_t = time(nullptr); now_t != s_timeAt) {
        s_timeAt = now_t;
        strftime(timebuf, sizeof(timebuf), "%H:%M", localtime(&now_t));
    }

    const float sc       = g_watermarkScale;
    const float PAD_EDGE = 14.0f;
    const float ROUNDING = g_wmBgRounding * sc;
    const float BOX_H    = 22.0f * sc;
    const float H_PAD    = 10.0f * sc;

    ImFont* wFont     = (g_wmFont && g_wmFont->IsLoaded()) ? g_wmFont : ImGui::GetFont();
    const float fontSize = wFont->FontSize;

    auto valueColor = [alpha](float v, float warn, bool lowerIsBad) -> ImU32 {
        bool bad = lowerIsBad ? (v <= warn) : (v >= warn);
        return bad
            ? IM_COL32((int)(g_wmAlertColor[0]*255), (int)(g_wmAlertColor[1]*255), (int)(g_wmAlertColor[2]*255), alpha)
            : IM_COL32((int)(g_wmTextColor[0]*255),  (int)(g_wmTextColor[1]*255),  (int)(g_wmTextColor[2]*255),  alpha);
    };
    const ImU32 dimCol  = IM_COL32((int)(g_wmDimColor[0]*255),  (int)(g_wmDimColor[1]*255),  (int)(g_wmDimColor[2]*255),  alpha);
    const ImU32 textCol = IM_COL32((int)(g_wmTextColor[0]*255), (int)(g_wmTextColor[1]*255), (int)(g_wmTextColor[2]*255), alpha);
    const ImU32 titleCol = IM_COL32((int)(g_wmTitleColor[0]*255), (int)(g_wmTitleColor[1]*255), (int)(g_wmTitleColor[2]*255), alpha);

    // text points at statics or locals of this function, all alive until the draw loop ends
    struct Item { const char* text; ImU32 color; float w; float adv; IDirect3DTexture9* icon; };
    Item items[k_wmMaxItems];
    int  nItems = 0;
    bool firstItem = true;

    const float iconDispSz = fontSize * 0.72f; // icon display size (~text height)

    auto push    = [&](const char* t, ImU32 col) {
        if (nItems < k_wmMaxItems) items[nItems++] = { t, col, 0.f, 0.f, nullptr };
    };
    auto pushIcon = [&](IDirect3DTexture9* tex, ImU32 col) {
        if (tex && nItems < k_wmMaxItems) items[nItems++] = { "", col, iconDispSz + 9.f, 0.f, tex }; // 3px lead + 6px trail
    };
    // sep: inserts separator text; sepI: inserts icon (or fallback text) before a field.
    // When icons are on, every field gets an icon unconditionally (no separator logic).
    auto sep  = [&]() { if (!firstItem) push(g_wmSeparator, dimCol); };
    auto sepI = [&](IDirect3DTexture9* iconTex) {
        if (g_wmUseIcons) pushIcon(iconTex, dimCol); // always, even for the first field
        else if (!firstItem) push(g_wmSeparator, dimCol);
    };

    if (g_drawWatermarkText) { push(GetTitleLabel(delta), titleCol); firstItem = false; }
    if (g_wmShowUsername) {
        const char* username = GetUsernameLabel();
        if (username[0]) { sepI(g_wmIconUserTex); push(username, dimCol); firstItem = false; }
    }
    if (g_drawTime)          { sepI(g_wmIconTimeTex); push(timebuf, textCol); firstItem = false; }

    char fpsBuf[16]{}, pingBuf[16]{}, lossBuf[16]{};
    const bool connected = g_conn.load();
    if (g_drawFPS)  { snprintf(fpsBuf,  16, "%d",   (int)g_fps.load());                            sepI(g_wmIconFpsTex);  push("fps ",  dimCol); push(fpsBuf,  valueColor(g_fps.load(),          (float)g_threshFPS,  true));  firstItem = false; }
    if (g_drawPing) { if (connected) snprintf(pingBuf, 16, "%d",   g_ping.load()); else strcpy(pingBuf, "--"); sepI(g_wmIconPingTex); push("ping ", dimCol); push(pingBuf, connected ? valueColor((float)g_ping.load(), (float)g_threshPing, false) : dimCol); firstItem = false; }
    if (g_drawLoss) { if (connected) snprintf(lossBuf, 16, "%d%%", g_loss.load()); else strcpy(lossBuf, "--"); sepI(g_wmIconLossTex); push("loss ", dimCol); push(lossBuf, connected ? valueColor((float)g_loss.load(), (float)g_threshLoss, false) : dimCol); firstItem = false; }

    if (nItems == 0) return;

    // Advance wm logo GIF animation
    if (g_wmLogoImgFrames.size() > 1) {
        g_wmLogoImgFrameTimer += delta;
        float fd = g_wmLogoImgDelays[g_wmLogoImgCurFrame];
        if (fd <= 0.f) fd = 0.1f;
        while (g_wmLogoImgFrameTimer >= fd) {
            g_wmLogoImgFrameTimer -= fd;
            g_wmLogoImgCurFrame = (g_wmLogoImgCurFrame + 1) % (int)g_wmLogoImgFrames.size();
            fd = g_wmLogoImgDelays[g_wmLogoImgCurFrame]; if (fd <= 0.f) fd = 0.1f;
        }
    }
    IDirect3DTexture9* wmLogoTex = g_wmLogoImgFrames.empty() ? nullptr
                                 : g_wmLogoImgFrames[g_wmLogoImgCurFrame];

    const bool  hasImgLogo = g_wmShowLogo && wmLogoTex != nullptr;
    const bool  hasDefLogo = g_wmShowLogo && g_logoTex;
    const float logoBaseH  = BOX_H * 0.70f;
    const float logoWmH    = hasImgLogo ? logoBaseH * g_wmLogoImgScale :
                             hasDefLogo ? logoBaseH : 0.f;
    const float logoImgAR  = (hasImgLogo && g_wmLogoImgH > 0)
                             ? (float)g_wmLogoImgW / (float)g_wmLogoImgH : 1.f;
    const float logoWmW    = logoWmH > 0.f
                             ? logoWmH * (hasImgLogo ? logoImgAR : kLogoAspect) : 0.f;
    const float logoWmOff  = logoWmH > 0.f ? (BOX_H - logoWmH) * 0.5f : 0.f;
    const float logoGap    = logoWmW > 0.f ? 4.f : 0.f;

    // For all-digit items: scan all 10 repeated-character strings of the same length
    // and return the maximum. This catches any per-string font metric variation that
    // per-character scanning misses, so "390" can never be wider than the stable value.
    auto isAllDigits = [](const char* s) -> bool {
        if (!*s) return false;
        for (const char* p = s; *p; ++p)
            if (*p < '0' || *p > '9') return false;
        return true;
    };
    auto stableDigits = [&](const char* s) -> float {
        int n = (int)strlen(s);
        if (n <= 0 || n >= 64) return 0.f;
        char tmp[64];
        float maxW = 0.f;
        for (char c = '0'; c <= '9'; ++c) {
            memset(tmp, c, n); tmp[n] = '\0';
            maxW = std::max(maxW, wFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, tmp).x);
        }
        return maxW;
    };

    float stableW = 0.f;
    for (int i = 0; i < nItems; ++i) {
        Item& it = items[i];
        if (!it.icon) // icon items have w pre-set to iconDispSz + gap; don't override
            it.w = wFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, it.text).x;
        it.adv   = (it.icon || !isAllDigits(it.text)) ? it.w : stableDigits(it.text);
        stableW += it.adv;
    }
    // Use digit-normalised width so box only resizes on digit-count changes.
    const float targetBoxW = logoWmW + logoGap + stableW + H_PAD * 2.0f - 2.f;

    // Smooth width -- critically-damped spring (no asymptotic stall, no overshoot)
    if (g_wmSmoothW < 1.f) { g_wmSmoothW = targetBoxW; g_wmSmoothWV = 0.f; } // first frame: snap
    SpringToward(g_wmSmoothW, g_wmSmoothWV, targetBoxW, delta);
    const float boxW = g_wmSmoothW;

    const float sw = io.DisplaySize.x;
    const float sh = io.DisplaySize.y;
    float boxX, boxY;

    const bool mpAvoid = g_enableMediaPlayer && g_mpAvoidWatermark
                         && g_mpLastBoxH > 0.f && g_mpLastBoxPos == g_watermarkPos;

    // Sum enabled MP outline layer thicknesses (scaled) for mpAvoid gap extension
    float mpOutlineExt = 0.f;
    if (mpAvoid) {
        const OutlineLayer* mpOL  = g_mpCopyWmStyle ? g_wmOutlineLayers : g_mpOutlineLayers;
        const int           mpOLN = g_mpCopyWmStyle ? g_wmOutlineLayerCount : g_mpOutlineLayerCount;
        const float         mpSc  = g_mpCopyWmStyle ? g_watermarkScale : g_mpScale;
        for (int _i = 0; _i < mpOLN; ++_i)
            if (mpOL[_i].enable) mpOutlineExt += mpOL[_i].thick * mpSc;
    }

    // mpAvoid: YOffset is always applied; mpAvoid only enforces a minimum gap
    // (clamp to just outside the MP box if YOffset would overlap it).
    switch (g_watermarkPos) {
    default:
    case WM_TOP_LEFT:
        boxX = PAD_EDGE + g_watermarkXOffset;
        { float yN = PAD_EDGE + g_watermarkYOffset*(sh*0.84f-BOX_H);
          float yA = g_mpLastBoxY + g_mpLastBoxH + mpOutlineExt + 4.f;
          boxY = mpAvoid ? std::max(yN, yA) : yN; }
        break;
    case WM_TOP_CENTER:
        boxX = (sw-boxW)*0.5f + g_watermarkXOffset;
        { float yN = PAD_EDGE + g_watermarkYOffset*(sh*0.84f-BOX_H);
          float yA = g_mpLastBoxY + g_mpLastBoxH + mpOutlineExt + 4.f;
          boxY = mpAvoid ? std::max(yN, yA) : yN; }
        break;
    case WM_TOP_RIGHT:
        boxX = sw-boxW-PAD_EDGE + g_watermarkXOffset;
        { float yN = PAD_EDGE + g_watermarkYOffset*(sh*0.84f-BOX_H);
          float yA = g_mpLastBoxY + g_mpLastBoxH + mpOutlineExt + 4.f;
          boxY = mpAvoid ? std::max(yN, yA) : yN; }
        break;
    case WM_BOTTOM_LEFT:
        boxX = PAD_EDGE + g_watermarkXOffset;
        { float yN = sh-BOX_H-PAD_EDGE-g_watermarkYOffset*(sh*0.84f-BOX_H);
          float yA = g_mpLastBoxY - mpOutlineExt - BOX_H - 4.f;
          boxY = mpAvoid ? std::min(yN, yA) : yN; }
        break;
    case WM_BOTTOM_CENTER:
        boxX = (sw-boxW)*0.5f + g_watermarkXOffset;
        { float yN = sh-BOX_H-PAD_EDGE-g_watermarkYOffset*(sh*0.84f-BOX_H);
          float yA = g_mpLastBoxY - mpOutlineExt - BOX_H - 4.f;
          boxY = mpAvoid ? std::min(yN, yA) : yN; }
        break;
    case WM_BOTTOM_RIGHT:
        boxX = sw-boxW-PAD_EDGE + g_watermarkXOffset;
        { float yN = sh-BOX_H-PAD_EDGE-g_watermarkYOffset*(sh*0.84f-BOX_H);
          float yA = g_mpLastBoxY - mpOutlineExt - BOX_H - 4.f;
          boxY = mpAvoid ? std::min(yN, yA) : yN; }
        break;
    }

    const float textY = boxY + (BOX_H - wFont->Ascent + wFont->Descent) * 0.5f;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    const ImDrawFlags wmCF = CornerFlagsToImDraw(g_wmBgCornerFlags);

    // Compute total outline extent for shadow bounding
    float wmTotalOutline = 0.f;
    for (int _i = 0; _i < g_wmOutlineLayerCount; ++_i)
        if (g_wmOutlineLayers[_i].enable) wmTotalOutline += g_wmOutlineLayers[_i].thick * sc;

    const float bgX0 = boxX      + g_wmBgInsetL * sc;
    const float bgY0 = boxY      + g_wmBgInsetT * sc;
    const float bgX1 = boxX+boxW - g_wmBgInsetR * sc;
    const float bgY1 = boxY+BOX_H - g_wmBgInsetB * sc;
    const float bgW  = std::max(0.f, bgX1 - bgX0);
    const float bgH  = std::max(0.f, bgY1 - bgY0);

    // ---- Box shadow -- drawn before everything else so it's behind ----
    if (g_wmBoxShadow && g_drawWatermarkBg) {
        float sAlpha = alpha / 255.f;
        DrawBoxShadow(dl,
            bgX0 - wmTotalOutline, bgY0 - wmTotalOutline,
            bgX1 + wmTotalOutline, bgY1 + wmTotalOutline,
            g_wmBoxShadowOffX * sc, g_wmBoxShadowOffY * sc,
            g_wmBoxShadowBlur * sc,
            g_wmBoxShadowColor, sAlpha, ROUNDING + wmTotalOutline, wmCF);
    }

    // ---- Kill glow -- smooth fade-in/out, drawn just outside the outline stack ----
    {
        // Fade in fast when rising, decay slowly after
        if (g_wmKillGlowRising) {
            g_wmKillGlowAlpha += delta * 8.0f;
            if (g_wmKillGlowAlpha >= 1.0f) { g_wmKillGlowAlpha = 1.0f; g_wmKillGlowRising = false; }
        } else if (g_wmKillGlowAlpha > 0.0f) {
            float decayRate = (g_wmKillGlowDuration > 0.0f) ? (1.0f / g_wmKillGlowDuration) : 2.0f;
            g_wmKillGlowAlpha = std::max(0.0f, g_wmKillGlowAlpha - delta * decayRate);
        }
    }
    if (g_wmKillGlowEnable && g_drawWatermarkBg && g_wmKillGlowAlpha > 0.01f)
    {
        const float olOffset = wmTotalOutline; // start just outside the outline stack

        // Stroked rings drawn outside the box -- never fills the interior so
        // semi-transparent backgrounds don't let glow bleed through.
        // Drawn outermost-first so inner (brighter) rings paint on top.
        const float eff    = g_wmKillGlowAlpha * g_watermarkFade;
        const int   kN     = 10;
        const float maxPx  = std::max(1.0f, g_wmKillGlowSpread) * sc;
        const float stepPx = maxPx / (float)kN;
        const int   R      = (int)(g_wmKillGlowColor[0] * 255);
        const int   G      = (int)(g_wmKillGlowColor[1] * 255);
        const int   B      = (int)(g_wmKillGlowColor[2] * 255);
        const float colA   = g_wmKillGlowColor[3];
        for (int i = kN - 1; i >= 0; --i)
        {
            float expand  = olOffset + (i + 0.5f) * stepPx;
            float t       = (float)i / (float)(kN - 1);  // 0=innermost, 1=outermost
            int   ai      = (int)((1.0f - t) * eff * colA * 200.0f);
            if (ai <= 0) continue;
            dl->AddRect(
                ImVec2(boxX - expand, boxY - expand),
                ImVec2(boxX + boxW + expand, boxY + BOX_H + expand),
                IM_COL32(R, G, B, ai),
                ROUNDING + expand, wmCF,
                stepPx + 1.0f);  // slight overlap between rings for smooth gradient
        }
    }

    if (g_drawWatermarkBg && g_bgBlur && g_d3dDev)
        DrawBgBlurRect(dl, bgX0, bgY0, bgX1, bgY1, ROUNDING, wmCF);
    if (g_drawWatermarkBg) {
        ImU32 wmBgFill = IM_COL32((int)(g_wmBgColor[0]*255),(int)(g_wmBgColor[1]*255),
                                   (int)(g_wmBgColor[2]*255),(int)(g_wmBgColor[3]*alpha));
        if (g_wmBgGrad[0].enable || g_wmBgGrad[1].enable || g_wmBgGrad[2].enable || g_wmBgGrad[3].enable)
            DrawBgSideGradients(dl, bgX0, bgY0, bgX1, bgY1, g_wmBgGrad, wmBgFill, ROUNDING, wmCF);
        else
            dl->AddRectFilled(ImVec2(bgX0, bgY0), ImVec2(bgX1, bgY1),
                wmBgFill, ROUNDING, wmCF);
    }

    const float gradFade = ComputeBgFadeFactor(
        (bgX0 + bgX1) * 0.5f, (bgY0 + bgY1) * 0.5f,
        bgX0, bgY0, bgX1, bgY1, g_wmBgGrad);

    if (g_wmBlobEnable && g_drawWatermarkBg) {
        float blobCol[3];
        float blobAlpha;
        if (g_wmBlobUseSheen) {
            GetAnimatedSheenColor(blobCol);
            blobAlpha = g_wmBlobAlpha * (alpha / 255.f) * gradFade;
        } else {
            blobCol[0] = g_wmBlobColor[0];
            blobCol[1] = g_wmBlobColor[1];
            blobCol[2] = g_wmBlobColor[2];
            blobAlpha  = g_wmBlobAlpha * (alpha / 255.f) * gradFade;
        }
        DrawBlobGradient(dl, bgX0, bgY0, bgW, bgH,
                         blobCol, blobAlpha, g_wmBlobSize, g_wmBlobSpeed, 0.f, ROUNDING);
    }

    if (g_drawWatermarkBg)
        DrawOutlineLayers(dl, bgX0, bgY0, bgX1, bgY1,
            g_wmOutlineLayers, g_wmOutlineLayerCount, sc, ROUNDING, wmCF, (alpha / 255.f) * gradFade);

    if (g_wmPrecipEnable && g_drawWatermarkBg) {
        float pc[4] = { g_wmPrecipColor[0], g_wmPrecipColor[1],
                        g_wmPrecipColor[2], g_wmPrecipColor[3] * (alpha / 255.f) * gradFade };
        DrawPrecipitation(dl, bgX0, bgY0, bgW, bgH,
            g_wmPrecipMode, g_wmPrecipCount, g_wmPrecipSpeed, g_wmPrecipSize,
            g_wmPrecipWind, pc, delta, 0);
    }

    // Box is sized by stableW; item positions use actual widths so there are no gaps.
    // All-digit items use stable advance so neighbouring items don't drift on value changes.
    const float contentStartX = boxX + H_PAD + logoWmW + logoGap;
    {
        float accum = 0.f;
        const int N = nItems;
        for (int i = 0; i < N; ++i) {
            const float targetX = contentStartX + accum;
            if (g_wmSmoothItemX[i] < 1.f) { g_wmSmoothItemX[i] = targetX; g_wmSmoothItemV[i] = 0.f; } // snap
            SpringToward(g_wmSmoothItemX[i], g_wmSmoothItemV[i], targetX, delta);
            accum += items[i].adv;
        }
        // Reset slots beyond current count so they snap on next use
        for (int i = N; i < k_wmMaxItems; ++i) { g_wmSmoothItemX[i] = 0.f; g_wmSmoothItemV[i] = 0.f; }
    }

    if (logoWmW > 0.f)
    {
        const ImU32 titleTint = IM_COL32(
            (int)(g_wmTitleColor[0]*255),(int)(g_wmTitleColor[1]*255),
            (int)(g_wmTitleColor[2]*255), alpha);
        if (hasImgLogo) {
            ImU32 tint = g_wmLogoImgColorize ? titleTint : IM_COL32(255,255,255,alpha);
            DrawLogoImage(dl, wmLogoTex, boxX + H_PAD - 3.f, boxY + logoWmOff, logoWmW, logoWmH,
                          tint, g_wmLogoImgRotation);
        } else {
            DrawLogo(dl, boxX + H_PAD - 3.f, boxY + logoWmOff, logoWmW, logoWmH, titleTint);
        }
    }
    // Ghost logo -- left-anchored behind first text item, clipped at box boundary (clarity-style)
    if (g_wmLogoGhost && (g_logoTex || wmLogoTex))
    {
        const bool  ghostUseImg = (wmLogoTex != nullptr);
        const float ghostAR   = (ghostUseImg && g_wmLogoImgH > 0)
                                  ? (float)g_wmLogoImgW / (float)g_wmLogoImgH : kLogoAspect;
        const float ghostH    = BOX_H * 1.6f;
        const float ghostW    = ghostH * ghostAR;
        const float item0X    = g_wmSmoothItemX[0];  // left edge of first text item
        const float ghostX    = item0X - ghostW * 0.65f; // shifted left, majority hangs outside/clips
        const float ghostY    = boxY + (BOX_H - ghostH) * 0.5f;
        const int   ghostA    = (int)(alpha * g_wmLogoGhostAlpha);
        const ImU32 ghostTint = IM_COL32(
            (int)(g_wmTitleColor[0]*255),(int)(g_wmTitleColor[1]*255),
            (int)(g_wmTitleColor[2]*255), ghostA);
        dl->PushClipRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + BOX_H), true);
        if (ghostUseImg) {
            ImU32 tint = g_wmLogoImgColorize ? ghostTint : IM_COL32(255,255,255,ghostA);
            DrawLogoImage(dl, wmLogoTex, ghostX, ghostY, ghostW, ghostH, tint, g_wmLogoImgRotation);
        } else {
            DrawLogo(dl, ghostX, ghostY, ghostW, ghostH, ghostTint);
        }
        dl->PopClipRect();
    }

    const bool wmDropShadow = g_wmFontFlags[4];
    const bool wmOutline    = g_wmFontFlags[5];
    const ImU32 shadowCol   = IM_COL32(0, 0, 0, (int)(alpha * 0.55f));
    const ImU32 outlineCol  = IM_COL32(0, 0, 0, (int)(alpha));
    const float shadowOff   = 1.5f * sc;
    for (int i = 0; i < nItems; ++i) {
        const auto& it = items[i];
        const float curX = g_wmSmoothItemX[i];
        if (it.icon) {
            // Icon item -- draw SVG texture instead of text.
            // 3px lead-in so the icon sits visually centred between its neighbours.
            const float iSz = iconDispSz;
            const float iX  = curX + 4.3f;
            const float iY  = boxY + (BOX_H - iSz) * 0.5f;
            dl->AddImage((ImTextureID)it.icon,
                ImVec2(iX, iY), ImVec2(iX + iSz, iY + iSz),
                ImVec2(0,0), ImVec2(1,1), it.color);
            continue;
        }
        if (wmOutline) {
            dl->AddText(wFont, fontSize, ImVec2(curX - 1.f, textY),       outlineCol, it.text);
            dl->AddText(wFont, fontSize, ImVec2(curX + 1.f, textY),       outlineCol, it.text);
            dl->AddText(wFont, fontSize, ImVec2(curX,       textY - 1.f), outlineCol, it.text);
            dl->AddText(wFont, fontSize, ImVec2(curX,       textY + 1.f), outlineCol, it.text);
        }
        if (wmDropShadow)
            dl->AddText(wFont, fontSize, ImVec2(curX + shadowOff, textY + shadowOff), shadowCol, it.text);
        dl->AddText(wFont, fontSize, ImVec2(curX, textY), it.color, it.text);
    }

    g_wmLastBoxX    = boxX;
    g_wmLastBoxY2   = boxY;
    g_wmLastBoxW    = boxW;
    g_wmLastBoxH2   = BOX_H;
    g_wmLastBoxPos2 = g_watermarkPos;
}

// ---- Overlay image system ----------------------------------
static void Img_LoadSlotGIF(OverlayImageSlot& slot, IDirect3DDevice9* dev) {
    int W = 0, H = 0;
    if (LoadGifFrames(dev, slot.path, slot.frames, slot.frameDelays, W, H)) {
        slot.texW   = W;
        slot.texH   = H;
        slot.loaded = true;
    }
}

static void Img_LoadSlot(int idx) {
    auto& slot = g_imgSlots[idx];
    slot.ReleaseTextures();
    slot.needsLoad = false;
    if (!slot.path[0]) { DebugLog("Img_LoadSlot[%d]: empty path", idx); return; }
    if (!g_d3dDev)     { DebugLog("Img_LoadSlot[%d]: g_d3dDev null", idx); return; }

    DebugLog("Img_LoadSlot[%d]: loading '%s'", idx, slot.path);

    // Route GIF files through giflib for animated support
    const char* ext = strrchr(slot.path, '.');
    if (ext && _stricmp(ext, ".gif") == 0) {
        Img_LoadSlotGIF(slot, g_d3dDev);
        DebugLog("Img_LoadSlot[%d]: GIF result loaded=%d frames=%d", idx, (int)slot.loaded, (int)slot.frames.size());
        return;
    }

    // All other formats via D3DX (PNG, JPG, BMP, TGA, DDS)
    // TF2 uses IDirect3DDevice9Ex -- D3DPOOL_MANAGED is invalid on Ex devices; use D3DPOOL_DEFAULT.
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileExA(g_d3dDev,
        slot.path, D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2,
        1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
        D3DX_FILTER_LINEAR, D3DX_FILTER_LINEAR, 0, nullptr, nullptr, &tex);
    if (SUCCEEDED(hr) && tex) {
        D3DSURFACE_DESC desc{}; tex->GetLevelDesc(0, &desc);
        slot.texW = desc.Width; slot.texH = desc.Height;
        slot.frames.push_back(tex); slot.frameDelays.push_back(0.f);
        slot.loaded = true;
        DebugLog("Img_LoadSlot[%d]: D3DX ok %dx%d", idx, slot.texW, slot.texH);
    } else {
        DebugLog("Img_LoadSlot[%d]: D3DX failed hr=0x%08X tex=%p", idx, (unsigned)hr, (void*)tex);
    }
}

// ---- File picker -------------------------------------------------------------
// GetOpenFileNameA is modal: called from the menu it froze Present for as long as the dialog
// stayed open. It runs on its own thread instead, and FilePick_Service (h_Present) hands the
// path to its target, so closing the menu with the dialog up loses nothing. One dialog at a
// time: Browse while one is open brings that dialog back in front of the game. Eject does not
// wait for it; the DLL is never unloaded, so a late close only writes these statics.
enum { PICK_IDLE, PICK_OPEN, PICK_DONE };
static std::atomic<int>   s_pickState{PICK_IDLE};
static std::atomic<DWORD> s_pickThreadId{0};
static int                s_pickTarget = 0;   // set before the thread starts
static char               s_pickPath[MAX_PATH];

void FilePick_Start(int target)
{
    int idle = PICK_IDLE;
    if (!s_pickState.compare_exchange_strong(idle, PICK_OPEN)) {
        if (DWORD tid = s_pickThreadId.load())
            EnumThreadWindows(tid, [](HWND hwnd, LPARAM) -> BOOL {
                if (!IsWindowVisible(hwnd)) return TRUE;
                SetForegroundWindow(hwnd);
                return FALSE;
            }, 0);
        return;
    }
    s_pickTarget = target;
    try { std::thread([] {
        s_pickThreadId.store(GetCurrentThreadId());
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // shell dialog wants STA
        char buf[MAX_PATH] = {};
        OPENFILENAMEA ofn{}; ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga;*.dds\0All Files\0*.*\0";
        ofn.lpstrFile   = buf; ofn.nMaxFile = MAX_PATH;
        // NOCHANGEDIR: put the process CWD back afterwards (our VMT writers use GetGameDir,
        // but the game may rely on it). No owner window: owning the game window from this
        // thread would attach its input queue to the render thread's.
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        const bool ok = GetOpenFileNameA(&ofn) != 0;
        if (SUCCEEDED(co)) CoUninitialize();
        if (ok) memcpy(s_pickPath, buf, sizeof(s_pickPath));
        s_pickThreadId.store(0);
        s_pickState.store(ok ? PICK_DONE : PICK_IDLE);
    }).detach(); } catch (...) { s_pickState.store(PICK_IDLE); }   // no thread: never leave it stuck open
}

void FilePick_Service()
{
    if (s_pickState.load() != PICK_DONE) return;
    const int t = s_pickTarget;
    if (t >= 0 && t < k_imgMaxSlots) {
        strncpy_s(g_imgSlots[t].path, s_pickPath, _TRUNCATE);
        g_imgSlots[t].needsLoad = true;
    } else if (t == FILEPICK_LOGO) {
        strncpy_s(g_wmLogoImgPath, s_pickPath, _TRUNCATE);
        g_wmLogoImgNeedsLoad = true;
    } else if (t == FILEPICK_MEDIA_THUMB) {
        g_mpCustomThumbPath = s_pickPath;   // misc.cpp reloads on change, the menu re-syncs its buffer
    }
    s_pickState.store(PICK_IDLE);
}

void WmLogoImg_ReleaseFrames() {
    for (auto* t : g_wmLogoImgFrames) if (t) t->Release();
    g_wmLogoImgFrames.clear(); g_wmLogoImgDelays.clear();
    g_wmLogoImgCurFrame = 0; g_wmLogoImgFrameTimer = 0.f;
    g_wmLogoImgW = g_wmLogoImgH = 0;
}

static void WmLogoImg_LoadGIF(IDirect3DDevice9* dev) {
    if (LoadGifFrames(dev, g_wmLogoImgPath, g_wmLogoImgFrames, g_wmLogoImgDelays, g_wmLogoImgW, g_wmLogoImgH))
        DebugLog("WmLogoImg GIF: %dx%d %d frames", g_wmLogoImgW, g_wmLogoImgH, (int)g_wmLogoImgFrames.size());
}

static void WmLogoImg_Load(IDirect3DDevice9* dev) {
    WmLogoImg_ReleaseFrames();
    g_wmLogoImgNeedsLoad = false;
    if (!g_wmLogoImgPath[0] || !dev) return;

    const char* ext = strrchr(g_wmLogoImgPath, '.');
    if (ext && _stricmp(ext, ".gif") == 0) {
        WmLogoImg_LoadGIF(dev);
        return;
    }

    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileExA(dev,
        g_wmLogoImgPath, D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2,
        1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
        D3DX_FILTER_LINEAR, D3DX_FILTER_LINEAR, 0, nullptr, nullptr, &tex);
    if (SUCCEEDED(hr) && tex) {
        D3DSURFACE_DESC desc{};
        tex->GetLevelDesc(0, &desc);
        g_wmLogoImgFrames.push_back(tex);
        g_wmLogoImgDelays.push_back(0.f);
        g_wmLogoImgW = (int)desc.Width;
        g_wmLogoImgH = (int)desc.Height;
        DebugLog("WmLogoImg: loaded %dx%d '%s'", g_wmLogoImgW, g_wmLogoImgH, g_wmLogoImgPath);
    } else {
        DebugLog("WmLogoImg: failed hr=0x%08X '%s'", (unsigned)hr, g_wmLogoImgPath);
    }
}

// Helper: draw one image with rotation and scale.  idx is the slot index in g_imgSlots[].
static void Img_DrawOne(ImDrawList* dl, OverlayImageSlot& slot, int idx, float dt)
{
    if (!slot.enabled || !slot.loaded || slot.frames.empty()) return;

    // Advance GIF animation
    if (slot.frames.size() > 1) {
        slot.frameTimer += dt;
        float fd = slot.frameDelays[slot.curFrame];
        if (fd <= 0.f) fd = 0.1f;
        while (slot.frameTimer >= fd) {
            slot.frameTimer -= fd;
            slot.curFrame = (slot.curFrame + 1) % (int)slot.frames.size();
            fd = slot.frameDelays[slot.curFrame]; if (fd<=0.f) fd=0.1f;
        }
    }

    IDirect3DTexture9* tex = slot.frames[slot.curFrame];
    if (!tex) return;

    float dw = slot.texW * slot.scale;
    float dh = slot.texH * slot.scale;

    float cx, cy;
    if (slot.anchor == 1 && g_wmLastBoxH2 > 0.f) {
        // Anchor next to watermark: right edge (or left for left-side WM)
        bool wmOnRight = (g_wmLastBoxPos2 == 2 || g_wmLastBoxPos2 == 5);
        if (wmOnRight) {
            cx = g_wmLastBoxX - dw * 0.5f - slot.anchorOffX;
        } else {
            cx = g_wmLastBoxX + g_wmLastBoxW + dw * 0.5f + slot.anchorOffX;
        }
        cy = g_wmLastBoxY2 + g_wmLastBoxH2 * 0.5f + slot.anchorOffY;
    } else {
        // Free position -- support click-drag to reposition when menu is closed
        if (!g_showMenu) {
            ImGuiIO& io2 = ImGui::GetIO();
            float mx = io2.MousePos.x, my = io2.MousePos.y;
            bool hovering = (mx >= slot.posX && mx <= slot.posX + dw &&
                             my >= slot.posY && my <= slot.posY + dh);
            if (g_imgDragSlot == idx) {
                if (io2.MouseDown[0]) {
                    slot.posX += io2.MouseDelta.x;
                    slot.posY += io2.MouseDelta.y;
                } else {
                    g_imgDragSlot = -1;
                }
            } else if (hovering && io2.MouseClicked[0] && g_imgDragSlot == -1) {
                g_imgDragSlot = idx;
            }
        }
        cx = slot.posX + dw * 0.5f;
        cy = slot.posY + dh * 0.5f;
    }

    float angle = slot.rotation;
    float cosA = cosf(angle), sinA = sinf(angle);
    float hw = dw * 0.5f, hh = dh * 0.5f;
    ImVec2 p[4] = {
        { cx + cosA*(-hw) - sinA*(-hh), cy + sinA*(-hw) + cosA*(-hh) },
        { cx + cosA*( hw) - sinA*(-hh), cy + sinA*( hw) + cosA*(-hh) },
        { cx + cosA*( hw) - sinA*( hh), cy + sinA*( hw) + cosA*( hh) },
        { cx + cosA*(-hw) - sinA*( hh), cy + sinA*(-hw) + cosA*( hh) },
    };
    ImU32 tint = IM_COL32(255,255,255,(int)(slot.alpha * 255.f));
    if (slot.pixelArt)
        dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            g_d3dDev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            g_d3dDev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        }, nullptr);
    dl->AddImageQuad((ImTextureID)(uintptr_t)tex,
        p[0], p[1], p[2], p[3],
        ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1), tint);
    if (slot.pixelArt)
        dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            g_d3dDev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            g_d3dDev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        }, nullptr);
}

void RenderOverlayImages()
{
    // Process pending loads unconditionally -- the enable flag must not gate loading
    // or the user sees "Not loaded" even after picking a file.
    for (int i = 0; i < k_imgMaxSlots; ++i)
        if (g_imgSlots[i].needsLoad) Img_LoadSlot(i);
    if (g_wmLogoImgNeedsLoad && g_d3dDev) WmLogoImg_Load(g_d3dDev);

    if (!g_enableImageOverlay) return;
    float dt = std::min(AnimDt(), 0.1f);   // GIF frames in game time (animclock.h)

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for (int i = 0; i < k_imgMaxSlots; ++i)
        Img_DrawOne(dl, g_imgSlots[i], i, dt);
}

void EnumerateFonts()
{
    if (!g_fontList.empty()) return;

    WIN32_FIND_DATAA fd{};

    char userFontsPath[MAX_PATH];
    ExpandEnvironmentStringsA(
        "%LOCALAPPDATA%\\Microsoft\\Windows\\Fonts\\*.ttf",
        userFontsPath, MAX_PATH);
    char userFontsPathOtf[MAX_PATH];
    ExpandEnvironmentStringsA(
        "%LOCALAPPDATA%\\Microsoft\\Windows\\Fonts\\*.otf",
        userFontsPathOtf, MAX_PATH);
    char userFontsPathTtc[MAX_PATH];
    ExpandEnvironmentStringsA(
        "%LOCALAPPDATA%\\Microsoft\\Windows\\Fonts\\*.ttc",
        userFontsPathTtc, MAX_PATH);

    const char* patterns[] = {
        "C:\\Windows\\Fonts\\*.ttf",
        "C:\\Windows\\Fonts\\*.otf",
        "C:\\Windows\\Fonts\\*.ttc",
        userFontsPath,
        userFontsPathOtf,
        userFontsPathTtc,
        nullptr
    };

    for (int pi = 0; patterns[pi]; ++pi)
    {
        HANDLE h = FindFirstFileA(patterns[pi], &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::string filename = fd.cFileName;
            std::string pattern = patterns[pi];
            size_t star = pattern.find('*');
            std::string directory = pattern.substr(0, star);

            std::string fullPath = directory + filename;

            std::string display = filename;
            size_t dot = display.rfind('.');
            if (dot != std::string::npos) display = display.substr(0, dot);
            if (!display.empty()) display[0] = (char)toupper((unsigned char)display[0]);

            g_fontList.push_back({display, fullPath});
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    std::sort(g_fontList.begin(), g_fontList.end(),
        [](const FontEntry& a, const FontEntry& b){

            std::string la = a.displayName, lb = b.displayName;
            for (auto& c : la) c = (char)tolower((unsigned char)c);
            for (auto& c : lb) c = (char)tolower((unsigned char)c);
            return la < lb;
        });
}

// g_fontList index of the font whose file name (no directory) is `file`, -1 if none.
int FontIndexForFile(const std::string& file)
{
    EnumerateFonts();
    if (file.empty()) return -1;
    for (int i = 0; i < (int)g_fontList.size(); ++i)
    {
        const std::string& full = g_fontList[i].fullPath;
        const size_t slash = full.rfind('\\');
        const char* fn = full.c_str() + (slash != std::string::npos ? slash + 1 : 0);
        if (_stricmp(fn, file.c_str()) == 0) return i;
    }
    return -1;
}

void ResolveFontIndex() { g_wmFontIndex = FontIndexForFile(g_wmFontFile); }
