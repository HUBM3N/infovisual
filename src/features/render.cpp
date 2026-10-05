#include "../globals.h"
#include "../common.h"
#include "playerlist.h"
#include "websurface.h"
#include "embedded_image.h"
#include "../include/blur_shaders.h"
#include "drawutils.h"
#include "watermark.h"
#include "crosshair.h"
#include "menu.h"
#include "visuals.h"
#include "misc.h"
#include "render.h"
#include "killfeed.h"
#include "animclock.h"
#include "weaponhud.h"
#include "netgraph.h"
#include "screenfx.h"
#include "bloom.h"
#include "fx_colorcorr.h"   // k_fxColorCorrBytes -- compiled from fx_colorcorr.hlsl by fxc
#include "../doctrine_font.h"
#include "jobs.h"

Present_t    o_Present    = nullptr;
Reset_t      o_Reset      = nullptr;

// Logo: eye ring + iris textures (iris animated)
static constexpr int   kLogoTexW   = 220;
static constexpr int   kLogoTexH   = 134;
IDirect3DTexture9* g_logoTex     = nullptr; // eye ring only
IDirect3DTexture9* g_logoIrisTex = nullptr; // iris only (same canvas size)
// Watermark field icons (24x24 D3DPOOL_DEFAULT textures)
IDirect3DTexture9* g_wmIconFpsTex  = nullptr; // stats bars  -- fps
IDirect3DTexture9* g_wmIconPingTex = nullptr; // signal bars -- ping
IDirect3DTexture9* g_wmIconLossTex = nullptr; // globe       -- loss
IDirect3DTexture9* g_wmIconTimeTex = nullptr; // clock       -- time
IDirect3DTexture9* g_wmIconUserTex = nullptr; // person      -- username
// Menu tab icons (24x24)
IDirect3DTexture9* g_tabIconWatermarkTex = nullptr;
IDirect3DTexture9* g_tabIconNamesTex     = nullptr;
IDirect3DTexture9* g_tabIconSheenTex     = nullptr;
IDirect3DTexture9* g_tabIconVisualsTex   = nullptr;
IDirect3DTexture9* g_tabIconCrosshairTex = nullptr;
IDirect3DTexture9* g_tabIconMiscTex      = nullptr;
IDirect3DTexture9* g_tabIconSkinsTex     = nullptr;
IDirect3DTexture9* g_tabIconImagesTex    = nullptr;
IDirect3DTexture9* g_tabIconConfigTex    = nullptr;
IDirect3DTexture9* g_decorImageTex       = nullptr;
bool               g_wmShowLogo      = false;
bool               g_wmLogoGhost     = false; // semi-transparent logo behind leftmost text item
float              g_wmLogoGhostAlpha = 0.25f;
char               g_wmLogoImgPath[MAX_PATH] = {};
std::vector<IDirect3DTexture9*> g_wmLogoImgFrames;
std::vector<float>              g_wmLogoImgDelays;
int                g_wmLogoImgCurFrame   = 0;
float              g_wmLogoImgFrameTimer = 0.f;
int                g_wmLogoImgW      = 0;
int                g_wmLogoImgH      = 0;
bool               g_wmLogoImgNeedsLoad = false;
float              g_wmLogoImgScale  = 1.f;
float              g_wmLogoImgRotation = 0.f; // radians
bool               g_wmLogoImgColorize = false;

// Eye outline only (path1)
static const char k_EyeSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg version="1.1" style="display:block" viewBox="0 0 1911.4941 1188.2795" width="1911.4941" height="1188.2795" preserveAspectRatio="none" xmlns="http://www.w3.org/2000/svg"><path fill="#ffffff" d="m 908.9682,1.0835981 c 183.376,-7.937 366.016,27.8169999 532.856,104.3140019 213.23,99.362 388.19,265.307 469.67,488.737 -33.16,95.21 -92.28,190.39 -160.41,264.3 -195.56,212.15 -464.66,315.23 -748.29,328.76 -182.707,7.91 -364.694,-27.52 -531.091,-103.39 C 257.0312,984.3146 82.305203,818.5846 2.3353317e-6,594.1346 35.614203,495.7876 90.510203,405.5436 161.4682,328.6956 353.2992,118.3076 628.4872,14.152598 908.9682,1.0835981 Z M 142.4162,593.5746 c 73.267,187.47 222.607,319.47 404.771,399.27 132.188,56.92 274.913,85.24 418.817,83.13 242.51,-3.8 489.72,-85.52 661.27,-262.36 61.96,-62.74 110.2,-137.66 141.68,-220.03 -72.44,-186.369 -222.04,-318.887 -402.77,-398.432 -133.19,-57.682 -277.14,-86.325 -422.26,-84.021 -243.877,4.696 -490.575,86.178 -662.398,265.024 -60.766,62.186 -108.114,136.189 -139.11,217.419 z" /></svg>)svg";

// Iris pieces (paths 2-5) in the same 1911x1188 coordinate space
static const char k_IrisSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg version="1.1" style="display:block" viewBox="0 0 1911.4941 1188.2795" width="1911.4941" height="1188.2795" preserveAspectRatio="none" xmlns="http://www.w3.org/2000/svg"><path fill="#ffffff" d="m 936.6442,255.0536 c 19.56,-1.626 41.25,0.127 60.71,1.95 l -62.18,232.131 c -26.398,6.241 -41.689,15.26 -60.273,34.86 l -134.548,-36.023 -97.734,-26.245 c 14.301,-33.507 33.825,-64.534 57.846,-91.925 61.44,-69.882 143.695,-108.682 236.179,-114.748 z" /><path fill="#ffffff" d="m 1088.0042,281.4066 c 143.66,63.376 218.74,199.032 203.9,354.328 l -46.66,-12.55 c -60.95,-15.24 -124.59,-33.37 -185.49,-49.63 -6.17,-26.075 -14.91,-41.078 -34.09,-59.589 z" /><path fill="#ffffff" d="m 618.5022,552.4486 232.738,62.416 c 4.55,24.41 16.683,42.35 34.372,59.12 l -49.344,184.15 -12.927,48.16 c -144.022,-63.92 -219.201,-197.72 -204.839,-353.846 z" /><path fill="#ffffff" d="m 1034.6642,664.3246 c 5.32,0.73 20.23,4.99 25.73,6.47 l 47.11,12.76 159.96,42.84 c -33.03,74.58 -83.63,129.76 -155.75,168.23 -66.14,33.3 -124.38,42.64 -197.594,35.76 l 62.354,-232.72 c 23.85,-4.45 41.61,-16.2 58.19,-33.34 z" /></svg>)svg";

// Watermark field icons
static const char k_WmIconFpsSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Isolation_Mode" data-name="Isolation Mode" viewBox="0 0 24 24" width="512" height="512"><path d="M3,21V0H0V21a3,3,0,0,0,3,3H24V21Z"/><rect x="18" y="9" width="3" height="9"/><rect x="6" y="9" width="3" height="9"/><rect x="12" y="4" width="3" height="14"/></svg>)svg";

static const char k_WmIconPingSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Bold" viewBox="0 0 24 24" width="512" height="512"><path d="M12,6A3,3,0,0,0,9,9V21a3,3,0,0,0,6,0V9A3,3,0,0,0,12,6Z"/><path d="M21,0a3,3,0,0,0-3,3V21a3,3,0,0,0,6,0V3A3,3,0,0,0,21,0Z"/><path d="M3,12a3,3,0,0,0-3,3v6a3,3,0,0,0,6,0V15A3,3,0,0,0,3,12Z"/></svg>)svg";

static const char k_WmIconLossSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Bold" viewBox="0 0 24 24" width="512" height="512"><path d="M12,0A12,12,0,1,0,24,12,12.013,12.013,0,0,0,12,0Zm8.941,11H17.463a18.368,18.368,0,0,0-2.289-7.411A9.013,9.013,0,0,1,20.941,11ZM9.685,14h4.63A16.946,16.946,0,0,1,12,19.9,16.938,16.938,0,0,1,9.685,14Zm-.132-3A16.246,16.246,0,0,1,12,4.1,16.241,16.241,0,0,1,14.447,11ZM8.826,3.589A18.368,18.368,0,0,0,6.537,11H3.059A9.013,9.013,0,0,1,8.826,3.589ZM3.232,14H6.641a18.906,18.906,0,0,0,2.185,6.411A9.021,9.021,0,0,1,3.232,14Zm11.942,6.411A18.884,18.884,0,0,0,17.359,14h3.409A9.021,9.021,0,0,1,15.174,20.411Z"/></svg>)svg";

static const char k_WmIconTimeSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Bold" viewBox="0 0 24 24" width="512" height="512"><path d="M12,0A12,12,0,1,0,24,12,12.013,12.013,0,0,0,12,0Zm0,21a9,9,0,1,1,9-9A9.01,9.01,0,0,1,12,21Z"/><path d="M10.5,11.055l-2.4,1.5a1.5,1.5,0,0,0-.475,2.068h0a1.5,1.5,0,0,0,2.068.475l2.869-1.8a2,2,0,0,0,.938-1.7V7.772a1.5,1.5,0,0,0-1.5-1.5h0a1.5,1.5,0,0,0-1.5,1.5Z"/></svg>)svg";

static const char k_WmIconUserSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" version="1.1" id="Capa_1" x="0px" y="0px" viewBox="0 0 512 512" xml:space="preserve" width="512" height="512"><g><path d="M244.317,299.051c-90.917,8.218-160.183,85.041-158.976,176.32V480c0,17.673,14.327,32,32,32l0,0c17.673,0,32-14.327,32-32v-5.909c-0.962-56.045,40.398-103.838,96-110.933c58.693-5.82,110.992,37.042,116.812,95.735c0.344,3.47,0.518,6.954,0.521,10.441V480c0,17.673,14.327,32,32,32l0,0c17.673,0,32-14.327,32-32v-10.667c-0.104-94.363-76.685-170.774-171.047-170.67C251.854,298.668,248.082,298.797,244.317,299.051z"/><path d="M256.008,256c70.692,0,128-57.308,128-128S326.7,0,256.008,0s-128,57.308-128,128C128.078,198.663,185.345,255.929,256.008,256z M256.008,64c35.346,0,64,28.654,64,64s-28.654,64-64,64s-64-28.654-64-64S220.662,64,256.008,64z"/></g></svg>)svg";

// Menu tab icons
static const char k_TabIconWatermarkSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Bold" viewBox="0 0 24 24" width="512" height="512"><path d="M18.5,0H5.5A5.506,5.506,0,0,0,0,5.5v8A5.506,5.506,0,0,0,5.5,19h5v2H8a1.5,1.5,0,0,0,0,3h8a1.5,1.5,0,0,0,0-3H13.5V19h5A5.506,5.506,0,0,0,24,13.5v-8A5.506,5.506,0,0,0,18.5,0ZM5.5,3h13A2.5,2.5,0,0,1,21,5.5V13H3V5.5A2.5,2.5,0,0,1,5.5,3Z"/></svg>)svg";

static const char k_TabIconNamesSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Layer_1" data-name="Layer 1" viewBox="0 0 24 24" width="512" height="512"><path d="M17.5,0h-9A5.5,5.5,0,0,0,3.607,3H2.5a1.5,1.5,0,0,0,0,3H3V8H2.5a1.5,1.5,0,0,0,0,3H3v2H2.5a1.5,1.5,0,0,0,0,3H3v2H2.5a1.5,1.5,0,0,0,0,3H3.607A5.5,5.5,0,0,0,8.5,24h9A5.506,5.506,0,0,0,23,18.5V5.5A5.506,5.506,0,0,0,17.5,0ZM20,18.5A2.5,2.5,0,0,1,17.5,21h-9A2.5,2.5,0,0,1,6,18.5V5.5A2.5,2.5,0,0,1,8.5,3h9A2.5,2.5,0,0,1,20,5.5Zm-9.5-9a2.5,2.5,0,0,1,5,0A2.5,2.5,0,0,1,10.5,9.5Zm6.488,6.732a.665.665,0,0,1-.673.768H9.654a.665.665,0,0,1-.673-.768C9.842,11.965,16.128,11.968,16.988,16.232Z"/></svg>)svg";

static const char k_TabIconSheenSVG[] = R"svg(<svg id="Layer_1" height="512" viewBox="0 0 24 24" width="512" xmlns="http://www.w3.org/2000/svg" data-name="Layer 1"><path d="m17.937 2.27a8.839 8.839 0 0 0 -6.983-2.21 9.173 9.173 0 0 0 -5.1 15.75c.955.901 1.146 2.19 1.146 3.969v.153a4.049 4.049 0 0 0 4.021 4.068h1.958a4.049 4.049 0 0 0 4.021-4.068v-.523c0-1.67.15-2.709.942-3.415a9.223 9.223 0 0 0 0-13.724zm-3.937 17.662a1.046 1.046 0 0 1 -1.021 1.068h-1.958a1.046 1.046 0 0 1 -1.021-1.068v-.153c0-.259 0-.519-.014-.779h4.02c0 .137-.006.274-.006.409zm1.945-6.177a5.292 5.292 0 0 0 -1.488 2.245h-.957v-4.917a2.993 2.993 0 0 0 1.5-2.583 1.5 1.5 0 0 0 -3 0 1.5 1.5 0 0 0 -3 0 2.993 2.993 0 0 0 1.5 2.583v4.917h-1.038a6.061 6.061 0 0 0 -1.54-2.368 6.18 6.18 0 0 1 -1.871-5.313 6.087 6.087 0 0 1 5.249-5.278 5.989 5.989 0 0 1 .7-.041 5.861 5.861 0 0 1 3.94 1.509 6.222 6.222 0 0 1 .005 9.246z"/></svg>)svg";

static const char k_TabIconVisualsSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" version="1.1" id="Capa_1" x="0px" y="0px" viewBox="0 0 509.348 509.348" xml:space="preserve" width="512" height="512"><g><path d="M488.935,188.541C437.397,109.024,349.407,60.662,254.652,59.773C159.898,60.662,71.908,109.024,20.37,188.541c-27.16,39.859-27.16,92.279,0,132.139c51.509,79.566,139.504,127.978,234.283,128.896c94.754-0.889,182.744-49.251,234.283-128.768C516.153,280.919,516.153,228.429,488.935,188.541z M436.199,284.541c-39.348,62.411-107.769,100.488-181.547,101.035c-73.777-0.546-142.198-38.624-181.547-101.035c-12.267-18.022-12.267-41.712,0-59.733c39.348-62.411,107.769-100.488,181.547-101.035c73.777,0.546,142.198,38.624,181.547,101.035C448.466,242.829,448.466,266.519,436.199,284.541z"/><circle cx="254.652" cy="254.674" r="85.333"/></g></svg>)svg";

static const char k_TabIconCrosshairSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" version="1.1" id="Capa_1" x="0px" y="0px" viewBox="0 0 512 512" xml:space="preserve" width="512" height="512"><g><path d="M480,224H288V32c0-17.673-14.327-32-32-32s-32,14.327-32,32v192H32c-17.673,0-32,14.327-32,32s14.327,32,32,32h192v192c0,17.673,14.327,32,32,32s32-14.327,32-32V288h192c17.673,0,32-14.327,32-32S497.673,224,480,224z"/></g></svg>)svg";

