#include "websurface.h"
#include "websurf_math.h"
#include "websurf_js.h"
#include "steamhtml.h"
#include "webview2host.h"
#include "drawutils.h"
#include "menu.h"

// ---- Steam plumbing ----

using FindOrCreate_t = void* (*)(HSteamUser, const char*);
using GetHUser_t     = HSteamUser (*)();
using RegisterCb_t   = void (*)(CCallbackBase*, int);
using UnregisterCb_t = void (*)(CCallbackBase*);
using RegisterCr_t   = void (*)(CCallbackBase*, SteamAPICall_t);
using UnregisterCr_t = void (*)(CCallbackBase*, SteamAPICall_t);
using RunCallbacks_t = void (*)();

static ISteamHTMLSurface* s_surface     = nullptr;
static RegisterCb_t       s_pfnRegister = nullptr;
static UnregisterCb_t     s_pfnUnreg    = nullptr;
static RegisterCr_t       s_pfnRegCr    = nullptr;
static UnregisterCr_t     s_pfnUnregCr  = nullptr;
static RunCallbacks_t     s_pfnRun      = nullptr;
static SteamAPICall_t     s_createCall  = 0;   // CreateBrowser call-result key
static volatile long      s_cbSeen      = 0;   // any callback: dead pump vs dead call
static volatile long      s_paints      = 0;
static volatile long      s_draws       = 0;

static HHTMLBrowser s_browser   = INVALID_HTMLBROWSER;
static bool  s_steamTried       = false;   // one attempt at interface, then give up
static bool  s_steamDead        = false;   // no interface, or Init() said no
static bool  s_created          = false;   // CreateBrowser issued
static bool  s_registered       = false;
static DWORD s_createdAtMs      = 0;
static bool  s_pumpSelf         = false;   // game never dispatched: pump SteamAPI_RunCallbacks ourselves
static char  s_status[128]      = "off";

// ---- engine choice ----
// WebView2 when installed; Steam = fallback (--disable-gpu, no sw H.264 -> Twitch/TikTok dead).
// WebView2 mode: s_browser = stand-in handle, engine events replayed as Steam callback structs.
static bool s_wv2      = false;   // WebView2 is this session's engine
static bool s_wv2Tried = false;   // one attempt; failure -> Steam for session
static const HHTMLBrowser k_wv2Handle = 1;
static int  s_mouseX = 0, s_mouseY = 0;   // last pointer: WebView2 wheel needs position

static bool WebAlive()      { return s_browser != INVALID_HTMLBROWSER && (s_wv2 || s_surface); }
static bool WebHaveEngine() { return s_wv2 || s_surface; }
static std::string s_curUrl;      // page on screen now (main thread)
static std::string s_resumeUrl;   // next browser opens this instead of g_webUrl (clear memory/cache)

// ---- pixels ----
static CRITICAL_SECTION s_lock;
static bool  s_lockInit = false;
// paint callback can fire before any entry point runs -> lock built at static init
static struct WsLockInit { WsLockInit() { InitializeCriticalSection(&s_lock); s_lockInit = true; } } s_lockInitOnce;

// Triple buffer: producer (Steam paint on main thread, or WV2 capture thread, never both) fills
// s_pixIn, swaps with shared s_pixels. Lock covers swap only: memcpy under lock stalled Present.
static std::vector<uint8_t> s_pixIn;     // producer-owned, no lock
static std::vector<uint8_t> s_pixels;    // shared: the newest complete page
static std::vector<uint8_t> s_pixOut;    // render-thread-owned: what the texture was built from
static int   s_pixW = 0, s_pixH = 0;     // shared, under s_lock
static bool  s_pixDirty = false;
static int   s_outW = 0, s_outH = 0;     // render thread: the size s_pixOut holds
static volatile bool s_pixWanted = true; // false = nothing on screen reads page, stop copying
static volatile bool s_freePixels = false;   // main thread -> render thread: drop tex + s_pixOut

static IDirect3DTexture9* s_tex = nullptr;
static int  s_texW = 0, s_texH = 0;
static bool s_texDynamic = false;   // DEFAULT+DYNAMIC: full-page DISCARD uploads, dies on reset

// producer thread; pitch = source row stride in bytes
static void WebPublishPixels(const uint8_t* bgra, int w, int h, int pitch)
{
    if (!s_lockInit || !bgra || w <= 0 || h <= 0 || !s_pixWanted) return;

    const size_t row = (size_t)w * 4, bytes = row * (size_t)h;
    if (s_pixIn.size() != bytes) s_pixIn.resize(bytes);
    if (pitch == (int)row) memcpy(s_pixIn.data(), bgra, bytes);
    else for (int y = 0; y < h; ++y) memcpy(&s_pixIn[(size_t)y * row], bgra + (size_t)y * pitch, row);

    EnterCriticalSection(&s_lock);
    s_pixels.swap(s_pixIn);
    s_pixW = w; s_pixH = h;
    s_pixDirty = true;
    LeaveCriticalSection(&s_lock);
}

// Engine stopped: free buffers (24 MB at 1080p). Main thread, producer already gone (wv2::Stop
// joins capture). s_pixOut + texture are render thread's -> flag it to drop them.
static void WebFreePixels()
{
    if (!s_lockInit) return;
    EnterCriticalSection(&s_lock);
    std::vector<uint8_t>().swap(s_pixels);
    s_pixW = s_pixH = 0;
    s_pixDirty = false;
    LeaveCriticalSection(&s_lock);
    std::vector<uint8_t>().swap(s_pixIn);
    s_freePixels = true;
}

// eject: all buffers. Own func: Web_Destroy has __try, no unwinding temporaries (C2712).
__declspec(noinline) static void WebFreeAllPixels()
{
    std::vector<uint8_t>().swap(s_pixels);
    std::vector<uint8_t>().swap(s_pixIn);
    std::vector<uint8_t>().swap(s_pixOut);
    s_pixW = s_pixH = s_outW = s_outH = 0;
    s_pixDirty = false;
}

// ---- post slot -> draw ----
static float  s_vp[16]      = {};       // view-projection, already transposed for D3D
static bool   s_vpValid     = false;
static WsVec3 s_viewOrigin, s_viewFwd;
static bool   s_viewValid   = false;
static DWORD  s_presentTid  = 0;

// ---- placed screen ----
static bool   s_placed = false;
static WsVec3 s_pos, s_normal, s_right, s_up;
static bool   s_rebake = true;

struct WsVert { float x, y, z; DWORD col; float u, v; };
inline constexpr int k_wsCornerSeg = 8;   // segs per rounded corner; more not visible
static const DWORD k_wsFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

static std::vector<WsVert>  s_verts;      // front face
static std::vector<WsVert>  s_vertsBack;  // same mesh, u mirrored ("readable back")
static std::vector<uint16_t> s_idx;
static unsigned s_meshEpoch = 0;   // bumped per bake -> render thread drops its copy

// mesh: built main thread, read render thread. Held only for vector copy, never across d3d call.
static CRITICAL_SECTION s_meshLock;
static struct WsMeshLockInit { WsMeshLockInit() { InitializeCriticalSection(&s_meshLock); } } s_meshLockOnce;

// placement traces collision world = main thread only; menu/bind just request, post slot does it
static volatile bool s_placeRequested = false;

// ---- key edges ----
// distance volume ramp (main thread -> volume JS). Audio = steamwebhelper/WV2, not positional.
static volatile float s_distAtten = 1.f;
static bool s_placeDown = false;
static bool s_mouseDownWorld = false;   // crosshair button, main thread
static bool s_mouseDownPanel = false;   // panel button, render thread
static volatile bool  s_crossInside = false;   // crosshair on placed screen, main thread
static volatile float s_crossU = 0.5f, s_crossV = 0.5f;

// ---- render thread -> main thread commands ----
// ISteamHTMLSurface not thread-safe, callbacks on main thread -> every call made there.
// Panel/menu (render thread) queue here; main thread performs.
enum WsOp { WS_NAV, WS_BACK, WS_FWD, WS_RELOAD, WS_MMOVE, WS_MDOWN, WS_MUP, WS_RDOWN, WS_RUP,
            WS_WHEEL, WS_KEYCHAR, WS_KEYDOWN, WS_KEYUP, WS_SIZE, WS_VOL, WS_CLEARCACHE, WS_PAUSE, WS_CLEARMEM };
struct WsCmd { int op; int a; int b; std::string s; };
static CRITICAL_SECTION s_cmdLock;
static struct WsCmdLockInit { WsCmdLockInit() { InitializeCriticalSection(&s_cmdLock); } } s_cmdLockOnce;
static std::vector<WsCmd> s_cmds;

static void WebPost(int op, int a = 0, int b = 0, const char* str = nullptr)
{
    EnterCriticalSection(&s_cmdLock);
    // cap: stalled main thread + mouse-move flood
    if (s_cmds.size() < 512) s_cmds.push_back(WsCmd{ op, a, b, str ? std::string(str) : std::string() });
    LeaveCriticalSection(&s_cmdLock);
}

// page + wall sizes: always read these, never raw globals, so pointer maps to browser shape
static void WebRes(int& w, int& h)           { WsAspectRes(g_webAspect, g_webResH, w, h); }
static void WebWorldDims(float& w, float& h) { WsAspectDims(g_webAspect, g_webScale, w, h); }

// GetAsyncKeyState sees keys in any window: alt-tabbed typing must not reach page
static bool WebGameFocused() { return g_hwnd && GetForegroundWindow() == g_hwnd; }

// ---- Steam: interface, callbacks, browser ----

static void WebSetStatus(const char* s) { strncpy_s(s_status, s, _TRUNCATE); }

// CreateBrowser stylesheet: cosmetic adblock only, Steam's one channel; cannot block requests
static const char* k_adCss =
    "ins.adsbygoogle,.adsbygoogle,[id^='google_ads'],[id^='div-gpt-ad'],iframe[src*='doubleclick'],"
    "iframe[src*='googlesyndication'],iframe[src*='adservice'],.ad-container,.ad-banner,.advertisement,"
    "[class*='sponsored-ad'],[data-ad-slot],ytd-promoted-video-renderer,ytd-display-ad-renderer,"
    "ytd-ad-slot-renderer,.ytp-ad-overlay-container,#player-ads,#masthead-ad,"
    "ytd-banner-promo-renderer,ytd-in-feed-ad-layout-renderer,ytd-companion-slot-renderer,"
    "ytd-statement-banner-renderer,ytd-merch-shelf-renderer,ytd-rich-item-renderer:has(ytd-ad-slot-renderer),"
    "ytd-rich-section-renderer:has(ytd-statement-banner-renderer),ytm-promoted-sparkles-web-renderer"
    "{display:none!important;visibility:hidden!important;height:0!important}";

// YouTube ads, two layers:
// 1. network, uBlock uAssets way: json-prune adPlacements/playerAds/adSlots in /youtubei fetch,
//    XHR, JSON.parse; shorts = reel entries w/ adClientParams.isAd. Steam ExecuteJavascript runs
//    after page scripts -> first video of a load keeps its ads, layer 2 handles it.
// 2. player: click skip, mute + 16x + seek end, restore rate (<video> reused for real video).
// SPA: pushState fires no Steam callbacks -> script arms own 250ms timer behind guard, host
// re-sends every 1s. <style> tag too: CreateBrowser CSS only exists if adblock on at create.
static const char* k_adJs = R"JS((function(){try{
if(window.__fpsAd)return;window.__fpsAd=1;
var YT=/(^|\.)youtube(-nocookie)?\.com$/.test(location.hostname);
if(YT){
  var AD=['adPlacements','playerAds','adSlots'];
  var prune=function(o){try{
    if(!o||typeof o!=='object')return o;
    var roots=[o,o.playerResponse];
    if(Array.isArray(o))o.forEach(function(x){if(x)roots.push(x,x.playerResponse);});
    roots.forEach(function(r){if(r&&typeof r==='object')AD.forEach(function(k){if(k in r)delete r[k];});});
    if(Array.isArray(o.entries))o.entries=o.entries.filter(function(e){try{return !e.command.reelWatchEndpoint.adClientParams.isAd;}catch(x){return true;}});
  }catch(e){}return o;};
  var hit=function(u){return /\/youtubei\/v1\/(player|next|get_watch|reel\/reel_watch_sequence)|\/playlist\?/.test(String(u||''));};
  var scrub=function(t){return typeof t==='string'?t.replace(/"adPlacements"/g,'"no_ads"').replace(/"adSlots"/g,'"no_ads"').replace(/"playerAds"/g,'"no_ads"'):t;};
  var jp=JSON.parse;JSON.parse=function(){return prune(jp.apply(this,arguments));};
  var f=window.fetch;
  if(f)window.fetch=function(input){
    var u=input&&input.url?input.url:input;
    var p=f.apply(this,arguments);
    if(!hit(u))return p;
    return p.then(function(r){return r.clone().text().then(function(t){
      return new Response(scrub(t),{status:r.status,statusText:r.statusText,headers:r.headers});
    },function(){return r;});});
  };
  var X=XMLHttpRequest.prototype,xo=X.open;
  X.open=function(m,u){this.__fpsU=u;return xo.apply(this,arguments);};
  ['responseText','response'].forEach(function(k){
    var d=Object.getOwnPropertyDescriptor(X,k);if(!d||!d.get)return;
    Object.defineProperty(X,k,{configurable:true,enumerable:d.enumerable,get:function(){
      var v=d.get.call(this);if(!hit(this.__fpsU))return v;
      if(typeof v==='string')return scrub(v);
      return prune(v);}});
  });
}
var css=document.createElement('style');css.id='__fpsAdCss';
css.textContent="%CSS%";
(document.head||document.documentElement).appendChild(css);
var sped=false;
// YT only, scoped to player: whole-doc sweeps 4x/s on growing DOMs ate game CPU.
if(YT)setInterval(function(){try{
  if(!document.getElementById('__fpsAdCss'))(document.head||document.documentElement).appendChild(css);
  var p=document.querySelector('.html5-video-player'),v=p&&p.querySelector('video');
  if(p)p.querySelectorAll('.ytp-ad-skip-button,.ytp-skip-ad-button,.ytp-ad-skip-button-modern,.ytp-ad-skip-button-container button,.ytp-ad-overlay-close-button').forEach(function(b){b.click();});
  if(p&&v&&p.classList.contains('ad-showing')){
    v.muted=true;v.playbackRate=16;sped=true;
    if(isFinite(v.duration)&&v.duration>0)v.currentTime=v.duration;
  }else if(sped&&v){v.playbackRate=1;sped=false;}
  document.querySelectorAll('ytd-enforcement-message-view-model').forEach(function(e){
    var d=e.closest('tp-yt-paper-dialog');(d||e).remove();
    var w=document.querySelector('video');if(w&&w.paused)w.play();});
}catch(e){}},250);
}catch(e){}})())JS";

// built once: k_adJs w/ escaped CSS spliced in
static const std::string& WebAdJs()
{
    static std::string s;
    if (s.empty()) {
        std::string css;
        for (const char* c = k_adCss; *c; ++c) { if (*c == '"' || *c == '\\') css += '\\'; css += *c; }
        s = k_adJs;
        s.replace(s.find("%CSS%"), 5, css);
    }
    return s;
}

