#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

// Color correction tone curve. Up to k_ccCurveMaxPts control points, sorted by x, all in [0,1].
// Evaluated as a monotone cubic Hermite spline (PCHIP / Fritsch-Butland tangents): smooth through
// every point, never overshoots between two points, so no ripples or clipping bumps. Left of the
// first point and right of the last one the curve holds flat, like Photoshop.
// Engine-free so tests/cccurve_test.cpp can cover it. Thread note: the menu edits curves on the
// render thread and the LUT bake reads them there too; Eval still clamps n and tolerates
// unsorted x so a config load racing the render thread cannot read out of bounds.

constexpr int   k_ccCurveMaxPts = 16;
constexpr float k_ccCurveMinGap = 0.01f;   // min x spacing between two points
constexpr int   k_ccLutSize     = 256;

struct CcCurve {
    int   n = 2;
    float x[k_ccCurveMaxPts] = { 0.f, 1.f };
    float y[k_ccCurveMaxPts] = { 0.f, 1.f };
};

inline void CcCurve_Reset(CcCurve& c) { c = CcCurve{}; }

inline bool CcCurve_IsIdentity(const CcCurve& c)
{
    if (c.n < 2 || c.n > k_ccCurveMaxPts) return false;
    if (c.x[0] > 1e-4f || c.x[c.n - 1] < 1.f - 1e-4f) return false;
    for (int i = 0; i < c.n; i++)
        if (std::fabs(c.x[i] - c.y[i]) > 1e-4f) return false;
    return true;
}

inline float CcCurve_Eval(const CcCurve& c, float t)
{
    const int n = std::clamp(c.n, 1, k_ccCurveMaxPts);
    if (n == 1 || t <= c.x[0]) return c.y[0];
    if (t >= c.x[n - 1])       return c.y[n - 1];

    int k = 0;
    while (k < n - 2 && t >= c.x[k + 1]) ++k;

    auto h = [&](int i) { return c.x[i + 1] - c.x[i]; };
    auto d = [&](int i) { const float hi = h(i); return hi > 0.f ? (c.y[i + 1] - c.y[i]) / hi : 0.f; };
    // Endpoints take the secant; interior points the weighted harmonic mean of both secants,
    // zero at a local min/max. Both keep m/d in [0,3], the Fritsch-Carlson monotone region.
    auto m = [&](int i) -> float {
        if (i == 0)     return d(0);
        if (i == n - 1) return d(n - 2);
        const float d0 = d(i - 1), d1 = d(i);
        if (d0 * d1 <= 0.f) return 0.f;
        const float h0 = h(i - 1), h1 = h(i);
        return 3.f * (h0 + h1) / ((2.f * h1 + h0) / d0 + (h1 + 2.f * h0) / d1);
    };

    const float hk = h(k);
    if (hk <= 0.f) return c.y[k];
    const float s = (t - c.x[k]) / hk, s2 = s * s, s3 = s2 * s;
    return ( 2.f * s3 - 3.f * s2 + 1.f) * c.y[k]
         + (       s3 - 2.f * s2 + s  ) * hk * m(k)
         + (-2.f * s3 + 3.f * s2      ) * c.y[k + 1]
         + (       s3 -       s2      ) * hk * m(k + 1);
}

// Moves point i, keeping it between its neighbours (plus the min gap) and inside [0,1].
inline void CcCurve_Move(CcCurve& c, int i, float px, float py)
{
    if (i < 0 || i >= c.n) return;
    const float lo = i > 0       ? c.x[i - 1] + k_ccCurveMinGap : 0.f;
    const float hi = i < c.n - 1 ? c.x[i + 1] - k_ccCurveMinGap : 1.f;
    c.x[i] = std::clamp(px, lo, std::max(lo, hi));
    c.y[i] = std::clamp(py, 0.f, 1.f);
}

// Inserts a point in x order. Returns its index, or -1 when full or too close to another point.
inline int CcCurve_Insert(CcCurve& c, float px, float py)
{
    if (c.n >= k_ccCurveMaxPts) return -1;
    px = std::clamp(px, 0.f, 1.f);
    int at = 0;
    while (at < c.n && c.x[at] < px) ++at;
    if ((at > 0 && px - c.x[at - 1] < k_ccCurveMinGap) ||
        (at < c.n && c.x[at] - px < k_ccCurveMinGap)) return -1;
    for (int j = c.n; j > at; j--) { c.x[j] = c.x[j - 1]; c.y[j] = c.y[j - 1]; }
    c.x[at] = px;
    c.y[at] = std::clamp(py, 0.f, 1.f);
    c.n++;
    return at;
}