static const char k_TabIconMiscSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Bold" viewBox="0 0 24 24" width="512" height="512"><path d="M8,7H22.5a1.5,1.5,0,0,0,0-3H8A1.5,1.5,0,0,0,8,7Z"/><path d="M22.5,11H8a1.5,1.5,0,0,0,0,3H22.5a1.5,1.5,0,0,0,0-3Z"/><path d="M22.5,18H8a1.5,1.5,0,0,0,0,3H22.5a1.5,1.5,0,0,0,0-3Z"/><circle cx="2.5" cy="5.5" r="2.5"/><circle cx="2.5" cy="12" r="2.5"/><circle cx="2.5" cy="19" r="2.5"/></svg>)svg";

static const char k_TabIconSkinsSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Bold" viewBox="0 0 24 24" width="512" height="512"><path d="M18.5,9.5A1.5,1.5,0,0,0,20,8V7.313A5.32,5.32,0,0,0,14.687,2H13.5V1.5a1.5,1.5,0,0,0-3,0V2H9.313A5.313,5.313,0,0,0,7.772,12.4l2.728.746V19H9.313A2.316,2.316,0,0,1,7,16.687V16a1.5,1.5,0,0,0-3,0v.687A5.32,5.32,0,0,0,9.313,22H10.5v.5a1.5,1.5,0,0,0,3,0V22h1.187a5.313,5.313,0,0,0,1.541-10.4L13.5,10.856V5h1.187A2.316,2.316,0,0,1,17,7.313V8A1.5,1.5,0,0,0,18.5,9.5Zm-3.118,4.979a2.314,2.314,0,0,1-.7,4.521H13.5V13.965ZM10.5,10.035,8.618,9.521A2.314,2.314,0,0,1,9.313,5H10.5Z"/></svg>)svg";

static const char k_TabIconImagesSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" id="Bold" viewBox="0 0 24 24" width="512" height="512"><path d="M19.5,0H4.5A4.505,4.505,0,0,0,0,4.5v15A4.505,4.505,0,0,0,4.5,24h15A4.505,4.505,0,0,0,24,19.5V4.5A4.505,4.505,0,0,0,19.5,0ZM4.5,3h15A1.5,1.5,0,0,1,21,4.5v15a1.492,1.492,0,0,1-.44,1.06l-8.732-8.732a4,4,0,0,0-5.656,0L3,15V4.5A1.5,1.5,0,0,1,4.5,3Z"/><circle cx="15.5" cy="7.5" r="2.5"/></svg>)svg";

static const char k_TabIconConfigSVG[] = R"svg(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" version="1.1" id="Capa_1" x="0px" y="0px" viewBox="0 0 512 512" xml:space="preserve" width="512" height="512"><g><path d="M228.267,56c-17.455-37.114-61.692-53.05-98.805-35.595C113.814,27.765,101.226,40.353,93.867,56H32C14.327,56,0,70.327,0,88l0,0c0,17.673,14.327,32,32,32h61.76c17.455,37.114,61.692,53.05,98.805,35.595c15.647-7.359,28.235-19.948,35.595-35.595H480c17.673,0,32-14.327,32-32l0,0c0-17.673-14.327-32-32-32H228.267z"/><path d="M351.04,181.333c-28.765,0.051-54.931,16.659-67.221,42.667H32c-17.673,0-32,14.327-32,32l0,0c0,17.673,14.327,32,32,32h251.733c17.455,37.114,61.692,53.05,98.805,35.595c15.647-7.359,28.235-19.948,35.595-35.595H480c17.673,0,32-14.327,32-32l0,0c0-17.673-14.327-32-32-32h-61.76C405.953,197.999,379.798,181.393,351.04,181.333z"/><path d="M160.96,349.333c-28.758,0.059-54.913,16.666-67.2,42.667H32c-17.673,0-32,14.327-32,32l0,0c0,17.673,14.327,32,32,32h61.76c17.455,37.114,61.692,53.05,98.805,35.595c15.647-7.359,28.235-19.948,35.595-35.595H480c17.673,0,32-14.327,32-32l0,0c0-17.673-14.327-32-32-32H228.267C215.963,365.965,189.756,349.352,160.96,349.333z"/></g></svg>)svg";

bool Font_MissingForFeature();

// overlay animation clock (animclock.h)
static std::atomic<double> s_animTime{0.0};
static float               s_animDt = 0.f;

double   AnimTime()   { return s_animTime.load(std::memory_order_relaxed); }
uint64_t AnimTimeMs() { return uint64_t(AnimTime() * 1000.0); }
float    AnimDt()     { return s_animDt; }

__declspec(noinline) static bool AnimEngineBool(int slot)
{
    if (!g_engine) return false;
    bool r = false;
    __try { r = VCall<bool>(g_engine, slot); } __except(EXCEPTION_EXECUTE_HANDLER) { r = false; }
    return r;
}

// Render thread, once per Present (engine interfaces are called from here and nowhere else).
static void AnimClock_Tick()
{
    static LARGE_INTEGER s_freq{}, s_last{};
    if (!s_freq.QuadPart) QueryPerformanceFrequency(&s_freq);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double real = (s_last.QuadPart && s_freq.QuadPart) ? double(now.QuadPart - s_last.QuadPart) / double(s_freq.QuadPart) : 0.0;
    s_last = now;

    static ConVar* s_hf = nullptr, *s_ts = nullptr, *s_dts = nullptr, *s_cheats = nullptr;
    if (g_cvar && !(s_hf && s_ts && s_dts && s_cheats)) {
        if (!s_hf)     s_hf     = g_cvar->FindVar("host_framerate");
        if (!s_ts)     s_ts     = g_cvar->FindVar("host_timescale");
        if (!s_dts)    s_dts    = g_cvar->FindVar("demo_timescale");
        if (!s_cheats) s_cheats = g_cvar->FindVar("sv_cheats");
    }
    const AnimClockIn in = {
        real,
        s_hf  ? s_hf->GetFloat()  : 0.f,
        s_ts  ? s_ts->GetFloat()  : 1.f,
        s_dts ? s_dts->GetFloat() : 1.f,
        s_cheats && s_cheats->GetFloat() != 0.f,
        AnimEngineBool(animslot::engIsPlayingDemo),
        AnimEngineBool(animslot::engIsPaused),
    };
    const double dt = AnimClockStep(in);
    s_animDt = float(dt);
    s_animTime.store(AnimTime() + dt, std::memory_order_relaxed);

    // Edge-triggered: what drives the clock, logged when it changes.
    const int   mode = AnimClockMode(in);
    const float val  = mode == 1 ? in.hostFramerate : mode == 2 ? in.hostTimescale : 0.f;
    const float dsc  = in.demo ? in.demoTimescale : 0.f;
    static int s_mode = -1; static float s_val = -1.f, s_dsc = -1.f;
    if (mode != s_mode || val != s_val || dsc != s_dsc) {
        s_mode = mode; s_val = val; s_dsc = dsc;
        static constexpr const char* k_names[] = { "real time", "host_framerate", "host_timescale", "paused" };
        DebugLog("animclock: %s %.3f (cvars hf=%d ts=%d demo_ts=%d cheats=%d), demo %d x%.2f", k_names[mode], val,
                 s_hf != nullptr, s_ts != nullptr, s_dts != nullptr, s_cheats != nullptr,
                 (int)in.demo, in.demoTimescale);
    }
}

HRESULT __stdcall h_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND win, const RGNDATA* dirty)
{
    if (g_eject) return o_Present(dev, src, dst, win, dirty);

    ++g_hookDepth;
    ++g_presentCount;
    struct Dec { ~Dec() { --g_hookDepth; } } _dec;

    // one depth resolve per frame; this hook = render thread (post slot lands here only w/ multicore off, see screenfx.h)
    ScreenFx_NewFrame();

    if (!g_imguiInit)
    {
        SetPhase("present_init");
        D3DDEVICE_CREATION_PARAMETERS cp{};
        dev->GetCreationParameters(&cp);
        g_hwnd = cp.hFocusWindow;
        if (!g_hwnd) goto chain;

        ImGui::CreateContext();
        {
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

            ImFontConfig cfg;
            cfg.OversampleH = 3; cfg.OversampleV = 1; cfg.PixelSnapH = true;
            ImFont* segoe = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.f, &cfg,
                io.Fonts->GetGlyphRangesDefault());
            io.FontDefault = segoe ? segoe : io.Fonts->AddFontDefault();

            ImFontConfig wm_cfg;
            wm_cfg.OversampleH = g_wmFontFlags[2] ? 2 : 3;
            wm_cfg.OversampleV = 1;
            wm_cfg.PixelSnapH  = g_wmFontFlags[1] || g_wmFontFlags[3];
            wm_cfg.RasterizerMultiply = g_wmFontFlags[0] ? 1.4f : 1.0f;
            if (g_wmFontFlags[1]) { wm_cfg.OversampleH = 1; wm_cfg.OversampleV = 1; }
            std::string wmPath = GetWmFontPath();
            ImFont* wm = io.Fonts->AddFontFromFileTTF(wmPath.c_str(),
                15.f * g_watermarkScale, &wm_cfg, io.Fonts->GetGlyphRangesDefault());
            if (!wm) wm = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",
                15.f * g_watermarkScale, &wm_cfg, io.Fonts->GetGlyphRangesDefault());
            g_wmFont = wm ? wm : io.FontDefault;
            g_wmFontBuiltAtScale = g_watermarkScale;
            g_wmFontBuiltAtIndex = g_wmFontIndex;
            memcpy(g_wmFontFlagsBuilt, g_wmFontFlags, sizeof(g_wmFontFlags));
            // mp + menu fonts: fallback now, sentinels force dirty -> real build next frame
            g_mpFont = g_wmFont;
            g_mpFontBuiltAtScale = -1.f;
            g_mpFontBuiltAtIndex = -2;
            memset(g_mpFontFlagsBuilt, 0xFF, sizeof(g_mpFontFlagsBuilt));
            g_menuFont = io.FontDefault;
            g_menuFontBuiltAtIndex = -2;
            memset(g_menuFontFlagsBuilt, 0xFF, sizeof(g_menuFontFlagsBuilt));
        }

        SetupImGuiStyle();
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX9_Init(dev);
        g_d3dDev = dev;
        g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
        g_imguiInit = true;
        g_loadingCircleStartMs = GetTickCount();
        Logo_CreateTexture(dev);
        DebugLog("imgui_init ok hwnd=0x%IX", (uintptr_t)g_hwnd);
    }