// main thread
__declspec(noinline) static void WebExecJs_SEH(const char* js)
{
    if (s_wv2) { wv2::ExecJs(js); return; }
    __try { s_surface->ExecuteJavascript(s_browser, js); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void WebApplyAdblock()
{
    if (!g_webAdblock || !WebAlive()) return;
    WebExecJs_SEH(WebAdJs().c_str());
}

// ended watch + quality cap. WV2 gets both at doc start; this re-arms on 1s tick (Steam,
// swapped documents, smooth toggled mid-session).
static void WebApplyMediaJs()
{
    if (!WebAlive()) return;
    WebExecJs_SEH(k_wsEndedJs);
    if (g_webSmooth) WebExecJs_SEH(k_wsSmoothJs);
    WebExecJs_SEH(g_webSmooth ? "window.__fpsQOff=0" : "window.__fpsQOff=1");
}

// navigations refused; subresources never reach AllowStartRequest
static const char* const k_adHosts[] = {
    "doubleclick.net", "googlesyndication.com", "googleadservices.com",
    "adservice.google.com", "popads.net", "popcash.net", "propellerads.com",
    "onclickads.net", "adnxs.com",
};

// also WV2 NavFilter on engine thread: reads one bool + const table only
static bool WebUrlBlocked(const char* url)
{
    if (!g_webAdblock || !url) return false;
    for (const char* h : k_adHosts)
        if (strstr(url, h)) return true;
    return false;
}

// watch -> /embed/ = video w/o app or ad player (Steam only). WV2: top-level /embed/ has no
// Referer (Chromium strips a set one) -> YouTube "error 153"; k_adJs at doc start there instead.
static std::string WebRewriteUrl(const std::string& url)
{
    if (!g_webYtEmbed || s_wv2) return url;
    const char* p = strstr(url.c_str(), "youtube.com/watch?v=");
    if (p) {
        std::string id(p + 20);
        const size_t amp = id.find('&');
        if (amp != std::string::npos) id.resize(amp);
        if (!id.empty()) return "https://www.youtube-nocookie.com/embed/" + id + "?autoplay=1";
    }
    p = strstr(url.c_str(), "youtu.be/");
    if (p) {
        std::string id(p + 9);
        const size_t q = id.find_first_of("?&");
        if (q != std::string::npos) id.resize(q);
        if (!id.empty()) return "https://www.youtube-nocookie.com/embed/" + id + "?autoplay=1";
    }
    return url;
}

// no volume API in ISteamHTMLSurface v005 -> JS on media elements; Web Audio pages ignore it
static void WebApplyVolume()
{
    if (!WebAlive()) return;
    float vol = g_webVolume;
    if (vol < 0.f) vol = 0.f;
    if (vol > 1.f) vol = 1.f;
    if (g_webVolDistance) vol *= s_distAtten;

    char js[256];
    snprintf(js, sizeof(js),
             "(function(){try{var v=%.3f;document.querySelectorAll('video,audio')"
             ".forEach(function(e){e.volume=v;e.muted=(v<=0);});}catch(e){}})()", vol);
    WebExecJs_SEH(js);
}

// split: __try can't share frame w/ std::string (C2712)
__declspec(noinline) static void WebLoadUrl_SEH(const char* url)
{
    if (s_wv2) { wv2::Navigate(url); return; }
    __try { s_surface->LoadURL(s_browser, url, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void WebNavigate(const std::string& typed)
{
    const std::string url = WsNormalizeUrl(typed);   // "google.com", "tf2 frags" -> absolute URLs
    if (!WebAlive() || url.empty()) return;
    WebLoadUrl_SEH(WebRewriteUrl(url).c_str());
}

// ---- callbacks (game main thread, whoever pumps SteamAPI_RunCallbacks) ----

static DWORD s_readyAtMs = 0;

static void OnBrowserReady(HTML_BrowserReady_t* p)
{
    if (s_browser != INVALID_HTMLBROWSER) return;   // callback and call result can both land
    s_browser   = p->unBrowserHandle;
    s_readyAtMs = GetTickCount();
    WebSetStatus("ready");
    DebugLog("web: browser ready handle=%u", (unsigned)s_browser);
    int rw, rh; WebRes(rw, rh);
    __try {
        s_surface->SetSize(s_browser, (uint32_t)rw, (uint32_t)rh);
        s_surface->SetKeyFocus(s_browser, true);
        // no AddHeader UA/Sec-CH-UA: Steam drops them (httpbin echo). Identity = page-side k_uaJs.
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    WebNavigate(s_resumeUrl.empty() ? g_webUrl : s_resumeUrl);
    s_resumeUrl.clear();
}

static void OnNeedsPaint(HTML_NeedsPaint_t* p)
{
    InterlockedIncrement(&s_cbSeen);
    if (InterlockedIncrement(&s_paints) == 1)
        DebugLog("web: first paint %ux%u update %u,%u %ux%u",
                 p->unWide, p->unTall, p->unUpdateX, p->unUpdateY, p->unUpdateWide, p->unUpdateTall);
    if (!p->pBGRA || p->unWide == 0 || p->unTall == 0) return;
    // pBGRA = WHOLE page unWide x unTall; unUpdate* = damage only. Treating partial as packed
    // sub-image smeared rows. Upload is full-page DISCARD anyway -> damage rect unused.
    WebPublishPixels((const uint8_t*)p->pBGRA, (int)p->unWide, (int)p->unTall, (int)p->unWide * 4);
}

// always answer: Steam hangs page waiting for it
static void OnStartRequest(HTML_StartRequest_t* p)
{
    InterlockedIncrement(&s_cbSeen);
    const bool allow = !WebUrlBlocked(p->pchURL);
    DebugLog("web: start request %s -> %s", p->pchURL ? p->pchURL : "(null)", allow ? "allow" : "BLOCKED");
    __try { s_surface->AllowStartRequest(p->unBrowserHandle, allow); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// GAME LISTENS TOO: TF2 vgui::HTML (MOTD, alive all match) handles every HTML_* callback w/ NO
// handle check (sdk vgui_controls/HTML.cpp:1737):
//   alert/confirm -> hidden MessageBox DoModal -> esc/console/mouse dead all session
//   window.open/_blank -> vgui popup Activate(); <input type=file> -> modal
// Can't filter game side -> page never raises them. Run on URL change, finish, 1s tick.
static const char* k_guardJs =
    "(function(){try{if(window.__fpsGuard)return;window.__fpsGuard=1;"
    "window.alert=function(){};window.confirm=function(){return true;};window.prompt=function(){return null;};"
    "window.open=function(u){try{if(u)location.href=u;}catch(e){}return null;};"
    "document.addEventListener('click',function(e){try{var t=e.target;"
    "var a=t&&t.closest&&t.closest('a[target]');if(a&&a.target!=='_self')a.target='_self';"
    "var f=t&&t.closest&&t.closest('input[type=file]');if(f)e.preventDefault();"
    "}catch(x){}},true);"
    "var pc=HTMLInputElement.prototype.click;HTMLInputElement.prototype.click=function(){"
    "if(this.type==='file')return;return pc.apply(this,arguments);};"
    "if(HTMLInputElement.prototype.showPicker)HTMLInputElement.prototype.showPicker=function(){};"
    "}catch(e){}})()";

// last 8 <video> loads/errors for diag. Capture phase (media events don't bubble). Keeps first
// error: retrying players overwrite v.error (TikTok CDN fail hid behind dead www fallback).
static const char* k_mediaLogJs = R"JS((function(){try{
if(window.__fpsML)return;var L=window.__fpsML=[];
var sh=function(u){return String(u||'').replace(/^https?:\/\//,'').replace(/[?#].*$/,'').slice(0,44);};
var add=function(s){L.push(s);if(L.length>8)L.shift();};
document.addEventListener('loadstart',function(e){var t=e.target;
  if(t&&t.tagName==='VIDEO')add('L:'+sh(t.currentSrc||t.src)+(t.srcObject?'(obj)':''));},true);
document.addEventListener('error',function(e){var t=e.target;
  if(t&&t.tagName==='VIDEO'&&t.error)add('E'+t.error.code+':'+String(t.error.message||'').slice(0,48));},true);
}catch(e){}})())JS";

// Steam "clear cache": run on each visited origin. Cookies (every domain/path, __Host- ok),
// storage, IndexedDB, caches, SWs. HttpOnly unreachable: no names, jar shared w/ Steam client.
static const char* k_wipeJs = R"JS((function(){try{
var h=location.hostname.split('.'),D=[''],P=['/'],s=location.protocol==='https:'?';secure':'';
for(var i=0;i<h.length-1;i++){var d=h.slice(i).join('.');D.push(d,'.'+d);}
var p=location.pathname.split('/');for(var j=2;j<p.length;j++)P.push(p.slice(0,j).join('/'));
document.cookie.split(';').forEach(function(c){var n=c.split('=')[0].trim();if(!n)return;
  D.forEach(function(d){P.forEach(function(q){document.cookie=n+'=;expires=Thu, 01 Jan 1970 00:00:00 GMT;max-age=0;path='+q+(d?';domain='+d:'')+s;});});});
try{localStorage.clear();}catch(e){}try{sessionStorage.clear();}catch(e){}
try{indexedDB.databases().then(function(l){l.forEach(function(x){indexedDB.deleteDatabase(x.name);});});}catch(e){}
try{caches.keys().then(function(k){k.forEach(function(x){caches.delete(x);});});}catch(e){}
try{navigator.serviceWorker.getRegistrations().then(function(r){r.forEach(function(x){x.unregister();});});}catch(e){}
}catch(e){}})())JS";

// main thread: visited origins for WebWipeTick
static std::vector<std::string> s_origins;
static size_t s_wipeIdx   = 0;
static int    s_wipePhase = 0;   // 0 idle, 1 origin loading, 2 wipe sent
static DWORD  s_wipeAt    = 0;

static void WebNoteOrigin(const char* url)
{
    if (!url || strncmp(url, "http", 4) || s_origins.size() >= 64) return;
    const char* h = strstr(url, "://");
    if (!h) return;
    const char* e = h + 3;
    while (*e && *e != '/' && *e != '?' && *e != '#') ++e;
    std::string o(url, e);
    for (const auto& x : s_origins) if (x == o) return;
    s_origins.push_back(std::move(o));
}

// "Hide Steam": navigator = current Chrome + brands Steam leaves empty, on Navigator.prototype.
// __fpsUA = readyState at install ("loading" = in time). TikTok skipped: video URLs signed vs
// real UA (Steam drops header overrides); page 150 vs request 126 -> err4 COULD_NOT_OPEN.
static const char* k_uaJs = R"JS((function(){try{
if(window.__fpsUA)return;
if(/(^|\.)tiktok\.com$/.test(location.hostname)){window.__fpsUA='skip';return;}
window.__fpsUA=document.readyState;
var V='150',F=V+'.0.0.0';
var ua='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/'+F+' Safari/537.36';
var b=[{brand:'Chromium',version:V},{brand:'Google Chrome',version:V},{brand:'Not.A/Brand',version:'99'}];
var fb=[{brand:'Chromium',version:F},{brand:'Google Chrome',version:F},{brand:'Not.A/Brand',version:'99.0.0.0'}];
var hi={brands:b,fullVersionList:fb,mobile:false,platform:'Windows',platformVersion:'15.0.0',
  architecture:'x86',bitness:'64',model:'',uaFullVersion:F,wow64:false,formFactors:['Desktop']};
var uad={brands:b,mobile:false,platform:'Windows',
  getHighEntropyValues:function(h){var o={brands:b,mobile:false,platform:'Windows'};
    (h||[]).forEach(function(k){if(k in hi)o[k]=hi[k];});return Promise.resolve(o);},
  toJSON:function(){return {brands:b,mobile:false,platform:'Windows'};}};
var P=Navigator.prototype,d=function(k,v){try{Object.defineProperty(P,k,{get:function(){return v;},configurable:true});}catch(e){}};
d('userAgent',ua);d('appVersion',ua.slice(8));d('userAgentData',uad);d('vendor','Google Inc.');d('webdriver',false);
}catch(e){}})())JS";

// Steam claims HEVC (isTypeSupported) but Chromium HEVC = hw only -> deny it everywhere a page
// asks. __fpsHevcHw = real hw answer for diag. Twitch IVS runs MSE in blob Worker: P(global)
// runs on window + prepended (P.toString) to every classic blob worker; also strips h265/hevc
// from usher supported_codecs. Workers report via BroadcastChannel -> __fpsWk.
static const char* k_codecJs = R"JS((function(){try{
if(window.__fpsCodec)return;window.__fpsCodec=1;
var P=function(g){try{
var bad=/hvc1|hev1|hevc|bytevc1|h265/i,said={};
var rep=g.document?function(m){g.__fpsWk=((g.__fpsWk||'')+' '+m).slice(-160);}
  :function(m){try{new BroadcastChannel('__fps').postMessage(m);}catch(e){}};
var once=function(m){if(!said[m]){said[m]=1;rep(m);}};
[g.MediaSource,g.ManagedMediaSource].forEach(function(M){
  if(!M||!M.isTypeSupported)return;var o=M.isTypeSupported;
  M.isTypeSupported=function(t){if(bad.test(String(t))){if(!g.document)once('wdeny');return false;}return o.call(M,t);};});
if(g.HTMLMediaElement){var cp=g.HTMLMediaElement.prototype.canPlayType;
  g.HTMLMediaElement.prototype.canPlayType=function(t){return bad.test(String(t))?'':cp.call(this,t);};}
var mc=g.navigator&&g.navigator.mediaCapabilities;
if(mc&&mc.decodingInfo){var d=mc.decodingInfo.bind(mc);
  if(g.document){g.__fpsHevcHw='?';
  d({type:'media-source',video:{contentType:'video/mp4; codecs="hvc1.1.6.L93.B0"',width:1280,height:720,bitrate:2000000,framerate:30}})
   .then(function(r){g.__fpsHevcHw=(r.supported?1:0)+'/'+(r.smooth?1:0)+'/'+(r.powerEfficient?1:0);},function(){g.__fpsHevcHw='err';});}
  mc.decodingInfo=function(c){try{var v=c&&c.video;if(v&&bad.test(String(v.contentType)))
    return Promise.resolve({supported:false,smooth:false,powerEfficient:false,keySystemAccess:null});}catch(e){}return d(c);};}
var MP=g.MediaSource&&g.MediaSource.prototype;
if(MP&&MP.addSourceBuffer){var asb=MP.addSourceBuffer;
  MP.addSourceBuffer=function(t){once((g.document?'':'w:')+String(t).replace(/\s|video\/|audio\/|codecs=/g,''));return asb.apply(this,arguments);};}
var VD=g.VideoDecoder;
if(VD&&VD.isConfigSupported){var vi=VD.isConfigSupported.bind(VD);
  VD.isConfigSupported=function(c){return c&&bad.test(String(c.codec))?Promise.resolve({supported:false,config:c}):vi(c);};}
var cut=function(u){return String(u).replace(/([?&]supported_codecs=)([^&#]*)/,function(a,k,v){
  var r=decodeURIComponent(v).split(',').filter(function(c){return c&&!/^(h265|hevc)$/i.test(c);}).join(',');
  once('usher '+decodeURIComponent(v)+'>'+r);return k+encodeURIComponent(r);});};
var has=function(u){return String(u).indexOf('supported_codecs=')>=0;};
if(g.fetch){var f=g.fetch;g.fetch=function(i,o){try{
  if(typeof i==='string'||i instanceof URL){if(has(i))i=cut(i);}
  else if(i&&i.url&&has(i.url))i=new Request(cut(i.url),i);}catch(e){}return f.call(this,i,o);};}
if(g.XMLHttpRequest){var xo=g.XMLHttpRequest.prototype.open;
  g.XMLHttpRequest.prototype.open=function(m,u){var a=[].slice.call(arguments);
    try{if(has(u))a[1]=cut(u);}catch(e){}return xo.apply(this,a);};}
}catch(e){}};
P(window);
try{(window.__fpsBC=new BroadcastChannel('__fps')).onmessage=function(e){window.__fpsWk=((window.__fpsWk||'')+' '+e.data).slice(-160);};}catch(e){}
var W=window.Worker;if(W){var pre='('+P.toString()+')(self);\n';
  var NW=function(u,o){var b=null;try{var s=String(u);if(!(o&&o.type==='module')&&s.indexOf('blob:')===0){
    var x=new XMLHttpRequest();x.open('GET',s,false);x.send();
    b=u=URL.createObjectURL(new Blob([pre+x.responseText],{type:'text/javascript'}));
    window.__fpsWorkers=(window.__fpsWorkers||0)+1;}}catch(e){}
    var w=new W(u,o);
    // revoke patched blob after load, else leaks per playback session
    if(b)setTimeout(function(){try{URL.revokeObjectURL(b);}catch(e){}},30000);
    return w;};
  NW.prototype=W.prototype;window.Worker=NW;}
}catch(e){}})())JS";

