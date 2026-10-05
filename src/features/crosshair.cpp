#include "crosshair.h"
#include "../imgui/imgui_internal.h"
#include "ch_blend_modes.h"
#include "fx_chblend_vs.h"   // k_fxChBlendVsBytes -- fx_chblend_build.sh
#include "fx_chblend_ps.h"   // k_fxChBlendPs[]    -- one ps_3_0 per blend mode
#include <vector>

#define SCSGO_COND_OFF g_offPlayerCond
#define SCSGO_OWN_PAINTTRAVERSE   // no vgui PaintTraverse hook in this project
#include "scope_csgo.inl"    // ScopeCsgo_* -- csgo sniper scope

static constexpr float kChUnitToPx = 2.0f;

struct ChArm { float x1, y1, x2, y2; };

static void ChDrawRect(ImDrawList* dl, float x1, float y1, float x2, float y2, ImU32 col);
static void ChDrawOutline(ImDrawList* dl, float ax1, float ay1, float ax2, float ay2,
    float otUnits, ImU32 col,
    bool sideL = true, bool sideR = true, bool sideT = true, bool sideB = true);
static void ChDrawOutlineBlur(ImDrawList* dl, float ax1, float ay1, float ax2, float ay2,
    float otUnits, ImU32 col, float blurRadius,
    bool sideL = true, bool sideR = true, bool sideT = true, bool sideB = true);

static ChArm ChMakeArm(float cx, float cy, float thPx, float len, float gap, int dir)
{
    float lo = g_chAntialias ? (thPx * 0.5f) : floorf(thPx * 0.5f);
    float hi = g_chAntialias ? (thPx * 0.5f) : (thPx - lo);
    ChArm a{};
    switch (dir) {
        case 0: a = { cx-gap-len, cy-lo, cx-gap,      cy+hi }; break;
        case 1: a = { cx+gap,     cy-lo, cx+gap+len,  cy+hi }; break;
        case 2: a = { cx-lo, cy+gap,     cx+hi, cy+gap+len  }; break;
        case 3: a = { cx-lo, cy-gap-len, cx+hi, cy-gap      }; break;
    }
    return a;
}

static void ChDrawArmFancy(ImDrawList* dl, const ChArm& a, int dir,
    ImU32 centerCol, ImU32 outerCol,
    float gradStart, float gradEnd,
    float nearScale, float farScale);

static ImU32 ChLerpColor(ImU32 c0, ImU32 c1, float t)
{
    if (t <= 0.f) return c0;
    if (t >= 1.f) return c1;
    auto l = [](int a, int b, float t) { return (int)(a + (b - a) * t + 0.5f); };
    return IM_COL32(
        l((c0 >>  0) & 0xFF, (c1 >>  0) & 0xFF, t),
        l((c0 >>  8) & 0xFF, (c1 >>  8) & 0xFF, t),
        l((c0 >> 16) & 0xFF, (c1 >> 16) & 0xFF, t),
        l((c0 >> 24) & 0xFF, (c1 >> 24) & 0xFF, t)
    );
}

// Filled quad (p1 top-left, p2 top-right, p3 bottom-right, p4 bottom-left) with per-vertex colors.
static void ChDrawQuadGrad(ImDrawList* dl,
    ImVec2 p1, ImVec2 p2, ImVec2 p3, ImVec2 p4,
    ImU32 c1, ImU32 c2, ImU32 c3, ImU32 c4)
{
    const ImVec2 uv = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    dl->PrimReserve(6, 4);
    ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteIdx(base);   dl->PrimWriteIdx(base+1); dl->PrimWriteIdx(base+2);
    dl->PrimWriteIdx(base);   dl->PrimWriteIdx(base+2); dl->PrimWriteIdx(base+3);
    dl->PrimWriteVtx(p1, uv, c1);
    dl->PrimWriteVtx(p2, uv, c2);
    dl->PrimWriteVtx(p3, uv, c3);
    dl->PrimWriteVtx(p4, uv, c4);
}

// Scale the alpha channel of an ImU32 color by [0..1].
static inline ImU32 ChScaleAlpha(ImU32 col, float t)
{
    if (t <= 0.f) return col & 0x00FFFFFFu;
    if (t >= 1.f) return col;
    int a = (int)(((col >> 24) & 0xFF) * t + 0.5f);
    return (col & 0x00FFFFFFu) | ((ImU32)a << 24);
}

// Draw one arm with gradient and/or taper.
// Renders as 1px-wide axial slices so ChDrawRect handles transverse AA correctly at every
// point along the arm — including diagonally-tapered edges which parallelogram fringes cannot.
// dir: 0=L, 1=R, 2=B(down), 3=T(up).
// nearScale = half-thickness multiplier at the gap end; farScale at the tip.
void ChDrawArmFancy(ImDrawList* dl, const ChArm& a, int dir,
    ImU32 centerCol, ImU32 outerCol,
    float gradStart, float gradEnd,
    float nearScale, float farScale)
{
    gradStart = ImClamp(gradStart, 0.f, 1.f);
    gradEnd   = ImClamp(gradEnd, gradStart, 1.f);

    const float baseHalf   = (dir <= 1) ? (a.y2 - a.y1) * 0.5f : (a.x2 - a.x1) * 0.5f;
    const float innerHalf  = baseHalf * nearScale;
    const float outerHalf  = baseHalf * farScale;
    const float centerLine = (dir <= 1) ? (a.y1 + a.y2) * 0.5f : (a.x1 + a.x2) * 0.5f;
    const float gRange     = gradEnd - gradStart;
    const bool  hz         = (dir <= 1);

    // Axial coordinate at t=0 (near/gap side) and t=1 (far/tip side)
    float axStart, axEnd;
    switch (dir) {
    case 0:  axStart = a.x2; axEnd = a.x1; break;
    case 1:  axStart = a.x1; axEnd = a.x2; break;
    case 2:  axStart = a.y1; axEnd = a.y2; break;
    default: axStart = a.y2; axEnd = a.y1; break;
    }

    const float xMin   = fminf(axStart, axEnd);
    const float xMax   = fmaxf(axStart, axEnd);
    const float axSpan = axEnd - axStart;  // signed; negative for dir 0 and 3

    for (float x = floorf(xMin); x < xMax - 1e-4f; x += 1.f) {
        const float rx1  = fmaxf(x, xMin);
        const float rx2  = fminf(x + 1.f, xMax);
        const float xMid = (rx1 + rx2) * 0.5f;

        // Normalised position along the arm at this slice's midpoint
        const float t   = (fabsf(axSpan) > 1e-5f) ? ImClamp((xMid - axStart) / axSpan, 0.f, 1.f) : 0.5f;
        const float h   = innerHalf + t * (outerHalf - innerHalf);
        const float g   = (gRange > 0.001f) ? ImClamp((t - gradStart) / gRange, 0.f, 1.f)
                                             : (t >= gradStart ? 1.f : 0.f);
        const ImU32 col = ChLerpColor(centerCol, outerCol, g);

        if (hz) ChDrawRect(dl, rx1, centerLine - h, rx2, centerLine + h, col);
        else    ChDrawRect(dl, centerLine - h, rx1, centerLine + h, rx2, col);
    }
}

// Draw a rect with sub-pixel AA fringe on all 4 edges.
// x1/y1/x2/y2 are float (sub-pixel) coordinates.
// Core pixels are drawn at full alpha; the 1px fringe strip on each edge is
// drawn at alpha * fractional_coverage, simulating smooth partial coverage.
void ChDrawRect(ImDrawList* dl, float x1, float y1, float x2, float y2, ImU32 col)
{
    if (!g_chAntialias) {
        dl->AddRectFilled(ImVec2(floorf(x1), floorf(y1)), ImVec2(ceilf(x2), ceilf(y2)), col);
        return;
    }

    // Integer pixel extents of the solid core (inset by fractional part on each edge).
    float cx1 = ceilf(x1),  cy1 = ceilf(y1);
    float cx2 = floorf(x2), cy2 = floorf(y2);

    // Fractional coverage on each edge (0 = fully inside a pixel, 1 = full pixel covered).
    float fL = cx1 - x1;  // left fringe  coverage [0,1)
    float fR = x2 - cx2;  // right fringe coverage [0,1)
    float fT = cy1 - y1;  // top fringe   coverage [0,1)
    float fB = y2 - cy2;  // bottom fringe coverage [0,1)

    // Solid core
    if (cx1 < cx2 && cy1 < cy2)
        dl->AddRectFilled(ImVec2(cx1, cy1), ImVec2(cx2, cy2), col);

    // Edge fringe strips (1px wide, scaled alpha)
    if (fL > 0.001f) dl->AddRectFilled(ImVec2(cx1-1.f, cy1), ImVec2(cx1, cy2),     ChScaleAlpha(col, fL));
    if (fR > 0.001f) dl->AddRectFilled(ImVec2(cx2,     cy1), ImVec2(cx2+1.f, cy2), ChScaleAlpha(col, fR));
    if (fT > 0.001f) dl->AddRectFilled(ImVec2(cx1, cy1-1.f), ImVec2(cx2, cy1),     ChScaleAlpha(col, fT));
    if (fB > 0.001f) dl->AddRectFilled(ImVec2(cx1, cy2),     ImVec2(cx2, cy2+1.f), ChScaleAlpha(col, fB));

    // Corner fringe pixels (product of two edge coverages)
    if (fL > 0.001f && fT > 0.001f) dl->AddRectFilled(ImVec2(cx1-1.f, cy1-1.f), ImVec2(cx1, cy1), ChScaleAlpha(col, fL*fT));
    if (fR > 0.001f && fT > 0.001f) dl->AddRectFilled(ImVec2(cx2,     cy1-1.f), ImVec2(cx2+1.f, cy1), ChScaleAlpha(col, fR*fT));
    if (fL > 0.001f && fB > 0.001f) dl->AddRectFilled(ImVec2(cx1-1.f, cy2),     ImVec2(cx1, cy2+1.f), ChScaleAlpha(col, fL*fB));
    if (fR > 0.001f && fB > 0.001f) dl->AddRectFilled(ImVec2(cx2,     cy2),     ImVec2(cx2+1.f, cy2+1.f), ChScaleAlpha(col, fR*fB));
}

// Draw the outline rect for one arm/dot.
// otUnits < 1.0 -> 1px on all (enabled) sides.
// otUnits >= 1.0 -> floor(otUnits * kChUnitToPx) px on all (enabled) sides.
// sideL/R/T/B control which sides are drawn.
void ChDrawOutline(ImDrawList* dl,
    float ax1, float ay1, float ax2, float ay2,
    float otUnits, ImU32 col,
    bool sideL, bool sideR, bool sideT, bool sideB)
{
    const float px = g_chAntialias ? (otUnits * kChUnitToPx) : ((otUnits < 1.0f) ? 1.0f : floorf(otUnits * kChUnitToPx));

    float expL = sideL ? px : 0.f;
    float expR = sideR ? px : 0.f;
    float expT = sideT ? px : 0.f;
    float expB = sideB ? px : 0.f;

    ChDrawRect(dl, ax1 - expL, ay1 - expT, ax2 + expR, ay2 + expB, col);
}