chain:
    if (g_imguiInit)
    {
        AnimClock_Tick();   // before anything draws: every overlay animation this frame reads the same dt

        // periodic updates on render thread, not a poll thread: engine interfaces race otherwise
        static DWORD s_lastUpdate = 0;
        DWORD now = GetTickCount();
        DLightTick();

        if (now - s_lastUpdate > 100) {
            s_lastUpdate = now;
            UpdateNetStats();
            UpdateVisualsCvars();
            // mp WinRT poll thread runs only while enabled; stop = flag (seen within its 1 s wait),
            // handle closed here once exited, no restart till then. Whoever swaps handle out closes it (eject too).
            if (!g_enableMediaPlayer && g_mpRunning) g_mpRunning = false;
            if (!g_mpRunning && g_mpThread && WaitForSingleObject(g_mpThread, 0) == WAIT_OBJECT_0) {
                if (HANDLE h = InterlockedExchangePointer(&g_mpThread, nullptr)) CloseHandle(h);
            }
            if (g_enableMediaPlayer && !g_mpRunning && !g_mpThread && !g_eject) {
                g_mpRunning = true;
                g_mpThread  = CreateThread(nullptr, 0, MediaPlayerThread, nullptr, 0, nullptr);
                if (g_mpThread) SetThreadPriority(g_mpThread, THREAD_PRIORITY_BELOW_NORMAL);
                else            g_mpRunning = false;
            }
            // loopback capture: lazy start, stop when unused; thread polls 16 ms -> short join
            if (g_wfRunning && !(g_enableMediaPlayer && g_mpWaveform)) {
                g_wfRunning = false;
                if (g_wfThread) { WaitForSingleObject(g_wfThread, 250); CloseHandle(g_wfThread); g_wfThread = nullptr; }
                for (int i = 0; i < 5; ++i) g_wfBands[i] = 0.f;
            }
            EnsureEntityList();
            ResolveNetvars();
            TryInstallPropHooks();
            // never ApplyMapTextures here (lives in FSN RENDER_START): Present runs during map loads, world mats being freed
            PlayerList_ScanServer();
            NetGraph_Tick();
            ScreenFx_Tick();   // mat_queue_mode 0 while a depth pass is on
        }

        SetPhase("present_fontcheck");
        Font_Pump();   // swap in a finished background atlas build, before the dirty check reads the stamps
        {
            // Compare against what was actually built for mp (copy-wm uses wm values).
            const float  mpBuiltScale = g_mpFontBuiltAtScale;
            const int    mpBuiltIdx   = g_mpFontBuiltAtIndex;
            const bool*  mpBuiltFlags = g_mpFontFlagsBuilt;
            const float  mpCurScale   = g_mpCopyWmStyle ? g_watermarkScale : g_mpScale;
            const int    mpCurIdx     = g_mpCopyWmStyle ? g_wmFontIndex    : g_mpFontIndex;
            const bool*  mpCurFlags   = g_mpCopyWmStyle ? g_wmFontFlags    : g_mpFontFlags;

            bool fontDirty =
                g_watermarkScale != g_wmFontBuiltAtScale ||
                g_wmFontIndex    != g_wmFontBuiltAtIndex ||
                memcmp(g_wmFontFlags, g_wmFontFlagsBuilt, sizeof(g_wmFontFlags)) != 0 ||
                mpCurScale != mpBuiltScale ||
                mpCurIdx   != mpBuiltIdx   ||
                memcmp(mpCurFlags, mpBuiltFlags, sizeof(g_mpFontFlagsBuilt)) != 0 ||
                g_speedoScale      != g_speedoFontBuiltAtScale ||
                g_speedoFontIndex  != g_speedoFontBuiltAtIndex ||
                memcmp(g_speedoFontFlags, g_speedoFontFlagsBuilt, sizeof(g_speedoFontFlags)) != 0 ||
                g_menuFontScale    != g_menuFontBuiltAtScale ||
                g_menuFontIndex    != g_menuFontBuiltAtIndex ||
                memcmp(g_menuFontFlags, g_menuFontFlagsBuilt, sizeof(g_menuFontFlags)) != 0 ||
                g_kfFontScale != g_kfFontBuiltAtScale ||
                g_kfFontIndex != g_kfFontBuiltAtIndex ||
                memcmp(g_kfFontFlags, g_kfFontFlagsBuilt, sizeof(g_kfFontFlags)) != 0 ||
                g_wpnHudNumScale      != g_wpnHudNumFontBuiltAtScale ||
                g_wpnHudNumFontIndex  != g_wpnHudNumFontBuiltAtIndex ||
                memcmp(g_wpnHudNumFontFlags, g_wpnHudNumFontFlagsBuilt, sizeof(g_wpnHudNumFontFlags)) != 0 ||
                g_wpnHudNameScale     != g_wpnHudNameFontBuiltAtScale ||
                g_wpnHudNameFontIndex != g_wpnHudNameFontBuiltAtIndex ||
                memcmp(g_wpnHudNameFontFlags, g_wpnHudNameFontFlagsBuilt, sizeof(g_wpnHudNameFontFlags)) != 0 ||
                g_mpLyricsScale != g_mpLyricsBuiltAtScale ||
                Font_MissingForFeature();

            if (fontDirty && g_fontDirtyAt == 0)
                g_fontDirtyAt = GetTickCount();
            else if (!fontDirty)
                g_fontDirtyAt = 0;

            // Fire rebuild 500 ms after last change -- slider drags don't rebuild every frame.
            // The build runs on a worker; RebuildWatermarkFont no-ops while one is in flight.
            if (g_fontDirtyAt != 0 && (GetTickCount() - g_fontDirtyAt) >= 500)
                RebuildWatermarkFont();
        }

        SetPhase("present_updatefps");
        UpdateFPS();

        // Capture backbuffer and run GPU blur passes BEFORE ImGui so DrawBgBlurRect
        // is just a plain AddImageRounded — no mid-render RT switching.
        // A blur pass nobody samples is pure waste, and the menu one is the
        // expensive case: it is a full-screen multi-pass blur that used to run
        // every frame whether or not the menu was open.
        // g_bgBlur feeds both the watermark box and the media player card, so it
        // stays alive if either of those is drawing.
        const bool wantWmBlur   = g_bgBlur && (g_enableOverlay || g_enableMediaPlayer);
        const bool wantMenuBlur = g_menuBgBlur && Menu_Visible();

        // Bloom + grade normally ran already, pre-hud (SceneFx_PreHud from Push2DView). Only a
        // frame where that point never fired gets them here, over the hud.
        SceneFx_PresentFallback(dev);

        if (g_blurBBSurf) { g_blurBBSurf->Release(); g_blurBBSurf = nullptr; }
        if (wantWmBlur || wantMenuBlur) {
            if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &g_blurBBSurf))) {
                D3DSURFACE_DESC bbDesc{};
                g_blurBBSurf->GetDesc(&bbDesc);
                g_blurBBW = (int)bbDesc.Width;
                g_blurBBH = (int)bbDesc.Height;
                BlurRT_Init(dev);
                float sigma = std::max(wantWmBlur   ? g_bgBlurSigma     : 0.f,
                                       wantMenuBlur ? g_menuBgBlurSigma : 0.f);
                // one shared texture: menu open = watermark panels get the wide blur too
                BlurFrame_Run(dev, sigma, wantMenuBlur);
            }
        }
        // Two full-screen CC targets are dead weight once the feature is off.
        if (!g_colorCorrEnable && g_ccShader) ColorCorr_Destroy();
        // Same for bloom's three. Safe here: the pre-hud pass runs on this same render
        // thread (queued or direct), so nothing of ours can be mid-pass during Present.
        if (!g_bloomEnable) Bloom_Destroy();
        // Blur targets: keyed on the settings, not on the menu being open, or every menu
        // open would rebuild them. DrawBgBlurRect skips when they are gone.
        if (!g_bgBlur && !g_menuBgBlur && g_blurTexA) BlurRT_Destroy();

        SetPhase("present_newframe");
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        // The style only varies with the accent + text color, so rewriting all 55
        // ImVec4s and the spacing block every frame buys nothing.
        {
            static float s_styleAccent[3] = { -1.f, -1.f, -1.f };
            static float s_styleText[3]   = { -1.f, -1.f, -1.f };
            if (memcmp(s_styleAccent, g_accentColor,   sizeof(s_styleAccent)) != 0 ||
                memcmp(s_styleText,   g_menuTextColor, sizeof(s_styleText))   != 0) {
                memcpy(s_styleAccent, g_accentColor,   sizeof(s_styleAccent));
                memcpy(s_styleText,   g_menuTextColor, sizeof(s_styleText));
                SetupImGuiStyle();
            }
        }

        // Player join notification timer + render
        if (g_playerJoinNotifyTimer > 0.f) {
            g_playerJoinNotifyTimer -= AnimDt();
            if (g_playerJoinNotifyTimer < 0.f) g_playerJoinNotifyTimer = 0.f;
            ImGuiIO& _io = ImGui::GetIO();
            PlayerList_RenderNotify(ImGui::GetBackgroundDrawList(),
                _io.DisplaySize.x, _io.DisplaySize.y);
        }

        ImGui::GetIO().MouseDrawCursor = g_showMenu;
        if (g_showMenu) ClipCursor(nullptr);

        LogoIrisAnimTick();
        FilePick_Service();

        SetPhase("present_watermark");
        if (g_enableOverlay) RenderWatermarkAndOverlay();

        SetPhase("present_crosshair");
        RenderCrosshair();

        SetPhase("present_speedometer");
        if (g_speedoEnable || g_speedoGraph) RenderSpeedometer();

        SetPhase("present_mediaplayer");
        if (g_enableMediaPlayer) RenderMediaPlayer();
        else                     MediaPlayer_Drain();   // the poll thread parks thumbs either way

        SetPhase("present_images");
        RenderOverlayImages();

        SetPhase("present_web");
        Web_Present(dev);   // paint upload, the browser panel, and the always-on-top world draw

        SetPhase("present_snow");
        if (Menu_Visible()) RenderMenuSnow(Menu_Alpha());
        SetPhase("present_menu");
        if (Menu_Visible()) RenderMenu();
        SetPhase("present_weaponhud");
        WeaponHud_Tick(dev);

        SetPhase("present_killfeed");
        KillfeedTick();
        // When custom killfeed is off, keep hud_deathnotice_time at the user's value.
        // KillfeedTick forces it to 0 while active; on disable we restore it once.
        {
            static bool s_kfWasActive = false;
            bool kfNowActive = g_customKillfeedEnable && g_selfKillfeedOnly;
            if (s_kfWasActive && !kfNowActive)
                KillfeedRestoreNoticeTime();
            s_kfWasActive = kfNowActive;
        }
        SetPhase("present_minesweeper");
        RenderMinesweeper();

        RenderLoadingCircle();

        SetPhase("present_endframe");
        ImGui::EndFrame();
        ImGui::Render();
        Menu_RenderDrawData(ImGui::GetDrawData());

        if (g_blurBBSurf) { g_blurBBSurf->Release(); g_blurBBSurf = nullptr; }

        SetPhase("present_done");
    }
    return o_Present(dev, src, dst, win, dirty);
}

HRESULT __stdcall h_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    SetPhase("reset");
    DebugLog("h_Reset called imguiInit=%d", (int)g_imguiInit);
    if (g_imguiInit) ImGui_ImplDX9_InvalidateDeviceObjects();
    Logo_InvalidateTexture();    // D3DPOOL_DEFAULT lost on reset
    MediaPlayer_Invalidate();    // D3DPOOL_DEFAULT thumb lost on reset
    WeaponHud_InvalidateTextures();
    CsgoCrosshair_ReleaseTextures(); // reticle atlas -- rebuilt on the next draw
    Web_InvalidateTexture();     // browser page texture, DEFAULT pool on a d3d9ex device
    KillfeedUnload();            // icon sheets, DEFAULT pool
    if (g_blurBBSurf) { g_blurBBSurf->Release(); g_blurBBSurf = nullptr; }
    BlurRT_Destroy();         // release blur RTs before device reset
    Menu_ReleaseLayer();      // menu fade RT
    ColorCorr_Destroy();      // release color correction RT before device reset
    Bloom_Destroy();          // two shaders + three DEFAULT-pool targets, same story
    // Glow + flip world RTs are material-system textures: the engine recreates them on
    // reset. Releasing them here killed both features until the next map load.
    ScreenFx_Destroy();      // ao/dof targets + the resolved INTZ, all D3DPOOL_DEFAULT
    HRESULT hr = o_Reset(dev, pp);
    DebugLog("h_Reset result=0x%08X", (unsigned)hr);
    // IMaterial* survive a device reset, so no material epoch bump here: it used to wipe
    // the map-texture classification cache and re-run the whole world pass on alt-tab.
    if (g_imguiInit && SUCCEEDED(hr)) {
        ImGui_ImplDX9_CreateDeviceObjects();
        Logo_CreateTexture(dev);
    }
    SetPhase("reset_done");
    return hr;
}

