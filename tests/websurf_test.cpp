// Gate test for the web surface's engine-free math (src/features/websurf_math.h).
// Build + run:  cmake --build build --config Release --target websurf_test && build\Release\websurf_test.exe
//
// What it is here to catch: a screen whose text comes out backwards, a crosshair that clicks
// somewhere other than where it points, a rounded outline that walks off the quad, and the matrix
// transpose the fixed-function pipeline depends on.
#include "../src/features/websurf_math.h"
#include "../src/features/webview2host.h"
#include <cstdio>

static int s_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(float a, float b, float eps = 1e-4f) { return fabsf(a - b) < eps; }
static bool NearV(const WsVec3& a, const WsVec3& b, float eps = 1e-4f)
{
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

int main()
{
    // ---- basis on a wall ---------------------------------------------------------------
    // A wall whose face points at -Y: a player standing at -Y looks +Y at it. World up is
    // usable, so the screen must not be rolled, and +right must land on the player's right.
    {
        const WsVec3 n(0.f, -1.f, 0.f);
        const WsVec3 viewFwd(0.f, 1.f, 0.f);
        WsVec3 right, up;
        WsBasisFromNormal(n, viewFwd, right, up);

        CHECK(NearV(up, WsVec3(0.f, 0.f, 1.f)));      // upright
        CHECK(NearV(right, WsVec3(1.f, 0.f, 0.f)));   // the player's right, looking +Y with +Z up
        CHECK(Near(WsDot(right, up), 0.f));
        CHECK(Near(WsDot(right, n), 0.f));
        CHECK(Near(WsLen(right), 1.f));
        CHECK(Near(WsLen(up), 1.f));
        // right x up must give the normal back, or the quad faces away from the viewer.
        CHECK(NearV(WsCross(right, up), n));
    }

    // Same check on the other three walls: upright everywhere, never rolled.
    {
        const WsVec3 normals[4] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
        for (const WsVec3& n : normals) {
            WsVec3 right, up;
            WsBasisFromNormal(n, WsVec3(0.f, 0.f, -1.f), right, up);
            CHECK(NearV(up, WsVec3(0.f, 0.f, 1.f)));
            CHECK(NearV(WsCross(right, up), n));
        }
    }

    // ---- basis on a floor --------------------------------------------------------------
    // World up is parallel to the normal, so the view direction has to stand in for it. The
    // basis still has to be orthonormal and still has to face the viewer.
    {
        const WsVec3 n(0.f, 0.f, 1.f);
        const WsVec3 viewFwd = WsNorm(WsVec3(0.6f, 0.3f, -0.74f));   // looking down at it
        WsVec3 right, up;
        WsBasisFromNormal(n, viewFwd, right, up);

        CHECK(Near(WsLen(right), 1.f));
        CHECK(Near(WsLen(up), 1.f));
        CHECK(Near(WsDot(right, up), 0.f));
        CHECK(Near(WsDot(right, n), 0.f));
        CHECK(Near(WsDot(up, n), 0.f));
        CHECK(NearV(WsCross(right, up), n));
        // "Up" on the floor is the way the player is facing, flattened.
        CHECK(WsDot(up, WsNorm(WsVec3(viewFwd.x, viewFwd.y, 0.f))) > 0.999f);
    }

    // Looking straight down at a floor leaves nothing to flatten. The basis still has to be
    // orthonormal and still has to face up, or the screen collapses to nothing.
    {
        WsVec3 right, up;
        WsBasisFromNormal(WsVec3(0.f, 0.f, 1.f), WsVec3(0.f, 0.f, -1.f), right, up);
        CHECK(Near(WsLen(right), 1.f));
        CHECK(Near(WsLen(up), 1.f));
        CHECK(Near(WsDot(right, up), 0.f));
        CHECK(NearV(WsCross(right, up), WsVec3(0.f, 0.f, 1.f)));

        // Same on a ceiling, looking straight up at it.
        WsBasisFromNormal(WsVec3(0.f, 0.f, -1.f), WsVec3(0.f, 0.f, 1.f), right, up);
        CHECK(Near(WsLen(right), 1.f));
        CHECK(Near(WsDot(right, up), 0.f));
        CHECK(NearV(WsCross(right, up), WsVec3(0.f, 0.f, -1.f)));
    }

    // ---- crosshair hit test -------------------------------------------------------------
    {
        const WsVec3 p0(0.f, 0.f, 64.f);        // screen centre
        const WsVec3 n(0.f, -1.f, 0.f);
        const WsVec3 right(1.f, 0.f, 0.f), up(0.f, 0.f, 1.f);
        const float W = 128.f, H = 72.f;

        // Dead on the middle.
        {
            const WsVec3 ro(0.f, -100.f, 64.f), rd(0.f, 1.f, 0.f);
            float t; CHECK(WsRayPlane(ro, rd, p0, n, t));
            CHECK(Near(t, 100.f));
            float u, v; WsQuadUV(ro + rd * t, p0, right, up, W, H, u, v);
            CHECK(Near(u, 0.5f)); CHECK(Near(v, 0.5f));
            CHECK(WsInQuad(u, v));
        }
        // Top-left corner of the picture: -right, +up. v counts DOWN, like the browser does.
        {
            const WsVec3 hit = p0 + right * (-W * 0.5f) + up * (H * 0.5f);
            float u, v; WsQuadUV(hit, p0, right, up, W, H, u, v);
            CHECK(Near(u, 0.f)); CHECK(Near(v, 0.f));
            CHECK(WsInQuad(u, v));
        }
        // Bottom-right corner.
        {
            const WsVec3 hit = p0 + right * (W * 0.5f) + up * (-H * 0.5f);
            float u, v; WsQuadUV(hit, p0, right, up, W, H, u, v);
            CHECK(Near(u, 1.f)); CHECK(Near(v, 1.f));
            CHECK(WsInQuad(u, v));
        }
        // Just past the edge is a miss, so the click never lands on a page that is not there.
        {
            const WsVec3 hit = p0 + right * (W * 0.6f);
            float u, v; WsQuadUV(hit, p0, right, up, W, H, u, v);
            CHECK(!WsInQuad(u, v));
        }
        // Parallel ray, and a screen behind the viewer: both are misses.
        {
            float t;
            CHECK(!WsRayPlane(WsVec3(0, -100, 64), WsVec3(1, 0, 0), p0, n, t));
            CHECK(!WsRayPlane(WsVec3(0, 100, 64), WsVec3(0, 1, 0), p0, n, t));
        }
    }

    // ---- the quad's own points ------------------------------------------------------------
    // Corners must land exactly where the hit test says they are, or the picture and the
    // crosshair disagree about where a pixel is.
    {
        const WsVec3 p0(0.f, 0.f, 64.f), right(1.f, 0.f, 0.f), up(0.f, 0.f, 1.f);
        const float W = 128.f, H = 72.f;

        CHECK(NearV(WsQuadPoint(0.f, 0.f, p0, right, up, W, H),
                    p0 + right * (-W * 0.5f) + up * (H * 0.5f)));
        CHECK(NearV(WsQuadPoint(1.f, 1.f, p0, right, up, W, H),
                    p0 + right * (W * 0.5f) + up * (-H * 0.5f)));
        CHECK(NearV(WsQuadPoint(0.5f, 0.5f, p0, right, up, W, H), p0));

        for (int i = 0; i <= 10; ++i)
            for (int j = 0; j <= 10; ++j) {
                const float u = i / 10.f, v = j / 10.f;
                float qu, qv;
                WsQuadUV(WsQuadPoint(u, v, p0, right, up, W, H), p0, right, up, W, H, qu, qv);
                CHECK(Near(u, qu)); CHECK(Near(v, qv));
            }
    }

    // ---- the rounded outline ----------------------------------------------------------------
    {
        const int seg = 8;
        const int cap = 4 * (seg + 1);
        float uv[4 * (8 + 1) * 2];

        // Zero radius is the plain rectangle: four corners, in walk order.
        int n = WsRoundedRectOutline(0.f, 0.f, seg, uv, cap);
        CHECK(n == 4);
        CHECK(Near(uv[0], 0.f) && Near(uv[1], 0.f));   // top-left
        CHECK(Near(uv[2], 1.f) && Near(uv[3], 0.f));   // top-right
        CHECK(Near(uv[4], 1.f) && Near(uv[5], 1.f));   // bottom-right
        CHECK(Near(uv[6], 0.f) && Near(uv[7], 1.f));   // bottom-left

        // seg 0 must not divide by zero -- it falls back to the square.
        CHECK(WsRoundedRectOutline(0.25f, 0.25f, 0, uv, cap) == 4);

        // A real radius: every point inside the unit square, none in a cut corner, and the
        // walk closes without repeating its start.
        n = WsRoundedRectOutline(0.25f, 0.25f, seg, uv, cap);
        CHECK(n == cap);
        for (int i = 0; i < n; ++i) {
            const float u = uv[i * 2], v = uv[i * 2 + 1];
            CHECK(u >= -1e-4f && u <= 1.0001f);
            CHECK(v >= -1e-4f && v <= 1.0001f);
            // Outside every arc centre by more than its radius would mean a bulge, not a round.
            const float cx[4] = { 0.25f, 0.75f, 0.75f, 0.25f };
            const float cy[4] = { 0.25f, 0.25f, 0.75f, 0.75f };
            for (int c = 0; c < 4; ++c) {
                const bool inCornerBox = ((c == 0 || c == 3) ? u < 0.25f : u > 0.75f) &&
                                         ((c == 0 || c == 1) ? v < 0.25f : v > 0.75f);
                if (!inCornerBox) continue;
                const float dx = u - cx[c], dy = v - cy[c];
                CHECK(sqrtf(dx * dx + dy * dy) <= 0.25f + 1e-3f);
            }
        }
        // The square corners themselves are gone once it is rounded.
        for (int i = 0; i < n; ++i) {
            const float u = uv[i * 2], v = uv[i * 2 + 1];
            CHECK(!((u < 1e-3f && v < 1e-3f) || (u > 1.f - 1e-3f && v < 1e-3f) ||
                    (u > 1.f - 1e-3f && v > 1.f - 1e-3f) || (u < 1e-3f && v > 1.f - 1e-3f)));
        }

        // The radius is clamped, so a slider past half the screen cannot fold the outline
        // inside out.
        n = WsRoundedRectOutline(5.f, 5.f, seg, uv, cap);
        CHECK(n == cap);
        for (int i = 0; i < n; ++i) {
            CHECK(uv[i * 2] >= -1e-4f && uv[i * 2] <= 1.0001f);
            CHECK(uv[i * 2 + 1] >= -1e-4f && uv[i * 2 + 1] <= 1.0001f);
        }

        // A buffer too small truncates instead of writing past the end.
        CHECK(WsRoundedRectOutline(0.25f, 0.25f, seg, uv, 5) == 5);
        CHECK(WsRoundedRectOutline(0.f, 0.f, seg, uv, 3) == 0);

        // Non-square screen: the radii differ per axis, and the walk still stays in bounds.
        n = WsRoundedRectOutline(8.f / 128.f, 8.f / 72.f, seg, uv, cap);
        CHECK(n == cap);
        for (int i = 0; i < n; ++i) {
            CHECK(uv[i * 2] >= -1e-4f && uv[i * 2] <= 1.0001f);
            CHECK(uv[i * 2 + 1] >= -1e-4f && uv[i * 2 + 1] <= 1.0001f);
        }
    }

    // ---- the matrix the pipeline gets ----------------------------------------------------
    {
        float m[16], t[16];
        for (int i = 0; i < 16; ++i) m[i] = (float)i;
        WsTranspose4(m, t);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK(Near(t[r * 4 + c], m[c * 4 + r]));
        WsTranspose4(t, m);
        for (int i = 0; i < 16; ++i) CHECK(Near(m[i], (float)i));   // twice is identity

        // A view-projection carries the view axis in its bottom row; a world-to-view matrix
        // does not, and refusing that is what turns a wrong vtable slot into "nothing drawn".
        float vp[16]  = { 1,0,0,0, 0,1,0,0, 0,0,1,1, 0,0,-1,0 };
        float w2v[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        CHECK(WsIsProjection(vp));
        CHECK(!WsIsProjection(w2v));
    }

    // ---- AngleVectors ---------------------------------------------------------------------
    // Straight ahead down +X, and straight up, both in Source's own convention.
    {
        WsVec3 f, r, u;
        WsAngleVectors(WsVec3(0.f, 0.f, 0.f), f, r, u);
        CHECK(NearV(f, WsVec3(1.f, 0.f, 0.f)));
        WsAngleVectors(WsVec3(-90.f, 0.f, 0.f), f, r, u);
        CHECK(NearV(f, WsVec3(0.f, 0.f, 1.f)));
        WsAngleVectors(WsVec3(0.f, 90.f, 0.f), f, r, u);
        CHECK(NearV(f, WsVec3(0.f, 1.f, 0.f)));
    }

    // ---- aspect presets: pixels and world size share one shape ---------------------------
    {
        int w = 0, h = 0;
        WsAspectRes(0, 720, w, h);  CHECK(w == 1280 && h == 720);    // 16:9
        WsAspectRes(0, 480, w, h);  CHECK(w == 854 && h == 480);     // the old 854x480 preset
        WsAspectRes(0, 1080, w, h); CHECK(w == 1920 && h == 1080);
        WsAspectRes(1, 720, w, h);  CHECK(w == 720 && h == 1280);    // 9:16 portrait
        WsAspectRes(2, 720, w, h);  CHECK(w == 960 && h == 720);     // 4:3
        WsAspectRes(3, 720, w, h);  CHECK(w == 720 && h == 960);     // 3:4
        WsAspectRes(4, 1080, w, h); CHECK(w == 2520 && h == 1080);   // 21:9
        WsAspectRes(5, 720, w, h);  CHECK(w == 720 && h == 720);     // 1:1
        WsAspectRes(99, 720, w, h); CHECK(w == 1280 && h == 720);    // bad config value -> 16:9
        WsAspectRes(-1, 720, w, h); CHECK(w == 1280 && h == 720);
        for (int a = 0; a < k_wsAspectCount; ++a)
            for (int s : { 480, 720, 1080 }) {
                WsAspectRes(a, s, w, h);
                CHECK((w & 1) == 0 && (h & 1) == 0);
                CHECK((w < h ? w : h) == s);                         // the preset IS the short side
            }

        float fw = 0.f, fh = 0.f;
        WsAspectDims(0, 1.f, fw, fh); CHECK(Near(fw, 128.f) && Near(fh, 72.f));   // old default
        WsAspectDims(1, 1.f, fw, fh); CHECK(Near(fw, 72.f) && Near(fh, 128.f));
        WsAspectDims(4, 2.f, fw, fh); CHECK(Near(fw, 256.f) && Near(fh, 256.f * 9.f / 21.f));
        WsAspectDims(5, 0.5f, fw, fh); CHECK(Near(fw, 64.f) && Near(fh, 64.f));
        WsAspectDims(0, 0.f, fw, fh); CHECK(fw > 0.f && fh > 0.f);                 // never a 0-size quad
        // Pixels and world agree on the shape, so the pointer maps 1:1 onto the page.
        for (int a = 0; a < k_wsAspectCount; ++a) {
            WsAspectRes(a, 720, w, h); WsAspectDims(a, 1.f, fw, fh);
            CHECK(Near((float)w / (float)h, fw / fh, 0.01f));
        }
    }

    // ---- address bar: WebView2 only navigates absolute URLs -----------------------------
    {
        CHECK(WsNormalizeUrl("google.com") == "https://google.com");
        CHECK(WsNormalizeUrl("  en.wikipedia.org/wiki/Team_Fortress_2 ") == "https://en.wikipedia.org/wiki/Team_Fortress_2");
        CHECK(WsNormalizeUrl("https://www.twitch.tv/") == "https://www.twitch.tv/");
        CHECK(WsNormalizeUrl("HTTP://Example.com") == "HTTP://Example.com");
        CHECK(WsNormalizeUrl("about:blank") == "about:blank");
        CHECK(WsNormalizeUrl("data:text/html,hi") == "data:text/html,hi");
        CHECK(WsNormalizeUrl("localhost:8080/x") == "https://localhost:8080/x");
        CHECK(WsNormalizeUrl("site.com:443/a") == "https://site.com:443/a");
        CHECK(WsNormalizeUrl("tf2 frag video") == "https://www.google.com/search?q=tf2+frag+video");
        CHECK(WsNormalizeUrl("wikipedia") == "https://www.google.com/search?q=wikipedia");
        CHECK(WsNormalizeUrl("c++ & c#") == "https://www.google.com/search?q=c%2B%2B+%26+c%23");
        CHECK(WsNormalizeUrl("what is 2.5 rounded") == "https://www.google.com/search?q=what+is+2.5+rounded");
        CHECK(WsNormalizeUrl("") == "");
        CHECK(WsNormalizeUrl("   ") == "");
    }

    // ---- capture pacing (wv2::PaceDue) ------------------------------------------------------
    // 60 fps cap, clock in microseconds, 2 s of compositor frames. The old rule ("next = now +
    // period") is run alongside to prove these inputs are the ones that broke it.
    {
        const int64_t P = 1000000 / 60;
        auto run = [&](double hz, int jitterUs, bool oldRule) {
            int64_t next = 0; int taken = 0;
            const int n = (int)(hz * 2.0);
            for (int i = 0; i < n; ++i) {
                const int64_t t = 5000000 + (int64_t)(i * 1e6 / hz) + ((i & 1) ? -jitterUs : jitterUs);
                if (oldRule) { if (t >= next) { next = t + P; ++taken; } }
                else if (wv2::PaceDue(t, P, next)) ++taken;
            }
            return taken;
        };
        const int new60 = run(60.0, 150, false), old60 = run(60.0, 150, true);
        const int new144 = run(144.0, 0, false), old144 = run(144.0, 0, true);
        const int new240 = run(240.0, 300, false), new30 = run(30.0, 500, false);
        std::printf("pacing frames/2s: 60Hz new=%d old=%d | 144Hz new=%d old=%d | 240Hz=%d 30Hz=%d\n",
                    new60, old60, new144, old144, new240, new30);
        CHECK(new60 >= 118);                     // every frame of a jittery 60 Hz source
        CHECK(old60 <= 70);                      // the bug: half of them
        CHECK(new144 >= 115 && new144 <= 121);   // 60 a second off a 144 Hz screen, not 48
        CHECK(old144 <= 100);
        CHECK(new240 >= 115 && new240 <= 121);   // never above the cap
        CHECK(new30 == 60);                      // slow source: every frame, no catch-up burst
    }

    if (s_failed == 0) std::printf("websurf_test: all checks passed\n");
    else               std::printf("websurf_test: %d FAILED\n", s_failed);
    return s_failed ? 1 : 0;
}