// Multi-pass Gaussian outline blur.
// Per-layer alpha is computed via the OVER-composite inversion formula so the
// cumulative alpha at distance r from the outline edge equals baseA·gauss(r)
// exactly. Naive stacking of filled rects accumulates alpha like a CDF (boxy);
// this inversion corrects each layer's contribution to cancel that artifact.
void ChDrawOutlineBlur(ImDrawList* dl,
    float ax1, float ay1, float ax2, float ay2,
    float otUnits, ImU32 col, float blurRadius,
    bool sideL, bool sideR, bool sideT, bool sideB)
{
    const float baseA     = (float)((col >> 24) & 0xFF) / 255.f;
    const ImU32 rgb       = col & 0x00FFFFFFu;
    const float maxExpand = blurRadius * kChUnitToPx;
    const float sigma     = maxExpand * 0.4f + 0.5f;
    const int   N         = 16;
    const float baseThick = g_chAntialias
        ? (otUnits * kChUnitToPx)
        : ((otUnits < 1.0f) ? 1.0f : floorf(otUnits * kChUnitToPx));

    // cumTransp = product of (1 - layerAlpha) for all already-drawn layers.
    // Invariant after processing layer i: 1 - cumTransp == baseA * gauss(r_i).
    const bool allSides = sideL && sideR && sideT && sideB;
    float cumTransp = 1.f;
    for (int i = N - 1; i >= 0; --i) {
        const float br     = ((float)(i + 1) / N) * maxExpand;
        const float target = baseA * expf(-0.5f * (br / sigma) * (br / sigma));
        if (cumTransp < 1e-4f) break;
        const float layerA = 1.f - (1.f - target) / cumTransp;
        if (layerA <= 0.f) continue;
        const int   ai     = (int)(layerA * 255.f + 0.5f);
        if (ai <= 0) continue;
        cumTransp *= (1.f - layerA);
        const float px = baseThick + br;
        float eL = sideL ? px : 0.f, eR = sideR ? px : 0.f;
        float eT = sideT ? px : 0.f, eB = sideB ? px : 0.f;
        if (allSides)
            dl->AddRectFilled(ImVec2(ax1 - px, ay1 - px), ImVec2(ax2 + px, ay2 + px),
                              rgb | ((ImU32)ai << 24), px);
        else
            ChDrawRect(dl, ax1 - eL, ay1 - eT, ax2 + eR, ay2 + eB, rgb | ((ImU32)ai << 24));
    }
    // solid core outline on top
    ChDrawOutline(dl, ax1, ay1, ax2, ay2, otUnits, col, sideL, sideR, sideT, sideB);
}

// ==== CSGO default crosshair (styles 0 / 1) ==========================================
// cl_crosshairstyle 0/1 is NOT the vgui bar crosshair -- CHudWeapon::ShouldDraw returns
// false below style 2 (hud_weapon.cpp:73). It is a panorama panel tree,
// panorama/layout/hud/crosshair.xml + styles/crosshair.css (both live in
// csgo/panorama/code.pbin), positioned by CCSGO_HudReticle::UpdateCrosshairUI
// (csgo_hudreticle.cpp:894-1010). Five parts, in xml document order:
//
//   green pip x4  crosshairpip2.png  5x12   additive, wash = crosshairColor1
//   arc x4        bare 9x1 / 1x9 panel     additive, background = crosshairColor1
//   black pip x4  reticlepip.png     1x6    normal,   wash = #000000AA
//   black circle  reticlecircle.png  8x8 -> 7x7 stretch, normal, wash = #00000088
//   dot           1x1 panel, additive, background-color = crosshairColor1
//
// The dot's own <Image> adds nothing: it is reticlecircle.png (rgb 0,0,0) under
// wash-color-fast, and wash-color-fast is a plain multiply into the quad colour
// (source2surface.cpp:2091 m_multColor), so black art x any wash = black, and black
// added to the frame is a no-op. All the dot's green is the panel's background-color,
// which is opaque -- it is a hard, full-strength pixel, not the 96/255 the art averages to.
//
// The Crosshair panel runs SetUIScale(1/windowScale), so every size above is in real
// screen pixels at any resolution; only the gap/spread offsets are res-scaled.
// The panel itself carries transform: translate3d(0.5px, 0.5px, 0), which is what puts
// the odd-width parts on whole pixels.
static constexpr float kCsPipOffset      = 7.0f;   // @define cppPipDefaultOffset
static constexpr float kCsBlackPipOffset = 2.0f;   // @define cppBlackPipDefaultOffset
static constexpr float kCsArcOffset      = 4.0f;   // @define cppArcDefaultOffset
static constexpr float kCsArcSize        = 9.0f;   // @define arcSize
static constexpr float kCsFixedGap       = 3.0f;   // cl_fixedcrosshairgap default

// The art of materials/panorama/images/hud/reticle/, as it lands on screen -- NOT as it
// sits in the png. The .crosshair panel is shifted by translate3d(0.5px, 0.5px, 0), so a
// part whose panel is an ODD number of pixels along an axis lands on whole pixels, and a
// part whose panel is EVEN lands on a half pixel and the bilinear sampler splits every
// texel across two screen pixels. The pips are 12 and 6 long -- even -- so csgo's own
// crosshair shows them box-filtered down their length: one extra faded pixel at each end
// and a softer ramp into the tip. That IS the taper. Baking it in here (12 -> 13 rows,
// 6 -> 7) means point-sampling at scale > 1 blows up what csgo actually puts on screen
// instead of the raw art. A symmetric 2-tap filter commutes with the 180 degree flip, so
// one baked sprite still serves all four arms.
// RGB is kept as shipped (the faint texels are not pure white) and the sampler decodes it
// as sRGB, like TEXTURE_BINDFLAGS_SRGBREAD does in panorama_cshader.cpp:162.
static const unsigned char kCsPip[13][5][4] = {    // crosshairpip2.png, 5 x 12 -> 5 x 13
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255, 23}, {255,255,255,128}, {255,255,255, 23}, {255,255,255,  0} },
    { {255,255,255,  0}, {247,247,247, 42}, {255,255,255,255}, {247,247,247, 42}, {255,255,255,  0} },
    { {255,255,255,  0}, {231,231,231, 34}, {255,255,255,255}, {231,231,231, 34}, {255,255,255,  0} },
    { {255,255,255,  0}, {240,240,240, 14}, {255,255,255,255}, {240,240,240, 14}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,255}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,255}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {247,247,247,206}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {232,232,232,127}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {241,241,241, 48}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0} },
    { {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0}, {255,255,255,  0} },
};
static const unsigned char kCsBlackPip[7][1][4] = {   // reticlepip.png, 1 x 6 -> 1 x 7
    { {255,255,255,117} },
    { {255,255,255,213} },
    { {255,255,255,170} },
    { {255,255,255,128} },
    { {255,255,255, 86} },
    { {255,255,255, 42} },
    { {255,255,255, 10} },
};
// reticlecircle.png is 8 x 8 in a 7 x 7 panel with scaling="stretch": odd, so it lands on
// whole pixels, and the 8 -> 7 squeeze is the sampler's, baked here for the same reason.
static const unsigned char kCsCircle[7][7][4] = {
    { {  0,  0,  0,  5}, {  0,  0,  0, 70}, {  0,  0,  0,185}, {  0,  0,  0,218}, {  0,  0,  0,184}, {  0,  0,  0, 70}, {  0,  0,  0,  5} },
    { {  0,  0,  0, 70}, {  0,  0,  0,200}, {  0,  0,  0, 97}, {  0,  0,  0, 48}, {  0,  0,  0, 98}, {  0,  0,  0,200}, {  0,  0,  0, 69} },
    { {  0,  0,  0,185}, {  0,  0,  0, 98}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,100}, {  0,  0,  0,184} },
    { {  0,  0,  0,218}, {  0,  0,  0, 50}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0, 50}, {  0,  0,  0,218} },
    { {  0,  0,  0,185}, {  0,  0,  0,100}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,  0}, {  0,  0,  0,101}, {  0,  0,  0,183} },
    { {  0,  0,  0, 70}, {  0,  0,  0,200}, {  0,  0,  0, 99}, {  0,  0,  0, 49}, {  0,  0,  0,100}, {  0,  0,  0,200}, {  0,  0,  0, 68} },
    { {  0,  0,  0,  5}, {  0,  0,  0, 69}, {  0,  0,  0,184}, {  0,  0,  0,217}, {  0,  0,  0,184}, {  0,  0,  0, 69}, {  0,  0,  0,  5} },
};
static constexpr float kCsPipOpacity   = 0.99f;    // .crosshair__pip--green opacity
static constexpr float kCsBlackPipWash = 170.0f;   // wash-color-fast #000000AA
static constexpr float kCsCircleWash   = 136.0f;   // wash-color-fast #00000088

// AK-47 (weapon_ak47_prefab, items_game.txt:24892), the numbers CWeaponCSBase feeds
// GetReticleWeaponSpread. Schema values are scaled by 0.001 on load
// (cs_weapon_parse.cpp:1052-1060). TF2 weapons have no CS inaccuracy data, so the
// arcs ride these -- the movement STATE driving them is the local player's, live.
static constexpr float kCsInaccStand       = 0.00641f;
static constexpr float kCsInaccCrouch      = 0.00481f;
static constexpr float kCsInaccMove        = 0.17506f;
static constexpr float kCsInaccJump        = 0.14076f;
static constexpr float kCsInaccJumpInitial = 0.10094f;
static constexpr float kCsInaccLand        = 0.000242f;
static constexpr float kCsSpread           = 0.00060f;
static constexpr float kCsRecoveryStand    = 0.368f;
static constexpr float kCsRecoveryCrouch   = 0.305257f;
static constexpr float kCsMaxSpeedFallback = 215.0f;
static constexpr float kCsDuckSpeedMod     = 0.34f;      // CS_PLAYER_SPEED_DUCK_MODIFIER
static constexpr float kCsMoveCurveExp     = 0.25f;      // MOVEMENT_CURVE01_EXPONENT
static constexpr float kCsJumpImpulse      = 301.993377f;// sv_jump_impulse

struct CsPlayerState
{
    bool  valid    = false;
    bool  onGround = true;
    bool  ducking  = false;
    float speed2D  = 0.f;
    float velZ     = 0.f;
    float maxSpeed = kCsMaxSpeedFallback;
};