// ---- ImGui style -------------------------------------------
void SetupImGuiStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 8.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 3.0f;
    s.PopupRounding     = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 999.0f;  // pill-shaped grab
    s.TabRounding       = 4.0f;
    s.WindowBorderSize  = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
    s.ChildBorderSize   = 0.0f;
    s.WindowPadding     = ImVec2(10, 8);
    s.FramePadding      = ImVec2(7, 5);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(5, 4);
    s.IndentSpacing     = 14.0f;
    s.ScrollbarSize     = 6.0f;
    s.GrabMinSize       = 8.0f;

    const float ar = g_accentColor[0], ag = g_accentColor[1], ab = g_accentColor[2];

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = ImVec4(0.035f, 0.035f, 0.043f, 0.97f); // #090909 darker
    c[ImGuiCol_ChildBg]              = ImVec4(0.000f, 0.000f, 0.000f, 0.00f); // transparent
    c[ImGuiCol_PopupBg]              = ImVec4(0.035f, 0.035f, 0.043f, 0.98f);
    c[ImGuiCol_FrameBg]              = ImVec4(1.000f, 1.000f, 1.000f, 0.03f); // rgba(255,255,255,0.03)
    c[ImGuiCol_FrameBgHovered]       = ImVec4(1.000f, 1.000f, 1.000f, 0.05f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(1.000f, 1.000f, 1.000f, 0.07f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.051f, 0.051f, 0.059f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.051f, 0.051f, 0.059f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.051f, 0.051f, 0.059f, 0.80f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(1.000f, 1.000f, 1.000f, 0.10f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.000f, 1.000f, 1.000f, 0.16f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(1.000f, 1.000f, 1.000f, 0.22f);
    c[ImGuiCol_CheckMark]            = ImVec4(ar, ag, ab, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(ar, ag, ab, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(ar*1.2f>1.f?1.f:ar*1.2f, ag*1.2f>1.f?1.f:ag*1.2f, ab*1.2f>1.f?1.f:ab*1.2f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(1.000f, 1.000f, 1.000f, 0.03f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(1.000f, 1.000f, 1.000f, 0.06f);
    c[ImGuiCol_ButtonActive]         = ImVec4(1.000f, 1.000f, 1.000f, 0.10f);
    c[ImGuiCol_Header]               = ImVec4(1.000f, 1.000f, 1.000f, 0.03f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(1.000f, 1.000f, 1.000f, 0.06f);
    c[ImGuiCol_HeaderActive]         = ImVec4(1.000f, 1.000f, 1.000f, 0.10f);
    c[ImGuiCol_Separator]            = ImVec4(1.000f, 1.000f, 1.000f, 0.06f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(1.000f, 1.000f, 1.000f, 0.10f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(1.000f, 1.000f, 1.000f, 0.14f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(1.00f,  1.00f,  1.00f,  0.06f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(1.00f,  1.00f,  1.00f,  0.12f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(1.00f,  1.00f,  1.00f,  0.20f);
    c[ImGuiCol_Tab]                  = ImVec4(1.000f, 1.000f, 1.000f, 0.02f);
    c[ImGuiCol_TabHovered]           = ImVec4(1.000f, 1.000f, 1.000f, 0.05f);
    c[ImGuiCol_TabActive]            = ImVec4(1.000f, 1.000f, 1.000f, 0.07f);
    const float tr = g_menuTextColor[0], tg = g_menuTextColor[1], tb = g_menuTextColor[2];
    c[ImGuiCol_Text]                 = ImVec4(tr, tg, tb, 1.00f);
    // disabled = same hue, dimmed to ~57% (matches old #DCDCDC -> #7E7E84 ratio)
    c[ImGuiCol_TextDisabled]         = ImVec4(tr * 0.573f, tg * 0.573f, tb * 0.601f, 1.00f);
    c[ImGuiCol_TextSelectedBg]       = ImVec4(ar, ag, ab, 0.25f);
    c[ImGuiCol_Border]               = ImVec4(1.000f, 1.000f, 1.000f, 0.06f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f,  0.00f,  0.00f,  0.00f);
    c[ImGuiCol_NavHighlight]         = ImVec4(ar, ag, ab, 0.70f);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f,  0.00f,  0.00f,  0.45f);
}

// ---- Overlay render ----------------------------------------
ImFont* g_wmFont             = nullptr;
float   g_wmFontBuiltAtScale = 0.f;
int     g_wmFontBuiltAtIndex = -2; // sentinel: force first build
ImFont* g_mpFont             = nullptr;
float   g_mpFontBuiltAtScale = 0.f;
int     g_mpFontBuiltAtIndex = -2;
ImFont* g_mpLyricsFont        = nullptr;
float   g_mpLyricsBuiltAtScale = 0.f;
ImFont* g_speedoFont            = nullptr;
ImFont* g_msFont                = nullptr; // Minesweeper cell numbers -- aliased bold
ImFont* g_menuFont              = nullptr; // Menu UI font (custom, built at 16px)
ImFont* g_doctrineFont          = nullptr; // Doctrine W00 Regular (speedometer pre-jump parens)
ImFont* g_kfFont                = nullptr; // Custom killfeed font
float   g_kfFontBuiltAtScale    = -1.f;
int     g_kfFontBuiltAtIndex    = -2;
bool    g_kfFontFlagsBuilt[4]   = {};
ImFont* g_wpnHudNumFont              = nullptr;
float   g_wpnHudNumFontBuiltAtScale  = -1.f;
int     g_wpnHudNumFontBuiltAtIndex  = -2;
bool    g_wpnHudNumFontFlagsBuilt[4] = {};
ImFont* g_wpnHudNameFont             = nullptr;
float   g_wpnHudNameFontBuiltAtScale = -1.f;
int     g_wpnHudNameFontBuiltAtIndex = -2;
bool    g_wpnHudNameFontFlagsBuilt[4] = {};
IDirect3DDevice9* g_d3dDev   = nullptr;
DWORD             g_fontDirtyAt = 0; // GetTickCount of last font setting change

// Returns the full path to use for the watermark font.
// Falls back to segoeui (honoring the legacy weight selection) if no custom font is chosen.
std::string GetWmFontPath()
{
    if (g_wmFontIndex >= 0 && g_wmFontIndex < (int)g_fontList.size())
        return g_fontList[g_wmFontIndex].fullPath;
    // Legacy weight fallback
    if (g_wmFontWeight == 2) return "C:\\Windows\\Fonts\\segoeuib.ttf";
    if (g_wmFontWeight == 1) return "C:\\Windows\\Fonts\\seguisb.ttf";
    return "C:\\Windows\\Fonts\\segoeui.ttf";
}

// ---- Logo texture management -------------------------------
// Rasterizes an SVG string into a D3DPOOL_DEFAULT texture.  Returns nullptr on
// failure.  Caller must Release() the returned pointer when done.
static IDirect3DTexture9* Logo_RasterizeSVG(IDirect3DDevice9* dev,
    const char* svgData, size_t svgLen, const char* label)
{
    char* buf = new char[svgLen];
    memcpy(buf, svgData, svgLen);

    NSVGimage* img = nsvgParse(buf, "px", 96.f);
    delete[] buf;
    if (!img) { DebugLog("logo[%s]: nsvgParse failed", label); return nullptr; }
    if (img->width <= 0.f || img->height <= 0.f)
    {
        DebugLog("logo[%s]: zero size", label); nsvgDelete(img); return nullptr;
    }

    const float scaleX = (float)kLogoTexW / img->width;
    const float scaleY = (float)kLogoTexH / img->height;
    const float scale  = (scaleX < scaleY) ? scaleX : scaleY;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) { nsvgDelete(img); return nullptr; }

    auto* pixels = new uint8_t[(size_t)kLogoTexW * kLogoTexH * 4]();
    nsvgRasterize(rast, img, 0.f, 0.f, scale,
                  pixels, kLogoTexW, kLogoTexH, kLogoTexW * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);

    // SYSTEMMEM staging -- D3DPOOL_DEFAULT
    IDirect3DTexture9* stage = nullptr;
    HRESULT hr = dev->CreateTexture(kLogoTexW, kLogoTexH, 1, 0,
        D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &stage, nullptr);
    if (FAILED(hr)) { delete[] pixels; return nullptr; }

    D3DLOCKED_RECT lr{};
    hr = stage->LockRect(0, &lr, nullptr, 0);
    if (FAILED(hr)) { stage->Release(); delete[] pixels; return nullptr; }
    {
        int rowPitch = lr.Pitch / 4;
        auto* dst = reinterpret_cast<uint32_t*>(lr.pBits);
        for (int y = 0; y < kLogoTexH; ++y)
        for (int x = 0; x < kLogoTexW; ++x)
        {
            int si = (y * kLogoTexW + x) * 4;
            uint8_t r = pixels[si+0], g = pixels[si+1],
                    b = pixels[si+2], a = pixels[si+3];
            dst[y * rowPitch + x] =
                ((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;
        }
    }
    stage->UnlockRect(0);
    delete[] pixels;

    IDirect3DTexture9* tex = nullptr;
    hr = dev->CreateTexture(kLogoTexW, kLogoTexH, 0, D3DUSAGE_AUTOGENMIPMAP,
        D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
    if (FAILED(hr)) { stage->Release(); return nullptr; }

    hr = dev->UpdateTexture(stage, tex);
    stage->Release();
    if (FAILED(hr)) { tex->Release(); return nullptr; }

    tex->GenerateMipSubLevels();

    DebugLog("logo[%s]: texture OK %dx%d scale=%.4f", label, kLogoTexW, kLogoTexH, scale);
    return tex;
}

void Logo_InvalidateTexture()
{
    if (g_logoTex)     { g_logoTex->Release();     g_logoTex     = nullptr; }
    if (g_logoIrisTex) { g_logoIrisTex->Release(); g_logoIrisTex = nullptr; }
    for (auto* t : g_wmLogoImgFrames) if (t) t->Release();
    g_wmLogoImgFrames.clear(); g_wmLogoImgDelays.clear();
    g_wmLogoImgCurFrame = 0; g_wmLogoImgFrameTimer = 0.f;
    if (g_wmLogoImgPath[0]) g_wmLogoImgNeedsLoad = true; // reload after reset
    // Watermark field icons
    auto releaseIcon = [](IDirect3DTexture9*& p) { if (p) { p->Release(); p = nullptr; } };
    releaseIcon(g_wmIconFpsTex);  releaseIcon(g_wmIconPingTex);
    releaseIcon(g_wmIconLossTex); releaseIcon(g_wmIconTimeTex); releaseIcon(g_wmIconUserTex);
    // Menu tab icons
    releaseIcon(g_tabIconWatermarkTex); releaseIcon(g_tabIconNamesTex);
    releaseIcon(g_tabIconSheenTex);     releaseIcon(g_tabIconVisualsTex);
    releaseIcon(g_tabIconCrosshairTex); releaseIcon(g_tabIconMiscTex);
    releaseIcon(g_tabIconSkinsTex);     releaseIcon(g_tabIconImagesTex);
    releaseIcon(g_tabIconConfigTex);
    releaseIcon(g_decorImageTex);
}

// Every D3D9 object this DLL owns, in one place. h_Reset only drops the
// D3DPOOL_DEFAULT ones because the rest survive a reset; eject has to drop all
// of them or the refs outlive the module.
void Render_ReleaseAllTextures()
{
    Logo_InvalidateTexture();
    WeaponHud_InvalidateTextures();
    CsgoCrosshair_ReleaseTextures();
    KillfeedUnload();
    MediaPlayer_Invalidate();
    if (g_blurBBSurf) { g_blurBBSurf->Release(); g_blurBBSurf = nullptr; }
    BlurRT_Destroy();
    Menu_ReleaseLayer();
    ColorCorr_Destroy();
    Bloom_Destroy();
    ScreenFx_Destroy();
    Web_Destroy();          // the browser, its texture, and the steam callbacks
    for (int i = 0; i < k_imgMaxSlots; ++i)
        g_imgSlots[i].ReleaseTextures();
}

// Rasterizes an SVG into a small (kIconSz x kIconSz) D3DPOOL_DEFAULT texture for icons.
static IDirect3DTexture9* Icon_RasterizeSVG(IDirect3DDevice9* dev,
    const char* svgData, size_t svgLen, const char* label)
{
    static const int kIconSz = 24;
    char* buf = new char[svgLen];
    memcpy(buf, svgData, svgLen);
    NSVGimage* img = nsvgParse(buf, "px", 96.f);
    delete[] buf;
    if (!img || img->width <= 0.f || img->height <= 0.f) {
        if (img) nsvgDelete(img);
        DebugLog("icon[%s]: parse failed", label); return nullptr;
    }
    const float scale = std::min((float)kIconSz / img->width, (float)kIconSz / img->height);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) { nsvgDelete(img); return nullptr; }
    auto* pixels = new uint8_t[(size_t)kIconSz * kIconSz * 4]();
    nsvgRasterize(rast, img, 0.f, 0.f, scale, pixels, kIconSz, kIconSz, kIconSz * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    IDirect3DTexture9* stage = nullptr;
    HRESULT hr = dev->CreateTexture(kIconSz, kIconSz, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &stage, nullptr);
    if (FAILED(hr)) { delete[] pixels; return nullptr; }
    D3DLOCKED_RECT lr{};
    hr = stage->LockRect(0, &lr, nullptr, 0);
    if (FAILED(hr)) { stage->Release(); delete[] pixels; return nullptr; }
    {
        int rowPitch = lr.Pitch / 4;
        auto* dst = reinterpret_cast<uint32_t*>(lr.pBits);
        for (int y = 0; y < kIconSz; ++y)
        for (int x = 0; x < kIconSz; ++x) {
            int si = (y * kIconSz + x) * 4;
            uint8_t a = pixels[si+3];
            // Store as white-on-transparent so AddImage tinting works correctly.
            // SVG paths rasterize as colored (often black) pixels; we only need
            // the alpha channel as coverage -- RGB tint supplies the actual color.
            dst[y * rowPitch + x] = ((uint32_t)a << 24) | 0x00FFFFFF;
        }
    }
    stage->UnlockRect(0);
    delete[] pixels;
    IDirect3DTexture9* tex = nullptr;
    hr = dev->CreateTexture(kIconSz, kIconSz, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr);
    if (FAILED(hr)) { stage->Release(); return nullptr; }
    hr = dev->UpdateTexture(stage, tex);
    stage->Release();
    if (FAILED(hr)) { tex->Release(); return nullptr; }
    return tex;
}

void Logo_CreateTexture(IDirect3DDevice9* dev)
{
    if (!dev) return;
    if (!g_logoTex)
        g_logoTex     = Logo_RasterizeSVG(dev, k_EyeSVG,  sizeof(k_EyeSVG),  "eye");
    if (!g_logoIrisTex)
        g_logoIrisTex = Logo_RasterizeSVG(dev, k_IrisSVG, sizeof(k_IrisSVG), "iris");
    // Watermark field icons
    if (!g_wmIconFpsTex)  g_wmIconFpsTex  = Icon_RasterizeSVG(dev, k_WmIconFpsSVG,  sizeof(k_WmIconFpsSVG),  "wm_fps");
    if (!g_wmIconPingTex) g_wmIconPingTex = Icon_RasterizeSVG(dev, k_WmIconPingSVG, sizeof(k_WmIconPingSVG), "wm_ping");
    if (!g_wmIconLossTex) g_wmIconLossTex = Icon_RasterizeSVG(dev, k_WmIconLossSVG, sizeof(k_WmIconLossSVG), "wm_loss");
    if (!g_wmIconTimeTex) g_wmIconTimeTex = Icon_RasterizeSVG(dev, k_WmIconTimeSVG, sizeof(k_WmIconTimeSVG), "wm_time");
    if (!g_wmIconUserTex) g_wmIconUserTex = Icon_RasterizeSVG(dev, k_WmIconUserSVG, sizeof(k_WmIconUserSVG), "wm_user");
    // Menu tab icons
    if (!g_tabIconWatermarkTex) g_tabIconWatermarkTex = Icon_RasterizeSVG(dev, k_TabIconWatermarkSVG, sizeof(k_TabIconWatermarkSVG), "tab_wm");
    if (!g_tabIconNamesTex)     g_tabIconNamesTex     = Icon_RasterizeSVG(dev, k_TabIconNamesSVG,     sizeof(k_TabIconNamesSVG),     "tab_names");
    if (!g_tabIconSheenTex)     g_tabIconSheenTex     = Icon_RasterizeSVG(dev, k_TabIconSheenSVG,     sizeof(k_TabIconSheenSVG),     "tab_sheen");
    if (!g_tabIconVisualsTex)   g_tabIconVisualsTex   = Icon_RasterizeSVG(dev, k_TabIconVisualsSVG,   sizeof(k_TabIconVisualsSVG),   "tab_vis");
    if (!g_tabIconCrosshairTex) g_tabIconCrosshairTex = Icon_RasterizeSVG(dev, k_TabIconCrosshairSVG, sizeof(k_TabIconCrosshairSVG), "tab_xhair");
    if (!g_tabIconMiscTex)      g_tabIconMiscTex      = Icon_RasterizeSVG(dev, k_TabIconMiscSVG,      sizeof(k_TabIconMiscSVG),      "tab_misc");
    if (!g_tabIconSkinsTex)     g_tabIconSkinsTex     = Icon_RasterizeSVG(dev, k_TabIconSkinsSVG,     sizeof(k_TabIconSkinsSVG),     "tab_skins");
    if (!g_tabIconImagesTex)    g_tabIconImagesTex    = Icon_RasterizeSVG(dev, k_TabIconImagesSVG,    sizeof(k_TabIconImagesSVG),    "tab_img");
    if (!g_tabIconConfigTex)    g_tabIconConfigTex    = Icon_RasterizeSVG(dev, k_TabIconConfigSVG,    sizeof(k_TabIconConfigSVG),    "tab_cfg");
    if (!g_decorImageTex && g_embeddedImageLen > 0)
        D3DXCreateTextureFromFileInMemoryEx(
            dev, g_embeddedImage, g_embeddedImageLen,
            D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 1, 0,
            D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
            D3DX_FILTER_LINEAR, D3DX_FILTER_LINEAR, 0, nullptr, nullptr, &g_decorImageTex);
}

// Merge all extended language + symbol glyph ranges into the most recently added font.
// Call immediately after AddFontFromFileTTF (before g_xFont = f) so MergeMode attaches.
// px = font pixel size (same as passed to the base AddFontFromFileTTF call).
static void MergeExtendedGlyphs(ImFontAtlas* a, float px)
{
    // Extended Latin, Cyrillic, Greek, Vietnamese, etc.
    {
        static const ImWchar latin_ext_ranges[] = {
            0x0020, 0x024F,   // Basic Latin + Latin-1 + Latin Extended A/B
            0x0250, 0x02AF,   // IPA Extensions
            0x02B0, 0x02FF,   // Spacing Modifier Letters
            0x0300, 0x036F,   // Combining Diacritical Marks
            0x0370, 0x03FF,   // Greek and Coptic
            0x0400, 0x04FF,   // Cyrillic
            0x0500, 0x052F,   // Cyrillic Supplement
            0x1E00, 0x1EFF,   // Latin Extended Additional (Vietnamese etc.)
            0x1F00, 0x1FFF,   // Greek Extended
            0,
        };
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 2; mcfg.OversampleV = 1;
        const char* segoe_paths[] = {
            "C:\\Windows\\Fonts\\segoeuib.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\verdana.ttf",
            nullptr
        };
        for (int pi = 0; segoe_paths[pi]; ++pi)
            if (a->AddFontFromFileTTF(segoe_paths[pi], px, &mcfg, latin_ext_ranges))
                break;
    }
    // Arabic, Hebrew, Persian
    {
        static const ImWchar arabic_ranges[] = {
            0x0590, 0x05FF,   // Hebrew
            0x0600, 0x06FF,   // Arabic
            0x0750, 0x077F,   // Arabic Supplement
            0xFB00, 0xFB4F,   // Alphabetic Presentation Forms (Hebrew)
            0xFB50, 0xFDFF,   // Arabic Presentation Forms-A
            0xFE70, 0xFEFF,   // Arabic Presentation Forms-B
            0,
        };
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        a->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", px, &mcfg, arabic_ranges);
    }
    // Thai, Devanagari (Hindi)
    {
        static const ImWchar thai_ranges[] = {
            0x0E00, 0x0E7F,   // Thai
            0x0900, 0x097F,   // Devanagari
            0,
        };
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        if (!a->AddFontFromFileTTF("C:\\Windows\\Fonts\\leelawui.ttf", px, &mcfg, thai_ranges))
            a->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahoma.ttf", px, &mcfg, thai_ranges);
    }
    // Japanese
    {
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        if (!a->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", px, &mcfg, a->GetGlyphRangesJapanese()))
            a->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", px, &mcfg, a->GetGlyphRangesJapanese());
    }
    // Chinese Simplified common
    {
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        if (!a->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", px, &mcfg, a->GetGlyphRangesChineseSimplifiedCommon()))
            a->AddFontFromFileTTF("C:\\Windows\\Fonts\\simsun.ttc", px, &mcfg, a->GetGlyphRangesChineseSimplifiedCommon());
    }
    // Korean Hangul
    {
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        if (!a->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", px, &mcfg, a->GetGlyphRangesKorean()))
            a->AddFontFromFileTTF("C:\\Windows\\Fonts\\gulim.ttc", px, &mcfg, a->GetGlyphRangesKorean());
    }
    // Symbols, math, arrows, box-drawing, dingbats, etc.
    {
        static const ImWchar sym_ranges[] = {
            0x2000, 0x206F,   // General Punctuation
            0x2070, 0x209F,   // Superscripts and Subscripts
            0x20A0, 0x20CF,   // Currency Symbols
            0x20D0, 0x20FF,   // Combining Diacritical Marks for Symbols
            0x2100, 0x214F,   // Letterlike Symbols
            0x2150, 0x218F,   // Number Forms
            0x2190, 0x21FF,   // Arrows
            0x2200, 0x22FF,   // Mathematical Operators
            0x2300, 0x23FF,   // Misc Technical
            0x2400, 0x243F,   // Control Pictures
            0x2440, 0x245F,   // Optical Character Recognition
            0x2460, 0x24FF,   // Enclosed Alphanumerics (-> etc)
            0x2500, 0x257F,   // Box Drawing
            0x2580, 0x259F,   // Block Elements
            0x25A0, 0x25FF,   // Geometric Shapes
            0x2600, 0x26FF,   // Misc Symbols
            0x2700, 0x27BF,   // Dingbats
            0x27C0, 0x27EF,   // Misc Mathematical Symbols-A
            0x27F0, 0x27FF,   // Supplemental Arrows-A
            0x2900, 0x297F,   // Supplemental Arrows-B
            0x2980, 0x29FF,   // Misc Mathematical Symbols-B
            0x2A00, 0x2AFF,   // Supplemental Mathematical Operators
            0x2B00, 0x2BFF,   // Misc Symbols and Arrows
            0x2E00, 0x2E7F,   // Supplemental Punctuation
            0x2E80, 0x2EFF,   // CJK Radicals Supplement
            0x2F00, 0x2FDF,   // Kangxi Radicals
            0xFE50, 0xFE6F,   // Small Form Variants
            0x1D400, 0x1D7FF, // Mathematical Alphanumeric Symbols
            0,
        };
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        a->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", px, &mcfg, sym_ranges);
    }
    // Phonetic extensions, Canadian Syllabics, Lisu, Tai Viet, Glagolitic, etc.
    {
        static const ImWchar phonetic_ranges[] = {
            0x1D00, 0x1DBF,   // Phonetic Extensions
            0x1DC0, 0x1DFF,   // Combining Diacritical Marks Supplement
            0x1400, 0x167F,   // Unified Canadian Aboriginal Syllabics
            0x18B0, 0x18FF,   // Unified Canadian Aboriginal Syllabics Extended
            0xA700, 0xA7FF,   // Modifier Tone Letters + Latin Extended-D
            0xA4D0, 0xA4FF,   // Lisu
            0xAA80, 0xAADF,   // Tai Viet
            0x2C00, 0x2C7F,   // Glagolitic + Latin Extended-C
            0x2D00, 0x2D2F,   // Georgian Supplement
            0,
        };
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        a->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", px, &mcfg, phonetic_ranges);
    }
    // Yi Syllables
    {
        static const ImWchar yi_ranges[] = {
            0xA000, 0xA48F,   // Yi Syllables
            0xA490, 0xA4CF,   // Yi Radicals
            0,
        };
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        a->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", px, &mcfg, yi_ranges);
    }
    // Georgian, Telugu, Malayalam, Bengali, Oriya, Gujarati, Gurmukhi
    {
        static const ImWchar indic_ranges[] = {
            0x10A0, 0x10FF,   // Georgian
            0x2D00, 0x2D2F,   // Georgian Supplement
            0x0980, 0x09FF,   // Bengali
            0x0C00, 0x0C7F,   // Telugu
            0x0D00, 0x0D7F,   // Malayalam
            0x0B00, 0x0B7F,   // Oriya
            0x0A80, 0x0AFF,   // Gujarati
            0x0A00, 0x0A7F,   // Gurmukhi
            0,
        };
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        if (!a->AddFontFromFileTTF("C:\\Windows\\Fonts\\Nirmala.ttf", px, &mcfg, indic_ranges))
            a->AddFontFromFileTTF("C:\\Windows\\Fonts\\nirmala.ttf", px, &mcfg, indic_ranges);
        static const ImWchar georgian_ranges[] = { 0x10A0, 0x10FF, 0x2D00, 0x2D2F, 0, };
        a->AddFontFromFileTTF("C:\\Windows\\Fonts\\sylfaen.ttf", px, &mcfg, georgian_ranges);
    }
    // Musical Symbols
    {
        static const ImWchar music_ranges[] = { 0x1D100, 0x1D1FF, 0, };
        ImFontConfig mcfg;
        mcfg.MergeMode   = true;
        mcfg.OversampleH = 1; mcfg.OversampleV = 1;
        a->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", px, &mcfg, music_ranges);
    }
}

static std::string FontPathFor(int index)
{
    return (index >= 0 && index < (int)g_fontList.size()) ? g_fontList[index].fullPath
                                                          : std::string("C:\\Windows\\Fonts\\segoeui.ttf");
}

// One user-styled font. flags: 0 bold, 1 monochrome, 2 sharp, 3 pixel snap. extendedGlyphs merges
// the CJK / symbol ranges -- only for fonts that print user text, since each merge adds thousands
// of glyphs to the atlas.
static ImFont* AddUserFont(ImFontAtlas* a, ImFont* fallback, const std::string& path, const bool* flags, float px, bool extendedGlyphs)
{
    ImFontConfig cfg;
    cfg.OversampleH        = flags[1] ? 1 : (flags[2] ? 2 : 3);
    cfg.OversampleV        = 1;
    cfg.PixelSnapH         = flags[1] || flags[3];
    cfg.RasterizerMultiply = flags[0] ? 1.4f : 1.0f;
    ImFont* f = a->AddFontFromFileTTF(path.c_str(), px, &cfg, a->GetGlyphRangesDefault());
    if (!f) f = a->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", px, &cfg, a->GetGlyphRangesDefault());
    if (f && extendedGlyphs) MergeExtendedGlyphs(a, px);
    return f ? f : fallback;
}

// Fonts only one feature uses, a bit each. They are built only while that feature is on, so an
// all-off config builds three fonts instead of eleven; switching a feature on marks the atlas
// dirty through Font_MissingForFeature.
enum : unsigned {
    kFontMp = 1, kFontLyrics = 2, kFontSpeedo = 4, kFontKf = 8, kFontWpnNum = 16, kFontWpnName = 32,
    kFontMs = 64, kFontDoctrine = 128,
};
static unsigned s_fontBuiltMask = 0;

static unsigned Font_NeededMask()
{
    unsigned m = 0;
    if (g_enableMediaPlayer)                      m |= kFontMp;
    if (g_enableMediaPlayer && g_mpShowLyrics)    m |= kFontLyrics;
    if (g_speedoEnable)                           m |= kFontSpeedo;
    if (g_speedoEnable && g_speedoShowPre)        m |= kFontDoctrine;
    if (g_customKillfeedEnable && g_selfKillfeedOnly) m |= kFontKf;
    if (g_enableWeaponHud && g_wpnHudShowNum)     m |= kFontWpnNum;
    if (g_enableWeaponHud && g_enableWpnHudName)  m |= kFontWpnName;
    if (g_minesweeperOpen)                        m |= kFontMs;
    return m;
}

bool Font_MissingForFeature() { return (Font_NeededMask() & ~s_fontBuiltMask) != 0; }

// Everything one atlas is built from, captured on the render thread. The worker reads only this,
// since the settings globals keep moving under it while a slider drags.
struct FontSpec {
    unsigned    need = 0;
    std::string wmPath, menuPath, mpPath, speedoPath, kfPath, wpnNumPath, wpnNamePath;
    float       wmScale = 0, menuScale = 0, mpScale = 0, lyricsScale = 0, speedoScale = 0,
                kfScale = 0, wpnNumScale = 0, wpnNameScale = 0;
    int         wmIndex = 0, menuIndex = 0, mpIndex = 0, speedoIndex = 0, kfIndex = 0,
                wpnNumIndex = 0, wpnNameIndex = 0;
    bool        wmFlags[6]{}, menuFlags[6]{}, mpFlags[6]{}, speedoFlags[6]{}, kfFlags[6]{},
                wpnNumFlags[6]{}, wpnNameFlags[6]{};
};

struct FontBuild {
    FontSpec     spec;
    ImFontAtlas* atlas = nullptr;   // null when Build() failed; the live atlas is kept
    ImFont*      def = nullptr, *wm = nullptr, *menu = nullptr, *mp = nullptr, *lyrics = nullptr,
                *speedo = nullptr, *kf = nullptr, *wpnNum = nullptr, *wpnName = nullptr,
                *ms = nullptr, *doctrine = nullptr;
    DWORD        buildMs = 0;
};

// Jobs worker. Pure CPU: font file reads + FreeType raster into a private atlas. No D3D, no engine,
// and no ImGui context: IMGUI_DISABLE_DEBUG_TOOLS keeps ImGui::MemAlloc from writing into it.
static FontBuild* Font_Build(const FontSpec& s)
{
    const DWORD t0 = GetTickCount();
    FontBuild* b = new FontBuild();
    b->spec = s;
    ImFontAtlas* a = IM_NEW(ImFontAtlas)();
    a->TexDesiredWidth = 4096; // cap atlas width to prevent OOM
    a->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

    // UI font -- Segoe UI Variable at 16px
    {
        ImFontConfig cfg;
        cfg.OversampleH = 3; cfg.OversampleV = 1; cfg.PixelSnapH = true;
        cfg.RasterizerMultiply = 1.15f; // nudge toward semibold weight
        ImFont* f = a->AddFontFromFileTTF("C:\\Windows\\Fonts\\SegUIVar.ttf", 16.f, &cfg, a->GetGlyphRangesDefault());
        if (!f) f = a->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.f, &cfg, a->GetGlyphRangesDefault());
        if (f) MergeExtendedGlyphs(a, 16.f);
        b->def = f ? f : a->AddFontDefault();
    }
    ImFont* const def = b->def;

    b->wm      = AddUserFont(a, def, s.wmPath,   s.wmFlags,   15.f * s.wmScale, true);
    b->menu    = AddUserFont(a, def, s.menuPath, s.menuFlags, 16.f * std::max(0.5f, s.menuScale), true);
    b->mp      = (s.need & kFontMp)      ? AddUserFont(a, def, s.mpPath, s.mpFlags, 15.f * s.mpScale, true) : def;
    b->lyrics  = (s.need & kFontLyrics)  ? AddUserFont(a, def, s.mpPath, s.mpFlags,
                                                       std::max(6.f, 15.f * s.mpScale * s.lyricsScale), true) : def;
    b->speedo  = (s.need & kFontSpeedo)  ? AddUserFont(a, def, s.speedoPath, s.speedoFlags, 20.f * s.speedoScale, false) : def;
    b->kf      = (s.need & kFontKf)      ? AddUserFont(a, def, s.kfPath, s.kfFlags, 15.f * s.kfScale, true) : def;
    b->wpnNum  = (s.need & kFontWpnNum)  ? AddUserFont(a, def, s.wpnNumPath, s.wpnNumFlags,
                                                       std::max(6.f, 15.f * s.wpnNumScale), false) : def;
    b->wpnName = (s.need & kFontWpnName) ? AddUserFont(a, def, s.wpnNamePath, s.wpnNameFlags,
                                                       std::max(6.f, 15.f * s.wpnNameScale), true) : def;

    // Minesweeper cell numbers -- Tahoma Bold, digits 1-8 only (24px cells)
    b->ms = def;
    if (s.need & kFontMs) {
        static const ImWchar ms_ranges[] = { '1', '8', 0 };
        ImFontConfig cfg;
        cfg.OversampleH = 1; cfg.OversampleV = 1; cfg.PixelSnapH = true;
        ImFont* f = a->AddFontFromFileTTF("C:\\Windows\\Fonts\\tahomabd.ttf", 19.f, &cfg, ms_ranges);
        if (!f) f = a->AddFontFromFileTTF("C:\\Windows\\Fonts\\verdanab.ttf", 19.f, &cfg, ms_ranges);
        if (f) b->ms = f;
    }

    // Doctrine W00 Regular at 32px, parens glyphs only (speedometer pre-jump)
    if (s.need & kFontDoctrine) {
        static const ImWchar paren_ranges[] = { '(', ')', 0 };
        ImFontConfig cfg;
        cfg.OversampleH = 3; cfg.OversampleV = 1; cfg.PixelSnapH = false;
        cfg.FontDataOwnedByAtlas = false;
        b->doctrine = a->AddFontFromMemoryTTF(
            const_cast<void*>(static_cast<const void*>(Doctrine_W00_Regular_compressed_data)),
            (int)Doctrine_W00_Regular_compressed_size, 32.f, &cfg, paren_ranges);
    }

    if (a->Build()) {
        // RGBA conversion here too, so the render thread's part is only CreateTexture + memcpy
        unsigned char* px = nullptr; int w = 0, h = 0;
        a->GetTexDataAsRGBA32(&px, &w, &h);
        b->atlas = a;
    } else {
        IM_DELETE(a);
    }
    b->buildMs = GetTickCount() - t0;
    return b;
}

static std::atomic<FontBuild*> s_fontReady{ nullptr };
static bool                    s_fontBusy = false;   // render thread only; one build in flight at a time

// Queues an atlas rebuild from the current settings. Rasterizing every font (each CJK merge is
// thousands of glyphs) on the render thread froze the game for the whole build, so it runs on a
// Jobs worker and Font_Pump swaps the finished atlas in. Name is from when only the watermark had one.
void RebuildWatermarkFont()
{
    if (!g_d3dDev || !g_imguiInit || s_fontBusy) return;

    // media player + lyrics follow the watermark's font when copy-style is on
    const bool* mpFlags = g_mpCopyWmStyle ? g_wmFontFlags : g_mpFontFlags;

    FontSpec s;
    s.need        = Font_NeededMask();
    s.wmPath      = GetWmFontPath();
    s.wmScale     = g_watermarkScale;  s.wmIndex = g_wmFontIndex;
    memcpy(s.wmFlags, g_wmFontFlags, sizeof(g_wmFontFlags));
    s.menuIndex   = g_menuFontIndex;   s.menuPath = FontPathFor(s.menuIndex);  s.menuScale = g_menuFontScale;
    memcpy(s.menuFlags, g_menuFontFlags, sizeof(g_menuFontFlags));
    s.mpIndex     = g_mpCopyWmStyle ? g_wmFontIndex : g_mpFontIndex;
    s.mpPath      = FontPathFor(s.mpIndex);
    s.mpScale     = g_mpCopyWmStyle ? g_watermarkScale : g_mpScale;
    s.lyricsScale = g_mpLyricsScale;
    memcpy(s.mpFlags, mpFlags, sizeof(g_mpFontFlagsBuilt));
    s.speedoIndex = g_speedoFontIndex; s.speedoPath = FontPathFor(s.speedoIndex); s.speedoScale = g_speedoScale;
    memcpy(s.speedoFlags, g_speedoFontFlags, sizeof(g_speedoFontFlags));
    s.kfIndex     = g_kfFontIndex;     s.kfPath = FontPathFor(s.kfIndex);  s.kfScale = g_kfFontScale;
    memcpy(s.kfFlags, g_kfFontFlags, sizeof(g_kfFontFlags));
    s.wpnNumIndex = g_wpnHudNumFontIndex;  s.wpnNumPath = FontPathFor(s.wpnNumIndex);  s.wpnNumScale = g_wpnHudNumScale;
    memcpy(s.wpnNumFlags, g_wpnHudNumFontFlags, sizeof(g_wpnHudNumFontFlags));
    s.wpnNameIndex = g_wpnHudNameFontIndex; s.wpnNamePath = FontPathFor(s.wpnNameIndex); s.wpnNameScale = g_wpnHudNameScale;
    memcpy(s.wpnNameFlags, g_wpnHudNameFontFlags, sizeof(g_wpnHudNameFontFlags));

    s_fontBusy = true;
    auto job = [s] { s_fontReady.store(Font_Build(s)); };
    if (Jobs::WorkerCount() > 0) Jobs::Submit(job);
    else                         job();   // pool not running: build inline, the old behavior
}

// Render thread, before ImGui::NewFrame: NewFrame re-reads io.Fonts, so the swap has to land
// between frames. Old fonts stay live and drawing until this point.
void Font_Pump()
{
    FontBuild* b = s_fontReady.exchange(nullptr);
    if (!b) return;
    s_fontBusy = false;
    const FontSpec& s = b->spec;

    if (b->atlas) {
        const DWORD t0 = GetTickCount();
        ImGuiIO& io = ImGui::GetIO();
        ImGui_ImplDX9_InvalidateDeviceObjects();
        IM_DELETE(io.Fonts);          // context owns io.Fonts; DestroyContext frees whatever sits there
        io.Fonts         = b->atlas;
        io.FontDefault   = b->def;
        g_wmFont         = b->wm;
        g_menuFont       = b->menu;
        g_mpFont         = b->mp;
        g_mpLyricsFont   = b->lyrics;
        g_speedoFont     = b->speedo;
        g_kfFont         = b->kf;
        g_wpnHudNumFont  = b->wpnNum;
        g_wpnHudNameFont = b->wpnName;
        g_msFont         = b->ms;
        g_doctrineFont   = b->doctrine;
        ImGui_ImplDX9_CreateDeviceObjects();
        DebugLog("fonts: built on worker in %lu ms, swap+upload %lu ms", b->buildMs, GetTickCount() - t0);
    } else {
        DebugLog("fonts: atlas build failed after %lu ms, keeping old fonts", b->buildMs);
    }

    // Stamp what this build was made FROM, not the current globals: a change made while the
    // worker ran then still reads as dirty and queues the next build.
    s_fontBuiltMask = s.need;
    g_wmFontBuiltAtScale = s.wmScale;
    g_wmFontBuiltAtIndex = s.wmIndex;
    memcpy(g_wmFontFlagsBuilt, s.wmFlags, sizeof(g_wmFontFlagsBuilt));
    g_mpFontBuiltAtScale = s.mpScale;
    g_mpFontBuiltAtIndex = s.mpIndex;
    memcpy(g_mpFontFlagsBuilt, s.mpFlags, sizeof(g_mpFontFlagsBuilt));
    g_mpLyricsBuiltAtScale = s.lyricsScale;
    g_menuFontBuiltAtScale = s.menuScale;
    g_menuFontBuiltAtIndex = s.menuIndex;
    memcpy(g_menuFontFlagsBuilt, s.menuFlags, sizeof(g_menuFontFlagsBuilt));
    g_speedoFontBuiltAtScale = s.speedoScale;
    g_speedoFontBuiltAtIndex = s.speedoIndex;
    memcpy(g_speedoFontFlagsBuilt, s.speedoFlags, sizeof(g_speedoFontFlagsBuilt));
    g_kfFontBuiltAtScale = s.kfScale;
    g_kfFontBuiltAtIndex = s.kfIndex;
    memcpy(g_kfFontFlagsBuilt, s.kfFlags, sizeof(g_kfFontFlagsBuilt));
    g_wpnHudNumFontBuiltAtScale = s.wpnNumScale;
    g_wpnHudNumFontBuiltAtIndex = s.wpnNumIndex;
    memcpy(g_wpnHudNumFontFlagsBuilt, s.wpnNumFlags, sizeof(g_wpnHudNumFontFlagsBuilt));
    g_wpnHudNameFontBuiltAtScale = s.wpnNameScale;
    g_wpnHudNameFontBuiltAtIndex = s.wpnNameIndex;
    memcpy(g_wpnHudNameFontFlagsBuilt, s.wpnNameFlags, sizeof(g_wpnHudNameFontFlagsBuilt));
    g_fontDirtyAt = 0;
    delete b;
}

// Eject, after Jobs::Stop: a finished build that never got swapped in.
void Font_DropPending()
{
    if (FontBuild* b = s_fontReady.exchange(nullptr)) {
        if (b->atlas) IM_DELETE(b->atlas);
        delete b;
    }
    s_fontBusy = false;
}

// ---- Color clipboard ---------------------------------------
float g_colorClipboard[4]    = { 0.f, 0.f, 0.f, 1.f };
bool  g_colorClipboardFilled   = false;


// ---- Label animation ---------------------------------------
// UTF-8 helpers for animation modes that need to work per-codepoint.
// Returns number of codepoints in a UTF-8 string.
static int Utf8CountCodepoints(const char* s, int byteLen)
{
    int n = 0;
    for (int i = 0; i < byteLen; )
    {
        unsigned char c = (unsigned char)s[i];
        if      (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else               i += 4;
        ++n;
    }
    return n;
}
// Returns the byte offset of the Nth codepoint start (N is codepoint index).
static int Utf8ByteOffset(const char* s, int byteLen, int cpIdx)
{
    int i = 0, n = 0;
    while (i < byteLen && n < cpIdx)
    {
        unsigned char c = (unsigned char)s[i];
        if      (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else               i += 4;
        ++n;
    }
    return i;
}

// Returns the current animated slice of the title (after cycle resolution) based on g_labelAnimMode.
// Cycle is independent: it picks which string to display, then the anim mode shapes it.
// All state is kept in statics so nothing leaks into the game thread.
const char* GetTitleLabel(float delta)
{
    static char s_buf[512] = {};

    const char* srcRaw = g_customLabel.c_str();

    // Cycle titles
    if (g_titleCycleEnable && g_titleCount > 0)
    {
        ULONGLONG now = AnimTimeMs();
        ULONGLONG ms  = (ULONGLONG)(g_titleInterval * 1000.f);
        if (g_titleLastChange == 0) g_titleLastChange = now;
        if (now - g_titleLastChange >= ms)
        {
            g_titleIdx = (g_titleIdx + 1) % g_titleCount;
            g_titleLastChange = now;
        }
        int idx = (g_titleIdx < g_titleCount) ? g_titleIdx : 0;
        if (g_titleList[idx].text[0]) srcRaw = g_titleList[idx].text;
    }

    // Apply animation mode
    int byteLen = (int)strlen(srcRaw);
    if (byteLen == 0) { s_buf[0] = '\0'; return s_buf; }
    int cpLen = Utf8CountCodepoints(srcRaw, byteLen);

    if (g_labelAnimMode == 1) // scroll
    {
        static float       s_scrollPos  = 0.f;
        static const char* s_lastSrc    = nullptr;
        if (s_lastSrc != srcRaw) { s_scrollPos = 0.f; s_lastSrc = srcRaw; }

        s_scrollPos += delta * g_labelScrollSpeed;
        if (s_scrollPos >= (float)cpLen) s_scrollPos -= (float)cpLen;
        int offsetCp  = (int)s_scrollPos % cpLen;
        int byteStart = Utf8ByteOffset(srcRaw, byteLen, offsetCp);
        int tailBytes = byteLen - byteStart;
        memcpy(s_buf,             srcRaw + byteStart, (size_t)tailBytes);
        memcpy(s_buf + tailBytes, srcRaw,             (size_t)byteStart);
        s_buf[byteLen] = '\0';
        return s_buf;
    }

    if (g_labelAnimMode == 2) // typewriter
    {
        static int         s_phase    = 0;
        static float       s_acc      = 0.f;
        static int         s_visCp    = 0;
        static const char* s_lastSrc2 = nullptr;
        if (s_lastSrc2 != srcRaw) { s_phase = 0; s_acc = 0.f; s_visCp = 0; s_lastSrc2 = srcRaw; }

        if (s_phase == 0)
        {
            s_acc += delta * g_labelTypeSpeed;
            while (s_acc >= 1.f && s_visCp < cpLen) { ++s_visCp; s_acc -= 1.f; }
            if (s_visCp >= cpLen) { s_visCp = cpLen; s_acc = 0.f; s_phase = 1; }
        }
        else if (s_phase == 1)
        {
            s_acc += delta;
            if (s_acc >= g_labelHoldTime) { s_acc = 0.f; s_phase = 2; }
        }
        else
        {
            s_acc += delta * g_labelEraseSpeed;
            while (s_acc >= 1.f && s_visCp > 0) { --s_visCp; s_acc -= 1.f; }
            if (s_visCp <= 0) { s_visCp = 0; s_acc = 0.f; s_phase = 0; }
        }

        int visBytes = Utf8ByteOffset(srcRaw, byteLen, s_visCp);
        memcpy(s_buf, srcRaw, (size_t)visBytes);
        s_buf[visBytes] = '\0';
        return s_buf;
    }

    // Mode 0: no animation
    return srcRaw;
}

// Returns the string to show as the username in the watermark based on g_wmLabelSource:
//   0 = Custom  -- g_wmUsernameLabel (user-typed override)
//   1 = Username -- ingame name: killfeed-changed name if changer active, otherwise real Steam name
//   2 = Windows  -- Windows account name, lazy-loaded once
const char* GetUsernameLabel()
{
    static char s_buf[128] = {};

    // Lazy-load Windows username (needed for source 2 and fallback)
    if (g_windowsUsername.empty())
    {
        char buf[256] = {};
        DWORD len = sizeof(buf);
        if (GetUserNameA(buf, &len)) g_windowsUsername = buf;
    }

    const char* srcRaw = "";

    switch (g_wmLabelSource)
    {
    case 0: // Custom label
        srcRaw = g_wmUsernameLabel.c_str();
        break;

    case 1: // Ingame username -- killfeed name if active, otherwise real Steam name
        if (g_useCustomUsername)
        {
            if (g_enableNameCycle && g_nameCount > 0)
            {
                int idx = (g_nameIdx < g_nameCount) ? g_nameIdx : 0;
                if (g_nameList[idx].text[0]) { srcRaw = g_nameList[idx].text; break; }
            }
            if (!g_customUsername.empty()) { srcRaw = g_customUsername.c_str(); break; }
        }
        // Real Steam name (set before killfeed masking in GetPlayerName hook)
        srcRaw = g_localRealName[0] ? g_localRealName : g_windowsUsername.c_str();
        break;

    default: // 2 = Windows
        srcRaw = g_windowsUsername.c_str();
        break;
    }

    strncpy(s_buf, srcRaw, sizeof(s_buf) - 1);
    s_buf[sizeof(s_buf) - 1] = '\0';
    return s_buf;
}


// ---- Corner / side flag helpers ----------------------------
// cornerFlags: 0=all, bit0=TL, bit1=TR, bit2=BL, bit3=BR
ImDrawFlags CornerFlagsToImDraw(int cf) {
    if (cf == 0) return ImDrawFlags_RoundCornersAll;
    ImDrawFlags f = ImDrawFlags_RoundCornersNone;
    if (cf & 1) f = (ImDrawFlags)(f | ImDrawFlags_RoundCornersTopLeft);
    if (cf & 2) f = (ImDrawFlags)(f | ImDrawFlags_RoundCornersTopRight);
    if (cf & 4) f = (ImDrawFlags)(f | ImDrawFlags_RoundCornersBottomLeft);
    if (cf & 8) f = (ImDrawFlags)(f | ImDrawFlags_RoundCornersBottomRight);
    // If none selected, treat as all
    if (f == ImDrawFlags_RoundCornersNone) return ImDrawFlags_RoundCornersAll;
    return f;
}
void BlurRT_Init(IDirect3DDevice9* dev)
{
    if (!dev || !g_blurBBSurf || g_blurBBW <= 0 || g_blurBBH <= 0) return;
    if (g_blurShaderW == g_blurBBW && g_blurShaderH == g_blurBBH &&
        g_blurTexA && g_blurTexB && g_blurShaderX && g_blurShaderY) return;

    BlurRT_Destroy();

    D3DSURFACE_DESC d{};
    g_blurBBSurf->GetDesc(&d);
    D3DFORMAT fmt = d.Format;

    if (FAILED(dev->CreateTexture(g_blurBBW, g_blurBBH, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &g_blurTexA, nullptr))) return;
    if (FAILED(g_blurTexA->GetSurfaceLevel(0, &g_blurSurfA))) { BlurRT_Destroy(); return; }
    if (FAILED(dev->CreateTexture(g_blurBBW, g_blurBBH, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &g_blurTexB, nullptr))) { BlurRT_Destroy(); return; }
    if (FAILED(g_blurTexB->GetSurfaceLevel(0, &g_blurSurfB))) { BlurRT_Destroy(); return; }

    if (!g_blurShaderX) {
        unsigned char buf[800];
        int len = BlurDecodeShader(k_blurX85, (int)(sizeof(k_blurX85)-1), buf, (int)sizeof(buf));
        HRESULT hr = (len > 0) ? dev->CreatePixelShader((const DWORD*)buf, &g_blurShaderX) : E_FAIL;
        DebugLog("BlurRT_Init: shaderX len=%d hr=0x%08X ptr=%p", len, (unsigned)hr, g_blurShaderX);
    }
    if (!g_blurShaderY) {
        unsigned char buf[800];
        int len = BlurDecodeShader(k_blurY85, (int)(sizeof(k_blurY85)-1), buf, (int)sizeof(buf));
        HRESULT hr = (len > 0) ? dev->CreatePixelShader((const DWORD*)buf, &g_blurShaderY) : E_FAIL;
        DebugLog("BlurRT_Init: shaderY len=%d hr=0x%08X ptr=%p", len, (unsigned)hr, g_blurShaderY);
    }

    g_blurShaderW = g_blurBBW;
    g_blurShaderH = g_blurBBH;
    DebugLog("BlurRT_Init: done %dx%d texA=%p texB=%p", g_blurShaderW, g_blurShaderH, g_blurTexA, g_blurTexB);
}

void BlurRT_Destroy()
{
    if (g_blurSurfB)   { g_blurSurfB->Release();   g_blurSurfB   = nullptr; }
    if (g_blurTexB)    { g_blurTexB->Release();     g_blurTexB    = nullptr; }
    if (g_blurSurfA)   { g_blurSurfA->Release();    g_blurSurfA   = nullptr; }
    if (g_blurTexA)    { g_blurTexA->Release();     g_blurTexA    = nullptr; }
    if (g_blurShaderX) { g_blurShaderX->Release();  g_blurShaderX = nullptr; }
    if (g_blurShaderY) { g_blurShaderY->Release();  g_blurShaderY = nullptr; }
    g_blurShaderW = g_blurShaderH = 0;
}

// ---- Color correction --------------------------------------
// HLSL ps_3_0 shader: RGB gain, exposure, temperature, levels, curves (master + per-channel RGB),
// saturation, hue rotation, highlights/shadows, contrast.
// Render-target flow:
//   1. StretchRect(backbuffer → g_ccInputSurf)          — pure GPU BLT, no pipeline stall
//   2. SetRenderTarget(g_ccOutSurf), Draw sampling g_ccInputTex — apply CC
//   3. StretchRect(g_ccOutSurf → backbuffer)            — blit corrected frame back to BB
// Shader source + constants layout: fx_colorcorr.hlsl (precompiled by fxc, no D3DX at runtime).

static int s_ccBBW = 0, s_ccBBH = 0;
static CcCurve s_ccLutSrc[4];      // curves the LUT texture was last baked from
static bool    s_ccLutValid = false;

void ColorCorr_Init(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    D3DSURFACE_DESC desc{};
    if (!dev || !bb || FAILED(bb->GetDesc(&desc))) return;
    if (g_ccShader && g_ccInputTex && g_ccOutTex && g_ccLutTex &&
        s_ccBBW == (int)desc.Width && s_ccBBH == (int)desc.Height) return;
    ColorCorr_Destroy();

    // A failed shader create would otherwise retry every frame.
    static bool s_shaderFailed = false;
    if (s_shaderFailed) return;
    HRESULT hr = dev->CreatePixelShader((const DWORD*)k_fxColorCorrBytes, &g_ccShader);
    if (FAILED(hr)) { DebugLog("ColorCorr_Init: CreatePixelShader 0x%08X", (unsigned)hr); s_shaderFailed = true; return; }

    D3DFORMAT fmt = desc.Format;

    // RT input texture — StretchRect(BB→ccInputSurf) is a pure GPU BLT, no pipeline stall
    hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                             fmt, D3DPOOL_DEFAULT, &g_ccInputTex, nullptr);
    if (FAILED(hr)) { DebugLog("ColorCorr_Init: inputTex 0x%08X", (unsigned)hr); ColorCorr_Destroy(); return; }
    g_ccInputTex->GetSurfaceLevel(0, &g_ccInputSurf);

    // RT texture for CC output — StretchRect(ccOutSurf→BB) is a valid BLT
    hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                             fmt, D3DPOOL_DEFAULT, &g_ccOutTex, nullptr);
    if (FAILED(hr)) { DebugLog("ColorCorr_Init: ccOutTex 0x%08X", (unsigned)hr); ColorCorr_Destroy(); return; }
    g_ccOutTex->GetSurfaceLevel(0, &g_ccOutSurf);

    // Curves LUT, refilled by ColorCorr_Apply whenever g_ccCurve changes
    hr = dev->CreateTexture(k_ccLutSize, 1, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                            D3DPOOL_DEFAULT, &g_ccLutTex, nullptr);
    if (FAILED(hr)) { DebugLog("ColorCorr_Init: lutTex 0x%08X", (unsigned)hr); ColorCorr_Destroy(); return; }

    s_ccBBW = (int)desc.Width;
    s_ccBBH = (int)desc.Height;
    DebugLog("ColorCorr_Init: ok %dx%d fmt=%d shader=%p", s_ccBBW, s_ccBBH, (int)fmt, g_ccShader);
}

void ColorCorr_Destroy()
{
    if (g_ccOutSurf)   { g_ccOutSurf->Release();   g_ccOutSurf   = nullptr; }
    if (g_ccOutTex)    { g_ccOutTex->Release();     g_ccOutTex    = nullptr; }
    if (g_ccInputSurf) { g_ccInputSurf->Release();  g_ccInputSurf = nullptr; }
    if (g_ccInputTex)  { g_ccInputTex->Release();   g_ccInputTex  = nullptr; }
    if (g_ccLutTex)    { g_ccLutTex->Release();     g_ccLutTex    = nullptr; }
    if (g_ccShader)    { g_ccShader->Release();     g_ccShader    = nullptr; }
    s_ccBBW = s_ccBBH = 0;
    s_ccLutValid = false;
}

// Rodrigues hue rotation matrix around the luminance axis.
static void ComputeHueMat(float theta, float m[3][3])
{
    float c = cosf(theta), s = sinf(theta);
    const float lx = 0.213f, ly = 0.715f, lz = 0.072f;
    float ic = 1.f - c;
    m[0][0] = c + ic*lx*lx;    m[0][1] = ic*lx*ly - s*lz;  m[0][2] = ic*lx*lz + s*ly;
    m[1][0] = ic*ly*lx + s*lz; m[1][1] = c + ic*ly*ly;      m[1][2] = ic*ly*lz - s*lx;
    m[2][0] = ic*lz*lx - s*ly; m[2][1] = ic*lz*ly + s*lx;   m[2][2] = c + ic*lz*lz;
}

bool ColorCorr_Apply(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    if (!dev || !bb || !g_ccShader ||
        !g_ccInputTex || !g_ccInputSurf || !g_ccOutTex || !g_ccOutSurf || !g_ccLutTex) return false;

    // Rebake the curves LUT only when a curve changed (a ~530 byte compare per frame).
    {
        CcCurve cur[4];
        std::memcpy(cur, g_ccCurve, sizeof(cur));
        if (!s_ccLutValid || std::memcmp(cur, s_ccLutSrc, sizeof(cur)) != 0) {
            D3DLOCKED_RECT lr{};
            if (SUCCEEDED(g_ccLutTex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) {
                uint32_t lut[k_ccLutSize];
                CcCurve_BakeLut(cur, lut);
                std::memcpy(lr.pBits, lut, sizeof(lut));
                g_ccLutTex->UnlockRect(0);
                std::memcpy(s_ccLutSrc, cur, sizeof(cur));
                s_ccLutValid = true;
            } else {
                return false;   // no LUT, no pass: an unfilled dynamic texture is garbage
            }
        }
    }

    // Pure GPU BLT, no CPU readback stall.
    HRESULT hr = dev->StretchRect(bb, nullptr, g_ccInputSurf, nullptr, D3DTEXF_NONE);
    if (FAILED(hr)) { DebugLog("ColorCorr_Apply: input StretchRect 0x%08X", (unsigned)hr); return false; }

    float W = (float)s_ccBBW, H = (float)s_ccBBH;
    struct Vert { float x,y,z,rhw,u,v; };
    const Vert quad[4] = {
        {-0.5f,   -0.5f,   0,1, 0,0},
        {W-0.5f,  -0.5f,   0,1, 1,0},
        {-0.5f,   H-0.5f,  0,1, 0,1},
        {W-0.5f,  H-0.5f,  0,1, 1,1},
    };

    // Save state
    IDirect3DSurface9 *rtOld=nullptr, *dsOld=nullptr;
    IDirect3DPixelShader9* psOld=nullptr;
    IDirect3DBaseTexture9* tex0Old=nullptr, *tex1Old=nullptr;
    D3DVIEWPORT9 vpOld{};
    DWORD zenable, zwrite, ablend, atest, cull, scissor, addrU, addrV, minF, magF, fvf;
    DWORD srgbWrite, srgbTex;
    DWORD addrU1, addrV1, minF1, magF1, mipF1, srgbTex1;
    IDirect3DVertexDeclaration9* declOld=nullptr;
    // Runs mid-frame now (pre-hud) and the engine draws after us: the vertex shader, stream 0
    // and the index buffer go back too -- DrawPrimitiveUP nulls the last two behind shaderapi's cache.
    IDirect3DVertexShader9* vsOld=nullptr;
    IDirect3DVertexBuffer9* streamOld=nullptr;
    IDirect3DIndexBuffer9*  idxOld=nullptr;
    UINT streamOff=0, streamStride=0;
    DWORD colorWrite, fogOn, clipOn, stencilOn;
    float psc[7][4]{};  // save PS constants c0-c6 — they persist across frames and can corrupt TF2 prop shaders

    dev->GetRenderTarget(0, &rtOld);
    dev->GetDepthStencilSurface(&dsOld);
    dev->GetPixelShader(&psOld);
    dev->GetTexture(0, &tex0Old);
    dev->GetViewport(&vpOld);
    dev->GetRenderState(D3DRS_ZENABLE,           &zenable);
    dev->GetRenderState(D3DRS_ZWRITEENABLE,      &zwrite);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE,  &ablend);
    dev->GetRenderState(D3DRS_ALPHATESTENABLE,   &atest);
    dev->GetRenderState(D3DRS_CULLMODE,          &cull);
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
    dev->GetRenderState(D3DRS_SRGBWRITEENABLE,   &srgbWrite);
    dev->GetSamplerState(0, D3DSAMP_ADDRESSU,    &addrU);
    dev->GetSamplerState(0, D3DSAMP_ADDRESSV,    &addrV);
    dev->GetSamplerState(0, D3DSAMP_MINFILTER,   &minF);
    dev->GetSamplerState(0, D3DSAMP_MAGFILTER,   &magF);
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &srgbTex);
    dev->GetTexture(1, &tex1Old);
    dev->GetSamplerState(1, D3DSAMP_ADDRESSU,    &addrU1);
    dev->GetSamplerState(1, D3DSAMP_ADDRESSV,    &addrV1);
    dev->GetSamplerState(1, D3DSAMP_MINFILTER,   &minF1);
    dev->GetSamplerState(1, D3DSAMP_MAGFILTER,   &magF1);
    dev->GetSamplerState(1, D3DSAMP_MIPFILTER,   &mipF1);
    dev->GetSamplerState(1, D3DSAMP_SRGBTEXTURE, &srgbTex1);
    dev->GetFVF(&fvf);
    dev->GetVertexDeclaration(&declOld);
    dev->GetPixelShaderConstantF(0, psc[0], 7);
    dev->GetVertexShader(&vsOld);
    dev->GetStreamSource(0, &streamOld, &streamOff, &streamStride);
    dev->GetIndices(&idxOld);
    dev->GetRenderState(D3DRS_COLORWRITEENABLE,  &colorWrite);
    dev->GetRenderState(D3DRS_FOGENABLE,         &fogOn);
    dev->GetRenderState(D3DRS_CLIPPLANEENABLE,   &clipOn);
    dev->GetRenderState(D3DRS_STENCILENABLE,     &stencilOn);

    D3DVIEWPORT9 vp{0,0,(DWORD)W,(DWORD)H,0.f,1.f};
    dev->SetDepthStencilSurface(nullptr);
    dev->SetViewport(&vp);
    dev->SetVertexShader(nullptr);   // a bound vertex shader would ignore the FVF
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,  0xF);
    dev->SetRenderState(D3DRS_FOGENABLE,         FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE,   0);
    dev->SetRenderState(D3DRS_STENCILENABLE,     FALSE);
    dev->SetRenderState(D3DRS_ZENABLE,           FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,      FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,   FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,          D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,   FALSE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(1, D3DSAMP_MINFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(1, D3DSAMP_MIPFILTER,   D3DTEXF_NONE);
    dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, 0);
    dev->SetPixelShader(g_ccShader);

    float exposureMult = expf(g_colorCorrExposure * 0.693147f); // 2^EV
    float c0[4] = { g_colorCorrR, g_colorCorrG, g_colorCorrB, g_colorCorrContrast };
    float c1[4] = { g_colorCorrSat, g_colorCorrTemp, exposureMult, 0.f };
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShaderConstantF(1, c1, 1);
    float hueMat[3][3];
    ComputeHueMat(g_colorCorrHue * (3.14159265f / 180.f), hueMat);
    float c2[4] = { hueMat[0][0], hueMat[0][1], hueMat[0][2], 0.f };
    float c3[4] = { hueMat[1][0], hueMat[1][1], hueMat[1][2], 0.f };
    float c4[4] = { hueMat[2][0], hueMat[2][1], hueMat[2][2], 0.f };
    dev->SetPixelShaderConstantF(2, c2, 1);
    dev->SetPixelShaderConstantF(3, c3, 1);
    dev->SetPixelShaderConstantF(4, c4, 1);
    float inWhite = std::max(g_colorCorrLevInWhite, g_colorCorrLevInBlack + 0.001f);
    float invGamma = (g_colorCorrLevGamma > 0.001f) ? (1.f / g_colorCorrLevGamma) : 1.f;
    float c5[4] = { g_colorCorrLevInBlack, inWhite, invGamma, 0.f };
    float c6[4] = { g_colorCorrLevOutBlack,
                    g_colorCorrLevOutWhite - g_colorCorrLevOutBlack,
                    g_colorCorrHighlights, g_colorCorrShadows };
    dev->SetPixelShaderConstantF(5,  c5,  1);
    dev->SetPixelShaderConstantF(6,  c6,  1);

    dev->SetRenderTarget(0, g_ccOutSurf);
    dev->SetTexture(0, g_ccInputTex);
    dev->SetTexture(1, g_ccLutTex);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vert));

    // Restore state.
    dev->SetRenderTarget(0, rtOld);
    dev->SetDepthStencilSurface(dsOld);
    dev->SetPixelShader(psOld);
    dev->SetTexture(0, tex0Old);
    dev->SetViewport(&vpOld);
    dev->SetRenderState(D3DRS_ZENABLE,           zenable);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,      zwrite);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  ablend);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,   atest);
    dev->SetRenderState(D3DRS_CULLMODE,          cull);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,   srgbWrite);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,    addrU);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,    addrV);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER,   minF);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER,   magF);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, srgbTex);
    dev->SetTexture(1, tex1Old);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSU,    addrU1);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSV,    addrV1);
    dev->SetSamplerState(1, D3DSAMP_MINFILTER,   minF1);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER,   magF1);
    dev->SetSamplerState(1, D3DSAMP_MIPFILTER,   mipF1);
    dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, srgbTex1);
    if (declOld) { dev->SetVertexDeclaration(declOld); declOld->Release(); }
    else         { dev->SetFVF(fvf); }
    if (rtOld)   rtOld->Release();
    if (dsOld)   dsOld->Release();
    if (psOld)   psOld->Release();
    if (tex0Old) tex0Old->Release();
    if (tex1Old) tex1Old->Release();
    dev->SetPixelShaderConstantF(0, psc[0], 7);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,  colorWrite);
    dev->SetRenderState(D3DRS_FOGENABLE,         fogOn);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE,   clipOn);
    dev->SetRenderState(D3DRS_STENCILENABLE,     stencilOn);
    dev->SetVertexShader(vsOld);
    dev->SetStreamSource(0, streamOld, streamOff, streamStride);
    dev->SetIndices(idxOld);
    if (vsOld)     vsOld->Release();
    if (streamOld) streamOld->Release();
    if (idxOld)    idxOld->Release();

    // Blit corrected frame to the backbuffer.
    return SUCCEEDED(dev->StretchRect(g_ccOutSurf, nullptr, bb, nullptr, D3DTEXF_NONE));
}

