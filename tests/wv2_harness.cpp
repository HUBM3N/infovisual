// Integration harness for src/features/webview2host.cpp -- the WebView2 engine, outside TF2.
// Not a gate test (needs the WebView2 runtime + network, takes seconds). Run before shipping
// engine changes:
//   wv2_harness play  <url> <seconds> <out.bmp> [smooth]   PASS = a <video> advanced, frames arrived
//                                                  (smooth: inject k_wsSmoothJs, prints q=<target>)
//   wv2_harness input                              PASS = click + typed keys reached the page
//   wv2_harness hover                              PASS = resting pointer stays hovered, parking ends it, right click
//   wv2_harness media                              PASS = play/pause scripts work, main video end raises __fpsended
//   wv2_harness playpause <url>                    PASS = Remove pauses, Place plays (and from 0) on a real site
//   wv2_harness lifecycle                         PASS = resize, clear, fast stop, restart
// Muted throughout. Prints every engine event.
#include "features/webview2host.h"
#include "features/websurf_js.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

static std::mutex           g_lock;
static std::vector<uint8_t> g_px;
static int                  g_w = 0, g_h = 0;
static long                 g_sinkCalls = 0;

static void Sink(const uint8_t* p, int w, int h, int pitch)
{
    std::lock_guard<std::mutex> lk(g_lock);
    g_px.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) memcpy(&g_px[(size_t)y * w * 4], p + (size_t)y * pitch, (size_t)w * 4);
    g_w = w; g_h = h; ++g_sinkCalls;
}

static bool Blocked(const char* url) { return strstr(url, "blocked.invalid") != nullptr; }

static void SaveBmp(const char* path)
{
    std::lock_guard<std::mutex> lk(g_lock);
    if (g_px.empty()) return;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih); ih.biWidth = g_w; ih.biHeight = -g_h; ih.biPlanes = 1; ih.biBitCount = 32;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + (DWORD)g_px.size();
    fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(g_px.data(), 1, g_px.size(), f);
    fclose(f);
}

// FNV over a sparse sample of the frame: changes when the picture changes.
static unsigned FrameHash(double* meanLuma)
{
    std::lock_guard<std::mutex> lk(g_lock);
    unsigned h = 2166136261u; double sum = 0; int n = 0;
    for (size_t i = 0; i + 3 < g_px.size(); i += 4 * 997) {
        h = (h ^ g_px[i]) * 16777619u; h = (h ^ g_px[i + 1]) * 16777619u; h = (h ^ g_px[i + 2]) * 16777619u;
        sum += 0.114 * g_px[i] + 0.587 * g_px[i + 1] + 0.299 * g_px[i + 2]; ++n;
    }
    if (meanLuma) *meanLuma = n ? sum / n : 0;
    return h;
}

static std::string g_lastProbe, g_lastTitle;
static bool g_ready = false, g_failed = false;

static void Pump(int ms)
{
    const DWORD end = GetTickCount() + ms;
    do {
        wv2::Event e;
        while (wv2::PollEvent(e)) {
            switch (e.type) {
            case wv2::EV_READY:    g_ready = true; printf("[ev] READY\n"); break;
            case wv2::EV_FAILED:   g_failed = true; printf("[ev] FAILED %s\n", e.a.c_str()); break;
            case wv2::EV_URL:      printf("[ev] URL %.120s\n", e.a.c_str()); break;
            case wv2::EV_FINISHED: printf("[ev] FINISHED ok=%d %.100s title=\"%.60s\"\n", e.n, e.a.c_str(), e.b.c_str()); break;
            case wv2::EV_CLEARED:  printf("[ev] CLEARED %s\n", e.a.c_str()); break;
            case wv2::EV_CRASHED:  printf("[ev] CRASHED %s\n", e.a.c_str()); break;
            case wv2::EV_TITLE:
                if (!strncmp(e.a.c_str(), "__h|", 4)) g_lastProbe = e.a.substr(4);
                else g_lastTitle = e.a;
                break;
            }
        }
        Sleep(15);
    } while ((int)(end - GetTickCount()) > 0);
}