static constexpr float kCsXDistGoal = 4.0f;   // cl_crosshairgap_useweaponvalue 0

// Everything the csgo reticle eases from frame to frame. The in-game crosshair and the menu
// preview each own one, so the preview never drags the real reticle's animation around.
struct CsAnim
{
    float penalty   = 0.f;
    bool  wasGround = true;
    float lastVelZ  = 0.f;
    float gapSm     = 0.f;
    float arcSm     = 0.f;
    float dist      = kCsXDistGoal;
    float distSm    = 0.f;
    float cappedSm  = 0.f;
};
static CsAnim s_csAnimGame, s_csAnimPreview;

// Where one crosshair lands and what drives it: the screen centre and the local player in
// game, the preview box and the test speed in the menu.
struct ChTarget
{
    ImDrawList*   dl;
    float         cx, cy;   // centre, unsnapped
    float         scale;    // custom crosshair size factor (the preview shrinks it to fit)
    float         speed;    // horizontal speed for the custom dynamic gap, < 0 = none
    CsPlayerState player;   // csgo movement input
    CsAnim*       anim;
};

static CsPlayerState CsGetPlayerState()
{
    CsPlayerState s;
    if (g_offFlags <= 0 || g_offVelocity <= 0) return s;
    const int idx = GetLocalPlayerIndex();
    if (idx <= 0) return s;
    void* pEnt = GetClientEntity(idx);
    if (!pEnt) return s;
    const uintptr_t base = reinterpret_cast<uintptr_t>(pEnt);
    if (base < 0x10000) return s;
    __try {
        const int flags = *reinterpret_cast<int*>(base + g_offFlags);
        const float* v  = reinterpret_cast<const float*>(base + g_offVelocity);
        s.onGround = (flags & (1 << 0)) != 0;   // FL_ONGROUND
        s.ducking  = (flags & (1 << 1)) != 0;   // FL_DUCKING
        s.speed2D  = sqrtf(v[0]*v[0] + v[1]*v[1]);
        s.velZ     = v[2];
        if (g_offMaxspeed > 0) {
            const float ms = *reinterpret_cast<const float*>(base + g_offMaxspeed);
            if (ms > 1.f) s.maxSpeed = ms;      // class run speed stands in for GetMaxSpeed
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return s; }
    s.valid = true;
    return s;
}

static float CsRemapClamped(float v, float a, float b, float ra, float rb)
{
    if (a == b) return v >= b ? rb : ra;
    float t = (v - a) / (b - a);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return ra + (rb - ra) * t;
}

// Exponential ease toward a new value. csgo snaps: the gap jumps between four fixed
// steps and the spread lands wherever the frame's inaccuracy puts it. tau > 0 spreads
// that jump over time instead -- pure display smoothing, nothing here feeds the game.
static float ChCsEase(float& cur, float target, float dt, float tau)
{
    if (tau <= 0.001f || dt <= 0.f) { cur = target; return cur; }
    float a = 1.f - expf(-dt / tau);
    if (a > 1.f) a = 1.f;
    cur += (target - cur) * a;
    return cur;
}

// CWeaponCSBase::UpdateAccuracyPenalty (:3810) + GetInaccuracy (:1258), minus the
// firing/reload/ladder/turning terms TF2 can't feed. Runs off frame time, not
// TICK_INTERVAL, since it is only ever a display value here.
// velSens scales every term that rides the player's motion -- the landing bank, the air
// penalty, the speed curve and the takeoff/apex ramp. 1 = csgo, 0 = a still reticle.
static float CsUpdateInaccuracy(const CsPlayerState& st, float dt, float velSens, CsAnim& an)
{
    float& s_penalty   = an.penalty;
    bool&  s_wasGround = an.wasGround;
    float& s_lastVelZ  = an.lastVelZ;

    // OnLand (:3941): the fall speed is banked into the penalty the frame we touch down.
    if (st.onGround && !s_wasGround)
        s_penalty += kCsInaccLand * fabsf(s_lastVelZ) * velSens;
    s_wasGround = st.onGround;
    s_lastVelZ  = st.velZ;

    float target = 0.f;
    if (!st.onGround)    target = kCsInaccStand + kCsInaccJump * velSens;  // weapon_air_spread_scale 1
    else if (st.ducking) target = kCsInaccCrouch;
    else                 target = kCsInaccStand;

    if (target > s_penalty)
    {
        s_penalty = target;                                     // penalties snap on
    }
    else
    {
        const float recovery = !st.onGround ? kCsRecoveryCrouch * 4.0f     // 400% air penalty
                             : st.ducking   ? kCsRecoveryCrouch
                                            : kCsRecoveryStand;
        const float decay = logf(10.0f) / recovery;
        const float t = expf(dt * -decay);                       // Lerp(t, target, penalty)
        s_penalty = target + (s_penalty - target) * t;
    }

    float acc = s_penalty;

    // Instantaneous movement term -- MOVEMENT_ACCURACY_DECAYED is 0, so this one does
    // NOT persist: it tracks speed frame by frame and vanishes the moment you stop.
    const float moveScale = CsRemapClamped(st.speed2D,
                                           st.maxSpeed * kCsDuckSpeedMod,
                                           st.maxSpeed * 0.95f, 0.f, 1.f);
    if (moveScale > 0.f)
        acc += powf(moveScale, kCsMoveCurveExp) * kCsInaccMove * velSens;

    // Airborne: worst at takeoff/landing speed, best at the apex, capped at 2x.
    if (!st.onGround)
    {
        const float sqMax = sqrtf(kCsJumpImpulse);
        float air = CsRemapClamped(sqrtf(fabsf(st.velZ)), sqMax * 0.25f, sqMax,
                                   0.f, kCsInaccJumpInitial);
        if (air > 2.0f * kCsInaccJumpInitial) air = 2.0f * kCsInaccJumpInitial;
        acc += air * velSens;
    }

    if (acc > 1.0f) acc = 1.0f;
    return acc;
}

// hudreticle paints pips, arcs and the dot with -s2-mix-blend-mode: SRGBadditive.
// SRCALPHA/ONE is that equation; ImGui_ImplDX9's own state is SRCALPHA/INVSRCALPHA.
// (panorama's own state is ONE/ONE over a premultiplied source -- same product, since
// ImGui's src is texel x vertex colour and its src alpha is texel a x vertex a.)
//
// Light: panorama composites in LINEAR light. panorama_cshader.cpp:53 turns on sRGB
// WRITE for the target, :162 binds the art SRGBREAD, and every css colour goes through
// SrgbGammaToLinear on the way to the vertex (source2surface.cpp:291). So the add and
// the black wash both happen on linear values and only the final store is re-encoded.
// Doing the same arithmetic straight on sRGB bytes -- which is what an ImGui overlay
// does by default -- crushes exactly the two things that read as wrong: a faint additive
// texel (the pip's taper, alpha 14..48) lands far dimmer than it should, and a black
// wash over a lit frame bites far harder. D3DRS_SRGBWRITEENABLE + linear vertex colours
// hands that back to the ROP. Hardware without D3DPMISCCAPS_POSTBLENDSRGBCONVERT
// converts before blending instead, which just returns the old sRGB-space look.
//
// Filter: at scale 1 the art lands texel-on-pixel and point == linear. Blown up, linear
// smears one texel over `scale` pixels and the taper turns to mush, while point keeps
// the pip's flare and the ring as hard shapes -- that is the crisp look of a low-res
// capture scaled up. ImGui's own draws need LINEAR back, so ChCsEnd restores it.
static void ChCsSetFilter(DWORD f)
{
    if (!g_d3dDev) return;
    g_d3dDev->SetSamplerState(0, D3DSAMP_MINFILTER, f);
    g_d3dDev->SetSamplerState(0, D3DSAMP_MAGFILTER, f);
}
static void ChCsSetState(DWORD destBlend)
{
    if (!g_d3dDev) return;
    g_d3dDev->SetRenderState(D3DRS_DESTBLEND, destBlend);
    g_d3dDev->SetRenderState(D3DRS_SRGBWRITEENABLE, g_chCsgoLinear ? TRUE : FALSE);
    g_d3dDev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, g_chCsgoLinear ? TRUE : FALSE);
    ChCsSetFilter(g_chCsgoCrisp ? D3DTEXF_POINT : D3DTEXF_LINEAR);
}
static void ChCsBlendAdd(const ImDrawList*, const ImDrawCmd*)
{
    ChCsSetState(D3DBLEND_ONE);
}
static void ChCsBlendNormal(const ImDrawList*, const ImDrawCmd*)
{
    ChCsSetState(D3DBLEND_INVSRCALPHA);
}
static void ChCsEnd(const ImDrawList*, const ImDrawCmd*)
{
    if (!g_d3dDev) return;
    g_d3dDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_d3dDev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    g_d3dDev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    ChCsSetFilter(D3DTEXF_LINEAR);
}

// Custom crosshair, additive. ImGui_ImplDX9 draws SRCALPHA/INVSRCALPHA; flipping only
// the dest factor to ONE makes each quad src*srcAlpha + dst, so alpha still controls
// how hot the add is. Off restores ImGui's own factor for everything drawn after.
static void ChBlendAddOn(const ImDrawList*, const ImDrawCmd*)
{
    if (g_d3dDev) g_d3dDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
}
static void ChBlendAddOff(const ImDrawList*, const ImDrawCmd*)
{
    if (g_d3dDev) g_d3dDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
}

// ==== blend modes 2+ (ch_blend_modes.h) =============================================
// Everything past normal/additive needs the frame UNDER the arms, and no fixed-function
// blend factor can read that. So ChBlendOn copies the render target into s_bmTex and binds
// that mode's ps_3_0 (fx_chblend_ps.hlsl -- Krita's formulas, checked against Krita's own
// code by tools/chblend_test), which reads the copy at VPOS. The shader returns the blended
// colour plus the crosshair's alpha, and ImGui's SRCALPHA/INVSRCALPHA does the opacity lerp.
// Blending is in the frame's sRGB-encoded values, the way Krita blends an 8-bit layer.
//
// The copy is taken after the outline went down and before the arms, so the outline stays a
// normal blend and is IN the frame the arms blend with.
//
// ONE COPY PER IMGUI FRAME PER RENDER TARGET. The same draw lists can be submitted several
// times in one frame (every backbuffer scene when vgui paints off-thread). A fresh copy each
// time would hold the previous pass's crosshair, and blending onto that again is not a no-op:
// difference twice IS the original frame, so the crosshair would blink with the scene count.
// Reusing the first copy makes every pass write the same pixels. The first backbuffer scene
// already holds the world (colour correction grades there for the same reason).
static IDirect3DDevice9*       s_bmDev     = nullptr;
static IDirect3DVertexShader9* s_bmVs      = nullptr;
static IDirect3DPixelShader9*  s_bmPs[kChBlendCount] = {};
static bool                    s_bmPsBad[kChBlendCount] = {};   // refused once: not retried per scene
static IDirect3DTexture9*      s_bmTex     = nullptr;
static IDirect3DSurface9*      s_bmSurf    = nullptr;
static UINT                    s_bmW = 0, s_bmH = 0;
static IDirect3DSurface9*      s_bmResolve = nullptr;           // msaa frame: resolve here, then copy
static D3DSURFACE_DESC         s_bmResolveDesc{};
static UINT                    s_bmFailW = 0, s_bmFailH = 0;    // size whose copy target was refused
static int                     s_bmCopyFrame = -1;              // ImGui frame + target the copy holds
static IDirect3DSurface9*      s_bmCopyRt    = nullptr;         // identity only, never dereferenced
static bool                    s_bmDead    = false;             // no shader model 3: stay normal
static bool                    s_bmLive    = false;             // ChBlendOn bound its state this draw

