// Gate test for the overlay animation clock (src/features/animclock.h).
// Build + run:  cmake --build build --config Release --target animclock_test && build\Release\animclock_test.exe
#include "../src/features/animclock.h"
#include <cmath>
#include <cstdio>

static int s_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static AnimClockIn Live(double real) { return { real, 0.f, 1.f, 1.f, false, false, false }; }

int main()
{
    // Live, sv_cheats 0: host_framerate and host_timescale do nothing to the game, so nothing here either.
    { AnimClockIn in = Live(1.0 / 60); in.hostFramerate = 240.f; in.hostTimescale = 0.25f;
      CHECK(Near(AnimClockStep(in), 1.0 / 60) && AnimClockMode(in) == 0); }

    // sv_cheats 1 + host_framerate 240 at 60 real fps (startmovie): each frame is 1/240 s of game time,
    // so a 1 s killfeed fade spans 240 frames = 4 s real, 1 s in the recording.
    { AnimClockIn in = Live(1.0 / 60); in.cheats = true; in.hostFramerate = 240.f;
      CHECK(Near(AnimClockStep(in), 1.0 / 240) && AnimClockMode(in) == 1);
      double t = 0; int frames = 0; while (t < 1.0 - 1e-9) { t += AnimClockStep(in); ++frames; }
      CHECK(frames == 240); }

    // host_framerate <= 1 is the frame time itself (engine: `comiss hf, 1.0; jbe` skips the divide).
    { AnimClockIn in = Live(0.5); in.cheats = true; in.hostFramerate = 0.01f;
      CHECK(std::fabs(AnimClockStep(in) - 0.01) < 1e-7); }
    { AnimClockIn in = Live(0.5); in.cheats = true; in.hostFramerate = 1.f;
      CHECK(Near(AnimClockStep(in), 1.0)); }

    // host_framerate wins over host_timescale (the engine's else-if), real dt is ignored entirely.
    { AnimClockIn in = Live(0.2); in.cheats = true; in.hostFramerate = 120.f; in.hostTimescale = 0.1f;
      CHECK(Near(AnimClockStep(in), 1.0 / 120)); }

    // host_timescale 0.5 with cheats: half speed; 0 or negative: real time.
    { AnimClockIn in = Live(0.02); in.cheats = true; in.hostTimescale = 0.5f;
      CHECK(Near(AnimClockStep(in), 0.01) && AnimClockMode(in) == 2);
      in.hostTimescale = 0.f;  CHECK(Near(AnimClockStep(in), 0.02) && AnimClockMode(in) == 0);
      in.hostTimescale = -2.f; CHECK(Near(AnimClockStep(in), 0.02)); }

    // A demo opens the gate without sv_cheats, and demo_timescale scales both gated branches.
    { AnimClockIn in = Live(0.02); in.demo = true; in.demoTimescale = 0.5f;
      CHECK(Near(AnimClockStep(in), 0.01));                        // host_timescale 1 (default) * demo 0.5
      in.hostFramerate = 60.f; CHECK(Near(AnimClockStep(in), 1.0 / 120));
      in.demoTimescale = 0.f;  CHECK(Near(AnimClockStep(in), 1.0 / 60));   // unreadable scale: 1
    }
    // Not a demo: demo_timescale is ignored.
    { AnimClockIn in = Live(0.02); in.cheats = true; in.demoTimescale = 4.f; CHECK(Near(AnimClockStep(in), 0.02)); }

    // Paused (or demo_pause): no time at all, whatever else is set.
    { AnimClockIn in = Live(0.02); in.cheats = true; in.hostFramerate = 240.f; in.paused = true;
      CHECK(AnimClockStep(in) == 0.0 && AnimClockMode(in) == 3); }

    // Real-time stalls are one capped frame; a clock that went backwards is no time.
    CHECK(Near(AnimClockStep(Live(5.0)), k_animMaxRealDt));
    CHECK(AnimClockStep(Live(-1.0)) == 0.0);
    { AnimClockIn in = Live(5.0); in.cheats = true; in.hostTimescale = 2.f; CHECK(Near(AnimClockStep(in), k_animMaxRealDt * 2)); }

    std::printf(s_failed ? "animclock_test: %d FAILED\n" : "animclock_test: all passed\n", s_failed);
    return s_failed ? 1 : 0;
}