static void WebApplyGuard()
{
    if (!WebAlive()) return;
    WebExecJs_SEH(k_guardJs);
    WebExecJs_SEH(k_mediaLogJs);
    // Steam-only: WV2 = real Edge, true identity + codecs
    if (g_webHideSteam && !s_wv2) { WebExecJs_SEH(k_uaJs); WebExecJs_SEH(k_codecJs); }
}

// capability probe, once per browser. ExecuteJavascript returns nothing, alert() banned ->
// answer via page TITLE (set briefly, caught by HTML_ChangedTitle_t, restored).
static const char* k_probeTag = "__fpsprobe|";
static const char* k_probeJs =
    "(function(){try{var M=window.MediaSource,t=function(c){return M&&M.isTypeSupported(c)?1:0;};"
    "var s='h264='+t('video/mp4; codecs=\"avc1.4d401f\"')"
    "+'|hevc='+t('video/mp4; codecs=\"hvc1.1.6.L93.B0\"')"
    "+'|vp9='+t('video/webm; codecs=\"vp9\"')"
    "+'|av1='+t('video/mp4; codecs=\"av01.0.05M.08\"')"
    "+'|mse='+(M?1:0)+'|mms='+(window.ManagedMediaSource?1:0)"
    "+'|wasm='+(typeof WebAssembly==='object'?1:0)"
    "+'|webcodecs='+(typeof VideoDecoder==='function'?1:0)"
    "+'|webgl='+(function(){try{return document.createElement('canvas').getContext('webgl')?1:0;}catch(e){return 0;}})()"
    "+'|sab='+(typeof SharedArrayBuffer==='function'?1:0)"
    "+'|webdriver='+(navigator.webdriver?1:0)"
    "+'|chrome='+(window.chrome?1:0)"
    "+'|uad='+(navigator.userAgentData?navigator.userAgentData.brands.map(function(b){return b.brand+'/'+b.version;}).join(','):'none')"
    "+'|spoof='+(window.__fpsUA||'none')+'|host='+location.hostname;"
    // real title in __fpsT0: overlapping probes must never "restore" a __fps title
    "var t0=function(){var c=document.title;return /^__fps/.test(c)?(window.__fpsT0||''):(window.__fpsT0=c);};"
    "var send=function(w){var o=t0();document.title='__fpsprobe|'+s+'|widevine='+w+'|ua='+navigator.userAgent;"
    "setTimeout(function(){document.title=o;},200);};"
    "if(navigator.requestMediaKeySystemAccess)navigator.requestMediaKeySystemAccess('com.widevine.alpha',"
    "[{initDataTypes:['cenc'],videoCapabilities:[{contentType:'video/mp4; codecs=\"avc1.42E01E\"'}]}])"
    ".then(function(){send(1);},function(){send(0);});else send(0);"
    // diag at 8s + 30s, two lines (DebugLog caps 512):
    //  diag  = decode self-test (h264 mp4 + vp8 webm, loadeddata = really decoded), ml history, mse types
    //  diag2 = each <video> state, CDN answer for first src, WebCodecs, "not supported" text
    "if(!window.__fpsDiagArmed&&!window.__fpsNoDiag){window.__fpsDiagArmed=1;"
    "var say=function(a){var o=t0(),i=0,n=function(){if(i>=a.length)return;document.title=a[i++];"
    "setTimeout(function(){document.title=o;setTimeout(n,200);},200);};n();};"
    "var tv=function(u){return new Promise(function(res){try{var v=document.createElement('video'),d=0,"
    "f=function(r){if(d)return;d=1;res(r);try{v.removeAttribute('src');v.load();}catch(e){}};"
    "v.muted=true;v.preload='auto';v.onloadeddata=function(){f('ok'+v.videoWidth);};"
    "v.onerror=function(){f('err'+(v.error?v.error.code+':'+String(v.error.message||'').slice(0,40):''));};"
    "setTimeout(function(){f('to'+v.readyState);},7000);v.src=u;}catch(e){res('x');}});};"
    "var M='https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.';"
    "var diag=function(){try{"
    "var ST=window.__fpsST||(window.__fpsST=Promise.all([tv(M+'mp4'),tv(M+'webm')]));"
    "var ve=[].slice.call(document.querySelectorAll('video')).map(function(v){"
    "return (v.error?'err'+v.error.code+':'+String(v.error.message||'').slice(0,60):'ok')+'/rs'+v.readyState"
    "+'/'+String(v.currentSrc||'').slice(0,40);}).join(';')||'none';"
    // err4 COULD_NOT_OPEN = bytes not video: ask CDN (403/text/html = refused, 206 video/mp4 = fine)
    "var v0=[].slice.call(document.querySelectorAll('video')).filter(function(v){return /^https?:/.test(v.currentSrc||'');})[0];"
    "var st=v0?fetch(v0.currentSrc,{headers:{Range:'bytes=0-7'},credentials:'include'}).then(function(r){"
    "return r.status+' '+(r.headers.get('content-type')||'');},function(){return 'fetchfail';}):Promise.resolve('-');"
    "var tx=document.body?document.body.innerText:'',i=tx.search(/#\\d{4}|not supported|unsupported|supported browser|isn.t supported|update your browser|obs.ugiwan/i),"
    "m=i<0?'':tx.substr(Math.max(0,i-80),160).replace(/\\s+/g,' ');"
    "var vc=function(c,hw){return window.VideoDecoder?VideoDecoder.isConfigSupported({codec:c,hardwareAcceleration:hw})"
    ".then(function(r){return r.supported?1:0;},function(){return 'x';}):Promise.resolve('-');};"
    "var ac=window.AudioDecoder?AudioDecoder.isConfigSupported({codec:'mp4a.40.2',sampleRate:48000,numberOfChannels:2})"
    ".then(function(r){return r.supported?1:0;},function(){return 'x';}):Promise.resolve('-');"
    "Promise.all([vc('avc1.4d401f','prefer-software'),vc('avc1.4d401f','prefer-hardware'),"
    "vc('av01.0.05M.08','prefer-software'),vc('vp09.00.10.08','prefer-software'),ac,st,ST]).then(function(d){"
    "say(['__fpsdiag|'+location.hostname+'|test h264mp4='+d[6][0]+' vp8webm='+d[6][1]"
    "+'|workers='+(window.__fpsWorkers||0)+'|mse='+(window.__fpsWk||'-')"
    "+'|ml='+((window.__fpsML||[]).join(' ')||'-'),"
    "'__fpsdiag2|'+location.hostname+'|video='+ve+'|src='+d[5]"
    "+'|dec h264sw='+d[0]+' h264hw='+d[1]+' av1sw='+d[2]+' vp9sw='+d[3]+' aac='+d[4]"
    "+'|hevcHw='+(window.__fpsHevcHw||'-')+'|msg='+m]);});}catch(e){}};"
    "setTimeout(diag,8000);setTimeout(diag,30000);}"
    "}catch(e){}})()";
static char s_probeLine[256] = "";   // non-empty once probed (once per browser)

static void OnFinishedRequest(HTML_FinishedRequest_t* p)
{
    DebugLog("web: finished %.160s  title=\"%.80s\"",
             p->pchURL ? p->pchURL : "", p->pchPageTitle ? p->pchPageTitle : "");
    if (s_wipePhase == 1 && p->unBrowserHandle == s_browser) {   // clear sequence: page is up, wipe it
        WebExecJs_SEH(k_wipeJs);
        s_wipePhase = 2;
        s_wipeAt    = GetTickCount();
        return;
    }
    // WV2 has guard/adblock/media as doc-start scripts; Steam needs them per load.
    // Volume to both: new document = new <video> elements.
    if (!s_wv2) { WebApplyGuard(); WebApplyAdblock(); WebApplyMediaJs(); }
    WebApplyVolume();
    if (!WebAlive()) return;
    // Steam: probe every load (spoof= shows if UA landed in time) + decode diag
    if (!s_wv2) { WebExecJs_SEH(k_probeJs); return; }
    // WV2: engine caps don't change per page -> probe once, no diag clips
    if (!s_probeLine[0]) { WebExecJs_SEH("window.__fpsNoDiag=1"); WebExecJs_SEH(k_probeJs); }
}

// earliest hook on new document: guard lands before page scripts alert
static void OnURLChanged(HTML_URLChanged_t* p)
{
    if (p->unBrowserHandle == s_browser) {   // TF2's MOTD browser gets here too
        WebNoteOrigin(p->pchURL);
        if (p->pchURL && !strncmp(p->pchURL, "http", 4)) s_curUrl = p->pchURL;
    }
    if (!s_wv2) WebApplyGuard();   // WV2: doc-start script; this fires per pushState
}

static void OnChangedTitle(HTML_ChangedTitle_t* p)
{
    const char* m = p->pchTitle;
    // main video ended (k_wsEndedJs); handle check: TF2 MOTD titles broadcast too
    if (m && !strcmp(m, k_wsEndedTag) && p->unBrowserHandle == s_browser) {
        if (g_webAutoRemove && s_placed) {
            DebugLog("web: video ended -- auto remove");
            Web_Unplace();
        }
        return;
    }
    // __fpsdiag| / __fpsdiag2| -> "web: diag|..." (DebugLog caps 512). h264mp4= is real decode
    // test (Steam H.264 = MF hw only, dead w/ GPU web views off); "Media load rejected" = CSP.
    if (m && !strncmp(m, "__fpsdiag", 9)) {
        DebugLog("web: %.490s", m + 5);
        return;
    }
    if (!m || strncmp(m, k_probeTag, strlen(k_probeTag))) return;
    DebugLog("web: probe %s", m + strlen(k_probeTag));
    snprintf(s_probeLine, sizeof(s_probeLine), "%s", m + strlen(k_probeTag));
}

