#pragma once
// Edge WebView2 offscreen engine for "Web on surface". Why: Steam CEF runs --disable-gpu, no sw
// H.264 -> Twitch/TikTok dead. Falls back to Steam when runtime missing.
// One STA thread owns window (parked right of virtual desktop), controller, WGC capture. Any
// thread queues commands, polls events; frames -> sink as BGRA on capture thread.
// No game headers: tests/wv2_harness.cpp runs it outside TF2; own lib (DX SDK d3d11.h clash).
#include <cstdint>
#include <string>

namespace wv2 {

// BGRA top-down, capture thread; copy out, don't keep pointer
using FrameSink = void (*)(const uint8_t* bgra, int w, int h, int pitch);
// engine thread, before each top-level nav; true = cancel
using NavFilter = bool (*)(const char* url);

enum EvType {
    EV_READY,      // controller up: nav, scripts, input work
    EV_FAILED,     // couldn't start (a = reason); nothing else arrives
    EV_URL,        // a = new top-level URL (incl. pushState)
    EV_FINISHED,   // a = url, b = title, n = 1 ok / 0 failed (b = error status)
    EV_TITLE,      // a = title (also page->host channel, websurface k_probeJs)
    EV_CLEARED,    // ClearData done
    EV_CRASHED,    // a = what died; engine must be stopped + restarted
};
struct Event { EvType type; std::string a, b; int n = 0; };

enum Mouse { M_MOVE, M_LDOWN, M_LUP, M_RDOWN, M_RUP, M_WHEEL };
enum Key   { K_DOWN, K_UP, K_CHAR };

bool RuntimeVersion(char* out, size_t cap);   // false = WebView2 runtime not installed
bool Start(int w, int h, FrameSink sink, NavFilter filter, const char* extraArgs = nullptr);
void Stop();                                  // blocking, bounded wait; safe to call twice
bool Running();

// off = frames dropped before GPU readback; page keeps playing. Default on.
void SetCaptureEnabled(bool on);

void Navigate(const char* url);
void ExecJs(const char* js);
void AddStartScript(const char* js);          // every doc, at creation, before page scripts
void Resize(int w, int h);
void MouseInput(Mouse m, int x, int y, int wheelDelta = 0);
// K_DOWN/K_UP: VK. K_CHAR: UTF-16 unit; < 0x20 ignored (K_DOWN covers, VK_RETURN sends "\r").
// KMOD_* not MOD_*: those are Windows macros.
enum { KMOD_ALT = 1, KMOD_CTRL = 2, KMOD_SHIFT = 8 };   // CDP Input modifier bits
void KeyInput(Key k, uint32_t code, int mods = 0);
void Back();
void Forward();
void Reload();
void SetMuted(bool muted);
void ClearData();                             // every cookie (HttpOnly too), cache, storage, history

bool PollEvent(Event& out);
std::string Diag();                           // one line for log / menu

// capture pacing, pure (websurf_test fake clock). Fixed grid + period/4 slack: "period since last
// frame" dropped jittery early frames (30 fps on 60 Hz). Behind -> resync, no burst.
inline bool PaceDue(int64_t now, int64_t period, int64_t& next)
{
    if (now < next - period / 4) return false;
    next += period;
    if (next < now) next = now + period;
    return true;
}

} // namespace wv2