// ---- Scene FX: bloom + grade, kept off the hud ------------------------------------------------
// Both used to run at Present over the finished backbuffer, hud and all. Now they run from
// IVRenderView::Push2DView (viewrender.cpp:2298): every 3D pass, the gun, the engine's own post
// processing and screen overlays are in the frame, and VGui_Paint (2401) has not started.
// screenfx.cpp owns the hook and the thread fork; this owns the draw.
//
// One run per frame, keyed on g_presentCount (render thread only: the queued pass replays before
// the Present it belongs to). A frame the pre-hud point missed (hook not in, target not the
// backbuffer, queue flushed) falls back to Present so the effect never just vanishes.
static int s_sceneFxDone = -1;   // g_presentCount when the pre-hud pass last ran

static bool SceneFx_Wanted() { return (g_bloomEnable || g_colorCorrEnable) && IsInGame(); }

static void SceneFx_Draw(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    // Glow first, then the grade: the glow is a lens, the grade is the film.
    Bloom_Frame(dev);
    if (!g_colorCorrEnable) return;
    ColorCorr_Init(dev, bb);
    // The world web screen stays out of CC: its pixels get the pre-CC copy back.
    if (ColorCorr_Apply(dev, bb)) Web_RestoreOverColorCorr(dev, g_ccInputTex, s_ccBBW, s_ccBBH);
}