static void ChBlendReleaseTargets()
{
    if (s_bmResolve) { s_bmResolve->Release(); s_bmResolve = nullptr; }
    if (s_bmSurf)    { s_bmSurf->Release();    s_bmSurf    = nullptr; }
    if (s_bmTex)     { s_bmTex->Release();     s_bmTex     = nullptr; }
    s_bmW = s_bmH = 0;
    s_bmResolveDesc = D3DSURFACE_DESC{};
    s_bmCopyFrame = -1;
    s_bmCopyRt    = nullptr;
}

static void ChBlendRelease()
{
    ChBlendReleaseTargets();
    for (IDirect3DPixelShader9*& p : s_bmPs) if (p) { p->Release(); p = nullptr; }
    for (bool& b : s_bmPsBad) b = false;
    if (s_bmVs) { s_bmVs->Release(); s_bmVs = nullptr; }
    s_bmFailW = s_bmFailH = 0;
    s_bmDev  = nullptr;
    s_bmDead = false;
    s_bmLive = false;
}

// cfg can hold anything; an old cfg only knows the additive checkbox
static int ChBlendMode()
{
    const int m = (g_chBlendMode > 0) ? g_chBlendMode : (g_chAdditive ? 1 : 0);
    return (m >= 0 && m < kChBlendCount) ? m : 0;
}

// Callback payload: mode in the low byte, the ImGui frame it was recorded in above it.
static void* ChBlendPack(int mode)
{
    return (void*)(((intptr_t)ImGui::GetFrameCount() << 8) | (intptr_t)mode);
}

static IDirect3DPixelShader9* ChBlendShader(IDirect3DDevice9* dev, int mode)
{
    if (dev != s_bmDev) { ChBlendRelease(); s_bmDev = dev; }
    if (s_bmDead || s_bmPsBad[mode]) return nullptr;

    if (!s_bmVs)
    {
        D3DCAPS9 caps{};
        if (FAILED(dev->GetDeviceCaps(&caps)) ||
            caps.PixelShaderVersion  < D3DPS_VERSION(3, 0) ||
            caps.VertexShaderVersion < D3DVS_VERSION(3, 0))
        { DebugLog("chblend: no shader model 3 -- blend modes draw normal"); s_bmDead = true; return nullptr; }

        std::vector<DWORD> code((sizeof(k_fxChBlendVsBytes) + 3) / 4);   // BYTE arrays carry no alignment
        memcpy(code.data(), k_fxChBlendVsBytes, sizeof(k_fxChBlendVsBytes));
        if (FAILED(dev->CreateVertexShader(code.data(), &s_bmVs)))
        { DebugLog("chblend: vertex shader refused"); s_bmVs = nullptr; s_bmDead = true; return nullptr; }
    }

    if (!s_bmPs[mode])
    {
        const FxChBlendPs& b = k_fxChBlendPs[mode];
        std::vector<DWORD> code((b.size + 3) / 4);
        if (b.bytes) memcpy(code.data(), b.bytes, b.size);
        if (!b.bytes || FAILED(dev->CreatePixelShader(code.data(), &s_bmPs[mode])))
        {
            DebugLog("chblend: pixel shader for mode %d refused", mode);
            s_bmPs[mode]    = nullptr;
            s_bmPsBad[mode] = true;
            return nullptr;
        }
    }
    return s_bmPs[mode];
}

// The copy is always X8R8G8B8 and keyed on size alone: the game frame (often X8, maybe msaa)
// and fpsoverlay's A8 menu layer can both blend in one frame, and a format-keyed cache would
// rebuild the target twice a frame. StretchRect converts between the two 32-bit formats.
// DEFAULT pool: gone on Reset, rebuilt on the next blended frame.
static bool ChBlendTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& rt)
{
    if (!s_bmSurf || s_bmW != rt.Width || s_bmH != rt.Height)
    {
        if (s_bmFailW == rt.Width && s_bmFailH == rt.Height) return false;
        ChBlendReleaseTargets();
        HRESULT hr = dev->CreateTexture(rt.Width, rt.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                        D3DPOOL_DEFAULT, &s_bmTex, nullptr);
        if (FAILED(hr))
            hr = dev->CreateTexture(rt.Width, rt.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                    D3DPOOL_DEFAULT, &s_bmTex, nullptr);
        if (SUCCEEDED(hr)) hr = s_bmTex->GetSurfaceLevel(0, &s_bmSurf);
        if (FAILED(hr))
        {
            DebugLog("chblend: copy target %ux%u failed 0x%08lx", rt.Width, rt.Height, (unsigned long)hr);
            ChBlendReleaseTargets();
            s_bmFailW = rt.Width; s_bmFailH = rt.Height;
            return false;
        }
        s_bmW = rt.Width; s_bmH = rt.Height;
        DebugLog("chblend: copy target %ux%u", rt.Width, rt.Height);
    }

    if (rt.MultiSampleType == D3DMULTISAMPLE_NONE) return true;

    // msaa: resolve into a plain surface of the frame's own format first, same as dof does
    if (s_bmResolve && s_bmResolveDesc.Width == rt.Width && s_bmResolveDesc.Height == rt.Height &&
        s_bmResolveDesc.Format == rt.Format)
        return true;
    if (s_bmResolve) { s_bmResolve->Release(); s_bmResolve = nullptr; }
    if (FAILED(dev->CreateRenderTarget(rt.Width, rt.Height, rt.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                                       &s_bmResolve, nullptr)))
    {
        DebugLog("chblend: msaa resolve target %ux%u fmt %d failed", rt.Width, rt.Height, (int)rt.Format);
        s_bmResolve = nullptr;
        ChBlendReleaseTargets();
        s_bmFailW = rt.Width; s_bmFailH = rt.Height;
        return false;
    }
    s_bmResolveDesc = rt;
    return true;
}

static void ChBlendOn(const ImDrawList*, const ImDrawCmd* cmd)
{
    s_bmLive = false;
    IDirect3DDevice9* dev = g_d3dDev;
    const intptr_t packed = (intptr_t)cmd->UserCallbackData;
    const int mode  = (int)(packed & 0xFF);
    const int frame = (int)(packed >> 8);
    if (!dev || mode < 2 || mode >= kChBlendCount) return;

    // Anything failing below leaves ImGui's own state untouched: the arms just draw normal.
    IDirect3DPixelShader9* ps = ChBlendShader(dev, mode);
    if (!ps) return;

    IDirect3DSurface9* rt = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &rt)) || !rt) return;
    D3DSURFACE_DESC desc{};
    HRESULT hr = rt->GetDesc(&desc);
    if (SUCCEEDED(hr) && !ChBlendTargets(dev, desc)) hr = E_FAIL;
    if (SUCCEEDED(hr) && (s_bmCopyFrame != frame || s_bmCopyRt != rt))
    {
        hr = dev->StretchRect(rt, nullptr, s_bmResolve ? s_bmResolve : s_bmSurf, nullptr, D3DTEXF_NONE);
        if (SUCCEEDED(hr) && s_bmResolve)
            hr = dev->StretchRect(s_bmResolve, nullptr, s_bmSurf, nullptr, D3DTEXF_NONE);
        s_bmCopyFrame = SUCCEEDED(hr) ? frame : -1;
        s_bmCopyRt    = SUCCEEDED(hr) ? rt : nullptr;
    }
    rt->Release();
    if (FAILED(hr))
    {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("chblend: frame copy failed 0x%08lx", (unsigned long)hr); }
        return;
    }

    // The vertex shader must rebuild exactly the ortho ImGui built, and ImGui builds it from
    // DisplaySize -- not from the viewport, which only matches when its SetViewport succeeded.
    // The copy's own size maps VPOS back to texels.
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float vsc[4] = { ds.x, ds.y, 0.f, 0.f };
    const float psc[4] = { 0.f, 1.f / (float)s_bmW, 1.f / (float)s_bmH, 0.f };
    dev->SetVertexShader(s_bmVs);
    dev->SetPixelShader(ps);
    dev->SetVertexShaderConstantF(0, vsc, 1);
    dev->SetPixelShaderConstantF(0, psc, 1);
    // texture first: TF2's wrapper turns sRGB decode back on inside SetTexture
    dev->SetTexture(1, s_bmTex);
    dev->SetSamplerState(1, D3DSAMP_MINFILTER,   D3DTEXF_POINT);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER,   D3DTEXF_POINT);
    dev->SetSamplerState(1, D3DSAMP_MIPFILTER,   D3DTEXF_NONE);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, FALSE);
    s_bmLive = true;
}

static void ChBlendOff(const ImDrawList*, const ImDrawCmd*)
{
    if (!s_bmLive || !g_d3dDev) return;
    s_bmLive = false;
    // back to ImGui's fixed pipeline; its state block puts the rest back at the end
    g_d3dDev->SetVertexShader(nullptr);
    g_d3dDev->SetPixelShader(nullptr);
    g_d3dDev->SetTexture(1, nullptr);
}