// always answer (unanswered = frozen page). Reached only if alert beat guard -> game modal too.
static void OnJSAlert(HTML_JSAlert_t* p)
{
    if (p->pchMessage) DebugLog("web: page alert got through the guard: %.200s", p->pchMessage);
    __try { s_surface->JSDialogResponse(p->unBrowserHandle, true); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
static void OnJSConfirm(HTML_JSConfirm_t* p)
{
    __try { s_surface->JSDialogResponse(p->unBrowserHandle, true); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
static void OnFileDialog(HTML_FileOpenDialog_t* p)
{
    __try { s_surface->FileLoadDialogResponse(p->unBrowserHandle, nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// _blank/window.open: kill new browser, load link in ours. __try split out: C2712 (WebNavigate std::string).
__declspec(noinline) static void WebRemoveBrowser_SEH(HHTMLBrowser h)
{
    __try { s_surface->RemoveBrowser(h); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void OnNewWindow(HTML_NewWindow_t* p)
{
    if (p->unNewWindow_BrowserHandle != INVALID_HTMLBROWSER)
        WebRemoveBrowser_SEH(p->unNewWindow_BrowserHandle);
    if (p->pchURL && !WebUrlBlocked(p->pchURL)) WebNavigate(p->pchURL);
}

static void OnCloseBrowser(HTML_CloseBrowser_t*) { WebSetStatus("page closed itself"); }

static WsCallback<HTML_BrowserReady_t>    s_cbReady   (&OnBrowserReady);
// CreateBrowser = CALL RESULT (callback id 4501 never fires); both registered
static WsCallResult<HTML_BrowserReady_t>  s_crReady   (&OnBrowserReady);
static WsCallback<HTML_NeedsPaint_t>      s_cbPaint   (&OnNeedsPaint);
static WsCallback<HTML_StartRequest_t>    s_cbStart   (&OnStartRequest);
static WsCallback<HTML_FinishedRequest_t> s_cbFinished(&OnFinishedRequest);
static WsCallback<HTML_JSAlert_t>         s_cbAlert   (&OnJSAlert);
static WsCallback<HTML_JSConfirm_t>       s_cbConfirm (&OnJSConfirm);
static WsCallback<HTML_FileOpenDialog_t>  s_cbFile    (&OnFileDialog);
static WsCallback<HTML_NewWindow_t>       s_cbNewWin  (&OnNewWindow);
static WsCallback<HTML_CloseBrowser_t>    s_cbClose   (&OnCloseBrowser);
static WsCallback<HTML_URLChanged_t>      s_cbUrl     (&OnURLChanged);
static WsCallback<HTML_ChangedTitle_t>    s_cbTitle   (&OnChangedTitle);

struct WsCbReg { CCallbackBase* cb; int id; };
static const WsCbReg k_cbs[] = {
    { &s_cbReady,    HTML_BrowserReady_t::k_iCallback },
    { &s_cbPaint,    HTML_NeedsPaint_t::k_iCallback },
    { &s_cbStart,    HTML_StartRequest_t::k_iCallback },
    { &s_cbFinished, HTML_FinishedRequest_t::k_iCallback },
    { &s_cbAlert,    HTML_JSAlert_t::k_iCallback },
    { &s_cbConfirm,  HTML_JSConfirm_t::k_iCallback },
    { &s_cbFile,     HTML_FileOpenDialog_t::k_iCallback },
    { &s_cbNewWin,   HTML_NewWindow_t::k_iCallback },
    { &s_cbClose,    HTML_CloseBrowser_t::k_iCallback },
    { &s_cbUrl,      HTML_URLChanged_t::k_iCallback },
    { &s_cbTitle,    HTML_ChangedTitle_t::k_iCallback },
};

// main thread. Flat accessor first, then SteamInternal_FindOrCreateUserInterface (survives SDK bumps).
static void WebSteamInit()
{
    if (s_steamTried) return;
    s_steamTried = true;

    HMODULE h = GetModuleHandleA("steam_api64.dll");
    if (!h) h = GetModuleHandleA("steam_api.dll");
    if (!h) { s_steamDead = true; WebSetStatus("steam_api not loaded"); return; }

    s_pfnRegister = (RegisterCb_t)  GetProcAddress(h, "SteamAPI_RegisterCallback");
    s_pfnUnreg    = (UnregisterCb_t)GetProcAddress(h, "SteamAPI_UnregisterCallback");
    s_pfnRegCr    = (RegisterCr_t)  GetProcAddress(h, "SteamAPI_RegisterCallResult");
    s_pfnUnregCr  = (UnregisterCr_t)GetProcAddress(h, "SteamAPI_UnregisterCallResult");
    s_pfnRun      = (RunCallbacks_t)GetProcAddress(h, "SteamAPI_RunCallbacks");

    if (auto flat = (void* (*)())GetProcAddress(h, "SteamAPI_SteamHTMLSurface_v005"))
        s_surface = (ISteamHTMLSurface*)flat();

    if (!s_surface) {
        auto foc  = (FindOrCreate_t)GetProcAddress(h, "SteamInternal_FindOrCreateUserInterface");
        auto user = (GetHUser_t)    GetProcAddress(h, "SteamAPI_GetHSteamUser");
        if (foc && user) s_surface = (ISteamHTMLSurface*)foc(user(), STEAMHTMLSURFACE_INTERFACE_VERSION);
    }

    if (!s_surface || !s_pfnRegister) {
        s_steamDead = true;
        WebSetStatus("Steam has no HTML surface");
        DebugLog("web: no ISteamHTMLSurface (surface=%p reg=%p)", (void*)s_surface, (void*)s_pfnRegister);
        return;
    }

    bool ok = false;
    __try { ok = s_surface->Init(); } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) {
        s_steamDead = true;
        WebSetStatus("HTMLSurface Init failed");
        DebugLog("web: ISteamHTMLSurface::Init returned false");
        return;
    }

    for (const WsCbReg& r : k_cbs) s_pfnRegister(r.cb, r.id);
    s_registered = true;
    DebugLog("web: ISteamHTMLSurface ready at %p (regCallResult=%p)",
             (void*)s_surface, (void*)s_pfnRegCr);
}

__declspec(noinline) static SteamAPICall_t WebCreateBrowser_SEH(const char* css)
{
    SteamAPICall_t h = 0;
    __try { h = s_surface->CreateBrowser(nullptr, css); }
    __except(EXCEPTION_EXECUTE_HANDLER) { h = 0; }
    return h;
}

static void WebCreateBrowser()
{
    if (s_created || !s_surface) return;
    s_created     = true;
    s_createdAtMs = GetTickCount();
    WebSetStatus("creating browser...");

    s_createCall = WebCreateBrowser_SEH(g_webAdblock ? k_adCss : nullptr);
    if (!s_createCall) {
        WebSetStatus("CreateBrowser returned nothing");
        DebugLog("web: CreateBrowser returned 0");
        return;
    }
    // registration that matters: sdk isteamhtmlsurface.h:43 STEAM_CALL_RESULT(HTML_BrowserReady_t),
    // answered vs this handle, never broadcast on 4501
    if (s_pfnRegCr) s_pfnRegCr(&s_crReady, s_createCall);
    DebugLog("web: CreateBrowser call=%llu callresult=%s",
             (unsigned long long)s_createCall, s_pfnRegCr ? "registered" : "NO EXPORT");
}

// main thread
static void WebSteamDrop()
{
    if (s_pfnUnregCr && s_createCall) {
        __try { s_pfnUnregCr(&s_crReady, s_createCall); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (s_browser != INVALID_HTMLBROWSER) WebRemoveBrowser_SEH(s_browser);
    s_browser    = INVALID_HTMLBROWSER;
    s_createCall = 0;
    s_created    = false;
    s_readyAtMs  = 0;
    s_paints     = 0;
}

// main thread, from command queue
static void WebRecreateBrowser()
{
    if (!s_surface) return;
    DebugLog("web: clear cache -- dropping browser %u", (unsigned)s_browser);
    WebSteamDrop();
    WebSetStatus("clearing cache...");
    WebCreateBrowser();
}

// Steam clear: each origin's /robots.txt (small, same origin) -> k_wipeJs -> fresh browser
// (drops history + memory cache)
static void WebWipeNext()
{
    if (s_wipeIdx >= s_origins.size() || s_browser == INVALID_HTMLBROWSER) {
        DebugLog("web: clear -- wiped %u of %u site(s), new browser", (unsigned)s_wipeIdx, (unsigned)s_origins.size());
        s_wipePhase = 0;
        s_origins.clear();
        WebRecreateBrowser();
        return;
    }
    char st[64];
    snprintf(st, sizeof(st), "clearing site data %u/%u", (unsigned)s_wipeIdx + 1, (unsigned)s_origins.size());
    WebSetStatus(st);
    s_wipePhase = 1;
    s_wipeAt    = GetTickCount();
    WebLoadUrl_SEH((s_origins[s_wipeIdx] + "/robots.txt").c_str());
}

static void WebWipeTick()
{
    if (!s_wipePhase) return;
    const DWORD dt = GetTickCount() - s_wipeAt;
    if (s_wipePhase == 1 && dt > 5000) {           // never finished loading: wipe what's there
        WebExecJs_SEH(k_wipeJs);
        s_wipePhase = 2;
        s_wipeAt    = GetTickCount();
    } else if (s_wipePhase == 2 && dt > 800) {     // async IndexedDB/cache deletes get their moment
        ++s_wipeIdx;
        WebWipeNext();
    }
}

// ---- WebView2 engine lifecycle (main thread) ----

// capture thread
static void WebWv2Sink(const uint8_t* bgra, int w, int h, int pitch)
{
    InterlockedIncrement(&s_paints);
    WebPublishPixels(bgra, w, h, pitch);
}

static int s_wv2Restarts = 0;   // crash/clear restarts this session; past 3 crashes -> Steam

static void WebWv2TryStart()
{
    s_wv2Tried = true;
    char ver[64] = "";
    if (!wv2::RuntimeVersion(ver, sizeof(ver))) {
        DebugLog("web: Edge WebView2 runtime not installed -- Steam's browser (Twitch/TikTok video won't play)");
        return;
    }
    int rw, rh; WebRes(rw, rh);
    if (!wv2::Start(rw, rh, &WebWv2Sink, &WebUrlBlocked)) {
        DebugLog("web: WebView2 engine thread did not start -- Steam's browser");
        return;
    }
    s_wv2        = true;
    s_created    = true;
    s_createdAtMs = GetTickCount();
    s_paints     = 0;
    WebSetStatus("starting Edge WebView2...");
    DebugLog("web: engine = Edge WebView2 %s", ver);
}

// retry = fresh engine next tick (clear, crash); else Steam takes over
static void WebWv2Drop(bool retry)
{
    const DWORD t0 = GetTickCount();
    wv2::Stop();
    DebugLog("web: WebView2 stopped in %lu ms", GetTickCount() - t0);
    WebFreePixels();
    s_wv2     = false;
    s_browser = INVALID_HTMLBROWSER;
    s_created = false;
    s_readyAtMs = 0;
    s_wv2Tried = !retry;
}

static void WebWv2Pump()
{
    static bool s_frameLogged = false;
    if (!s_frameLogged && s_paints > 0) { s_frameLogged = true; DebugLog("web: first WebView2 frame -- %s", wv2::Diag().c_str()); }

    wv2::Event e;
    while (wv2::PollEvent(e)) {
        switch (e.type) {
        case wv2::EV_READY:
            s_browser   = k_wv2Handle;
            s_readyAtMs = GetTickCount();
            WebSetStatus("ready");
            DebugLog("web: WebView2 ready");
            // doc-start scripts, before page's first script. Adblock toggle applies next engine start.
            wv2::AddStartScript(k_guardJs);
            wv2::AddStartScript(k_mediaLogJs);
            wv2::AddStartScript(k_wsEndedJs);
            if (g_webAdblock) wv2::AddStartScript(WebAdJs().c_str());
            if (g_webSmooth)  wv2::AddStartScript(k_wsSmoothJs);   // before player reads prefs
            WebNavigate(s_resumeUrl.empty() ? g_webUrl : s_resumeUrl);
            s_resumeUrl.clear();
            break;
        case wv2::EV_FAILED:
            DebugLog("web: WebView2 failed: %s -- falling back to Steam's browser", e.a.c_str());
            WebWv2Drop(false);
            WebSetStatus("Edge WebView2 failed, using Steam's browser");
            return;
        case wv2::EV_CRASHED:
            DebugLog("web: WebView2 %s -- %s", e.a.c_str(), s_wv2Restarts < 3 ? "restarting" : "giving up, Steam's browser");
            WebWv2Drop(++s_wv2Restarts <= 3);
            return;
        case wv2::EV_CLEARED:
            // fresh engine too: ClearBrowsingDataAll keeps back/forward list
            s_resumeUrl = s_curUrl;
            DebugLog("web: cleared all browsing data (%s) -- fresh browser", e.a.c_str());
            WebWv2Drop(true);
            WebSetStatus("cleared -- cookies, cache, history");
            return;
        case wv2::EV_URL: {
            HTML_URLChanged_t u{};
            u.unBrowserHandle = s_browser;
            u.pchURL = e.a.c_str();
            OnURLChanged(&u);
            break;
        }
        case wv2::EV_FINISHED: {
            if (!e.n) DebugLog("web: load failed %.160s (%s)", e.a.c_str(), e.b.c_str());
            HTML_FinishedRequest_t f{};
            f.unBrowserHandle = s_browser;
            f.pchURL       = e.a.c_str();
            f.pchPageTitle = e.b.c_str();
            OnFinishedRequest(&f);
            break;
        }
        case wv2::EV_TITLE: {
            HTML_ChangedTitle_t t{};
            t.unBrowserHandle = s_browser;
            t.pchTitle = e.a.c_str();
            OnChangedTitle(&t);
            break;
        }
        }
    }
}

// ---- texture upload (render thread) ----

static void WebReleaseTexture()
{
    if (s_tex) { s_tex->Release(); s_tex = nullptr; }
    s_texW = s_texH = 0;
}

// h_Reset kills DEFAULT tex; page still in s_pixOut -> next upload rebuilds from it
void Web_InvalidateTexture() { WebReleaseTexture(); }

static void WebUploadPixels(IDirect3DDevice9* dev)
{
    if (!dev || !s_lockInit) return;

    // swap newest page out, unlock before any d3d call
    bool fresh = false;
    EnterCriticalSection(&s_lock);
    if (s_pixDirty) {
        s_pixOut.swap(s_pixels);
        s_outW = s_pixW; s_outH = s_pixH;
        s_pixDirty = false;
        fresh = true;
    }
    LeaveCriticalSection(&s_lock);

    const int w = s_outW, h = s_outH;
    if (w <= 0 || h <= 0 || s_pixOut.size() < (size_t)w * h * 4) return;

    if (!s_tex || s_texW != w || s_texH != h) {
        WebReleaseTexture();
        // MANAGED first (survives reset). TF2 x64 = D3D9Ex, refuses MANAGED (hr 0x8876086C)
        // -> DEFAULT+DYNAMIC, full-page DISCARD uploads from s_pixOut.
        HRESULT hr = dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_tex, nullptr);
        s_texDynamic = false;
        if (FAILED(hr) || !s_tex) {
            s_tex = nullptr;
            hr = dev->CreateTexture(w, h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                    D3DPOOL_DEFAULT, &s_tex, nullptr);
            s_texDynamic = true;
        }
        if (FAILED(hr) || !s_tex) {
            s_tex = nullptr;
            static bool s_logged = false;
            if (!s_logged) { s_logged = true; DebugLog("web: CreateTexture %dx%d failed hr=0x%08X", w, h, (unsigned)hr); }
            return;
        }
        s_texW = w; s_texH = h;
        fresh = true;   // new texture is empty
        DebugLog("web: texture created %dx%d tex=%p pool=%s", w, h, (void*)s_tex,
                 s_texDynamic ? "DEFAULT+DYNAMIC" : "MANAGED");
    } else if (!fresh) {
        return;         // same texture, no new page
    }

    // whole page: sub-rect lock w/o DISCARD stalls GPU, with DISCARD loses rest of page
    D3DLOCKED_RECT lr{};
    if (FAILED(s_tex->LockRect(0, &lr, nullptr, s_texDynamic ? D3DLOCK_DISCARD : 0))) return;
    const size_t row = (size_t)w * 4;
    if (lr.Pitch == (int)row) memcpy(lr.pBits, s_pixOut.data(), row * (size_t)h);
    else for (int y = 0; y < h; ++y)
        memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, &s_pixOut[(size_t)y * row], row);
    s_tex->UnlockRect(0);
}

// ---- placement: trace, basis, mesh ----

inline constexpr int kTrace_TraceRay = 4;   // IEngineTrace, same slot dof.cpp uses

struct WsPlane   { WsVec3 normal; float dist; unsigned char type, signbits, pad[2]; };
struct WsSurface { const char* name; short surfaceProps; unsigned short flags; };

struct WsTrace
{
    WsVec3    startpos, endpos;
    WsPlane   plane;
    float     fraction = 1.f;
    int       contents = 0;
    unsigned short dispFlags = 0;
    bool      allsolid = false, startsolid = false;
    float     fractionleftsolid = 0.f;
    WsSurface surface{};
    int       hitgroup = 0;
    short     physicsbone = 0;
    void*     m_pEnt = nullptr;
    int       hitbox = 0;
};

struct __declspec(align(16)) WsVecAligned {
    float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
    void Set(float X = 0.f, float Y = 0.f, float Z = 0.f) { x = X; y = Y; z = Z; }
};

struct WsRay
{
    WsVecAligned m_Start, m_Delta, m_StartOffset, m_Extents;
    bool m_IsRay = true, m_IsSwept = true;
    void Init(const WsVec3& start, const WsVec3& end)
    {
        const WsVec3 d = end - start;
        m_Delta.Set(d.x, d.y, d.z);
        m_IsSwept = (d.x * d.x + d.y * d.y + d.z * d.z) != 0.f;
        m_Extents.Set();
        m_IsRay = true;
        m_StartOffset.Set();
        m_Start.Set(start.x, start.y, start.z);
    }
};

// MASK_PLAYERSOLID (const.h) minus PLAYERCLIP + MONSTER:
//   CONTENTS_SOLID 0x1 | WINDOW 0x2 | GRATE 0x8 | MOVEABLE 0x4000
inline constexpr unsigned k_wsMask = 0x1 | 0x2 | 0x8 | 0x4000;

// ---- walk-through brush entities ----
// Mirrors movement's ShouldCollide (util_shared.cpp / tf_gamerules.cpp) via netvars, no vtable guess:
//   not solid (SOLID_NONE, FSOLID_NOT_SOLID / FSOLID_TRIGGER)  -> skip
//   collision group movement ignores (const.h / tf_shareddefs.h) -> skip
//   TFCOLLISION_GROUP_RESPAWNROOMS: solid only to OTHER team    -> skip ours / unassigned
static int s_offCollGroup = 0, s_offSolidType = 0, s_offSolidFlags = 0;   // 0 unresolved, -1 missing

static void WebResolveCollisionNetvars()
{
    if (s_offCollGroup) return;
    RecvTable* t = FindNetworkTable("DT_BaseEntity");
    if (!t) return;   // tables not up yet: retry next placement
    const int g = FindPropOffset(t, "m_CollisionGroup");
    const int s = FindPropOffset(t, "m_nSolidType");      // inside m_Collision (DT_CollisionProperty)
    const int f = FindPropOffset(t, "m_usSolidFlags");
    s_offCollGroup  = g > 0 ? g : -1;
    s_offSolidType  = s > 0 ? s : -1;
    s_offSolidFlags = f > 0 ? f : -1;
    DebugLog("web: collision netvars group=%d solid=%d flags=%d", s_offCollGroup, s_offSolidType, s_offSolidFlags);
}

static bool WsGroupWalkable(int g)
{
    switch (g) {
    case 1:  case 2:           // DEBRIS, DEBRIS_TRIGGER
    case 10: case 11: case 12: // IN_VEHICLE, WEAPON, VEHICLE_CLIP
    case 13: case 14: case 15: // PROJECTILE, DOOR_BLOCKER, PASSABLE_DOOR
    case 16: case 17:          // DISSOLVING, PUSHAWAY
    case 20: case 23: case 24: // TF: GRENADES, COMBATOBJECT, ROCKETS
    case 26: case 27:          // TF: TANK, ROCKET_BUT_NOT_WITH_OTHER_ROCKETS
        return true;
    default:
        return false;
    }
}

static int WebLocalTeam()
{
    const int idx = GetLocalPlayerIndex();
    void* me = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!me || g_offTeam <= 0) return 0;
    int team = 0;
    __try { team = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(me) + g_offTeam); }
    __except(EXCEPTION_EXECUTE_HANDLER) { team = 0; }
    return team;
}

static int s_placeTeam = 0;   // local team, read once per placement (filter runs per entity)

// main thread (trace). true = ray stops here.
static bool WsSolidToPlayer(void* pHandle)
{
    if (!pHandle) return false;
    __try {
        // IClientUnknown::GetBaseEntity = slot 7 (~IHandleEntity 0, Set/GetRefEHandle 1-2, then
        // IClientUnknown's six, 5th). Static props -> null = world, always wall.
        void** vt = *(void***)pHandle;
        void* ent = ((void* (__fastcall*)(void*))vt[7])(pHandle);
        if (!ent) return true;
        // IHandleEntity = primary base -> same ptr. Mismatch = slot wrong: just hit it.
        if (ent != pHandle) return true;
        const uintptr_t e = reinterpret_cast<uintptr_t>(ent);
        if (s_offSolidType > 0 && *reinterpret_cast<unsigned char*>(e + s_offSolidType) == 0) return false;
        if (s_offSolidFlags > 0 && (*reinterpret_cast<unsigned short*>(e + s_offSolidFlags) & (0x4 | 0x8))) return false;
        if (s_offCollGroup > 0) {
            const int g = *reinterpret_cast<int*>(e + s_offCollGroup);
            if (WsGroupWalkable(g)) return false;
            if (g == 25 && g_offTeam > 0) {   // TFCOLLISION_GROUP_RESPAWNROOMS
                const int team = *reinterpret_cast<int*>(e + g_offTeam);
                if (team < 2 || team == s_placeTeam) return false;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}

struct WsFilter
{
    void** vfptr = nullptr;
    void*  pSkip = nullptr;
    static bool __fastcall sShouldHit(WsFilter*, void* pEnt, int) { return WsSolidToPlayer(pEnt); }
    // TRACE_EVERYTHING_FILTER_PROPS: brushes + static props. Players excluded by mask (no MONSTER).
    static int  __fastcall sTraceType(WsFilter*) { return 3; }
    void Setup() { static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType }; vfptr = vt; }
};

static void* s_engineTrace = nullptr;

static void WebTrace(const WsVec3& start, const WsVec3& end, WsTrace* tr)
{
    tr->fraction = 1.f;
    tr->endpos   = end;
    if (!s_engineTrace) s_engineTrace = GetInterface("engine.dll", "EngineTraceClient003");
    if (!s_engineTrace) return;

    WsFilter filter; filter.Setup();
    WsRay ray; ray.Init(start, end);
    using TraceRay_t = void(__fastcall*)(void*, const WsRay&, unsigned, void*, WsTrace*);
    __try {
        void** vt = *(void***)s_engineTrace;
        ((TraceRay_t)vt[kTrace_TraceRay])(s_engineTrace, ray, k_wsMask, &filter, tr);
    } __except(EXCEPTION_EXECUTE_HANDLER) { tr->fraction = 1.f; tr->endpos = end; }
}

// rounded-rect triangle fan on placed plane; rebuilt on placement, size or radius change
static void WebBakeMesh()
{
    std::vector<WsVert>   verts, vertsBack;
    std::vector<uint16_t> idx;

    if (!s_placed) {
        EnterCriticalSection(&s_meshLock);
        s_verts.clear(); s_vertsBack.clear(); s_idx.clear();
        ++s_meshEpoch;
        LeaveCriticalSection(&s_meshLock);
        s_rebake = false;
        return;
    }

    // radius in world units, converted per axis -> circular corners on non-square screen.
    // Rounding = geometry (fixed function, no shader mask).
    float ww, wh; WebWorldDims(ww, wh);
    const float w = ww > 1.f ? ww : 1.f;
    const float h = wh > 1.f ? wh : 1.f;
    float rad = g_webRound;
    const float maxRad = (w < h ? w : h) * 0.5f;
    if (rad < 0.f)      rad = 0.f;
    if (rad > maxRad)   rad = maxRad;

    float uv[4 * (k_wsCornerSeg + 1) * 2];
    const int n = WsRoundedRectOutline(rad / w, rad / h, rad > 0.01f ? k_wsCornerSeg : 0,
                                       uv, 4 * (k_wsCornerSeg + 1));
    if (n < 3) { s_rebake = false; return; }

    // vert 0 = page centre, outline follows, fan closes on itself
    {
        const WsVec3 c = WsQuadPoint(0.5f, 0.5f, s_pos, s_right, s_up, ww, wh);
        WsVert vt{ c.x, c.y, c.z, 0xFFFFFFFF, 0.5f, 0.5f };
        verts.push_back(vt);
        vertsBack.push_back(vt);   // u 0.5 mirrors to itself
    }
    for (int i = 0; i < n; ++i) {
        const float u = uv[i * 2], v = uv[i * 2 + 1];
        const WsVec3 p = WsQuadPoint(u, v, s_pos, s_right, s_up, ww, wh);
        WsVert vt{ p.x, p.y, p.z, 0xFFFFFFFF, u, v };
        verts.push_back(vt);
        vt.u = 1.f - u;
        vertsBack.push_back(vt);
    }
    for (int i = 0; i < n; ++i) {
        idx.push_back(0);
        idx.push_back((uint16_t)(1 + i));
        idx.push_back((uint16_t)(1 + ((i + 1) % n)));
    }

    // publish at once: render thread never sees half mesh
    EnterCriticalSection(&s_meshLock);
    s_verts.swap(verts);
    s_vertsBack.swap(vertsBack);
    s_idx.swap(idx);
    ++s_meshEpoch;
    LeaveCriticalSection(&s_meshLock);
    s_rebake = false;
}

// any thread: just a request. Tracing from render thread (menu) crashes; post slot does it.
void Web_PlaceAtCrosshair() { s_placeRequested = true; }

static void WebDoPlace()
{
    if (!s_viewValid) return;

    WebResolveCollisionNetvars();
    s_placeTeam = WebLocalTeam();
    WsTrace tr;
    WebTrace(s_viewOrigin, s_viewOrigin + s_viewFwd * 8192.f, &tr);
    if (tr.fraction >= 1.f) return;

    s_normal = WsNorm(tr.plane.normal);
    s_pos    = tr.endpos + s_normal * 0.5f;
    WsBasisFromNormal(s_normal, s_viewFwd, s_right, s_up);
    s_placed = true;
    s_rebake = true;
    if (g_webAutoPlay && WebAlive()) {
        if (g_webPlayFromStart) WebExecJs_SEH("window.__fpsPlayFrom0=1");
        WebExecJs_SEH(k_wsPlayJs);
    }

    float ww, wh; WebWorldDims(ww, wh);
    const WsVec3 tl = s_pos + s_right * (-ww * 0.5f) + s_up * (wh * 0.5f);
    const WsVec3 br = s_pos + s_right * ( ww * 0.5f) + s_up * (-wh * 0.5f);
    DebugLog("web: placed at %.1f %.1f %.1f n=%.2f %.2f %.2f r=%.2f %.2f %.2f u=%.2f %.2f %.2f",
             s_pos.x, s_pos.y, s_pos.z, s_normal.x, s_normal.y, s_normal.z,
             s_right.x, s_right.y, s_right.z, s_up.x, s_up.y, s_up.z);
    DebugLog("web:   corners tl=%.1f %.1f %.1f br=%.1f %.1f %.1f  eye=%.1f %.1f %.1f "
             "eyeDist=%.1f planeOff=%.1f",
             tl.x, tl.y, tl.z, br.x, br.y, br.z,
             s_viewOrigin.x, s_viewOrigin.y, s_viewOrigin.z,
             WsLen(s_viewOrigin - s_pos), WsDot(s_viewOrigin - s_pos, s_normal));
}

// ---- draw ----

struct WsSavedState
{
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex0;
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    UINT   streamOffset, streamStride;
    DWORD  fvf;
    D3DMATRIX world, view, proj;
    DWORD  rs[22];
    DWORD  samp[6];
    DWORD  tss[8];
    DWORD  tss1[2];   // stage 1 disabled below -> restore its ops too
};

// as screenfx: DrawIndexedPrimitiveUP nulls stream 0 + indices behind matsys cache -> restore
// both or next engine mesh draws from nothing
static const D3DRENDERSTATETYPE k_wsRS[22] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_ALPHABLENDENABLE,
    D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE,
    D3DRS_LIGHTING, D3DRS_FOGENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE,
    D3DRS_STENCILFUNC, D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
    D3DRS_STENCILPASS, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL, D3DRS_TWOSIDEDSTENCILMODE,
};
static_assert(sizeof(k_wsRS) / sizeof(k_wsRS[0]) == sizeof(WsSavedState::rs) / sizeof(DWORD), "save list");
static const D3DSAMPLERSTATETYPE k_wsSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};
static const D3DTEXTURESTAGESTATETYPE k_wsTSS[8] = {
    D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
    D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2,
    D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS,
};

static void WsSave(IDirect3DDevice9* dev, WsSavedState& s)
{
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetTexture(0, &s.tex0);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetTransform(D3DTS_WORLD, &s.world);
    dev->GetTransform(D3DTS_VIEW, &s.view);
    dev->GetTransform(D3DTS_PROJECTION, &s.proj);
    for (int i = 0; i < 22; ++i) dev->GetRenderState(k_wsRS[i], &s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->GetSamplerState(0, k_wsSamp[i], &s.samp[i]);
    for (int i = 0; i < 8;  ++i) dev->GetTextureStageState(0, k_wsTSS[i], &s.tss[i]);
    dev->GetTextureStageState(1, D3DTSS_COLOROP, &s.tss1[0]);
    dev->GetTextureStageState(1, D3DTSS_ALPHAOP, &s.tss1[1]);
}

static void WsRestore(IDirect3DDevice9* dev, WsSavedState& s)
{
    for (int i = 0; i < 22; ++i) dev->SetRenderState(k_wsRS[i], s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->SetSamplerState(0, k_wsSamp[i], s.samp[i]);
    for (int i = 0; i < 8;  ++i) dev->SetTextureStageState(0, k_wsTSS[i], s.tss[i]);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, s.tss1[0]);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, s.tss1[1]);
    dev->SetTransform(D3DTS_WORLD, &s.world);
    dev->SetTransform(D3DTS_VIEW, &s.view);
    dev->SetTransform(D3DTS_PROJECTION, &s.proj);
    dev->SetTexture(0, s.tex0);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    if (s.tex0)    s.tex0->Release();
    if (s.ps)      s.ps->Release();
    if (s.vs)      s.vs->Release();
    if (s.decl)    s.decl->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
}

static const D3DMATRIX k_identity = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

// ---- stay out of color correction ----
// CC (render.cpp ColorCorr_Apply) hits whole backbuffer pre-HUD. Screen marks visible pixels in
// stencil bit 0x80 (glow uses low bits); Web_RestoreOverColorCorr copies pre-CC back there and
// clears mark. Only trusted on same depth surface the mark went into.
static constexpr DWORD k_wsStencilBit = 0x80;
static IDirect3DSurface9* s_markDs  = nullptr;   // identity only, never dereferenced or released
static bool               s_marked  = false;

static bool WsDsHasStencil(IDirect3DSurface9* ds)
{
    D3DSURFACE_DESC d{};
    if (!ds || FAILED(ds->GetDesc(&d))) return false;
    return d.Format == D3DFMT_D24S8 || d.Format == D3DFMT_D24FS8 || d.Format == D3DFMT_D24X4S4 ||
           d.Format == D3DFMT_D15S1;
}

// frame view-proj as D3DTS_PROJECTION + world-space verts = free perspective. Facing decided
// on CPU, CULL_NONE: never guess winding.
__declspec(noinline) static void WebDrawWorld(IDirect3DDevice9* dev, bool depthTest)
{
    if (!dev || !s_tex || !s_placed || !s_vpValid) return;

    const bool facing = WsDot(s_viewOrigin - s_pos, s_normal) > 0.f;
    if (!facing && g_webBackMode == 1) return;     // back not rendered
    const bool wantBack = (!facing && g_webBackMode == 2);   // readable from behind

    float a = g_webWorldOpacity;
    if (a <= 0.003f) return;
    if (a > 1.f) a = 1.f;
    const DWORD col = D3DCOLOR_ARGB((int)(a * 255.f + 0.5f), 255, 255, 255);

    // render thread copy of main-thread mesh; re-copy on bake, side or tint (vertex colour) change
    static std::vector<WsVert>   s_draw;
    static std::vector<uint16_t> s_drawIdx;
    static DWORD    s_drawCol   = 0;
    static bool     s_drawBack  = false;
    static unsigned s_drawEpoch = 0xFFFFFFFFu;
    if (s_drawCol != col || s_drawBack != wantBack || s_drawEpoch != s_meshEpoch) {
        EnterCriticalSection(&s_meshLock);
        s_draw      = wantBack ? s_vertsBack : s_verts;
        s_drawIdx   = s_idx;
        s_drawEpoch = s_meshEpoch;
        LeaveCriticalSection(&s_meshLock);
        for (WsVert& v : s_draw) v.col = col;
        s_drawCol  = col;
        s_drawBack = wantBack;
    }
    if (s_draw.empty() || s_drawIdx.empty()) return;

    WsSavedState st{};
    __try {
        WsSave(dev, st);

        dev->SetVertexShader(nullptr);
        dev->SetPixelShader(nullptr);
        dev->SetVertexDeclaration(nullptr);
        dev->SetFVF(k_wsFvf);

        D3DMATRIX proj;
        memcpy(&proj, s_vp, sizeof(proj));
        dev->SetTransform(D3DTS_WORLD, &k_identity);
        dev->SetTransform(D3DTS_VIEW, &k_identity);
        dev->SetTransform(D3DTS_PROJECTION, &proj);

        dev->SetRenderState(D3DRS_ZENABLE, depthTest ? TRUE : FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        // CC restore mark (k_wsStencilBit)
        bool mark = false;
        if (g_colorCorrEnable) {
            IDirect3DSurface9* ds = nullptr;
            if (SUCCEEDED(dev->GetDepthStencilSurface(&ds)) && ds) {
                mark = WsDsHasStencil(ds);
                if (mark) s_markDs = ds;
                ds->Release();
            }
        }
        dev->SetRenderState(D3DRS_STENCILENABLE, mark ? TRUE : FALSE);
        if (mark) {
            dev->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
            dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
            dev->SetRenderState(D3DRS_STENCILREF, k_wsStencilBit);
            dev->SetRenderState(D3DRS_STENCILMASK, k_wsStencilBit);
            dev->SetRenderState(D3DRS_STENCILWRITEMASK, k_wsStencilBit);
            dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
            dev->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
            dev->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
            s_marked = true;
        }
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        // page already in frame space; sRGB write = glow at dxlevel 90+ (as imgui_impl_dx9)
        dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);

        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        dev->SetTexture(0, s_tex);
        if (InterlockedIncrement(&s_draws) == 1)
            DebugLog("web: first world draw verts=%d tris=%d depthTest=%d",
                     (int)s_draw.size(), (int)(s_drawIdx.size() / 3), (int)depthTest);
        dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, (UINT)s_draw.size(),
                                    (UINT)(s_drawIdx.size() / 3), s_drawIdx.data(), D3DFMT_INDEX16,
                                    s_draw.data(), sizeof(WsVert));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try { WsRestore(dev, st); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- onto render thread: same 3-tier call queue as screenfx (matsys 98 -> rc 147 -> queue 0) ----

inline constexpr int k_slotGetRenderContext = 98;
inline constexpr int k_slotGetCallQueue     = 147;

static bool s_cqRefused = false;
static volatile bool s_cqRan = false;
static int  s_cqQueued = 0;

struct WsFunctor { void** vfptr; unsigned userId; };

static int   __fastcall WsFn_AddRef (void*)             { return 1; }
static int   __fastcall WsFn_Release(void*)             { return 1; }
static void* __fastcall WsFn_Dtor   (void* p, unsigned) { return p; }
static void  __fastcall WsFn_Call   (void*)
{
    s_cqRan = true;
    WebDrawWorld(g_d3dDev, g_webDepthMode == 0);
}
static void* s_wsFnVt[4] = { (void*)&WsFn_AddRef, (void*)&WsFn_Release, (void*)&WsFn_Dtor, (void*)&WsFn_Call };
static WsFunctor s_wsFunctor = { s_wsFnVt, 0 };

static bool WebQueueOnRenderThread()
{
    if (!g_matSystem) return false;
    __try {
        void** mvt = *(void***)g_matSystem;
        if (reinterpret_cast<uintptr_t>(mvt) < 0x10000) return false;
        void* rc = ((void* (__fastcall*)(void*))mvt[k_slotGetRenderContext])(g_matSystem);
        if (reinterpret_cast<uintptr_t>(rc) < 0x10000) return false;
        void** rvt = *(void***)rc;
        if (reinterpret_cast<uintptr_t>(rvt) < 0x10000) return false;
        void* cq = ((void* (__fastcall*)(void*))rvt[k_slotGetCallQueue])(rc);
        if (reinterpret_cast<uintptr_t>(cq) < 0x10000) return false;
        void** qvt = *(void***)cq;
        if (reinterpret_cast<uintptr_t>(qvt) < 0x10000) return false;
        ((void(__fastcall*)(void*, void*))qvt[0])(cq, &s_wsFunctor);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Web_WantsSingleThreadedMat()
{
    return s_cqRefused && g_webEnable && s_placed && g_webDepthMode != 2 && IsInGame();
}

// ---- input ----

static ISteamHTMLSurface::EHTMLKeyModifiers WebMods()
{
    int m = 0;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) m |= ISteamHTMLSurface::k_eHTMLKeyModifier_AltDown;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= ISteamHTMLSurface::k_eHTMLKeyModifier_CtrlDown;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) m |= ISteamHTMLSurface::k_eHTMLKeyModifier_ShiftDown;
    return (ISteamHTMLSurface::EHTMLKeyModifiers)m;
}

// same mods, CDP bits
static int WebWv2Mods()
{
    const int m = WebMods();
    return ((m & ISteamHTMLSurface::k_eHTMLKeyModifier_AltDown)   ? wv2::KMOD_ALT   : 0) |
           ((m & ISteamHTMLSurface::k_eHTMLKeyModifier_CtrlDown)  ? wv2::KMOD_CTRL  : 0) |
           ((m & ISteamHTMLSurface::k_eHTMLKeyModifier_ShiftDown) ? wv2::KMOD_SHIFT : 0);
}

__declspec(noinline) static void WebMouseMovePx_SEH(int x, int y)
{
    s_mouseX = x; s_mouseY = y;
    if (s_wv2) { wv2::MouseInput(wv2::M_MOVE, x, y); return; }
    __try { s_surface->MouseMove(s_browser, x, y); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// delta: Steam unit 100/notch; WV2 wants WHEEL_DELTA 120/notch
__declspec(noinline) static void WebWheel_SEH(int delta)
{
    if (s_wv2) { wv2::MouseInput(wv2::M_WHEEL, s_mouseX, s_mouseY, delta * WHEEL_DELTA / 100); return; }
    __try { s_surface->MouseWheel(s_browser, (int32_t)delta); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void WebKeyChar_SEH(uint32_t c)
{
    if (s_wv2) { wv2::KeyInput(wv2::K_CHAR, c, WebWv2Mods()); return; }
    __try { s_surface->KeyChar(s_browser, c, WebMods()); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void WebKey_SEH(bool down, uint32_t vk)
{
    if (s_wv2) { wv2::KeyInput(down ? wv2::K_DOWN : wv2::K_UP, vk, WebWv2Mods()); return; }
    __try {
        if (down) s_surface->KeyDown(s_browser, vk, WebMods(), false);
        else      s_surface->KeyUp  (s_browser, vk, WebMods());
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void WebSetSize_SEH(uint32_t w, uint32_t h)
{
    if (s_wv2) { wv2::Resize((int)w, (int)h); return; }
    __try { s_surface->SetSize(s_browser, w, h); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Steam browser up but never painted: CEF draws nothing in background mode / no layout (not
// observable) -> clear both, bounce size. Few tries only: nudge loop worse than no picture.
__declspec(noinline) static void WebNudgePaint_SEH(uint32_t w, uint32_t h)
{
    __try {
        s_surface->SetBackgroundMode(s_browser, false);
        s_surface->SetSize(s_browser, w + 1, h + 1);
        s_surface->SetSize(s_browser, w, h);
        s_surface->SetKeyFocus(s_browser, true);
        s_surface->Reload(s_browser);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// 0 back, 1 forward, 2 reload
__declspec(noinline) static void WebSimple_SEH(int which)
{
    if (s_wv2) { if (which == 0) wv2::Back(); else if (which == 1) wv2::Forward(); else wv2::Reload(); return; }
    __try {
        if      (which == 0) s_surface->GoBack(s_browser);
        else if (which == 1) s_surface->GoForward(s_browser);
        else                 s_surface->Reload(s_browser);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// main thread, u/v 0..1, every frame crosshair on screen. Move = cross-process call -> unchanged
// pos only re-sent as 250ms hover keep-alive.
static void WebMouseAt(float u, float v)
{
    if (!WebAlive()) return;
    int rw, rh; WebRes(rw, rh);
    const int x = (int)(u * (float)rw), y = (int)(v * (float)rh);
    static DWORD s_lastSent = 0;
    const DWORD now = GetTickCount();
    if (x == s_mouseX && y == s_mouseY && now - s_lastSent < 250) return;
    s_lastSent = now;
    WebMouseMovePx_SEH(x, y);
}

// "click only": park pointer off page after click -> mouseleave, no stuck hover overlays
static const int k_wsAway = -1;

static void WebMousePark() { if (WebAlive()) WebMouseMovePx_SEH(k_wsAway, k_wsAway); }

static void WebMouseButton(bool down)
{
    if (!WebAlive()) return;
    if (s_wv2) { wv2::MouseInput(down ? wv2::M_LDOWN : wv2::M_LUP, s_mouseX, s_mouseY); return; }
    __try {
        if (down) s_surface->MouseDown(s_browser, ISteamHTMLSurface::eHTMLMouseButton_Left);
        else      s_surface->MouseUp  (s_browser, ISteamHTMLSurface::eHTMLMouseButton_Left);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void WebMouseRight(bool down)
{
    if (!WebAlive()) return;
    if (s_wv2) { wv2::MouseInput(down ? wv2::M_RDOWN : wv2::M_RUP, s_mouseX, s_mouseY); return; }
    __try {
        if (down) s_surface->MouseDown(s_browser, ISteamHTMLSurface::eHTMLMouseButton_Right);
        else      s_surface->MouseUp  (s_browser, ISteamHTMLSurface::eHTMLMouseButton_Right);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// main thread only
static void WebDrainCommands()
{
    if (!WebAlive()) return;

    static std::vector<WsCmd> cmds;   // swapped w/ s_cmds, both keep capacity
    cmds.clear();
    EnterCriticalSection(&s_cmdLock);
    cmds.swap(s_cmds);
    LeaveCriticalSection(&s_cmdLock);

    for (const WsCmd& c : cmds) {
        switch (c.op) {
        case WS_NAV:     WebNavigate(c.s); break;
        case WS_BACK:    WebSimple_SEH(0); break;
        case WS_FWD:     WebSimple_SEH(1); break;
        case WS_RELOAD:  WebSimple_SEH(2); break;
        case WS_MMOVE:   WebMouseMovePx_SEH(c.a, c.b); break;
        case WS_MDOWN:   WebMouseButton(true); break;
        case WS_MUP:     WebMouseButton(false); break;
        case WS_WHEEL:   WebWheel_SEH(c.a); break;
        case WS_KEYCHAR: WebKeyChar_SEH((uint32_t)c.a); break;
        case WS_KEYDOWN: WebKey_SEH(true,  (uint32_t)c.a); break;
        case WS_KEYUP:   WebKey_SEH(false, (uint32_t)c.a); break;
        case WS_SIZE:    WebSetSize_SEH((uint32_t)c.a, (uint32_t)c.b); break;
        // 1s tick. Steam: re-arm all (no doc-start hook). WV2: has them per document; only
        // smooth toggle needs re-send (each ExecJs = cross-process compile).
        case WS_VOL: {
            static int s_lastSmooth = -1;
            if (!s_wv2) {
                WebApplyGuard(); WebApplyAdblock(); WebApplyMediaJs();
            } else if (s_lastSmooth != (int)g_webSmooth) {
                s_lastSmooth = (int)g_webSmooth;
                WebApplyMediaJs();
            }
            WebApplyVolume();
            break;
        }
        case WS_PAUSE:   WebExecJs_SEH(k_wsPauseJs); break;
        case WS_CLEARMEM:
            // fresh processes, same profile: RAM back, logins kept, page reopened
            s_resumeUrl = s_curUrl;
            DebugLog("web: clear memory -- restarting the browser at %.160s", s_resumeUrl.c_str());
            if (s_wv2) { WebWv2Drop(true); WebSetStatus("restarting browser..."); }
            else       WebRecreateBrowser();
            return;   // rest of queue was for old browser
        case WS_RDOWN:   WebMouseRight(true); break;
        case WS_RUP:     WebMouseRight(false); break;
        case WS_CLEARCACHE:
            // WV2 real API: all cookies (HttpOnly too), cache, storage, history
            if (s_wv2) { WebSetStatus("clearing browsing data..."); wv2::ClearData(); }
            else if (!s_wipePhase) { s_wipeIdx = 0; WebWipeNext(); }
            break;
        default: break;
        }
    }
}

void Web_ClearMemory() { WebPost(WS_CLEARMEM); }

// any thread (menu address bar = render thread)
void Web_Navigate(const char* url) { if (url && *url) WebPost(WS_NAV, 0, 0, url); }

void Web_ClearCache() { WebPost(WS_CLEARCACHE); }

// browser keeps running (panel), media paused. Any thread -> pause via queue.
void Web_Unplace()
{
    if (s_placed) WebPost(WS_PAUSE);
    s_placed = false;
    s_rebake = true;
}

// main thread (CreateMove). s_crossInside only true w/ menu closed.
bool Web_BlockFire()
{
    return g_webEnable && g_webBlockFire && g_webCrosshairPointer && s_placed && s_crossInside;
}

// WndProcHook rebind; true = swallowed
bool Web_CaptureKey(int vk)
{
    if (g_webKeyCapture == 0) return false;
    if (vk != VK_ESCAPE) {
        if      (g_webKeyCapture == 1) g_webPlaceKey  = vk;
        else if (g_webKeyCapture == 2) g_webClickKey  = vk;
        else if (g_webKeyCapture == 4) g_webRClickKey = vk;
        else                           g_webRemoveKey = vk;
    }
    g_webKeyCapture = 0;
    return true;
}

// menu closed: wheel + Up/Down to page only while crosshair on placed screen (always-visible
// panel ate jump-bind wheel). WndProcHook; true = swallowed; queued, any thread.
bool Web_OnInput(unsigned msg, uintptr_t wParam)
{
    if (!g_webEnable || !g_webScrollInput || g_showMenu) return false;
    if (!WebAlive() || !s_placed || !s_crossInside) return false;
    const float u = s_crossU, v = s_crossV;

    const bool isArrow = wParam == VK_UP || wParam == VK_DOWN;
    switch (msg) {
    case WM_MOUSEWHEEL: {
        // browser scrolls under pointer -> move there first; click-only parks it after
        int rw, rh; WebRes(rw, rh);
        WebPost(WS_MMOVE, (int)(u * (float)rw), (int)(v * (float)rh));
        WebPost(WS_WHEEL, (int)GET_WHEEL_DELTA_WPARAM(wParam) * 100 / WHEEL_DELTA);
        if (g_webClickOnly) WebPost(WS_MMOVE, k_wsAway, k_wsAway);
        return true;
    }
    case WM_KEYDOWN: if (isArrow) { WebPost(WS_KEYDOWN, (int)wParam); return true; } break;
    case WM_KEYUP:   if (isArrow) { WebPost(WS_KEYUP,   (int)wParam); return true; } break;
    default: break;
    }
    return false;
}

// ---- game post slot, main thread ----

// CViewSetup, public/view_shared.h; same offsets screenfx reads
inline constexpr size_t k_vsOrigin = 64;
inline constexpr size_t k_vsAngles = 76;

// main thread every frame incl. main menu (FSN FRAME_START): engine init, pump, commands.
// Not in post slot: that only runs with a world rendered.
void Web_MainTick()
{
    if (g_eject) return;
    // feature off: stop browser (processes eat CPU/audio); re-enable starts fresh
    if (!g_webEnable) {
        if (s_wv2) { WebWv2Drop(true); WebSetStatus("off"); }
        else if (s_surface && s_browser != INVALID_HTMLBROWSER) {
            DebugLog("web: off -- dropping Steam browser %u", (unsigned)s_browser);
            s_wipePhase = 0;
            WebSteamDrop();
            WebFreePixels();
            WebSetStatus("off");
        }
        return;
    }

    static bool s_tickLogged = false;
    if (!s_tickLogged) { s_tickLogged = true; DebugLog("web: main tick up (inGame=%d)", (int)IsInGame()); }

    // capture gate: nothing shows page -> skip readback/copy/upload; browser keeps playing,
    // last page stays in texture
    s_pixWanted = s_placed || (g_webPanel && (g_webPanelAlways || Menu_Visible()));
    wv2::SetCaptureEnabled(s_pixWanted);

    if (!s_wv2Tried) WebWv2TryStart();
    if (s_wv2) {
        WebWv2Pump();
        if (s_wv2) WebDrainCommands();
        return;
    }

    WebSteamInit();
    if (s_steamDead) return;
    if (!s_created) WebCreateBrowser();

    // game normally pumps callbacks; none 5s after CreateBrowser -> pump ourselves, main thread
    // only (game's own handlers expect it)
    if (!s_pumpSelf && s_created && s_browser == INVALID_HTMLBROWSER && s_pfnRun &&
        GetTickCount() - s_createdAtMs > 5000) {
        s_pumpSelf = true;
        DebugLog("web: browser not ready after 5s (callbacks seen=%ld) -- pumping "
                 "SteamAPI_RunCallbacks ourselves", s_cbSeen);
    }
    if (s_pumpSelf && s_pfnRun) { __try { s_pfnRun(); } __except(EXCEPTION_EXECUTE_HANDLER) {} }

    WebDrainCommands();
    WebWipeTick();

    // up but no pixels: nudge every 3s, 5 times max
    if (s_browser != INVALID_HTMLBROWSER && s_paints == 0 && s_readyAtMs) {
        static int   s_nudges = 0;
        static DWORD s_lastNudge = 0;
        const DWORD now = GetTickCount();
        if (s_nudges < 5 && now - s_readyAtMs > 3000 && now - s_lastNudge > 3000) {
            s_lastNudge = now;
            ++s_nudges;
            int rw, rh; WebRes(rw, rh);
            DebugLog("web: no paint %u ms after ready -- nudge %d (size %dx%d)",
                     now - s_readyAtMs, s_nudges, rw, rh);
            WebNudgePaint_SEH((uint32_t)rw, (uint32_t)rh);
        }
    }
}

// post slot: world-screen half only (view, placement, crosshair, draw)
void Web_OnPostScreenSpaceEffects(const void* setup)
{
    if (g_eject) return;

    if (!g_webEnable || !WebHaveEngine()) {
        s_viewValid = false;
        return;
    }

    // grab view + matrix here: draw may run later on render thread where neither is reachable
    if (setup) {
        __try {
            const uintptr_t p = reinterpret_cast<uintptr_t>(setup);
            const float* o = reinterpret_cast<const float*>(p + k_vsOrigin);
            const float* a = reinterpret_cast<const float*>(p + k_vsAngles);
            const WsVec3 origin(o[0], o[1], o[2]);
            const WsVec3 angles(a[0], a[1], a[2]);
            WsVec3 fwd, right, up;
            WsAngleVectors(angles, fwd, right, up);
            s_viewOrigin = origin;
            s_viewFwd    = fwd;
            s_viewValid  = true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { s_viewValid = false; }
    }

    // IVEngineClient::WorldToScreenMatrix slot 36 (flipmath.h). Flip World returns TRUE matrix
    // here and mirrors finished frame later -> screen mirrors with world.
    if (g_engine) {
        __try {
            const float* m = VCall<const float*>((void*)g_engine, 36);
            if (m && WsIsProjection(m)) { WsTranspose4(m, s_vp); s_vpValid = true; }
        } __except(EXCEPTION_EXECUTE_HANDLER) { s_vpValid = false; }
    }

    // binds; not w/ menu open (key may be typed in text box)
    static bool s_removeDown = false;
    if (!g_showMenu && WebGameFocused()) {
        const bool placeNow = g_webPlaceKey && (GetAsyncKeyState(g_webPlaceKey) & 0x8000) != 0;
        if (placeNow && !s_placeDown) s_placeRequested = true;
        s_placeDown = placeNow;
        const bool removeNow = g_webRemoveKey && (GetAsyncKeyState(g_webRemoveKey) & 0x8000) != 0;
        if (removeNow && !s_removeDown) Web_Unplace();
        s_removeDown = removeNow;
    } else {
        s_placeDown = false;
        s_removeDown = false;
    }
    if (s_placeRequested) { s_placeRequested = false; WebDoPlace(); }

    // rebake only on shape change
    {
        static float s_lastW = -1.f, s_lastH = -1.f, s_lastRound = -1.f;
        float ww, wh; WebWorldDims(ww, wh);
        if (s_lastW != ww || s_lastH != wh || s_lastRound != g_webRound) {
            s_lastW = ww; s_lastH = wh; s_lastRound = g_webRound;
            s_rebake = true;
        }
    }
    if (s_rebake && s_placed) WebBakeMesh();

    // distance ramp: linear near..far, silent past far, 1 w/ no placed screen
    if (g_webVolDistance && s_placed && s_viewValid) {
        // not "near"/"far": windef.h macros
        const float d  = WsLen(s_viewOrigin - s_pos);
        const float n0 = g_webVolNear;
        const float f0 = g_webVolFar > n0 + 1.f ? g_webVolFar : n0 + 1.f;
        s_distAtten = (d <= n0) ? 1.f : (d >= f0 ? 0.f : 1.f - (d - n0) / (f0 - n0));
    } else {
        s_distAtten = 1.f;
    }

    // crosshair hit for scroll input (WndProc reads); separate from pointer: works w/ pointer off
    {
        bool in = false;
        float t;
        if (s_placed && s_viewValid && !g_showMenu && WsRayPlane(s_viewOrigin, s_viewFwd, s_pos, s_normal, t)) {
            float u, v, ww, wh;
            WebWorldDims(ww, wh);
            WsQuadUV(s_viewOrigin + s_viewFwd * t, s_pos, s_right, s_up, ww, wh, u, v);
            in = WsInQuad(u, v);
            // decal mode: wall in front hides screen -> not aimable through it (else fire block
            // eats shots at wall). Other modes draw through walls -> clickable through them.
            if (in && g_webDepthMode == 0 && t > 2.f) {
                WsTrace tr;
                WebTrace(s_viewOrigin, s_viewOrigin + s_viewFwd * (t - 2.f), &tr);
                if (tr.fraction < 1.f) in = false;
            }
            if (in) {
                const bool facing = WsDot(s_viewOrigin - s_pos, s_normal) > 0.f;
                s_crossU = (!facing && g_webBackMode == 2) ? 1.f - u : u;
                s_crossV = v;
            }
        }
        s_crossInside = in;
    }

    // crosshair = pointer (u/v already mirrored for readable back). Leaving screen parks pointer
    // off page -> hover ends like real mouse.
    static bool s_ptrOnPage   = false;
    static bool s_rDownWorld  = false;
    if (s_placed && s_viewValid && g_webCrosshairPointer && !g_showMenu) {
        const bool inside  = s_crossInside;
        const bool focused = WebGameFocused();
        const bool clickNow = inside && focused && g_webClickKey &&
                              (GetAsyncKeyState(g_webClickKey) & 0x8000) != 0;
        const bool rNow     = inside && focused && g_webRClickKey &&
                              (GetAsyncKeyState(g_webRClickKey) & 0x8000) != 0;
        // click only: pointer enters on press, stays for drag until release
        if (inside && (!g_webClickOnly || clickNow || rNow || s_mouseDownWorld || s_rDownWorld)) {
            WebMouseAt(s_crossU, s_crossV);
            s_ptrOnPage = true;
        } else if (s_ptrOnPage && !inside) {
            WebMousePark();
            s_ptrOnPage = false;
        }
        if (clickNow != s_mouseDownWorld) {
            WebMouseButton(clickNow);
            s_mouseDownWorld = clickNow;
        }
        if (rNow != s_rDownWorld) {
            WebMouseRight(rNow);
            s_rDownWorld = rNow;
        }
        if (g_webClickOnly && s_ptrOnPage && !clickNow && !rNow) { WebMousePark(); s_ptrOnPage = false; }
    } else {
        if (s_mouseDownWorld) { WebMouseButton(false); s_mouseDownWorld = false; }
        if (s_rDownWorld)     { WebMouseRight(false);  s_rDownWorld = false; }
        if (s_ptrOnPage)      { WebMousePark();        s_ptrOnPage = false; }
    }

    if (!s_placed || g_webDepthMode == 2) return;   // on-top mode draws in Present
    if (!s_presentTid) return;

    // queue first, never "same thread as last Present": map load presents from main thread,
    // leaves that id stale (screenfx.cpp)
    if (!s_cqRefused && WebQueueOnRenderThread()) {
        if (++s_cqQueued > 240 && !s_cqRan) {
            s_cqRefused = true;
            DebugLog("web: call queue took %d functors and ran none -- mat_queue_mode 0", s_cqQueued);
        }
        return;
    }
    if (GetCurrentThreadId() != s_presentTid) {
        if (!s_cqRefused) {
            s_cqRefused = true;
            DebugLog("web: call queue refused -- forcing mat_queue_mode 0 for the world screen");
        }
        return;
    }
    WebDrawWorld(g_d3dDev, g_webDepthMode == 0);
}

// ---- panel + always-on-top draw: render thread, inside ImGui frame ----

// keys that never arrive as chars; rest via KeyChar
static const int k_wsNavKeys[] = { VK_BACK, VK_RETURN, VK_TAB, VK_DELETE, VK_ESCAPE,
                                   VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_HOME, VK_END,
                                   VK_PRIOR, VK_NEXT };
static constexpr int k_wsNavCount = (int)(sizeof(k_wsNavKeys) / sizeof(k_wsNavKeys[0]));
static bool s_navDown[k_wsNavCount] = {};

// focus left mid-press: key-up every held nav key
static void WebPanelKeysRelease()
{
    for (int i = 0; i < k_wsNavCount; ++i)
        if (s_navDown[i]) { s_navDown[i] = false; WebPost(WS_KEYUP, k_wsNavKeys[i]); }
}

static void WebPanelKeys()
{
    if (!WebAlive()) return;
    ImGuiIO& io = ImGui::GetIO();
    // menu text box typing or alt-tabbed (GetAsyncKeyState sees all) -> page gets nothing
    if (io.WantTextInput || !WebGameFocused()) { WebPanelKeysRelease(); return; }

    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        const ImWchar c = io.InputQueueCharacters[i];
        if (c >= 32) WebPost(WS_KEYCHAR, (int)c);
    }

    for (int i = 0; i < k_wsNavCount; ++i) {
        const bool down = (GetAsyncKeyState(k_wsNavKeys[i]) & 0x8000) != 0;
        if (down == s_navDown[i]) continue;
        s_navDown[i] = down;
        WebPost(down ? WS_KEYDOWN : WS_KEYUP, k_wsNavKeys[i]);
    }
}

// panel: menu open = chrome (menu font/palette); menu closed + "always visible" = bare page at
// same rect so toggling menu doesn't move it. Radius 6 = menu.cpp background.
static constexpr float kWebRound = 6.f;

static ImVec2 s_imgPos(0.f, 0.f);      // last page rect, screen space
static ImVec2 s_imgSize(0.f, 0.f);

static void WebPanelChromeless()
{
    if (!s_tex || s_imgSize.x < 2.f || s_imgSize.y < 2.f) return;

    ImGui::SetNextWindowPos(s_imgPos);
    ImGui::SetNextWindowSize(s_imgSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##websurf_bare", nullptr, flags)) {
        float pa = g_webPanelOpacity; if (pa < 0.f) pa = 0.f; if (pa > 1.f) pa = 1.f;
        ImGui::GetWindowDrawList()->AddImageRounded(
            (ImTextureID)s_tex, s_imgPos, ImVec2(s_imgPos.x + s_imgSize.x, s_imgPos.y + s_imgSize.y),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, (int)(pa * 255.f + 0.5f)), kWebRound);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// menu MenuDropdown pill look. icon: 0 label, 1 back, 2 fwd, 3 reload, 4 close (glyph, no pill)
static bool WebBtn(const char* id, const char* label, ImVec2 size, int icon = 0)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();

    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID sid = ImGui::GetItemID();
    float ht = st->GetFloat(sid, 0.f);
    ht += ((hov ? 1.f : 0.f) - ht) * fminf(1.f, ImGui::GetIO().DeltaTime * 10.f);
    st->SetFloat(sid, ht);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + size.x, p.y + size.y);
    if (icon != 4) {
        dl->AddRectFilled(p, q, IM_COL32(255, 255, 255, held ? 20 : (int)(10 + ht * 8)), 5.f);
        dl->AddRect(p, q, IM_COL32(255, 255, 255, (int)(18 + ht * 14)), 5.f, 0, 1.f);
    }

    const int lv = held ? 210 : (int)(164 + ht * 30);
    const ImU32 tc = IM_COL32(lv, lv, lv + 4, 255);
    const ImVec2 c((p.x + q.x) * 0.5f, (p.y + q.y) * 0.5f);
    const float s = size.y * 0.2f;
    switch (icon) {
    case 1: case 2: {
        const float d = icon == 1 ? s * 0.55f : -s * 0.55f;
        const ImVec2 pts[3] = { ImVec2(c.x + d, c.y - s), ImVec2(c.x - d, c.y), ImVec2(c.x + d, c.y + s) };
        dl->AddPolyline(pts, 3, tc, 0, 1.6f);
        break;
    }
    case 3: {
        const float pi = 3.14159265f, a1 = pi * 1.8f;
        dl->PathArcTo(c, s, pi * 0.3f, a1, 16);
        dl->PathStroke(tc, 0, 1.6f);
        const ImVec2 e(c.x + cosf(a1) * s, c.y + sinf(a1) * s);
        const ImVec2 t(-sinf(a1), cosf(a1)), n(cosf(a1), sinf(a1));   // tangent, radial
        dl->AddTriangleFilled(ImVec2(e.x + t.x * s * 0.6f, e.y + t.y * s * 0.6f),
                              ImVec2(e.x + n.x * s * 0.45f, e.y + n.y * s * 0.45f),
                              ImVec2(e.x - n.x * s * 0.45f, e.y - n.y * s * 0.45f), tc);
        break;
    }
    case 4:
        dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), tc, 1.6f);
        dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), tc, 1.6f);
        break;
    default: {
        const ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), tc, label);
    }
    }
    return pressed;
}

static void WebPanel()
{
    if (!WebHaveEngine()) return;

    // chrome only w/ menu up (only time WndProc lets input reach ImGui); fades with menu layer.
    // "always visible": bare page under it during fade -> page stays solid.
    if (g_webPanelAlways && Menu_Alpha() < 1.f) WebPanelChromeless();
    if (!Menu_Visible()) return;
    const bool live = g_showMenu;   // closing: still drawn for the fade, takes no input

    float pa = g_webPanelOpacity; if (pa < 0.f) pa = 0.f; if (pa > 1.f) pa = 1.f;
    const float aspect = s_texH > 0 ? (float)s_texW / (float)s_texH : 16.f / 9.f;
    const float w = 480.f * (g_webPanelScale <= 0.05f ? 1.f : g_webPanelScale);

    // aspect preset change reshapes window once; portrait sized by height to stay on screen
    static int s_lastAspect = -1;
    const bool reshape = s_lastAspect != -1 && s_lastAspect != g_webAspect;
    s_lastAspect = g_webAspect;
    if (reshape) {
        const WsAspect& as = WsAspectAt(g_webAspect);
        const float ar = (float)as.w / (float)as.h;   // texture still old shape
        const ImVec2 sz = ar < 1.f ? ImVec2(w * 0.75f * ar, w * 0.75f + 110.f)
                                   : ImVec2(w, w / ar + 110.f);
        ImGui::SetNextWindowSize(sz, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowSize(ImVec2(w, w / aspect + 110.f), ImGuiCond_FirstUseEver);
    }

    // menu look: own bg + header, no stock title/border/grip (edges still resize)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kWebRound);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(240.f, 160.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 6.f));
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, IM_COL32(255, 255, 255, 20));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive,  IM_COL32(255, 255, 255, 40));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,           IM_COL32(255, 255, 255, 10));   // MenuDropdown pill
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,    IM_COL32(255, 255, 255, 18));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,     IM_COL32(255, 255, 255, 20));
    ImGui::PushStyleColor(ImGuiCol_Border,            IM_COL32(255, 255, 255, 18));
    ImGui::PushStyleColor(ImGuiCol_Text,              IM_COL32(210, 210, 214, 255));
    ImGui::PushFont(g_menuFont ? g_menuFont : ImGui::GetIO().FontDefault);
    auto popAll = [] { ImGui::End(); ImGui::PopFont(); ImGui::PopStyleColor(8); ImGui::PopStyleVar(8); };

    if (!ImGui::Begin("Web##websurf", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                               ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                               ImGuiWindowFlags_NoScrollWithMouse |
                                               (live ? 0 : ImGuiWindowFlags_NoInputs))) {
        popAll();
        return;
    }

    // bg as RenderMenuBody: blur under 238 menu-color fill, 15 white border + divider
    const float hdrH = 34.f;
    {
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        const ImVec2 we(wp.x + ws.x, wp.y + ws.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float wr = kWebRound;
        // full-window clip: default sits WindowPadding/2 inside -> square-looking corners
        ImGui::PushClipRect(wp, we, false);
        if (g_menuBgBlur && g_d3dDev)
            DrawBgBlurRect(dl, wp.x, wp.y, we.x, we.y, wr, ImDrawFlags_RoundCornersAll);
        dl->AddRectFilled(wp, we, IM_COL32((int)(g_menuBgColor[0] * 255), (int)(g_menuBgColor[1] * 255),
                                           (int)(g_menuBgColor[2] * 255), (int)(238 * pa)), wr);
        dl->AddRect(wp, we, IM_COL32(255, 255, 255, 15), wr, 0, 1.f);
        dl->AddRectFilled(ImVec2(wp.x, wp.y + hdrH), ImVec2(we.x, wp.y + hdrH + 1.f), IM_COL32(255, 255, 255, 15));
        ImGui::PopClipRect();

        const ImVec2 ts = ImGui::CalcTextSize("WEB");
        dl->AddText(ImVec2(wp.x + 14.f, wp.y + (hdrH - ts.y) * 0.5f), IM_COL32(94, 94, 94, 255), "WEB");

        ImGui::SetCursorScreenPos(ImVec2(we.x - 30.f, wp.y + (hdrH - 22.f) * 0.5f));
        if (WebBtn("##webclose", nullptr, ImVec2(22.f, 22.f), 4)) g_webPanel = false;
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, hdrH + 10.f));
    }

    // address bar: Enter or Go navigates
    static char s_url[512] = {};
    static bool s_urlInit = false;
    if (!s_urlInit) { strncpy_s(s_url, g_webUrl.c_str(), _TRUNCATE); s_urlInit = true; }

    const float fh  = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    if (WebBtn("##webback",   nullptr, ImVec2(fh, fh), 1)) WebPost(WS_BACK);
    ImGui::SameLine();
    if (WebBtn("##webfwd",    nullptr, ImVec2(fh, fh), 2)) WebPost(WS_FWD);
    ImGui::SameLine();
    if (WebBtn("##webreload", nullptr, ImVec2(fh, fh), 3)) WebPost(WS_RELOAD);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 44.f - gap);
    const bool entered = ImGui::InputTextWithHint("##weburl", "https://...", s_url, sizeof(s_url),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (WebBtn("##webgo", "Go", ImVec2(44.f, fh)) || entered) {
        g_webUrl = s_url;
        WebPost(WS_NAV, 0, 0, g_webUrl.c_str());
    }

    // place/remove/clears row; narrow window -> short labels
    const float btnW = (ImGui::GetContentRegionAvail().x - gap * 3.f) / 4.f;
    const bool  narrow = btnW < ImGui::CalcTextSize("Clear memory").x + 12.f;
    if (WebBtn("##webplace",  "Place",  ImVec2(btnW, fh))) Web_PlaceAtCrosshair();
    ImGui::SameLine();
    if (WebBtn("##webremove", "Remove", ImVec2(btnW, fh))) Web_Unplace();
    ImGui::SameLine();
    if (WebBtn("##webclear",  narrow ? "Cache" : "Clear cache", ImVec2(btnW, fh))) Web_ClearCache();
    ImGui::SameLine();
    if (WebBtn("##webclrmem", narrow ? "Memory" : "Clear memory", ImVec2(btnW, fh))) Web_ClearMemory();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float dw = avail.x, dh = dw / aspect;
    if (dh > avail.y && avail.y > 1.f) { dh = avail.y; dw = dh * aspect; }

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (!s_tex) {
        // no pixels yet: show status, not empty window
        ImGui::TextDisabled("%s", Web_Status());
        popAll();
        return;
    }

    // Dummy claims item for hover (AddImageRounded doesn't)
    ImGui::Dummy(ImVec2(dw, dh));
    ImGui::GetWindowDrawList()->AddImageRounded(
        (ImTextureID)s_tex, p0, ImVec2(p0.x + dw, p0.y + dh), ImVec2(0, 0), ImVec2(1, 1),
        IM_COL32(255, 255, 255, (int)(pa * 255.f + 0.5f)), kWebRound);

    // for chromeless pass: no jump when menu closes
    s_imgPos  = p0;
    s_imgSize = ImVec2(dw, dh);

    const bool down  = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool rdown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    static bool s_rDownPanel = false;
    static bool s_panelPtrIn = false;   // page pointer on picture (hover)
    if (ImGui::IsItemHovered() && dw > 1.f && dh > 1.f) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float u = (m.x - p0.x) / dw, v = (m.y - p0.y) / dh;
        int rw, rh; WebRes(rw, rh);
        const int px = (int)(u * (float)rw), py = (int)(v * (float)rh);
        const float wheel = ImGui::GetIO().MouseWheel;
        // click only: pointer in only for press/drag/wheel, then parked
        if (!g_webClickOnly || down || rdown || s_mouseDownPanel || s_rDownPanel || wheel != 0.f) {
            WebPost(WS_MMOVE, px, py);
            s_panelPtrIn = true;
        }
        if (down != s_mouseDownPanel) { WebPost(down ? WS_MDOWN : WS_MUP); s_mouseDownPanel = down; }
        if (rdown != s_rDownPanel)    { WebPost(rdown ? WS_RDOWN : WS_RUP); s_rDownPanel = rdown; }
        if (wheel != 0.f) WebPost(WS_WHEEL, (int)(wheel * 100.f));
        if (g_webClickOnly && !down && !rdown && (wheel != 0.f || ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                                                  ImGui::IsMouseReleased(ImGuiMouseButton_Right))) {
            WebPost(WS_MMOVE, k_wsAway, k_wsAway);
            s_panelPtrIn = false;
        }
    } else {
        // released outside picture: send up, else page thinks still held
        if (s_mouseDownPanel && !down) { WebPost(WS_MUP); s_mouseDownPanel = false; }
        if (s_rDownPanel && !rdown)    { WebPost(WS_RUP); s_rDownPanel = false; }
        // off picture, nothing held: end hover
        if (s_panelPtrIn && !s_mouseDownPanel && !s_rDownPanel) {
            WebPost(WS_MMOVE, k_wsAway, k_wsAway);
            s_panelPtrIn = false;
        }
    }
    if (live && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) WebPanelKeys();
    else WebPanelKeysRelease();

    popAll();
}

// render thread, right after ColorCorr_Apply. preCC = CC input copy; blit it back where stencil
// marked, clear mark same pass (STENCILPASS ZERO). Gun/HUD drawn over screen stays uncorrected.
void Web_RestoreOverColorCorr(IDirect3DDevice9* dev, IDirect3DTexture9* preCC, int w, int h)
{
    if (!s_marked) return;
    s_marked = false;
    if (!dev || !preCC || w <= 0 || h <= 0) return;

    // backbuffer + marked depth surface only
    IDirect3DSurface9 *ds = nullptr, *rt = nullptr, *bb = nullptr;
    dev->GetDepthStencilSurface(&ds);
    dev->GetRenderTarget(0, &rt);
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    const bool same = ds && ds == s_markDs && rt && rt == bb;
    if (ds) ds->Release();
    if (rt) rt->Release();
    if (bb) bb->Release();
    if (!same) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("web: depth surface at CC is not the one marked -- CC not undone on the screen"); }
        return;
    }

    struct V { float x, y, z, rhw, u, v; };
    const float W = (float)w, H = (float)h;
    const V quad[4] = {
        { -0.5f,     -0.5f,     0.f, 1.f, 0.f, 0.f }, { W - 0.5f, -0.5f,     0.f, 1.f, 1.f, 0.f },
        { -0.5f,     H - 0.5f,  0.f, 1.f, 0.f, 1.f }, { W - 0.5f, H - 0.5f,  0.f, 1.f, 1.f, 1.f },
    };
    WsSavedState st{};
    D3DVIEWPORT9 vpOld{};
    __try {
        WsSave(dev, st);
        dev->GetViewport(&vpOld);
        D3DVIEWPORT9 vp{ 0, 0, (DWORD)w, (DWORD)h, 0.f, 1.f };
        dev->SetViewport(&vp);
        dev->SetVertexShader(nullptr);
        dev->SetPixelShader(nullptr);
        dev->SetVertexDeclaration(nullptr);
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
        dev->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
        dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
        dev->SetRenderState(D3DRS_STENCILREF, k_wsStencilBit);
        dev->SetRenderState(D3DRS_STENCILMASK, k_wsStencilBit);
        dev->SetRenderState(D3DRS_STENCILWRITEMASK, k_wsStencilBit);
        dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_ZERO);
        dev->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
        dev->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        dev->SetTexture(0, preCC);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { dev->SetViewport(&vpOld); WsRestore(dev, st); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void Web_Present(IDirect3DDevice9* dev)
{
    s_presentTid = GetCurrentThreadId();
    // engine gone: drop render-thread-owned tex + s_pixOut
    if (s_freePixels) {
        s_freePixels = false;
        WebReleaseTexture();
        std::vector<uint8_t>().swap(s_pixOut);
        s_outW = s_outH = 0;
    }
    if (g_eject || !g_webEnable || (s_steamDead && !s_wv2)) return;

    // size + volume follow menu live
    if (WebAlive()) {
        static int s_lastW = 0, s_lastH = 0;
        int rw, rh; WebRes(rw, rh);
        if (s_lastW != rw || s_lastH != rh) {
            s_lastW = rw; s_lastH = rh;
            WebPost(WS_SIZE, rw, rh);
        }
        static float s_lastVol = -1.f, s_lastAtten = -1.f;
        static DWORD s_lastVolMs = 0;
        const DWORD now = GetTickCount();
        // 1s re-apply (swapped <video> loses it); distance ramp faster but only on real change
        // (each push = cross-process JS)
        const float atten = g_webVolDistance ? s_distAtten : 1.f;
        const bool  moved = fabsf(atten - s_lastAtten) > 0.02f && now - s_lastVolMs > 150;
        if (s_lastVol != g_webVolume || moved || now - s_lastVolMs > 1000) {
            s_lastVol = g_webVolume; s_lastAtten = atten; s_lastVolMs = now;
            WebPost(WS_VOL);
        }
    }

    WebUploadPixels(dev);

    // state line every 5s: shows which link of chain is missing
    {
        static DWORD s_lastLog = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastLog > 5000) {
            s_lastLog = now;
            DebugLog("web: state browser=%u paints=%ld tex=%p %dx%d placed=%d panel=%d "
                     "mode=%d draws=%ld cqQ=%d cqRan=%d",
                     (unsigned)s_browser, s_paints, (void*)s_tex, s_texW, s_texH,
                     (int)s_placed, (int)g_webPanel, g_webDepthMode, s_draws,
                     s_cqQueued, (int)s_cqRan);
        }
    }

    if (g_webPanel) WebPanel();

    if (s_placed && g_webDepthMode == 2) WebDrawWorld(dev, false);
}

// ---- lifetime ----

void Web_LevelShutdown()
{
    // placement belongs to old map -> unplace + pause. Main thread; bake clears mesh under s_meshLock.
    if (s_placed) WebPost(WS_PAUSE);
    s_placed = false;
    WebBakeMesh();
    s_engineTrace = nullptr;
}

void Web_Destroy()
{
    if (s_wv2) WebWv2Drop(false);   // joins engine thread: nothing outlives DLL
    if (s_registered && s_pfnUnreg) {
        for (const WsCbReg& r : k_cbs) { __try { s_pfnUnreg(r.cb); } __except(EXCEPTION_EXECUTE_HANDLER) {} }
        if (s_pfnUnregCr && s_createCall) {
            __try { s_pfnUnregCr(&s_crReady, s_createCall); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        s_registered = false;
    }
    s_createCall = 0;
    if (s_surface && s_browser != INVALID_HTMLBROWSER) {
        __try { s_surface->RemoveBrowser(s_browser); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        s_browser = INVALID_HTMLBROWSER;
    }
    WebReleaseTexture();
    WebFreeAllPixels();
    s_created = false;
    s_placed  = false;
    WebBakeMesh();
}

const char* Web_Status()
{
    if (!g_webEnable) return "";
    if (s_steamDead && !s_wv2) return s_status;
    if (s_browser == INVALID_HTMLBROWSER) return s_status;
    if (!s_tex) return s_paints ? "painting, texture not up yet"
                                : "browser up, no paint delivered yet";
    return s_cqRefused ? "drawing (mat_queue_mode 0)" : "drawing";
}