// Removes point i. A curve keeps at least two points.
inline bool CcCurve_Remove(CcCurve& c, int i)
{
    if (c.n <= 2 || i < 0 || i >= c.n) return false;
    for (int j = i; j < c.n - 1; j++) { c.x[j] = c.x[j + 1]; c.y[j] = c.y[j + 1]; }
    c.n--;
    c.x[c.n] = c.y[c.n] = 0.f;   // tail stays zero so memcmp change checks settle
    return true;
}

// Config form: "x,y;x,y;..." with 4 decimals.
inline std::string CcCurve_ToString(const CcCurve& c)
{
    std::string s;
    char buf[32];
    for (int i = 0; i < c.n && i < k_ccCurveMaxPts; i++) {
        std::snprintf(buf, sizeof(buf), "%s%.4f,%.4f", i ? ";" : "", c.x[i], c.y[i]);
        s += buf;
    }
    return s;
}

// Parses CcCurve_ToString output. False on anything malformed, and `out` is left untouched.
// Points are clamped to [0,1], sorted, and ones closer than the min gap are dropped.
inline bool CcCurve_Parse(const std::string& text, CcCurve& out)
{
    CcCurve c;
    c.n = 0;
    const char* p = text.c_str();
    while (*p) {
        if (c.n >= k_ccCurveMaxPts) return false;
        char* end = nullptr;
        const float px = std::strtof(p, &end);
        if (end == p || *end != ',' || !std::isfinite(px)) return false;
        p = end + 1;
        const float py = std::strtof(p, &end);
        if (end == p || !std::isfinite(py)) return false;
        p = end;
        if (*p == ';') ++p;
        else if (*p) return false;
        c.x[c.n] = std::clamp(px, 0.f, 1.f);
        c.y[c.n] = std::clamp(py, 0.f, 1.f);
        c.n++;
    }
    // insertion sort by x, then drop near-duplicates
    for (int i = 1; i < c.n; i++)
        for (int j = i; j > 0 && c.x[j] < c.x[j - 1]; j--) {
            std::swap(c.x[j], c.x[j - 1]);
            std::swap(c.y[j], c.y[j - 1]);
        }
    int w = 0;
    for (int i = 0; i < c.n; i++) {
        if (w > 0 && c.x[i] - c.x[w - 1] < k_ccCurveMinGap) continue;
        c.x[w] = c.x[i]; c.y[w] = c.y[i]; w++;
    }
    for (int i = w; i < k_ccCurveMaxPts; i++) c.x[i] = c.y[i] = 0.f;
    c.n = w;
    if (c.n < 2) return false;
    out = c;
    return true;
}

// Old configs stored 5 y offsets at fixed x = 0, .25, .5, .75, 1 (ccCurveOff*). Each key lands
// here one at a time: the first one lays out the 5-point grid, later ones fill it in, and a
// grid that ends up flat on the diagonal collapses back to the 2-point identity.
inline void CcCurve_ApplyLegacyOffset(CcCurve& c, int idx, float off)
{
    static const float gx[5] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
    if (idx < 0 || idx > 4 || !std::isfinite(off)) return;
    bool grid = c.n == 5;
    for (int i = 0; grid && i < 5; i++) grid = std::fabs(c.x[i] - gx[i]) < 1e-4f;
    if (!grid) {
        c = CcCurve{};
        c.n = 5;
        for (int i = 0; i < 5; i++) c.x[i] = c.y[i] = gx[i];
    }
    c.y[idx] = std::clamp(gx[idx] + off, 0.f, 1.f);
    if (CcCurve_IsIdentity(c)) CcCurve_Reset(c);
}

// Bakes curves {master, R, G, B} into a 256-texel D3DFMT_A8R8G8B8 row: channel = chan(master(x)).
// The shader samples texel centers, so an 8-bit input value k maps exactly onto texel k.
inline void CcCurve_BakeLut(const CcCurve ch[4], uint32_t lut[k_ccLutSize])
{
    auto q = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.f, 1.f) * 255.f); };
    for (int i = 0; i < k_ccLutSize; i++) {
        const float m = std::clamp(CcCurve_Eval(ch[0], i / (float)(k_ccLutSize - 1)), 0.f, 1.f);
        lut[i] = 0xFF000000u
               | (q(CcCurve_Eval(ch[1], m)) << 16)
               | (q(CcCurve_Eval(ch[2], m)) << 8)
               |  q(CcCurve_Eval(ch[3], m));
    }
}