bool Crosshair_BlendCombo(const char* id)
{
    const int cur = ChBlendMode();
    const bool open = ImGui::BeginCombo(id, kChBlend[cur].name, ImGuiComboFlags_HeightLargest);
    // read now, while the last item is still the combo frame -- after EndCombo it is a popup row
    const bool hovered = !open && ImGui::IsItemHovered();
    if (!open) return hovered;
    const char* cat = nullptr;
    for (int i = 0; i < kChBlendCount; ++i)
    {
        if (!cat || strcmp(cat, kChBlend[i].cat) != 0)
        {
            cat = kChBlend[i].cat;
            if (i > 0) ImGui::SeparatorText(cat);
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(kChBlend[i].name, i == cur))
        {
            g_chBlendMode = i;
            g_chAdditive  = (i == 1);   // old builds reading this cfg still see additive
        }
        ImGui::PopID();
        if (i == cur) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return false;
}

// SrgbGammaToLinear, quantised back to the 8 bits a vertex colour carries.
static int ChCsToLinear8(float srgb)
{
    if (srgb < 0.f) srgb = 0.f;
    if (srgb > 1.f) srgb = 1.f;
    const float lin = (srgb <= 0.04045f) ? (srgb / 12.92f)
                                         : powf((srgb + 0.055f) / 1.055f, 2.4f);
    return (int)(lin * 255.f + 0.5f);
}

// One texel of a part, at 1:1 scale.
static void ChCsTexel(ImDrawList* dl, float ox, float oy, int lx, int ly, ImU32 col, float s)
{
    dl->AddRectFilled(ImVec2(ox + lx * s, oy + ly * s),
                      ImVec2(ox + (lx + 1) * s, oy + (ly + 1) * s), col);
}

// Fallback for a device that would not give us the atlas: one hard rect per baked texel,
// tinted the same way the sampler would. dir 0=top,1=right,2=bottom,3=left; the Image
// panels carry orientation 0/90/180/270, i.e. the art rotated clockwise about the panel,
// which swaps the panel's own width/height on the horizontal pair.
static void ChCsDrawTexels(ImDrawList* dl, const unsigned char* art, int W, int H,
                           float cx, float cy, float off, int dir,
                           int r, int g, int b, float alphaMul, float s)
{
    const bool  horiz = (dir & 1) != 0;
    const float pcx = (dir == 1) ? cx + off : (dir == 3) ? cx - off : cx;
    const float pcy = (dir == 0) ? cy - off : (dir == 2) ? cy + off : cy;
    const float ox = pcx - (horiz ? H : W) * 0.5f * s;
    const float oy = pcy - (horiz ? W : H) * 0.5f * s;

    for (int ty = 0; ty < H; ++ty)
        for (int tx = 0; tx < W; ++tx)
        {
            const unsigned char* t = art + (ty * W + tx) * 4;
            const int a = (int)(t[3] * alphaMul + 0.5f);
            if (a <= 0) continue;
            int lx, ly;
            switch (dir) {
                case 1:  lx = H-1-ty; ly = tx;     break;
                case 2:  lx = W-1-tx; ly = H-1-ty; break;
                case 3:  lx = ty;     ly = W-1-tx; break;
                default: lx = tx;     ly = ty;     break;
            }
            ChCsTexel(dl, ox, oy, lx, ly,
                      IM_COL32(r * t[0] / 255, g * t[1] / 255, b * t[2] / 255, a), s);
        }
}

// ---- the art as real textures ------------------------------------------------------
// One quad per part, sampled, instead of a rect per texel: at scale 1 the baked art is
// already what the screen shows, and above it the sampler does the enlarging -- LINEAR
// for a smooth blow-up, POINT for the look of an upscaled capture.
// All three live in one 32x32 power-of-two atlas: TF2's device turned down the 5x13
// and 1x7 art as separate textures ("texture create failed" in the log), which is the
// non-pow2 conditional support DX9 hardware is allowed to refuse. Each sprite carries a
// 2px border of its own edge texels so the bilinear tap at a sprite edge sees what
// D3DTADDRESS_CLAMP would have shown it, not the neighbouring sprite.
struct CsSprite { int x, y, w, h; };
static constexpr int kCsAtlasDim = 32;
static constexpr int kCsSpritePad = 2;
static constexpr CsSprite kCsSpPip      { 2, 2, 5, 13 };   // crosshairpip2.png, baked
static constexpr CsSprite kCsSpCircle   { 12, 2, 7,  7 };  // reticlecircle.png, baked
static constexpr CsSprite kCsSpBlackPip { 24, 2, 1,  7 };  // reticlepip.png, baked

static IDirect3DTexture9* s_csAtlas    = nullptr;
static IDirect3DDevice9*  s_csTexDev   = nullptr;
static bool               s_csTexTried = false;

// RGBA straight from the png (through the bake), so a faint texel keeps the slightly
// off-white it was authored with instead of being forced to 255.
static void CsBlitSprite(unsigned char* bits, int pitch, const CsSprite& sp,
                         const unsigned char* rgba)
{
    for (int y = -kCsSpritePad; y < sp.h + kCsSpritePad; ++y)
    {
        unsigned int* row = (unsigned int*)(bits + (sp.y + y) * pitch);
        const int sy = (y < 0) ? 0 : (y >= sp.h ? sp.h - 1 : y);
        for (int x = -kCsSpritePad; x < sp.w + kCsSpritePad; ++x)
        {
            const int sx = (x < 0) ? 0 : (x >= sp.w ? sp.w - 1 : x);
            const unsigned char* t = rgba + (sy * sp.w + sx) * 4;
            row[sp.x + x] = ((unsigned int)t[3] << 24) | ((unsigned int)t[0] << 16)
                          | ((unsigned int)t[1] << 8) | (unsigned int)t[2];
        }
    }
}

void CsgoCrosshair_ReleaseTextures()
{
    ChBlendRelease();   // blend-mode shaders + DEFAULT-pool frame copy ride the same Reset/eject path
    ScopeCsgo_ReleaseTextures();
    if (s_csAtlas) { s_csAtlas->Release(); s_csAtlas = nullptr; }
    s_csTexDev   = nullptr;
    s_csTexTried = false;
}

static bool CsEnsureTextures()
{
    if (!g_d3dDev) return false;
    if (s_csTexDev != g_d3dDev) { CsgoCrosshair_ReleaseTextures(); s_csTexDev = g_d3dDev; }
    if (s_csTexTried) return s_csAtlas != nullptr;

    s_csTexTried = true;
    s_csTexDev   = g_d3dDev;

    // MANAGED survives a device Reset; DEFAULT+DYNAMIC is the fallback for wrappers that
    // refuse MANAGED, and h_Reset drops the atlas so that one gets rebuilt after a Reset.
    DWORD usage = 0;
    HRESULT hr = g_d3dDev->CreateTexture(kCsAtlasDim, kCsAtlasDim, 1, 0, D3DFMT_A8R8G8B8,
                                         D3DPOOL_MANAGED, &s_csAtlas, nullptr);
    if (FAILED(hr) || !s_csAtlas)
    {
        DebugLog("csgo xhair: atlas MANAGED hr=0x%08X, trying DEFAULT", (unsigned)hr);
        s_csAtlas = nullptr;
        usage = D3DUSAGE_DYNAMIC;
        hr = g_d3dDev->CreateTexture(kCsAtlasDim, kCsAtlasDim, 1, usage, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT, &s_csAtlas, nullptr);
    }
    if (FAILED(hr) || !s_csAtlas)
    {
        DebugLog("csgo xhair: atlas create failed hr=0x%08X -- texel-rect fallback", (unsigned)hr);
        s_csAtlas = nullptr;
        return false;
    }

    D3DLOCKED_RECT lr{};
    hr = s_csAtlas->LockRect(0, &lr, nullptr, (usage & D3DUSAGE_DYNAMIC) ? D3DLOCK_DISCARD : 0);
    if (FAILED(hr))
    {
        DebugLog("csgo xhair: atlas lock failed hr=0x%08X -- texel-rect fallback", (unsigned)hr);
        s_csAtlas->Release(); s_csAtlas = nullptr;
        return false;
    }

    unsigned char* bits = (unsigned char*)lr.pBits;
    for (int y = 0; y < kCsAtlasDim; ++y)
        memset(bits + y * lr.Pitch, 0, kCsAtlasDim * 4);
    CsBlitSprite(bits, lr.Pitch, kCsSpPip,      &kCsPip[0][0][0]);
    CsBlitSprite(bits, lr.Pitch, kCsSpCircle,   &kCsCircle[0][0][0]);
    CsBlitSprite(bits, lr.Pitch, kCsSpBlackPip, &kCsBlackPip[0][0][0]);
    s_csAtlas->UnlockRect(0);

    // POSTBLENDSRGBCONVERT is the cap that says the ROP blends in linear light and
    // encodes after, which is what panorama's sRGB write relies on. Without it the
    // linear switch quietly falls back to the old sRGB-space look.
    D3DCAPS9 caps{};
    const bool postBlend = SUCCEEDED(g_d3dDev->GetDeviceCaps(&caps))
                         && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_POSTBLENDSRGBCONVERT) != 0;
    DebugLog("csgo xhair: reticle atlas ok (%s), srgb postblend=%d",
             usage ? "DEFAULT" : "MANAGED", postBlend ? 1 : 0);
    return true;
}

// One Image panel. orient = the xml orientation attribute: 0/90/180/270, the art
// turned clockwise inside the panel, so the panel's own w/h are already swapped on
// the horizontal pair.
static void CsDrawImage(ImDrawList* dl, const CsSprite& sp, float pcx, float pcy,
                        float w, float h, int orient, ImU32 col)
{
    if (!s_csAtlas) return;
    const float x0 = pcx - w * 0.5f, y0 = pcy - h * 0.5f;
    const float x1 = pcx + w * 0.5f, y1 = pcy + h * 0.5f;
    const ImVec2 p1(x0, y0), p2(x1, y0), p3(x1, y1), p4(x0, y1);

    const float u0 = sp.x / (float)kCsAtlasDim,          v0 = sp.y / (float)kCsAtlasDim;
    const float u1 = (sp.x + sp.w) / (float)kCsAtlasDim, v1 = (sp.y + sp.h) / (float)kCsAtlasDim;
    const ImVec2 A(u0, v0), B(u1, v0), C(u1, v1), D(u0, v1);
    const ImTextureID id = (ImTextureID)s_csAtlas;

    switch (orient) {
        case 1:  dl->AddImageQuad(id, p1, p2, p3, p4, D, A, B, C, col); break;   // 90
        case 2:  dl->AddImageQuad(id, p1, p2, p3, p4, C, D, A, B, col); break;   // 180
        case 3:  dl->AddImageQuad(id, p1, p2, p3, p4, B, C, D, A, col); break;   // 270
        default: dl->AddImageQuad(id, p1, p2, p3, p4, A, B, C, D, col); break;   // 0
    }
}

// Part centre for one arm. dir 0=top,1=right,2=bottom,3=left.
static inline ImVec2 CsPartCenter(float cx, float cy, float off, int dir)
{
    return ImVec2((dir == 1) ? cx + off : (dir == 3) ? cx - off : cx,
                  (dir == 0) ? cy - off : (dir == 2) ? cy + off : cy);
}