static const char* k_videoProbe =
    "(function(){var v=[].slice.call(document.querySelectorAll('video'));"
    "var s=v.map(function(e){return e.readyState+'/'+e.currentTime.toFixed(2)+'/'+(e.paused?'p':'r')+'/err'"
    "+(e.error?e.error.code:0)+'/'+e.videoWidth+'x'+e.videoHeight;}).join(';')||'novideo';"
    "s+=' q='+(window.__fpsQState||'-');"
    "var o=document.title;document.title='__h|'+s;setTimeout(function(){document.title=o;},50);})()";

// Largest currentTime seen across the page's videos in a probe line.
static double MaxTime(const std::string& p)
{
    double best = -1; const char* s = p.c_str();
    while (*s) {
        double t; int rs;
        if (sscanf(s, "%d/%lf", &rs, &t) == 2 && t > best) best = t;
        const char* n = strchr(s, ';'); if (!n) break; s = n + 1;
    }
    return best;
}

static int Play(const char* url, int secs, const char* bmp, bool smooth)
{
    char ver[64] = "";
    if (!wv2::RuntimeVersion(ver, sizeof(ver))) { printf("FAIL: WebView2 runtime not installed\n"); return 2; }
    printf("runtime %s\n", ver);
    wv2::Start(1280, 720, &Sink, &Blocked);
    wv2::SetMuted(true);
    if (smooth) wv2::AddStartScript(k_wsSmoothJs);   // what the game injects with "Smooth video" on
    wv2::Navigate(url);
    double firstT = -1, lastT = -1; unsigned firstHash = 0; int hashChanges = 0; unsigned prevHash = 0;
    for (int t = 0; t < secs && !g_failed; t += 2) {
        Pump(2000);
        wv2::ExecJs(k_videoProbe);
        Pump(300);
        double luma = 0; unsigned h = FrameHash(&luma);
        if (prevHash && h != prevHash) ++hashChanges;
        prevHash = h;
        const double mt = MaxTime(g_lastProbe);
        if (mt >= 0 && firstT < 0) firstT = mt;
        if (mt >= 0) lastT = mt;
        printf("t=%2ds frames=%ld %dx%d luma=%.0f hash=%08x video=%s | %s\n", t + 2, g_sinkCalls, g_w, g_h, luma, h,
               g_lastProbe.c_str(), wv2::Diag().c_str());
        (void)firstHash;
    }
    SaveBmp(bmp);
    wv2::Stop();
    const bool advanced = lastT > firstT + 1.0;
    printf("RESULT video %.2f -> %.2f (%s), picture changed %d times, frames %ld, bmp %s\n",
           firstT, lastT, advanced ? "PLAYING" : "NOT PLAYING", hashChanges, g_sinkCalls, bmp);
    const bool pass = !g_failed && g_sinkCalls > 0 && advanced;
    printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// A local page that writes what it receives into its title.
static const char* k_inputPage =
    "data:text/html,<html><head><title>start</title></head><body style='margin:0'>"
    "<form onsubmit=\"L('submit');return false\">"
    "<input id=i style='position:absolute;left:100px;top:100px;width:300px;height:40px'></form>"
    "<div style='height:3000px'></div><script>"
    "var log=[];function L(s){log.push(s);document.title='__h|'+log.join(',');}"
    "document.addEventListener('mousedown',function(e){L('down@'+e.clientX+'x'+e.clientY);});"
    "document.addEventListener('click',function(e){L('click');});"
    "document.getElementById('i').addEventListener('input',function(e){L('val='+e.target.value);});"
    "document.addEventListener('keydown',function(e){L('kd'+e.keyCode);});"
    "window.addEventListener('scroll',function(){L('scroll'+Math.round(scrollY));},{once:true});"
    "</script></body></html>";

static int Input()
{
    wv2::Start(800, 600, &Sink, &Blocked);
    wv2::Navigate(k_inputPage);
    Pump(4000);
    const HWND fgBefore = GetForegroundWindow();
    wv2::MouseInput(wv2::M_MOVE, 150, 120);
    wv2::MouseInput(wv2::M_LDOWN, 150, 120);
    wv2::MouseInput(wv2::M_LUP, 150, 120);
    Pump(500);
    wv2::KeyInput(wv2::K_DOWN, 'A'); wv2::KeyInput(wv2::K_CHAR, 'a'); wv2::KeyInput(wv2::K_UP, 'A');
    wv2::KeyInput(wv2::K_DOWN, VK_SHIFT, wv2::KMOD_SHIFT);
    wv2::KeyInput(wv2::K_DOWN, 'B', wv2::KMOD_SHIFT); wv2::KeyInput(wv2::K_CHAR, 'B', wv2::KMOD_SHIFT);
    wv2::KeyInput(wv2::K_UP, 'B', wv2::KMOD_SHIFT);   wv2::KeyInput(wv2::K_UP, VK_SHIFT);
    wv2::KeyInput(wv2::K_DOWN, VK_BACK); wv2::KeyInput(wv2::K_CHAR, 8); wv2::KeyInput(wv2::K_UP, VK_BACK);
    wv2::KeyInput(wv2::K_DOWN, 'C'); wv2::KeyInput(wv2::K_CHAR, 'c'); wv2::KeyInput(wv2::K_UP, 'C');
    wv2::KeyInput(wv2::K_DOWN, VK_RETURN); wv2::KeyInput(wv2::K_UP, VK_RETURN);   // like the panel: no char
    Pump(500);
    wv2::MouseInput(wv2::M_WHEEL, 400, 300, -240);
    Pump(1000);
    // Navigation filter: must be cancelled, page must stay.
    wv2::Navigate("https://blocked.invalid/");
    Pump(1000);
    const HWND fgAfter = GetForegroundWindow();
    wv2::Stop();
    auto who = [](HWND h) {
        static char b[2][200]; static int k = 0; char* o = b[k ^= 1];
        char cls[64] = ""; DWORD pid = 0;
        if (h) { GetClassNameA(h, cls, 64); GetWindowThreadProcessId(h, &pid); }
        snprintf(o, 200, "%p class=%s pid=%lu%s", (void*)h, cls, pid, pid == GetCurrentProcessId() ? " (THIS process)" : "");
        return (const char*)o;
    };
    // "Stole focus" = the foreground is now ours or the browser's. Anything else moving is the
    // person at the desk (seen: Opera <-> VS Code mid-run).
    auto ours = [](HWND h) {
        DWORD pid = 0;
        if (!h || !GetWindowThreadProcessId(h, &pid)) return false;
        if (pid == GetCurrentProcessId()) return true;
        char path[MAX_PATH] = ""; DWORD n = MAX_PATH;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (p) { QueryFullProcessImageNameA(p, 0, path, &n); CloseHandle(p); }
        return strstr(path, "msedgewebview2") != nullptr;
    };
    const bool stole = ours(fgAfter) && !ours(fgBefore);
    printf("page log: %s\nfocus stolen: %s\n  before %s\n  after  %s\n", g_lastProbe.c_str(),
           stole ? "YES" : "no", who(fgBefore), who(fgAfter));
    const bool click = g_lastProbe.find("down@150x120") != std::string::npos && g_lastProbe.find("click") != std::string::npos;
    // a, Shift+B, Backspace, c -> "ac", every step once (no doubled chars).
    const bool typed = g_lastProbe.find("val=a,") != std::string::npos && g_lastProbe.find("val=aB,") != std::string::npos &&
                       g_lastProbe.find("val=ac") != std::string::npos && g_lastProbe.find("val=aa") == std::string::npos;
    const bool wheel = g_lastProbe.find("scroll") != std::string::npos;
    const bool enter = g_lastProbe.find("submit") != std::string::npos;
    printf("click=%d typed=%d enter=%d wheel=%d frames=%ld\n", click, typed, enter, wheel, g_sinkCalls);
    const bool pass = click && typed && enter && wheel && !stole && g_sinkCalls > 0;
    printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// Hover: the crosshair resting on the screen sends the same move every frame. The page must
// see the pointer enter and STAY (no mouseleave), :hover must hold, and a move off the view
// (-1,-1, click-only parking) must end the hover. Also a right click reaches the page.
static const char* k_hoverPage =
    "data:text/html,<html><head><title>start</title><style>%23b:hover{background:%23f00!important}</style></head><body style='margin:0'>"
    "<div id=b style='position:absolute;left:100px;top:100px;width:300px;height:200px;background:%23333'></div><script>"
    "var n={over:0,leave:0,ctx:0,rdown:0};var b=document.getElementById('b');"
    "b.addEventListener('mouseover',function(){n.over++;});b.addEventListener('mouseleave',function(){n.leave++;});"
    "document.addEventListener('mousedown',function(e){if(e.button==2)n.rdown++;});"
    "document.addEventListener('contextmenu',function(e){n.ctx++;e.preventDefault();});"
    "setInterval(function(){document.title='__h|over='+n.over+',leave='+n.leave"
    "+',rdown='+n.rdown+',ctx='+n.ctx;},50);"
    "</script></body></html>";

// The captured pixel is the ground truth for CSS :hover (element.matches(':hover') read 0 from a
// timer here while the red :hover rule was plainly painted).
static int RedAt(int x, int y)
{
    std::lock_guard<std::mutex> lk(g_lock);
    if (x >= g_w || y >= g_h) return -1;
    const uint8_t* q = &g_px[((size_t)y * g_w + x) * 4];
    return q[2] > 200 && q[1] < 60 && q[0] < 60;
}

static int Hover()
{
    wv2::Start(800, 600, &Sink, &Blocked);
    wv2::Navigate(k_hoverPage);
    Pump(4000);
    for (int i = 0; i < 90; ++i) { wv2::MouseInput(wv2::M_MOVE, 200, 180); Pump(16); }
    Pump(300);
    const std::string resting = g_lastProbe;
    const int red = RedAt(200, 180);
    wv2::MouseInput(wv2::M_RDOWN, 200, 180); wv2::MouseInput(wv2::M_RUP, 200, 180);
    Pump(400);
    const std::string right = g_lastProbe;
    wv2::MouseInput(wv2::M_MOVE, -1, -1);
    Pump(400);
    const std::string parked = g_lastProbe;
    const int redParked = RedAt(200, 180);
    wv2::Stop();
    printf("resting: %s red=%d\nright:   %s\nparked:  %s red=%d\n", resting.c_str(), red, right.c_str(),
           parked.c_str(), redParked);
    const bool held  = resting.find("over=1,leave=0") != std::string::npos && red == 1;
    const bool rclk  = right.find("rdown=1,ctx=1") != std::string::npos;
    const bool left  = parked.find("leave=1") != std::string::npos && redParked == 0;
    printf("hover held=%d right click=%d park ends hover=%d\n", held, rclk, left);
    const bool pass = held && rclk && left;
    printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// websurf_js.h's media scripts on a real 5s clip: pause stops it, play starts it, and the clip
// running out raises the "__fpsended" title (auto remove). A small muted preview video on the
// same page ending must NOT raise it (only the biggest video counts).
static int Media()
{
    const char* page =
        "data:text/html,<html><head><title>start</title></head><body style='margin:0'>"
        "<video id=m muted playsinline style='width:640px;height:360px' "
        "src='https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4'></video>"
        "<video id=p muted autoplay playsinline style='width:64px;height:36px' "
        "src='https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.webm'></video>"
        "</body></html>";
    wv2::Start(800, 600, &Sink, &Blocked);
    wv2::AddStartScript(k_wsEndedJs);
    wv2::Navigate(page);
    Pump(1500);   // engine up, page barely started: the clip has no data yet (the "place twice" race)
    const char* state = "(function(){var m=document.getElementById('m');var o=document.title;"
                        "document.title='__h|paused='+(m.paused?1:0)+',t='+m.currentTime.toFixed(1);"
                        "setTimeout(function(){document.title=o;},50);})()";
    wv2::ExecJs(k_wsPlayJs);    Pump(3500); wv2::ExecJs(state); Pump(300);
    const std::string afterPlay = g_lastProbe;
    wv2::ExecJs(k_wsPauseJs);   Pump(800);  wv2::ExecJs(state); Pump(300);
    const std::string afterPause = g_lastProbe;
    wv2::ExecJs("window.__fpsPlayFrom0=1"); wv2::ExecJs(k_wsPlayJs); Pump(700); wv2::ExecJs(state); Pump(300);
    const std::string afterFrom0 = g_lastProbe;
    wv2::ExecJs(k_wsPauseJs);   Pump(500);
    // The preview (autoplay) ended during the wait above without raising the tag; now let the
    // main clip run out.
    const bool earlyTag = g_lastTitle == k_wsEndedTag;
    g_lastTitle.clear();
    bool ended = false;
    wv2::ExecJs(k_wsPlayJs);
    for (int i = 0; i < 60 && !ended; ++i) {
        wv2::Event e;
        while (wv2::PollEvent(e)) if (e.type == wv2::EV_TITLE && e.a == k_wsEndedTag) ended = true;
        Sleep(100);
    }
    wv2::Stop();
    printf("after play: %s\nafter pause: %s\nafter from0: %s\npreview end raised tag: %d\nmain end raised tag: %d\n",
           afterPlay.c_str(), afterPause.c_str(), afterFrom0.c_str(), earlyTag, ended);
    const bool played = afterPlay.find("paused=0") != std::string::npos;
    const bool paused = afterPause.find("paused=1") != std::string::npos;
    double t0 = 99; const char* tp = strstr(afterFrom0.c_str(), "t="); if (tp) t0 = atof(tp + 2);
    const bool from0 = afterFrom0.find("paused=0") != std::string::npos && t0 < 1.2;
    const bool pass = played && paused && from0 && ended && !earlyTag;
    printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// Pause then play (then play from 0) on a real site player -- what Remove / Place do in game.
static int PlayPause(const char* url)
{
    const char* state = "(function(){var v=null,a=0;[].slice.call(document.querySelectorAll('video')).forEach(function(e){"
                        "var r=e.getBoundingClientRect(),s=r.width*r.height;if(s>a){a=s;v=e;}});var o=document.title;"
                        "document.title='__h|'+(v?'paused='+(v.paused?1:0)+',t='+v.currentTime.toFixed(1):'novideo');"
                        "setTimeout(function(){document.title=o;},50);})()";
    wv2::Start(1280, 720, &Sink, &Blocked);
    wv2::SetMuted(true);
    wv2::Navigate(url);
    Pump(10000);
    wv2::ExecJs(k_wsPauseJs); Pump(1500); wv2::ExecJs(state); Pump(300);
    const std::string p1 = g_lastProbe;
    wv2::ExecJs(k_wsPlayJs);  Pump(3000); wv2::ExecJs(state); Pump(300);
    const std::string p2 = g_lastProbe;
    wv2::ExecJs(k_wsPauseJs); Pump(1000);
    wv2::ExecJs("window.__fpsPlayFrom0=1"); wv2::ExecJs(k_wsPlayJs); Pump(3000); wv2::ExecJs(state); Pump(300);
    const std::string p3 = g_lastProbe;
    wv2::Stop();
    printf("paused: %s | played: %s | from0: %s\n", p1.c_str(), p2.c_str(), p3.c_str());
    double t3 = 99; const char* tp = strstr(p3.c_str(), "t="); if (tp) t3 = atof(tp + 2);
    const bool pass = p1.find("paused=1") != std::string::npos && p2.find("paused=0") != std::string::npos &&
                      p3.find("paused=0") != std::string::npos;
    printf("from0 t=%.1f (live streams keep their position)\n%s\n", t3, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// Resize follows into the captured frames; ClearData answers; Stop is quick (websurface calls it
// on the game's main thread); a second Start in the same process works (Clear, crash, toggle).
static int Lifecycle()
{
    bool ok = true;
    for (int round = 1; round <= 2; ++round) {
        g_ready = false; g_w = g_h = 0;
        wv2::Start(1280, 720, &Sink, &Blocked);
        wv2::Navigate("data:text/html,<body style='background:%23c33'>x</body>");
        Pump(3000);
        const bool r1 = g_ready && g_w == 1280 && g_h == 720;
        wv2::Resize(640, 360);
        Pump(1500);
        const bool r2 = g_w == 640 && g_h == 360;
        bool cleared = false;
        wv2::ClearData();
        for (int i = 0; i < 50 && !cleared; ++i) {
            wv2::Event e;
            while (wv2::PollEvent(e)) if (e.type == wv2::EV_CLEARED) { cleared = e.a == "ok"; printf("[ev] CLEARED %s\n", e.a.c_str()); }
            Sleep(100);
        }
        const DWORD t0 = GetTickCount();
        wv2::Stop();
        const DWORD stopMs = GetTickCount() - t0;
        printf("round %d: ready@1280x720=%d resize->640x360=%d (got %dx%d) cleared=%d stop=%lums running=%d\n",
               round, r1, r2, g_w, g_h, cleared, stopMs, (int)wv2::Running());
        ok = ok && r1 && r2 && cleared && stopMs < 1500 && !wv2::Running();
    }
    // Stop() before the engine has its window: the thread must still exit, not sit in GetMessage.
    for (int i = 0; i < 3; ++i) {
        const DWORD t0 = GetTickCount();
        wv2::Start(640, 360, &Sink, &Blocked);
        wv2::Stop();
        const DWORD ms = GetTickCount() - t0;
        const bool exited = wv2::Diag().find("did not exit") == std::string::npos;
        printf("early stop %d: %lums thread exited=%d\n", i, ms, exited);
        ok = ok && exited && ms < 1500;
    }
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// Capture gate + pacing on a page that repaints every frame. Off: the sink must go quiet while
// the page keeps running. On: frames again, never above the 60/s cap.
static const char* k_animPage =
    "data:text/html,<html><body style='margin:0'><canvas id=c width=64 height=64></canvas><script>"
    "var n=0,x=document.getElementById('c').getContext('2d');"
    "(function f(){x.fillStyle='rgb('+(n++%25256)+',0,0)';x.fillRect(0,0,64,64);requestAnimationFrame(f);})();"
    "</script></body></html>";

static int CapGate()
{
    wv2::Start(640, 360, &Sink, &Blocked);
    wv2::Navigate(k_animPage);
    Pump(3000);
    // Every start wipes the profile before the first page (OnController -> ClearBrowsingDataAll).
    const std::string diag = wv2::Diag();
    const bool cleared = diag.find("cache cleared at start") != std::string::npos;
    printf("start clear: %s (%s)\n", cleared ? "yes" : "NO", diag.c_str());
    const long a = g_sinkCalls;  Pump(2000);
    const long on1 = g_sinkCalls - a;
    wv2::SetCaptureEnabled(false);
    Pump(300);                                      // a frame mid-copy may still land
    const long b = g_sinkCalls;  Pump(2000);
    const long off = g_sinkCalls - b;
    wv2::SetCaptureEnabled(true);
    Pump(300);
    const long c = g_sinkCalls;  Pump(2000);
    const long on2 = g_sinkCalls - c;
    wv2::Stop();
    printf("frames per 2s: on=%ld off=%ld on-again=%ld (cap 60/s = 120)\n", on1, off, on2);
    const bool ok = cleared && on1 > 20 && off == 0 && on2 > 20 && on1 <= 125 && on2 <= 125;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && !strcmp(argv[1], "capgate")) return CapGate();
    if (argc >= 2 && !strcmp(argv[1], "lifecycle")) return Lifecycle();
    if (argc >= 2 && !strcmp(argv[1], "input")) return Input();
    if (argc >= 2 && !strcmp(argv[1], "hover")) return Hover();
    if (argc >= 2 && !strcmp(argv[1], "media")) return Media();
    if (argc >= 3 && !strcmp(argv[1], "playpause")) return PlayPause(argv[2]);
    if (argc >= 5 && !strcmp(argv[1], "play"))
        return Play(argv[2], atoi(argv[3]), argv[4], argc >= 6 && !strcmp(argv[5], "smooth"));
    printf("usage: wv2_harness play <url> <seconds> <out.bmp> | wv2_harness input\n");
    return 2;
}