void SceneFx_PreHud(IDirect3DDevice9* dev)
{
    const int frame = g_presentCount.load();
    if (!dev || g_eject || s_sceneFxDone == frame || !SceneFx_Wanted()) return;

    // Only the frame the player sees: a 2D view pushed onto some other target is not it.
    IDirect3DSurface9 *rt = nullptr, *bb = nullptr;
    const bool onBB = SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
                      SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb &&
                      rt == bb;
    if (onBB) {
        s_sceneFxDone = frame;
        SceneFx_Draw(dev, bb);
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("^FX: bloom/cc running pre-hud (Push2DView)"); }
    }
    if (rt) rt->Release();
    if (bb) bb->Release();
}

// h_Present, after its ++g_presentCount: this frame's pre-hud pass stamped the value before it.
void SceneFx_PresentFallback(IDirect3DDevice9* dev)
{
    if (s_sceneFxDone != g_presentCount.load() - 1 && SceneFx_Wanted()) {
        static unsigned fallbacks = 0;
        if ((fallbacks++ % 600) == 0)
            DebugLog("^FX: bloom/cc fell back to Present (hud gets it) -- %u frames so far", fallbacks);
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            SceneFx_Draw(dev, bb);
            bb->Release();
        }
    }
}

// Runs Gaussian blur passes pre-frame (before ImGui::NewFrame) so DrawBgBlurRect
// is a plain AddImageRounded with no mid-render RT switching.
void BlurFrame_Run(IDirect3DDevice9* dev, float sigma, bool widen)
{
    if (!dev || !g_blurBBSurf || !g_blurTexA || !g_blurTexB ||
        !g_blurSurfA || !g_blurSurfB || !g_blurShaderX || !g_blurShaderY) return;

    static bool s_logged = false;
    if (!s_logged) { DebugLog("BlurFrame_Run: first run sigma=%.1f", sigma); s_logged = true; }

    int passes = std::max(1, std::min(8, (int)roundf(sigma / 5.f)));
    float W = (float)g_blurShaderW, H = (float)g_blurShaderH;

    // Full-screen quad (D3D9 texel-center -0.5 offset)
    struct Vert { float x,y,z,rhw,u,v; };
    const Vert quad[4] = {
        {-0.5f,   -0.5f,   0,1, 0,0},
        {W-0.5f,  -0.5f,   0,1, 1,0},
        {-0.5f,   H-0.5f,  0,1, 0,1},
        {W-0.5f,  H-0.5f,  0,1, 1,1},
    };

    dev->StretchRect(g_blurBBSurf, nullptr, g_blurSurfA, nullptr, D3DTEXF_NONE);

    // Save everything we touch
    IDirect3DSurface9 *rtOld=nullptr, *dsOld=nullptr;
    IDirect3DPixelShader9* psOld=nullptr;
    IDirect3DBaseTexture9* tex0Old=nullptr;
    D3DVIEWPORT9 vpOld{};
    DWORD zenable, zwrite, ablend, atest, cull, scissor, addrU, addrV, minF, magF, fvf;
    DWORD srgbWrite, srgbTex;
    DWORD colWrite, c0Op, a0Op, a0Arg1, c1Op;
    IDirect3DVertexDeclaration9* declOld=nullptr;

    dev->GetRenderTarget(0, &rtOld);
    dev->GetDepthStencilSurface(&dsOld);
    dev->GetPixelShader(&psOld);
    dev->GetTexture(0, &tex0Old);
    dev->GetViewport(&vpOld);
    dev->GetRenderState(D3DRS_ZENABLE,           &zenable);
    dev->GetRenderState(D3DRS_ZWRITEENABLE,      &zwrite);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE,  &ablend);
    dev->GetRenderState(D3DRS_ALPHATESTENABLE,   &atest);
    dev->GetRenderState(D3DRS_CULLMODE,          &cull);
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
    dev->GetRenderState(D3DRS_SRGBWRITEENABLE,   &srgbWrite);
    dev->GetSamplerState(0, D3DSAMP_ADDRESSU,    &addrU);
    dev->GetSamplerState(0, D3DSAMP_ADDRESSV,    &addrV);
    dev->GetSamplerState(0, D3DSAMP_MINFILTER,   &minF);
    dev->GetSamplerState(0, D3DSAMP_MAGFILTER,   &magF);
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &srgbTex);
    dev->GetFVF(&fvf);
    dev->GetVertexDeclaration(&declOld);
    dev->GetRenderState(D3DRS_COLORWRITEENABLE,  &colWrite);
    dev->GetTextureStageState(0, D3DTSS_COLOROP,   &c0Op);
    dev->GetTextureStageState(0, D3DTSS_ALPHAOP,   &a0Op);
    dev->GetTextureStageState(0, D3DTSS_ALPHAARG1, &a0Arg1);
    dev->GetTextureStageState(1, D3DTSS_COLOROP,   &c1Op);

    // Set blur state
    D3DVIEWPORT9 vp{0,0,(DWORD)W,(DWORD)H,0.f,1.f};
    dev->SetDepthStencilSurface(nullptr);
    dev->SetViewport(&vp);
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetRenderState(D3DRS_ZENABLE,           FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,      FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,   FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,          D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,   FALSE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);

    // Ping-pong: A→B (X pass), B→A (Y pass), repeat.
    // widen: pass i steps i+1 texels (Kawase-style). Earlier passes already smoothed the
    // finer scales, so the wider taps don't alias; radius grows ~N^1.5 instead of ~sqrt(N).
    for (int i = 0; i < passes; ++i) {
        const float step = widen ? (float)(i + 1) : 1.f;
        float pX[4] = {step/W, 0,0,0};
        dev->SetRenderTarget(0, g_blurSurfB);
        dev->SetTexture(0, g_blurTexA);
        dev->SetPixelShader(g_blurShaderX);
        dev->SetPixelShaderConstantF(0, pX, 1);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vert));

        float pY[4] = {step/H, 0,0,0};
        dev->SetRenderTarget(0, g_blurSurfA);
        dev->SetTexture(0, g_blurTexB);
        dev->SetPixelShader(g_blurShaderY);
        dev->SetPixelShaderConstantF(0, pY, 1);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vert));
    }
    // Result is in g_blurTexA

    // Source keeps scene depth in backbuffer alpha, so the copy carried it
    // along and ImGui blended the panel by it: near/far showed different blur
    // strength. Stamp alpha = 1 (alpha channel only) so every panel is opaque.
    {
        struct AVert { float x,y,z,rhw; D3DCOLOR c; };
        const AVert aq[4] = {
            {-0.5f,  -0.5f,  0,1, 0xFFFFFFFF},
            {W-0.5f, -0.5f,  0,1, 0xFFFFFFFF},
            {-0.5f,  H-0.5f, 0,1, 0xFFFFFFFF},
            {W-0.5f, H-0.5f, 0,1, 0xFFFFFFFF},
        };
        dev->SetRenderTarget(0, g_blurSurfA);
        dev->SetPixelShader(nullptr);
        dev->SetTexture(0, nullptr);
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA);
        dev->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, aq, sizeof(AVert));
    }

    // Restore everything
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,  colWrite);
    dev->SetTextureStageState(0, D3DTSS_COLOROP,   c0Op);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   a0Op);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, a0Arg1);
    dev->SetTextureStageState(1, D3DTSS_COLOROP,   c1Op);
    dev->SetRenderTarget(0, rtOld);
    dev->SetDepthStencilSurface(dsOld);
    dev->SetPixelShader(psOld);
    dev->SetTexture(0, tex0Old);
    dev->SetViewport(&vpOld);
    dev->SetRenderState(D3DRS_ZENABLE,           zenable);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,      zwrite);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  ablend);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,   atest);
    dev->SetRenderState(D3DRS_CULLMODE,          cull);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,   srgbWrite);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,    addrU);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,    addrV);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER,   minF);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER,   magF);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, srgbTex);
    if (declOld) { dev->SetVertexDeclaration(declOld); declOld->Release(); }
    else         { dev->SetFVF(fvf); }
    if (rtOld)   { rtOld->Release(); }
    if (dsOld)   { dsOld->Release(); }
    if (psOld)   { psOld->Release(); }
    if (tex0Old) { tex0Old->Release(); }
}