static void DrawCsgoPanorama(const ChTarget& T, bool bStatic)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = T.dl;

    const int scrH = (int)io.DisplaySize.y;
    if (scrH <= 0) return;

    const CsPlayerState& st = T.player;

    float dt = AnimDt();   // game time (animclock.h)
    if (dt < 0.f)     dt = 0.f;
    if (dt > 0.25f)   dt = 0.25f;

    const float velSens = (g_chCsgoVelSens < 0.f) ? 0.f : g_chCsgoVelSens;
    const float tau     = (g_chCsgoSmooth  < 0.f) ? 0.f : g_chCsgoSmooth;

    // GetReticleCrosshairGap (weapon_csbase.cpp:1912). Style 1 ignores it and parks the
    // pips on cl_fixedcrosshairgap instead (csgo_hudreticle.cpp:1004).
    float gap;
    if (bStatic)
    {
        gap = kCsFixedGap;
    }
    else
    {
        float gapUnits = 10.f;
        if (st.valid)
        {
            if (!st.onGround)     gapUnits = 30.f;
            else if (st.ducking)  gapUnits = 3.f;
            else if (sqrtf(st.speed2D*st.speed2D + st.velZ*st.velZ) > 100.f) gapUnits = 20.f;
        }
        // 10 is the standing step. velSens rescales only how far the other three travel
        // from it, so 0 parks the arms where a standing player sees them and 1 is csgo.
        gapUnits = 10.f + (gapUnits - 10.f) * velSens;
        gap = floorf(scrH * gapUnits / 768.f);   // floor == the original's integer divide
    }
    gap += g_chCsgoGap;

    // csgo cuts straight to the new step; tau eases the four steps into each other.
    gap = ChCsEase(T.anim->gapSm, gap, dt, tau);

    // GetReticleWeaponSpread (:1890). FOV comes off fov_desired so a zoom moves the arcs
    // the same way GetFOV() does in csgo.
    float arcOff = 0.f;
    bool  drawArcs = !bStatic && g_chCsgoArcs;
    if (drawArcs)
    {
        static ConVar* s_fov = nullptr;
        if (!s_fov && g_cvar) s_fov = g_cvar->FindVarWalk("fov_desired");
        float fov = s_fov ? s_fov->GetFloat() : 90.f;
        if (fov < 1.f || fov > 179.f) fov = 90.f;

        const float inacc = st.valid ? CsUpdateInaccuracy(st, dt, velSens, *T.anim) : 0.f;

        const float halfFov = fov * 0.5f * 3.14159265f / 180.f;
        const float spreadDist = (inacc + kCsSpread) * 320.0f / tanf(halfFov);
        arcOff = floorf(spreadDist * scrH / 480.0f + 0.5f) + kCsArcOffset;

        arcOff = ChCsEase(T.anim->arcSm, arcOff, dt, tau);
    }

    // .crosshair carries translate3d(0.5px, 0.5px, 0) on top of a full-screen panel.
    const float cx = floorf(T.cx) + 0.5f;
    const float cy = floorf(T.cy) + 0.5f;

    // Every css colour reaches the vertex as linear light, so when we are blending in
    // linear light the wash has to be decoded the same way. Black decodes to black, so
    // the two black washes below need no branch.
    const int   r = g_chCsgoLinear ? ChCsToLinear8(g_chCsgoColor[0]) : (int)(g_chCsgoColor[0] * 255.f);
    const int   g = g_chCsgoLinear ? ChCsToLinear8(g_chCsgoColor[1]) : (int)(g_chCsgoColor[1] * 255.f);
    const int   b = g_chCsgoLinear ? ChCsToLinear8(g_chCsgoColor[2]) : (int)(g_chCsgoColor[2] * 255.f);
    const float aMul = g_chCsgoColor[3];
    const ImU32 litCol = IM_COL32(r, g, b, (int)(255.f * aMul + 0.5f));
    const float pipAlpha = aMul * kCsPipOpacity;
    const ImU32 pipCol = IM_COL32(r, g, b, (int)(255.f * pipAlpha + 0.5f));

    // csgo draws this at 1:1 screen pixels at any resolution (SetUIScale undoes the
    // window scale), so it is tiny by design. s blows the whole reticle up around its
    // centre -- art, gap and spread together -- the same way an upscaled screenshot does.
    const bool  bTex = CsEnsureTextures();
    const float s    = (g_chCsgoScale > 0.05f) ? g_chCsgoScale : 1.f;
    const float pipOff   = (gap + kCsPipOffset) * s;
    const float blackOff = (gap + kCsBlackPipOffset) * s;
    const float arcOffS  = arcOff * s;
    const ImU32 blackPipCol = IM_COL32(0, 0, 0, (int)(kCsBlackPipWash + 0.5f));
    const ImU32 circleCol   = IM_COL32(0, 0, 0, (int)(kCsCircleWash   + 0.5f));

    dl->AddCallback(ChCsBlendAdd, nullptr);

    if (g_chCsgoPips)
    for (int i = 0; i < 4; ++i)
    {
        if (!bTex) { ChCsDrawTexels(dl, &kCsPip[0][0][0], 5, 13,
                                    cx, cy, pipOff, i, r, g, b, pipAlpha, s); continue; }
        const bool  horiz = (i & 1) != 0;
        const ImVec2 c = CsPartCenter(cx, cy, pipOff, i);
        CsDrawImage(dl, kCsSpPip, c.x, c.y,
                    (horiz ? 13.f : 5.f) * s, (horiz ? 5.f : 13.f) * s, i, pipCol);
    }

    if (drawArcs)
    {
        const float h = kCsArcSize * 0.5f * s;
        const float t = 0.5f * s;                                       // 1px bar
        dl->AddRectFilled(ImVec2(cx - h, cy - arcOffS - t),
                          ImVec2(cx + h, cy - arcOffS + t), litCol);    // top
        dl->AddRectFilled(ImVec2(cx - h, cy + arcOffS - t),
                          ImVec2(cx + h, cy + arcOffS + t), litCol);    // bottom
        dl->AddRectFilled(ImVec2(cx - arcOffS - t, cy - h),
                          ImVec2(cx - arcOffS + t, cy + h), litCol);    // left
        dl->AddRectFilled(ImVec2(cx + arcOffS - t, cy - h),
                          ImVec2(cx + arcOffS + t, cy + h), litCol);    // right
    }

    dl->AddCallback(ChCsBlendNormal, nullptr);

    // the black pips are the arms' own shadow -- drop them with the arms, or four dark
    // nubs are left riding the gap with nothing on top of them.
    if (g_chCsgoPips)
    for (int i = 0; i < 4; ++i)
    {
        if (!bTex) { ChCsDrawTexels(dl, &kCsBlackPip[0][0][0], 1, 7,
                                    cx, cy, blackOff, i, 0, 0, 0,
                                    kCsBlackPipWash / 255.f, s); continue; }
        const bool  horiz = (i & 1) != 0;
        const ImVec2 c = CsPartCenter(cx, cy, blackOff, i);
        CsDrawImage(dl, kCsSpBlackPip, c.x, c.y,
                    (horiz ? 7.f : 1.f) * s, (horiz ? 1.f : 7.f) * s, i, blackPipCol);
    }

    if (bTex)
        CsDrawImage(dl, kCsSpCircle, cx, cy, 7.f * s, 7.f * s, 0, circleCol);
    else
        ChCsDrawTexels(dl, &kCsCircle[0][0][0], 7, 7, cx, cy, 0.f, 0,
                       0, 0, 0, kCsCircleWash / 255.f, s);

    // The dot is the panel's own background-color: opaque, additive, one pixel.
    dl->AddCallback(ChCsBlendAdd, nullptr);
    dl->AddRectFilled(ImVec2(cx - 0.5f * s, cy - 0.5f * s),
                      ImVec2(cx + 0.5f * s, cy + 0.5f * s), litCol);
    dl->AddCallback(ChCsEnd, nullptr);
}

// ==== CSGO classic crosshair (styles 2 / 3 / 4) =====================================
// CWeaponCSBase::DrawCrosshair (weapon_csbase.cpp:1969) + DrawCrosshairRect (:407).
// Nothing to do with the panorama reticle above: plain bars over a black outline rect,
// sized by cl_crosshairsize / cl_crosshairthickness / cl_crosshairgap. This is the
// crosshair in nearly every csgo clip.
//   2 = classic standard (spread-driven, splits past cl_crosshair_dynamic_splitdist)
//   3 = classic dynamic  (spread-driven, no split)
//   4 = classic static   (never moves)
// The one term TF2 can't feed is the per-shot bump (m_iShotsFired / GetCrosshairDelta-
// Distance), so the bars don't kick out while firing.
// vgui draws the outline with plain alpha and, when cl_crosshairusealpha is 0, the bar
// itself additively off the whiteAdditive texture -- so the blend flips per rect.
static void CsXRect(ImDrawList* dl, int r, int g, int b, int a,
                    float x0, float y0, float x1, float y1, bool bAdditive)
{
    if (a <= 0) return;
    if (g_csXOutline)
    {
        const float t = g_csXOutlineTh;
        if (bAdditive) dl->AddCallback(ChCsBlendNormal, nullptr);
        dl->AddRectFilled(ImVec2(x0 - t, y0 - t), ImVec2(x1 + t, y1 + t), IM_COL32(0, 0, 0, a));
        if (bAdditive) dl->AddCallback(ChCsBlendAdd, nullptr);
    }
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(r, g, b, a));
}

