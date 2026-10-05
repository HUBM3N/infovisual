// Gate test for the color correction tone curve (src/features/cccurve.h).
// Build + run:  cmake --build build --config Release --target cccurve_test && build\Release\cccurve_test.exe
#include "../src/features/cccurve.h"
#include <cstdio>
#include <cstring>

static int s_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static CcCurve Make(std::initializer_list<std::pair<float, float>> pts)
{
    CcCurve c; c.n = 0;
    for (auto& p : pts) { c.x[c.n] = p.first; c.y[c.n] = p.second; c.n++; }
    return c;
}

int main()
{
    // identity: default curve is y = x everywhere
    CcCurve id;
    CHECK(CcCurve_IsIdentity(id));
    for (int i = 0; i <= 100; i++) CHECK(Near(CcCurve_Eval(id, i / 100.f), i / 100.f));

    // passes through every control point
    CcCurve s = Make({ {0.f, 0.f}, {0.25f, 0.15f}, {0.5f, 0.5f}, {0.75f, 0.85f}, {1.f, 1.f} });
    for (int i = 0; i < s.n; i++) CHECK(Near(CcCurve_Eval(s, s.x[i]), s.y[i]));
    CHECK(!CcCurve_IsIdentity(s));

    // smooth: a real spline, not the old piecewise-linear tents (midpoint of a segment is off the chord)
    const float mid = CcCurve_Eval(s, 0.125f);
    CHECK(!Near(mid, 0.075f, 1e-3f));

    // monotone data never overshoots: no value leaves [0,1], no dip between increasing points
    CcCurve steep = Make({ {0.f, 0.f}, {0.4f, 0.02f}, {0.45f, 0.98f}, {1.f, 1.f} });
    float prev = -1.f;
    for (int i = 0; i <= 1000; i++) {
        const float v = CcCurve_Eval(steep, i / 1000.f);
        CHECK(v >= -1e-5f && v <= 1.f + 1e-5f);
        CHECK(v >= prev - 1e-5f);
        prev = v;
    }

    // local extremum holds: a bump peaks at its control point and does not go above it
    CcCurve bump = Make({ {0.f, 0.2f}, {0.5f, 0.9f}, {1.f, 0.2f} });
    float peak = 0.f;
    for (int i = 0; i <= 1000; i++) peak = std::max(peak, CcCurve_Eval(bump, i / 1000.f));
    CHECK(Near(peak, 0.9f, 1e-4f));

    // flat outside the end points
    CcCurve inset = Make({ {0.2f, 0.3f}, {0.8f, 0.6f} });
    CHECK(Near(CcCurve_Eval(inset, 0.f), 0.3f));
    CHECK(Near(CcCurve_Eval(inset, 1.f), 0.6f));

    // corrupt input never reads out of bounds
    CcCurve bad; bad.n = 999;
    (void)CcCurve_Eval(bad, 0.5f);
    bad.n = -5;
    (void)CcCurve_Eval(bad, 0.5f);
    CcCurve unsorted = Make({ {0.f, 0.f}, {0.6f, 0.5f}, {0.4f, 0.7f}, {1.f, 1.f} });
    const float u = CcCurve_Eval(unsorted, 0.5f);
    CHECK(std::isfinite(u));

    // insert keeps order, rejects near-duplicates and a full curve
    CcCurve e;
    CHECK(CcCurve_Insert(e, 0.5f, 0.7f) == 1);
    CHECK(e.n == 3 && Near(e.x[1], 0.5f) && Near(e.y[1], 0.7f));
    CHECK(CcCurve_Insert(e, 0.25f, 0.1f) == 1);
    CHECK(e.n == 4 && Near(e.x[2], 0.5f));
    CHECK(CcCurve_Insert(e, 0.505f, 0.5f) == -1);
    CHECK(CcCurve_Insert(e, 2.f, 0.5f) == -1);            // clamps to 1, collides with the end point
    CcCurve full;
    for (int i = 1; i < k_ccCurveMaxPts - 1; i++) CHECK(CcCurve_Insert(full, i / 16.f, 0.5f) >= 0);
    CHECK(full.n == k_ccCurveMaxPts);
    CHECK(CcCurve_Insert(full, 0.97f, 0.5f) == -1);

    // move clamps between neighbours and into [0,1]
    CcCurve_Move(e, 1, 0.9f, 1.5f);
    CHECK(Near(e.x[1], 0.5f - k_ccCurveMinGap) && Near(e.y[1], 1.f));
    CcCurve_Move(e, 1, -1.f, -1.f);
    CHECK(Near(e.x[1], k_ccCurveMinGap) && Near(e.y[1], 0.f));
    CcCurve_Move(e, 0, 0.3f, 0.2f);                         // end point can move inward
    CHECK(e.x[0] < e.x[1] && Near(e.y[0], 0.2f));
    CcCurve_Move(e, 99, 0.5f, 0.5f);                        // out of range: no-op, no crash

    // remove keeps at least two points
    CcCurve r;
    CHECK(!CcCurve_Remove(r, 0));
    CcCurve_Insert(r, 0.5f, 0.5f);
    CHECK(CcCurve_Remove(r, 1) && r.n == 2 && Near(r.x[1], 1.f));
    CHECK(r.x[2] == 0.f && r.y[2] == 0.f);
    CHECK(std::memcmp(&r, &id, sizeof(CcCurve)) == 0);      // back to a byte-identical default

    // config round trip
    CcCurve back;
    CHECK(CcCurve_Parse(CcCurve_ToString(s), back));
    CHECK(back.n == s.n);
    for (int i = 0; i < s.n; i++) CHECK(Near(back.x[i], s.x[i]) && Near(back.y[i], s.y[i]));

    // parse: sorts, clamps, drops near-duplicates
    CHECK(CcCurve_Parse("1,1;0,0;0.5,2", back));
    CHECK(back.n == 3 && Near(back.x[1], 0.5f) && Near(back.y[1], 1.f));
    CHECK(CcCurve_Parse("0,0;0.5,0.5;0.501,0.9;1,1", back) && back.n == 3);

    // parse: malformed input leaves the curve alone
    CcCurve keep = s;
    CHECK(!CcCurve_Parse("", keep));
    CHECK(!CcCurve_Parse("0,0", keep));                     // one point
    CHECK(!CcCurve_Parse("0,0;1", keep));
    CHECK(!CcCurve_Parse("0,0;abc,1", keep));
    CHECK(!CcCurve_Parse("0,0;1,1x", keep));
    CHECK(!CcCurve_Parse("0,0;nan,1;1,1", keep));
    std::string many;
    for (int i = 0; i <= k_ccCurveMaxPts; i++) many += (i ? ";" : "") + std::to_string(i / 20.f) + ",0.5";
    CHECK(!CcCurve_Parse(many, keep));
    CHECK(std::memcmp(&keep, &s, sizeof(CcCurve)) == 0);

    // legacy ccCurveOff migration: the dump in C:\infovisual\default.cfg (master Shd -0.04, Hlt +0.04)
    CcCurve leg;
    const float offs[5] = { 0.f, -0.0412614f, 0.f, 0.0412614f, 0.f };
    for (int i = 0; i < 5; i++) CcCurve_ApplyLegacyOffset(leg, i, offs[i]);
    CHECK(leg.n == 5);
    CHECK(Near(leg.x[1], 0.25f) && Near(leg.y[1], 0.25f - 0.0412614f));
    CHECK(Near(leg.x[3], 0.75f) && Near(leg.y[3], 0.75f + 0.0412614f));
    // all-zero legacy channel stays a clean 2-point identity, even over a custom curve
    CcCurve legZero = s;
    for (int i = 0; i < 5; i++) CcCurve_ApplyLegacyOffset(legZero, i, 0.f);
    CHECK(legZero.n == 2 && CcCurve_IsIdentity(legZero));
    // a non-zero offset after a zero run still lands
    CcCurve legLate;
    const float late[5] = { 0.f, 0.f, 0.f, 0.f, -0.2f };
    for (int i = 0; i < 5; i++) CcCurve_ApplyLegacyOffset(legLate, i, late[i]);
    CHECK(legLate.n == 5 && Near(legLate.y[4], 0.8f) && Near(legLate.y[2], 0.5f));

    // LUT: identity bakes to k -> k in every channel; master feeds the channel curves
    CcCurve chans[4];
    uint32_t lut[k_ccLutSize];
    CcCurve_BakeLut(chans, lut);
    for (int i = 0; i < k_ccLutSize; i++)
        CHECK(lut[i] == (0xFF000000u | ((uint32_t)i << 16) | ((uint32_t)i << 8) | (uint32_t)i));
    chans[0] = Make({ {0.f, 1.f}, {1.f, 0.f} });            // master invert
    chans[1] = Make({ {0.f, 0.f}, {1.f, 0.5f} });           // red halved
    CcCurve_BakeLut(chans, lut);
    CHECK(((lut[0] >> 16) & 0xFF) == 128);                  // 0 -> master 1 -> red 0.5
    CHECK(((lut[0] >> 8) & 0xFF) == 255);                   // green: master only
    CHECK((lut[255] & 0xFF) == 0);

    if (s_failed) { std::printf("cccurve_test: %d failed\n", s_failed); return 1; }
    std::printf("cccurve_test: all passed\n");
    return 0;
}