// ---- SetCursorPos hook -------------------------------------
BOOL WINAPI h_SetCursorPos(int x, int y) {
    if (g_showMenu) return TRUE;
    return o_SetCursorPos(x, y);
}

// ---- WndProc hook ------------------------------------------
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
WNDPROC g_origWndProc = nullptr;

LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN) {
        // Rebinding the menu key: capture the next pressed key (Escape cancels).
        if (g_menuKeyCapture) {
            if (wParam != VK_ESCAPE && wParam != VK_LBUTTON && wParam != VK_RBUTTON)
                g_menuKey = (int)wParam;
            g_menuKeyCapture = false;
            return 0;
        }
        // Rebinding a web surface key, same deal.
        if (Web_CaptureKey((int)wParam)) return 0;
    }

    // Mouse buttons for the web surface binds. They never arrive as WM_KEYDOWN, which is why
    // the first cut could not bind one. Taken before ImGui sees them, and only while a rebind
    // is armed -- ImGui::Button fires on release, so the click that armed it is already gone.
    if (g_webKeyCapture) {
        int mvk = 0;
        switch (msg) {
        case WM_LBUTTONDOWN: mvk = VK_LBUTTON; break;
        case WM_RBUTTONDOWN: mvk = VK_RBUTTON; break;
        case WM_MBUTTONDOWN: mvk = VK_MBUTTON; break;
        case WM_XBUTTONDOWN: mvk = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2; break;
        default: break;
        }
        if (mvk && Web_CaptureKey(mvk)) return 0;
    }

    if (msg == WM_KEYDOWN) {
        if ((int)wParam == g_menuKey) {
            g_showMenu = !g_showMenu;
            if (g_showMenu) { ShowCursor(TRUE);  ClipCursor(nullptr); }
            else            { ShowCursor(FALSE); }
            return 0;
        }
        if (wParam == VK_F11) { g_eject = true; return 0; }
    }

    // Web on surface: wheel / Up / Down scroll the page while aimed at it (menu closed).
    if (!g_showMenu && Web_OnInput(msg, (uintptr_t)wParam)) return 0;

    if (g_showMenu)
    {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        switch (msg)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:   case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:   case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:   case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:     case WM_KEYUP:
        case WM_SYSKEYDOWN:  case WM_SYSKEYUP:
        case WM_CHAR:        case WM_UNICHAR:
        case WM_INPUT:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        default: break;
        }
    }
    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
}