static void DrawCsgoClassic(const ChTarget& T, int style)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = T.dl;

    const int scrH = (int)io.DisplaySize.y;
    if (scrH <= 0) return;

    const CsPlayerState& st = T.player;
    const bool  bDyn = (style == 2 || style == 3);

    float dt = AnimDt();   // game time (animclock.h)
    if (dt < 0.f)   dt = 0.f;
    if (dt > 0.25f) dt = 0.25f;

    const float velSens = (g_chCsgoVelSens < 0.f) ? 0.f : g_chCsgoVelSens;
    const float tau     = (g_chCsgoSmooth  < 0.f) ? 0.f : g_chCsgoSmooth;

    // fCrosshairDistanceGoal (:2083) -- the movement multipliers only apply to 2/3
    float goal = kCsXDistGoal;
    if (bDyn && st.valid)
    {
        float mul = 1.0f;
        if (!st.onGround)     mul = 2.0f;
        else if (st.ducking)  mul = 0.5f;
        else if (sqrtf(st.speed2D*st.speed2D + st.velZ*st.velZ) > 100.f) mul = 1.5f;
        goal *= 1.0f + (mul - 1.0f) * velSens;   // velSens 0 -> the standing goal, always
    }

    float& s_dist = T.anim->dist;
    if (s_dist > goal)                                   // Lerp(t, goal, dist) (:2119)
        s_dist = goal + (s_dist - goal) * (dt / 0.025f);
    if (s_dist < goal)  s_dist = goal;
    if (s_dist > 25.f)  s_dist = 25.f;

    // GetInaccuracy() + GetSpread(), same numbers the panorama arcs ride
    static ConVar* s_fov = nullptr;
    if (!s_fov && g_cvar) s_fov = g_cvar->FindVarWalk("fov_desired");
    float fov = s_fov ? s_fov->GetFloat() : 90.f;
    if (fov < 1.f || fov > 179.f) fov = 90.f;
    const float inacc = st.valid ? CsUpdateInaccuracy(st, dt, velSens, *T.anim) : 0.f;
    const float halfFov = fov * 0.5f * 3.14159265f / 180.f;
    const float spreadDist = (inacc + kCsSpread) * 320.0f / tanf(halfFov);

    const float maxCross = g_csXSplitDist;
    const float cappedSpreadDist = (spreadDist > maxCross) ? maxCross : spreadDist;
    const float yres = scrH / 480.0f;
    const int iSpread       = (style < 4) ? (int)floorf(spreadDist       * yres + 0.5f) : 2;
    const int iCappedSpread = (style < 4) ? (int)floorf(cappedSpreadDist * yres + 0.5f) : 2;

    // :2132 -- note the gap rides ScreenHeight/1200 while the bars ride YRES
    int iDist   = (int)floorf(s_dist * scrH / 1200.0f + g_csXGap + 0.5f);
    int iCapped = 0;
    const int iBarSize   = (int)floorf(g_csXSize * yres + 0.5f);
    int iBarThickness    = (int)floorf(g_csXThick * yres + 0.5f);
    if (iBarThickness < 1) iBarThickness = 1;

    if (iSpread > 0 && bDyn)
    {
        iDist = iSpread + (int)g_csXGap;
        if (style == 2) iCapped = iCappedSpread + (int)g_csXGap;
    }
    else if (style == 4 || (iSpread == 0 && bDyn))
    {
        iDist   = (int)(goal + g_csXGap);
        iCapped = (int)(4.f + g_csXGap);
    }

    // csgo drops the bars wherever this frame's spread puts them; tau eases that walk.
    iDist   = (int)floorf(ChCsEase(T.anim->distSm,   (float)iDist,   dt, tau) + 0.5f);
    iCapped = (int)floorf(ChCsEase(T.anim->cappedSm, (float)iCapped, dt, tau) + 0.5f);

    const int cxi = (int)T.cx, cyi = (int)T.cy;

    int   r = (int)(g_chCsgoColor[0] * 255.f);
    int   g = (int)(g_chCsgoColor[1] * 255.f);
    int   b = (int)(g_chCsgoColor[2] * 255.f);
    const bool bAdditive = !g_csXUseAlpha;
    int alpha = g_csXAlpha;
    if (alpha < 0) alpha = 0; else if (alpha > 255) alpha = 255;
    if (bAdditive) alpha = 200;                       // :2036
    alpha = (int)(alpha * g_chCsgoColor[3] + 0.5f);

    int   innerDist    = iDist;
    float alphaInner   = (float)alpha;
    float alphaOuter   = (float)alpha;
    int   barSizeInner = iBarSize;

    if (bAdditive) dl->AddCallback(ChCsBlendAdd, nullptr);

    // style 2 past the split distance grows a second pair of pips outside the first
    if (style == 2 && spreadDist > maxCross)
    {
        innerDist    = iCapped;
        alphaInner   = alpha * g_csXInnerMod;
        alphaOuter   = alpha * g_csXOuterMod;
        barSizeInner = (int)ceilf ((float)iBarSize * (1.0f - g_csXSplitRatio));
        const int barSizeOuter = (int)floorf((float)iBarSize * g_csXSplitRatio);

        const int innerLeft  = (cxi - iDist - iBarThickness / 2) - barSizeInner;
        const int innerRight = innerLeft + 2 * (iDist + barSizeInner) + iBarThickness;
        const int outerLeft  = innerLeft  - barSizeOuter;
        const int outerRight = innerRight + barSizeOuter;
        const int y0 = cyi - iBarThickness / 2, y1 = y0 + iBarThickness;
        CsXRect(dl, r, g, b, (int)alphaOuter, (float)outerLeft,  (float)y0, (float)innerLeft,  (float)y1, bAdditive);
        CsXRect(dl, r, g, b, (int)alphaOuter, (float)innerRight, (float)y0, (float)outerRight, (float)y1, bAdditive);

        const int innerTop    = (cyi - iDist - iBarThickness / 2) - barSizeInner;
        const int innerBottom = innerTop + 2 * (iDist + barSizeInner) + iBarThickness;
        const int outerTop    = innerTop    - barSizeOuter;
        const int outerBottom = innerBottom + barSizeOuter;
        const int x0 = cxi - iBarThickness / 2, x1 = x0 + iBarThickness;
        if (!g_csXT)
            CsXRect(dl, r, g, b, (int)alphaOuter, (float)x0, (float)outerTop, (float)x1, (float)innerTop, bAdditive);
        CsXRect(dl, r, g, b, (int)alphaOuter, (float)x0, (float)innerBottom, (float)x1, (float)outerBottom, bAdditive);
    }

    // horizontal bars
    const int innerLeft  = cxi - innerDist - iBarThickness / 2;
    const int innerRight = innerLeft + 2 * innerDist + iBarThickness;
    const int outerLeft  = innerLeft  - barSizeInner;
    const int outerRight = innerRight + barSizeInner;
    const int y0 = cyi - iBarThickness / 2, y1 = y0 + iBarThickness;
    CsXRect(dl, r, g, b, (int)alphaInner, (float)outerLeft,  (float)y0, (float)innerLeft,  (float)y1, bAdditive);
    CsXRect(dl, r, g, b, (int)alphaInner, (float)innerRight, (float)y0, (float)outerRight, (float)y1, bAdditive);

    // vertical bars -- cl_crosshair_t drops the top one
    const int innerTop    = cyi - innerDist - iBarThickness / 2;
    const int innerBottom = innerTop + 2 * innerDist + iBarThickness;
    const int outerTop    = innerTop    - barSizeInner;
    const int outerBottom = innerBottom + barSizeInner;
    const int x0 = cxi - iBarThickness / 2, x1 = x0 + iBarThickness;
    if (!g_csXT)
        CsXRect(dl, r, g, b, (int)alphaInner, (float)x0, (float)outerTop, (float)x1, (float)innerTop, bAdditive);
    CsXRect(dl, r, g, b, (int)alphaInner, (float)x0, (float)innerBottom, (float)x1, (float)outerBottom, bAdditive);

    if (g_csXDot)
    {
        const int dx0 = cxi - iBarThickness / 2, dx1 = dx0 + iBarThickness;
        const int dy0 = cyi - iBarThickness / 2, dy1 = dy0 + iBarThickness;
        CsXRect(dl, r, g, b, alpha, (float)dx0, (float)dy0, (float)dx1, (float)dy1, bAdditive);
    }

    if (bAdditive) dl->AddCallback(ChCsEnd, nullptr);
}

