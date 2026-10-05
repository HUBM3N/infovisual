#pragma once

// Game-time clock for overlay animations (killfeed, sheen, weapon HUD, watermark, crosshair, ...), so a
// startmovie recording with host_framerate set shows them at playback speed and host_timescale / demo_timescale
// slow them down with the game. The menu and anything measuring the real world stay on real time.
//
// AnimClockStep is engine.dll's Host_AccumulateTime (rva 0x19ffe0, tools/check_vtables.py pins it), minus its
// real-time clamps:
//   gate = sv_cheats != 0 || demoplayer->IsPlayingBack()   (the same demoplayer vfunc IsPlayingDemo, 76, calls)
//   host_framerate > 0 && gate   frametime = hf > 1 ? 1 / hf : hf   (an fps, or the frame time itself if <= 1)
//   else host_timescale > 0 && gate   frametime = real * host_timescale
//   both gated branches then * demo playback timescale while a demo plays
//   else real
// IVEngineClient::IsPaused (84) covers pause and demo_pause (it asks the demoplayer too): no time passes.
namespace animslot {
    inline constexpr int engIsPlayingDemo = 76;   // IVEngineClient
    inline constexpr int engIsPaused      = 84;
}

struct AnimClockIn {
    double realDt;          // seconds since the previous overlay frame
    float  hostFramerate;   // host_framerate
    float  hostTimescale;   // host_timescale
    float  demoTimescale;   // demo_timescale
    bool   cheats;          // sv_cheats != 0
    bool   demo;            // playing a demo back
    bool   paused;
};

inline constexpr double k_animMaxRealDt = 0.25;   // a map load or alt-tab stall is one frame, not a jump

inline double AnimClockStep(const AnimClockIn& in)
{
    if (in.paused) return 0.0;
    double real = in.realDt > 0.0 ? in.realDt : 0.0;
    if (real > k_animMaxRealDt) real = k_animMaxRealDt;
    const bool   gate      = in.cheats || in.demo;
    const double demoScale = (in.demo && in.demoTimescale > 0.f) ? in.demoTimescale : 1.0;
    if (gate && in.hostFramerate > 0.f)
        return (in.hostFramerate > 1.f ? 1.0 / in.hostFramerate : in.hostFramerate) * demoScale;
    if (gate && in.hostTimescale > 0.f)
        return real * in.hostTimescale * demoScale;
    return real;
}

// What the clock is doing, for an edge-triggered log line. 0 real, 1 host_framerate, 2 host_timescale, 3 paused.
inline int AnimClockMode(const AnimClockIn& in)
{
    if (in.paused) return 3;
    const bool gate = in.cheats || in.demo;
    if (gate && in.hostFramerate > 0.f) return 1;
    if (gate && in.hostTimescale > 0.f) return 2;
    return 0;
}