// ---- Dynamic light tick ------------------------------------
// Called every frame from h_Present (render thread).
// CL_AllocDLight (slot 4) = world + model light; CL_AllocElight (slot 5) = entity
// light that reaches the viewmodel.  die = curtime + 0.05 so both auto-expire if
// the feature is toggled off mid-frame.
void DLightTick()
{
    // On the frame the feature is toggled off, force-expire both slots so the
    // engine doesn't leave a frozen light in place. Off and already expired: nothing to do.
    static bool s_dlightWasEnabled = false;
    if (!g_enableDLight && !s_dlightWasEnabled) return;
    if (!g_effects) return;
    if (!g_netvarsReady || g_offOrigin <= 0) return;

    int li = GetLocalPlayerIndex();
    if (li < 1) return;

    using AllocDL_t = dlight_t*(__fastcall*)(void*, int);

    if (!g_enableDLight) {
        if (s_dlightWasEnabled) {
            s_dlightWasEnabled = false;
            dlight_t* dl = nullptr;
            __try { dl = ((AllocDL_t)(*(void***)g_effects)[4])(g_effects, li); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
            if (dl) { dl->die = 0.f; dl->radius = 0.f; }

            dlight_t* el = nullptr;
            __try { el = ((AllocDL_t)(*(void***)g_effects)[5])(g_effects, li + 32); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
            if (el) { el->die = 0.f; el->radius = 0.f; }
        }
        return;
    }
    s_dlightWasEnabled = true;

    void* pEnt = GetClientEntity(li);
    if (!pEnt) return;

    // Read world-space origin from netvar
    const float* org = reinterpret_cast<const float*>(
        reinterpret_cast<uintptr_t>(pEnt) + g_offOrigin);

    // IVEngineClient::Time() = slot 14, returns engine curtime (seconds since map start)
    float curtime = 0.f;
    if (g_engine)
        __try { curtime = VCall<float>(g_engine, 14); }
        __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Resolve active color (sheen or manual pick)
    float activeR, activeG, activeB;
    if (g_dlightUseSheenColor) {
        float sc[3];
        GetAnimatedSheenColor(sc);
        activeR = sc[0]; activeG = sc[1]; activeB = sc[2];
    } else {
        activeR = g_dlightColor[0];
        activeG = g_dlightColor[1];
        activeB = g_dlightColor[2];
    }
    uint8_t cr = (uint8_t)(activeR * 255.f);
    uint8_t cg = (uint8_t)(activeG * 255.f);
    uint8_t cb = (uint8_t)(activeB * 255.f);

    // CL_AllocDLight (slot 4) -- lights world geometry only (DLIGHT_NO_MODEL_ILLUMINATION=0x2)
    // Model/viewmodel illumination is handled separately by CL_AllocElight below.
    // Using flags=0 (models enabled) triggers materialsystem per-mesh calls for every
    // nearby model each frame, which AVs when any model is in transitional state.
    dlight_t* dl = nullptr;
    __try { dl = ((AllocDL_t)(*(void***)g_effects)[4])(g_effects, li); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (dl) {
        dl->flags           = 0x2; // DLIGHT_NO_MODEL_ILLUMINATION -- world geometry only
        dl->origin[0]       = org[0];
        dl->origin[1]       = org[1];
        dl->origin[2]       = org[2] + 40.f; // ~waist height
        dl->radius          = g_dlightRadius;
        dl->die             = curtime + 0.05f;
        dl->decay           = 0.f;
        dl->minlight        = 0.f;
        dl->key             = li;
        dl->style           = 0;
        dl->color.r         = cr;
        dl->color.g         = cg;
        dl->color.b         = cb;
        dl->color.exponent  = 5;
        dl->direction[0]    = 0.f; dl->direction[1] = 0.f; dl->direction[2] = 0.f;
        dl->inner_angle     = 0.f;
        dl->outer_angle     = 0.f;
    }

    // CL_AllocElight (slot 5) -- entity light, affects entity models and the viewmodel.
    // Key offset +32 keeps it out of the dlight pool range (player indices are 1-32).
    dlight_t* el = nullptr;
    __try { el = ((AllocDL_t)(*(void***)g_effects)[5])(g_effects, li + 32); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (el) {
        el->flags           = 0;
        el->origin[0]       = org[0];
        el->origin[1]       = org[1];
        el->origin[2]       = org[2] + 40.f;
        el->radius          = g_dlightRadius;
        el->die             = curtime + 0.05f;
        el->decay           = 0.f;
        el->minlight        = 0.f;
        el->key             = li + 32;
        el->style           = 0;
        el->color.r         = cr;
        el->color.g         = cg;
        el->color.b         = cb;
        el->color.exponent  = 5;
        el->direction[0]    = 0.f; el->direction[1] = 0.f; el->direction[2] = 0.f;
        el->inner_angle     = 0.f;
        el->outer_angle     = 0.f;
    }
}


bool GetDX9VTable(void** vt, int n) {
    HMODULE d3d = GetModuleHandleW(L"d3d9.dll");
    if (!d3d) return false;
    auto Create = (IDirect3D9*(__stdcall*)(UINT))GetProcAddress(d3d, "Direct3DCreate9");
    if (!Create) return false;

    IDirect3D9* p = Create(D3D_SDK_VERSION);
    if (!p) return false;

    HWND w = CreateWindowExW(0, L"STATIC", nullptr, WS_POPUP, 0,0,2,2, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = w; pp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = p->CreateDevice(0, D3DDEVTYPE_NULLREF, w, D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev);

    if (FAILED(hr) || !dev) {
        pp.BackBufferWidth = pp.BackBufferHeight = 2;
        hr = p->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    }

    bool ok = false;
    if (SUCCEEDED(hr) && dev) {
        void** dvt = *(void***)dev;
        for (int i = 0; i < n; ++i) vt[i] = dvt[i];
        dev->Release(); ok = true;
    }
    p->Release();
    DestroyWindow(w);
    return ok;
}