// ---- Crosshair render --------------------------------------
static void DrawCustomCrosshair(const ChTarget& T)
{
    ImDrawList* dl = T.dl;
    const int chVtxStart = dl->VtxBuffer.Size;
    const float sc = T.scale;

    // additive wraps the whole crosshair as it always did; shader modes start after the outline
    const int blend = ChBlendMode();
    if (blend == 1) dl->AddCallback(ChBlendAddOn, nullptr);

    const bool  subPixel = (g_chThickness < 1.0f);
    const float thPx     = g_chAntialias ? (g_chThickness * kChUnitToPx * sc) : (subPixel ? 1.0f : floorf(g_chThickness * kChUnitToPx * sc));

    float cx = T.cx;
    float cy = T.cy;
    if (!g_chAntialias) {
        const float cxOff = ((int)thPx % 2 != 0) ? 0.5f : 0.0f;
        cx = floorf(cx) + cxOff;
        cy = floorf(cy) + cxOff;
    }

    const float len      = g_chAntialias ? (g_chLength * kChUnitToPx * sc) : floorf(g_chLength * kChUnitToPx * sc);
    const float gap      = g_chAntialias ? (g_chGap    * kChUnitToPx * sc) : floorf(g_chGap    * kChUnitToPx * sc);

    float dynGapPx = 0.f;
    if (g_chDynamic && g_chDynamicRef > 0.f)
    {
        const float speed = T.speed;
        if (speed >= 0.f && speed > g_chDynamicMin)
        {
            float range = g_chDynamicRef - g_chDynamicMin;
            float t = (range > 0.f) ? (speed - g_chDynamicMin) / range : 1.f;
            if (t > 1.f) t = 1.f;
            dynGapPx = g_chAntialias ? (t * g_chDynamicMax * kChUnitToPx * sc) : floorf(t * g_chDynamicMax * kChUnitToPx * sc);
        }
    }
    const float effectiveGap = gap + dynGapPx;
    const float gapAsp = g_chGapAspect > 0.01f ? g_chGapAspect : 0.01f;
    const float hGap   = g_chAntialias ? (effectiveGap * (gapAsp >= 1.0f ? gapAsp : 1.0f)) : floorf(effectiveGap * (gapAsp >= 1.0f ? gapAsp : 1.0f));
    const float vGap   = g_chAntialias ? (effectiveGap * (gapAsp <= 1.0f ? (1.0f / gapAsp) : 1.0f)) : floorf(effectiveGap * (gapAsp <= 1.0f ? (1.0f / gapAsp) : 1.0f));

    const float aspect = g_chAspect > 0.01f ? g_chAspect : 0.01f;
    const float hLen   = g_chAntialias ? (len * (aspect >= 1.0f ? aspect : 1.0f)) : floorf(len * (aspect >= 1.0f ? aspect : 1.0f));
    const float vLen   = g_chAntialias ? (len * (aspect <= 1.0f ? (1.0f / aspect) : 1.0f)) : floorf(len * (aspect <= 1.0f ? (1.0f / aspect) : 1.0f));

    auto packRGBA = [](const float c[4]) -> ImU32 {
        return IM_COL32((int)(c[0]*255.f),(int)(c[1]*255.f),
                        (int)(c[2]*255.f),(int)(c[3]*255.f));
    };

    // If crosshair is set to follow sheen color, build a temporary RGBA vec from
    // the current (possibly animated) sheen color, preserving the user's alpha.
    float chColorResolved[4];
    if (g_chUseSheenColor)
    {
        float sc[3];
        if (g_enableSheenAnim) GetAnimatedSheenColor(sc);
        else { sc[0] = g_sheenColor[0]; sc[1] = g_sheenColor[1]; sc[2] = g_sheenColor[2]; }
        chColorResolved[0] = sc[0];
        chColorResolved[1] = sc[1];
        chColorResolved[2] = sc[2];
        chColorResolved[3] = g_chColor[3]; // preserve alpha
    }
    else
    {
        chColorResolved[0] = g_chColor[0];
        chColorResolved[1] = g_chColor[1];
        chColorResolved[2] = g_chColor[2];
        chColorResolved[3] = g_chColor[3];
    }

    ImU32 col  = packRGBA(chColorResolved);

    const bool fancyArm = (!subPixel) && (g_chGradient || g_chTaper);
    ImU32 gradCenterCol = col, gradOuterCol = col;
    float gradStart = 0.f, gradEnd = 1.f;
    float taperNear = 1.0f, taperFar = 1.0f;
    if (fancyArm) {
        if (g_chGradient) {
            gradCenterCol = packRGBA(g_chGradColorCenter);
            gradOuterCol  = packRGBA(g_chGradColorOuter);
            gradStart     = g_chGradStart;
            gradEnd       = g_chGradEnd;
        }
        taperNear = g_chTaper ? g_chTaperNear : 1.0f;
        taperFar  = g_chTaper ? g_chTaperFar  : 1.0f;
    }

    // Resolve outline color (supports sheen, same as crosshair color)
    float chOutlineColorResolved[4];
    if (g_chOutlineUseSheenColor)
    {
        float sc[3];
        if (g_enableSheenAnim) GetAnimatedSheenColor(sc);
        else { sc[0] = g_sheenColor[0]; sc[1] = g_sheenColor[1]; sc[2] = g_sheenColor[2]; }
        chOutlineColorResolved[0] = sc[0];
        chOutlineColorResolved[1] = sc[1];
        chOutlineColorResolved[2] = sc[2];
        chOutlineColorResolved[3] = g_chOutlineColor[3];
    }
    else
    {
        chOutlineColorResolved[0] = g_chOutlineColor[0];
        chOutlineColorResolved[1] = g_chOutlineColor[1];
        chOutlineColorResolved[2] = g_chOutlineColor[2];
        chOutlineColorResolved[3] = g_chOutlineColor[3];
    }
    ImU32 outl = packRGBA(chOutlineColorResolved);

    // Outline side masks
    bool sL = !g_chOutlineSidesOnly || g_chOutlineSides[0];
    bool sR = !g_chOutlineSidesOnly || g_chOutlineSides[1];
    bool sT = !g_chOutlineSidesOnly || g_chOutlineSides[2];
    bool sB = !g_chOutlineSidesOnly || g_chOutlineSides[3];

    auto armLen = [&](int dir) { return (dir <= 1) ? hLen : vLen; };
    auto armGap = [&](int dir) { return (dir <= 1) ? hGap : vGap; };
    // dir: 0=L, 1=R, 2=B, 3=T
    auto armEnabled = [&](int dir) {
        return !g_chSidesOnly || g_chSides[dir];
    };

    if (g_chOutline)
    {
        const float ot = g_chOutlineThick < 0.1f ? 0.1f : g_chOutlineThick;
        const float otPx = g_chAntialias ? (ot * kChUnitToPx) : ((ot < 1.f) ? 1.f : floorf(ot * kChUnitToPx));
        const float armBaseHalf = thPx * 0.5f;
        if (g_chOutlineOnLines)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (!armEnabled(i)) continue;
                ChArm a = ChMakeArm(cx, cy, thPx, armLen(i), armGap(i), i);
                if (fancyArm && !g_chOutlineBlur) {
                    ChArm outA = a;
                    if (i <= 1) { outA.x1 -= otPx; outA.x2 += otPx; }
                    else        { outA.y1 -= otPx; outA.y2 += otPx; }
                    const float addScale = armBaseHalf > 0.f ? otPx / armBaseHalf : 0.f;
                    ChDrawArmFancy(dl, outA, i, outl, outl, 0.f, 0.f, taperNear + addScale, taperFar + addScale);
                } else if (g_chOutlineBlur) {
                    ChDrawOutlineBlur(dl, a.x1, a.y1, a.x2, a.y2, ot, outl, g_chOutlineBlurRadius, sL, sR, sT, sB);
                } else {
                    ChDrawOutline(dl, a.x1, a.y1, a.x2, a.y2, ot, outl, sL, sR, sT, sB);
                }
            }
        }
        if (g_chDot && g_chOutlineOnDot)
        {
            float r = thPx * 0.5f;
            if (g_chDotCircle && g_chOutlineBlur) {
                const float baseA2  = (float)((outl >> 24) & 0xFF) / 255.f;
                const ImU32 oRgb    = outl & 0x00FFFFFFu;
                const float maxExp2 = g_chOutlineBlurRadius * kChUnitToPx;
                const float sigma2  = maxExp2 * 0.4f + 0.5f;
                float cumT2 = 1.f;
                for (int i = 15; i >= 0; --i) {
                    const float br2 = ((float)(i + 1) / 16) * maxExp2;
                    const float tgt = baseA2 * expf(-0.5f * (br2 / sigma2) * (br2 / sigma2));
                    if (cumT2 < 1e-4f) break;
                    const float la  = 1.f - (1.f - tgt) / cumT2;
                    if (la <= 0.f) continue;
                    const int ai2   = (int)(la * 255.f + 0.5f);
                    if (ai2 <= 0) continue;
                    cumT2 *= (1.f - la);
                    dl->AddCircleFilled(ImVec2(cx, cy), r + otPx + br2, oRgb | ((ImU32)ai2 << 24));
                }
                dl->AddCircleFilled(ImVec2(cx, cy), r + otPx, outl);
            } else if (g_chDotCircle) {
                dl->AddCircleFilled(ImVec2(cx, cy), r + otPx, outl);
            } else if (g_chOutlineBlur) {
                ChDrawOutlineBlur(dl, cx-r, cy-r, cx+r, cy+r, ot, outl, g_chOutlineBlurRadius, sL, sR, sT, sB);
            } else {
                ChDrawOutline(dl, cx-r, cy-r, cx+r, cy+r, ot, outl, sL, sR, sT, sB);
            }
        }
    }

    // frame copy lands here: the outline is already in it, the arms blend on top
    if (blend >= 2) dl->AddCallback(ChBlendOn, ChBlendPack(blend));

    for (int i = 0; i < 4; ++i)
    {
        if (!armEnabled(i)) continue;
        ChArm a = ChMakeArm(cx, cy, thPx, armLen(i), armGap(i), i);
        if (subPixel)
        {
            ChDrawRect(dl, a.x1, a.y1, a.x2,      a.y1+1.f, col);
            ChDrawRect(dl, a.x1, a.y1, a.x1+1.f,  a.y2,     col);
        }
        else if (fancyArm)
        {
            ChDrawArmFancy(dl, a, i, gradCenterCol, gradOuterCol, gradStart, gradEnd, taperNear, taperFar);
        }
        else
        {
            ChDrawRect(dl, a.x1, a.y1, a.x2, a.y2, col);
        }
    }

    if (g_chDot)
    {
        float lo = thPx * 0.5f;
        ImU32 dotCol = g_chDotCustomColor
            ? packRGBA(g_chDotColor)
            : ((g_chGradient && !subPixel) ? gradCenterCol : col);
        if (g_chDotCircle)
            dl->AddCircleFilled(ImVec2(cx, cy), lo, dotCol);
        else
            ChDrawRect(dl, cx - lo, cy - lo, cx + lo, cy + lo, dotCol);
    }

    if (fabsf(g_chRotation) > 0.01f) {
        const float rad = g_chRotation * IM_PI / 180.f;
        const float ca = cosf(rad), sa = sinf(rad);
        for (int vi = chVtxStart; vi < dl->VtxBuffer.Size; ++vi) {
            float dx = dl->VtxBuffer[vi].pos.x - cx;
            float dy = dl->VtxBuffer[vi].pos.y - cy;
            dl->VtxBuffer[vi].pos.x = cx + dx * ca - dy * sa;
            dl->VtxBuffer[vi].pos.y = cy + dx * sa + dy * ca;
        }
    }

    if (blend == 1)      dl->AddCallback(ChBlendAddOff, nullptr);
    else if (blend >= 2) dl->AddCallback(ChBlendOff, nullptr);
}

// Two independent crosshairs: the custom one above and the csgo port. Either can
// be on alone or both together, exactly like SimpleVisual.
static void DrawCrosshairs(const ChTarget& T)
{
    if (g_enableCrosshair) DrawCustomCrosshair(T);
    if (g_chCsgoEnable)
    {
        if (g_chCsgoStyle <= 1) DrawCsgoPanorama(T, g_chCsgoStyle == 1);   // panorama reticle
        else                    DrawCsgoClassic(T, g_chCsgoStyle);         // vgui bars
    }
}

void RenderCrosshair()
{
    if (!g_enableCrosshair && !g_chCsgoEnable) return;
    // csgo scope (painted by the HUD, scope_csgo.inl): no crosshair of ours under it
    // (visuals.cpp holds the game's off)
    if (g_eject || ScopeCsgo_Active()) return;
    if (!IsInGame() || !IsLocalPlayerAlive()) return;

    const ImGuiIO& io = ImGui::GetIO();
    ChTarget T{};
    T.dl     = ImGui::GetBackgroundDrawList();
    T.cx     = io.DisplaySize.x * 0.5f;
    T.cy     = io.DisplaySize.y * 0.5f;
    T.scale  = 1.f;
    T.speed  = GetLocalPlayerSpeed();
    T.player = g_chCsgoEnable ? CsGetPlayerState() : CsPlayerState{};
    T.anim   = &s_csAnimGame;
    DrawCrosshairs(T);
}

void Crosshair_DrawPreview(ImDrawList* dl, ImVec2 min, ImVec2 max, float testSpeed)
{
    const float w = max.x - min.x, h = max.y - min.y;
    if (w < 4.f || h < 4.f) return;

    ChTarget T{};
    T.dl    = dl;
    T.cx    = min.x + w * 0.5f;
    T.cy    = min.y + h * 0.5f;
    T.speed = testSpeed;
    T.anim  = &s_csAnimPreview;

    // shrink the custom crosshair until its widest arm fits the box
    const float aspect      = g_chAspect > 0.01f ? g_chAspect : 0.01f;
    const float longerScale = aspect >= 1.f ? aspect : 1.f / aspect;
    const float extentPx    = fmaxf(0.f, floorf(g_chGap * kChUnitToPx))
                            + floorf(g_chLength * kChUnitToPx * longerScale)
                            + floorf(g_chThickness * kChUnitToPx * 0.5f);
    const float maxExtent   = (w < h ? w : h) * 0.45f;
    T.scale = (extentPx > maxExtent && extentPx > 0.f) ? maxExtent / extentPx : 1.f;

    // a standing player, walking at the test speed
    T.player.valid   = true;
    T.player.speed2D = testSpeed > 0.f ? testSpeed : 0.f;

    dl->PushClipRect(min, max, true);
    DrawCrosshairs(T);
    dl->PopClipRect();
}

