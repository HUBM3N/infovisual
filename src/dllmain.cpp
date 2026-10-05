// infovisual -- DX9 Present hook + ImGui overlay
// INSERT = toggle menu, F11 = eject

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "common.h"
#include "globals.h"
#include "features/drawutils.h"
#include "features/config.h"
#include "features/watermark.h"
#include "features/crosshair.h"
#include "features/menu.h"
#include "features/visuals.h"
#include "features/misc.h"
#include "features/names.h"
#include "features/skins.h"
#include "features/render.h"



// ---- Globals -----------------------------------------------
SetCursorPos_t o_SetCursorPos = nullptr;
ICvar*             g_cvar        = nullptr;

// ICvar::ConsoleColorPrintf is at vtable slot 23 (IAppSystem=5 slots, then
// counting through ICvar methods in ICVar.h -- confirmed from Amalgam's ICVar.h).
// Signature: void ConsoleColorPrintf(const Color_t& clr, const char* pFormat, ...)
// On x64, const Color_t& is passed as a pointer in rdx.
struct ConsoleColor4 { uint8_t r, g, b, a; };
using ConsoleColorPrintf_t = void(__fastcall*)(void*, const ConsoleColor4*, const char*, ...);

void ConsoleColorPrint(uint8_t r, uint8_t g, uint8_t b, const char* msg)
{
    if (!g_cvar) return;
    __try {
        auto fn = reinterpret_cast<ConsoleColorPrintf_t>((*reinterpret_cast<void***>(g_cvar))[23]);
        ConsoleColor4 col{ r, g, b, 255 };
        fn(g_cvar, &col, "%s", msg);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

HMODULE            g_hSelf       = nullptr;
HANDLE             g_hPollThread    = nullptr;
HANDLE             g_hWatchdogThread = nullptr;
IEngineClient*     g_engine      = nullptr;
void*              g_gameEventManager = nullptr; // IGameEventManager2 (engine.dll GAMEEVENTSMANAGER002)
void*              g_matSystem   = nullptr;
void*              g_studioRender = nullptr;
void*              g_renderView   = nullptr;  // IVRenderView (engine.dll VEngineRenderView014)
void*              g_modelRender = nullptr;  // IVModelRender  (engine.dll VEngineModel016)
void*              g_modelInfo   = nullptr;  // IVModelInfoClient (engine.dll VModelInfoClient006)
std::atomic<bool>  g_drawingProps {false};
HWND               g_hwnd        = nullptr;
bool               g_imguiInit   = false;
DWORD              g_loadingCircleStartMs = 0;        // set when imgui first inits
std::atomic<bool>  g_fullyInjected        {false};   // set when "infovisual on" prints
std::atomic<bool>  g_run         {true};
std::atomic<bool>  g_eject       {false};
HANDLE             g_hWatchdogExitEvent = nullptr;
std::atomic<int>   g_hookDepth   {0};
std::atomic<int>   g_fsnDepth    {0};
std::atomic<int>   g_miscHookDepth {0};
std::atomic<bool>  g_inGame      {false};

// Guards concurrent access to weapon attribute lists between the game thread
// (FSN_InjectAttribs) and the render thread (h_SheenOnBind). Without this,
// SetOrAdd writing m_Size on the game thread races with OnBind iterating
// m_Memory on the render thread, corrupting the pointer shaderapidx9 receives.
CRITICAL_SECTION   g_attrLock;
bool               g_attrLockInit = false;

// Features. Every feature toggle defaults off: these values are what a first-run config saves.
bool g_showMenu           = false;
int  g_menuKey            = VK_INSERT; // key that toggles the menu (default Insert)
bool g_menuKeyCapture     = false;     // true while waiting to rebind g_menuKey
bool g_enableOverlay      = false;
bool  g_enableMediaPlayer  = false; // media player overlay toggle
float g_mpLastBoxH         = 0.f;  // height of last rendered MP box (0 = not visible)
float g_mpLastBoxY         = 0.f;  // top-left Y of last rendered MP box
int   g_mpLastBoxPos       = -1;   // WM_* position enum of last rendered MP box

// ---- Watermark last box position (set each frame by RenderWatermarkAndOverlay)
float g_wmLastBoxX   = 0.f;
float g_wmLastBoxY2  = 0.f;
float g_wmLastBoxW   = 0.f;
float g_wmLastBoxH2  = 0.f;
int   g_wmLastBoxPos2 = -1;

// ---- Box shadow --------------------------------------------
bool  g_wmBoxShadow         = false;
float g_wmBoxShadowOffX     = 4.f;
float g_wmBoxShadowOffY     = 4.f;
float g_wmBoxShadowColor[4] = { 0.f, 0.f, 0.f, 0.70f };
float g_wmBoxShadowBlur     = 8.f;
bool  g_mpBoxShadow         = false;
float g_mpBoxShadowOffX     = 4.f;
float g_mpBoxShadowOffY     = 4.f;
float g_mpBoxShadowColor[4] = { 0.f, 0.f, 0.f, 0.70f };
float g_mpBoxShadowBlur     = 8.f;
bool  g_bgBlur          = false;   // global -- applies to all panels uniformly
float g_bgBlurSigma     = 8.f;     // watermark + media player
float g_menuBgBlurSigma = 8.f;     // menu panel (separate)
IDirect3DPixelShader9* g_blurShaderX  = nullptr;
IDirect3DPixelShader9* g_blurShaderY  = nullptr;
IDirect3DTexture9*     g_blurTexA     = nullptr; // ping-pong A — result lives here
IDirect3DSurface9*     g_blurSurfA    = nullptr;
IDirect3DTexture9*     g_blurTexB     = nullptr; // ping-pong B
IDirect3DSurface9*     g_blurSurfB    = nullptr;
int                    g_blurShaderW  = 0;
int                    g_blurShaderH  = 0;
bool                   g_menuBgBlur   = false;
IDirect3DSurface9* g_blurBBSurf  = nullptr;
int                g_blurBBW     = 0;
int                g_blurBBH     = 0;

// ---- Bloom (features/bloom.cpp) ----------------------------
bool  g_bloomEnable    = false;
float g_bloomIntensity = 0.6f;
float g_bloomThreshold = 0.7f;
float g_bloomRadius    = 0.5f;
float g_bloomSat       = 1.2f;

// ---- Color correction --------------------------------------
bool  g_colorCorrEnable   = false;
float g_colorCorrR        = 1.0f;  // red gain   [0..2, 1=neutral]
float g_colorCorrG        = 1.0f;  // green gain [0..2, 1=neutral]
float g_colorCorrB        = 1.0f;  // blue gain  [0..2, 1=neutral]
float g_colorCorrHue      = 0.0f;  // hue shift  [-180..180 deg, 0=neutral]
float g_colorCorrSat      = 1.0f;  // saturation [0..3, 1=neutral]
float g_colorCorrContrast = 1.0f;  // contrast   [0..3, 1=neutral]
float g_colorCorrTemp        = 0.0f;  // temperature [-1..1, 0=neutral]
float g_colorCorrExposure    = 0.0f;  // EV [-3..+3]
float g_colorCorrLevInBlack  = 0.0f;  // levels input black  [0..1]
float g_colorCorrLevInWhite  = 1.0f;  // levels input white  [0..1]
float g_colorCorrLevGamma    = 1.0f;  // levels gamma        [0.1..10]
float g_colorCorrLevOutBlack = 0.0f;  // levels output black [0..1]
float g_colorCorrLevOutWhite = 1.0f;  // levels output white [0..1]
float g_colorCorrHighlights  = 0.0f;  // highlights boost    [-1..+1]
float g_colorCorrShadows     = 0.0f;  // shadows boost       [-1..+1]
CcCurve g_ccCurve[4];                 // tone curves: master, R, G, B (identity by default)
IDirect3DPixelShader9* g_ccShader    = nullptr;
IDirect3DTexture9*     g_ccLutTex    = nullptr;  // 256x1 curves LUT, sampler 1
IDirect3DTexture9*     g_ccInputTex  = nullptr;  // RT input texture — StretchRect(BB→) then sample
IDirect3DSurface9*     g_ccInputSurf = nullptr;  // level 0 of g_ccInputTex
IDirect3DTexture9*     g_ccOutTex    = nullptr;  // output RT texture
IDirect3DSurface9*     g_ccOutSurf   = nullptr;  // level 0 of g_ccOutTex

// ---- Overlay image slots -----------------------------------
OverlayImageSlot g_imgSlots[k_imgMaxSlots];
int g_imgSelectedSlot = 0;
int g_imgDragSlot      = -1; // slot index being dragged, -1 = none

bool g_enableImageOverlay = false;

bool g_enablePureBypass   = false;
bool g_enableCheatsbypass = false;
bool g_menuSnow           = false;

// Screen effects (features/screenfx.cpp, ssao.cpp, dof.cpp). Defaults are coldtires' own.

bool  g_aoEnable      = false;
float g_aoIntensity   = 1.6f;
float g_aoRadius      = 40.f;
int   g_aoSamples     = 12;
float g_aoBias        = 0.2f;
float g_aoBlur        = 1.f;
float g_aoScale       = 0.75f;
float g_aoFadeStart   = 1200.f;
float g_aoFadeEnd     = 3000.f;
bool  g_aoViewmodel   = true;
bool  g_aoFogFade     = true;
bool  g_aoDebug       = false;

bool  g_dofEnable        = false;
bool  g_dofAutoFocus     = true;
float g_dofDistance      = 400.f;
float g_dofFocusSpeed    = 8.f;
float g_dofFocusLimit    = 4000.f;
float g_dofRange         = 300.f;
float g_dofRamp          = 600.f;
float g_dofMaxBlur       = 20.f;
float g_dofBokehBoost    = 4.f;
float g_dofBokehThreshold= 0.6f;
bool  g_dofNearBlur      = true;
bool  g_dofViewmodelSharp= true;
bool  g_dofSmooth        = true;

// Frame blending (features/frameblend.cpp). 8ms is a 1/125s shutter, which is about what a
// camera shooting 60fps footage of a game runs at -- long enough to see, short enough that the
// picture never stops being readable.
bool  g_fbEnable     = false;
float g_fbShutter    = 8.f;
float g_fbAmount     = 1.f;
bool  g_fbPlayers    = true;
bool  g_fbViewmodel  = true;
float g_fbVmAmount   = 1.f;
int   g_fbSamples    = 24;
float g_fbMaxStreak  = 0.06f;

// ---- Web on surface ---------------------------------------------------------------
bool  g_webEnable        = false;
bool  g_webPanel         = false;
float g_webPanelOpacity  = 1.f;
float g_webPanelScale    = 1.f;
int   g_webResH          = 720;     // short side; the long side comes from g_webAspect
float g_webVolume        = 0.5f;
std::string g_webUrl     = "https://www.youtube.com/";
int   g_webPlaceKey      = 0;
int   g_webClickKey      = 0;
int   g_webKeyCapture    = 0;
float g_webScale         = 1.f;     // long side = 128 units -- roughly a player and a half wide
int   g_webAspect        = 0;       // k_wsAspects index: 16:9
float g_webWorldOpacity  = 1.f;
int   g_webDepthMode     = 0;
int   g_webBackMode      = 0;
float g_webRound         = 4.f;
bool  g_webPanelAlways   = false;
bool  g_webVolDistance   = false;
float g_webVolNear       = 128.f;
float g_webVolFar        = 900.f;
bool  g_webAdblock       = true;
bool  g_webYtEmbed       = true;
bool  g_webCrosshairPointer = true;
bool  g_webClickOnly     = false;
int   g_webRemoveKey     = 0;
bool  g_webHideSteam     = true;
bool  g_webScrollInput   = true;
int   g_webRClickKey     = VK_RBUTTON;
bool  g_webBlockFire     = true;
bool  g_webSmooth        = true;
bool  g_webAutoPlay      = false;
bool  g_webPlayFromStart = false;
bool  g_webAutoRemove    = false;
bool g_minesweeperOpen    = false;
bool g_msDarkMode         = true;  // minesweeper.online night skin

// net_graph text recolor. Defaults match what the engine draws, so switching
// the feature on changes nothing until a color is actually picked.
bool  g_ngColorEnable    = false;
float g_ngTextColor[4]   = { 0.898f, 0.898f, 0.698f, 1.f }; // 229/229/178
float g_ngOrangeColor[4] = { 1.f,    0.490f, 0.122f, 1.f }; // 255/125/31
float g_ngRedColor[4]    = { 1.f,    0.122f, 0.122f, 1.f }; // 255/31/31
// yellow warn: lerp below cl_updaterate (255/255/31) and sv fps 10..20 (255/255/0).
// Two engine triples, one picker -- same warn tier.
float g_ngYellowColor[4] = { 1.f,    1.f,    0.122f, 1.f }; // 255/255/31

// Sheen globals
bool  g_enableSheen        = false;
float g_sheenColor[3]      = { 1.0f, 1.0f, 1.0f };

bool  g_enableSheenAnim    = false;

bool  g_enableCritColor    = false;
float g_critColor[3]       = { 1.0f, 0.5f, 0.0f };
bool  g_critUseSheenColor  = false;

bool  g_enableDRColor      = false;
float g_drColor[3]         = { 0.0f, 0.8f, 1.0f };
bool  g_drUseSheenColor    = false;

bool  g_enableEquipRegionUnlock = false;

// ---- Killfeed filter ---------------------------------------
bool  g_selfKillfeedOnly   = false; // hide entries that don't involve local player
float g_deathNoticeTime    = 5.0f;  // hud_deathnotice_time value (default 5)

// ---- Custom killfeed ---------------------------------------
bool  g_customKillfeedEnable   = false;
int   g_kfOrigin               = 1;     // 0=LEFT, 1=RIGHT, 2=CENTER
float g_kfOffsetX              = 10.f;
float g_kfOffsetY              = 10.f;
int   g_kfMaxEntries           = 5;
float g_kfEntryPadY            = 2.f;
float g_kfBgColor[4]           = { 0.05f, 0.05f, 0.07f, 0.78f };
float g_kfTextColor[4]         = { 0.90f, 0.90f, 0.90f, 1.0f  };
float g_kfRedColor[4]          = { 0.95f, 0.35f, 0.35f, 1.0f  };
float g_kfBluColor[4]          = { 0.35f, 0.60f, 1.0f,  1.0f  };
bool  g_kfBoxShadow            = false;
float g_kfBoxShadowOffX        = 3.f;
float g_kfBoxShadowOffY        = 3.f;
float g_kfBoxShadowColor[4]    = { 0.f, 0.f, 0.f, 0.55f };
float g_kfBoxShadowBlur        = 5.f;
OutlineLayer g_kfOutlineLayers[k_maxOutlineLayers] = {};
int          g_kfOutlineLayerCount = 0;
int   g_kfFontIndex            = -1;
float g_kfFontScale            = 1.0f;
bool  g_kfFontFlags[4]         = {};
std::string g_kfFontFile       = "";
bool  g_kfUseIcons             = true;
bool  g_kfPlainWeaponIcons     = false;
bool  g_kfConsistentIcons      = false;
float g_kfIconScale            = 1.0f;
bool  g_kfSlideIn              = true;
float g_kfSlideSpeed           = 12.f;
int   g_kfSlideDir             = 0;     // 0=auto, 1=from left, 2=from right
bool  g_kfFadeIn               = true;
float g_kfFadeSpeed            = 4.0f;
float g_kfBgRounding           = 3.f;
float g_kfPadX                 = 8.f;
float g_kfMinHeight            = 0.f;
bool  g_kfGrowDown             = false;   // false=newest at top, true=oldest at top (stack down)
float g_kfIconColor[4]         = { 1.0f, 1.0f, 1.0f, 1.0f };
bool  g_kfTextDropShadow         = false;
float g_kfTextDropShadowColor[4] = { 0.f, 0.f, 0.f, 0.6f };
float g_kfTextDropShadowOffX     = 1.5f;
float g_kfTextDropShadowOffY     = 1.5f;
bool  g_kfTextDropShadowBlur     = false;
bool  g_kfTextOutline            = false;
float g_kfTextOutlineColor[4]    = { 0.f, 0.f, 0.f, 0.8f };
float g_kfTextOutlineThickness   = 1.f;
bool  g_kfFadeOut      = false;
float g_kfFadeOutSpeed = 2.5f;
bool  g_kfSlideOut      = false;
float g_kfSlideOutSpeed = 8.f;
int   g_kfSlideOutDir   = 0;    // 0=auto (same side as entry), 1=to left, 2=to right
bool  g_kfSmoothShift      = false;
float g_kfSmoothShiftSpeed = 12.f;
int   g_kfBgCornerFlags = 0;
bool  g_kfBlobEnable  = false;
float g_kfBlobColor[3]= { 0.3f, 0.6f, 1.f };
float g_kfBlobAlpha   = 0.4f;
float g_kfBlobSize    = 1.f;
float g_kfBlobSpeed   = 1.f;
bool  g_kfPrecipEnable   = false;
int   g_kfPrecipMode     = 0;
int   g_kfPrecipCount    = 30;
float g_kfPrecipSpeed    = 1.f;
float g_kfPrecipSize     = 1.f;
float g_kfPrecipWind     = 0.f;
float g_kfPrecipColor[4] = { 1.f, 1.f, 1.f, 0.6f };
bool  g_kfKillGlowEnable   = false;
float g_kfKillGlowColor[4] = { 1.f, 0.6f, 0.1f, 0.8f };
float g_kfKillGlowDuration = 1.5f;
float g_kfKillGlowSpread   = 15.f;
bool  g_kfNameLive         = false;

// ---- Weapon HUD --------------------------------------------
bool  g_enableWeaponHud      = false;
bool  g_wpnHudEditMode       = false;
float g_wpnHudX              = 40.f;   // px from right screen edge /
float g_wpnHudY              = 0.f;    // vertical offset from centre (px)
float g_wpnHudIconSz         = 36.f;   // icon size in px
float g_wpnHudSlotGap        = 6.f;    // gap between slots in px
float g_wpnHudActiveColor[4]   = { 1.f, 1.f, 1.f, 1.f };
float g_wpnHudInactiveColor[4] = { 0.7f, 0.7f, 0.7f, 0.40f };
float g_wpnHudNumScale       = 0.75f;  // font scale for slot numbers
int   g_wpnHudNumPos        = 1;      // 0=top 1=center 2=bottom (relative to icon)
float g_wpnHudNumOffX       = 0.f;   // additional X offset for slot number
float g_wpnHudNumOffY       = 0.f;   // additional Y offset for slot number
bool  g_wpnHudFlipIcons     = false;
int   g_wpnHudOriginH       = 0;      // 0=right, 1=left, 2=center (horizontal bar)
int   g_wpnHudOriginV       = 0;      // 0=center, 1=top, 2=bottom
bool  g_wpnHudGlowEnabled   = false;
float g_wpnHudGlowRadius    = 10.f;   // Gaussian blur radius in pixels
float g_wpnHudGlowBolden    = 0.f;    // dilation radius before blur (makes outline+glow)
float g_wpnHudGlowAlpha     = 0.75f;
float g_wpnHudGlowColors[8][4] = {    // per visual slot (row 0=top), RGBA
    {1.f,1.f,1.f,1.f},{1.f,1.f,1.f,1.f},{1.f,1.f,1.f,1.f},{1.f,1.f,1.f,1.f},
    {1.f,1.f,1.f,1.f},{1.f,1.f,1.f,1.f},{1.f,1.f,1.f,1.f},{1.f,1.f,1.f,1.f},
};
bool  g_enableWpnHudName   = false;
bool  g_wpnHudShowNum      = true;   // show slot number labels
int   g_wpnHudNameMaxChars = 40;     // max displayed chars before "..." truncation (0 = no limit)
int   g_wpnHudNamePos      = 1;      // 0=above icon, 1=below icon
float g_wpnHudNameOffX     = 0.f;   // additional X offset for weapon name
float g_wpnHudNameOffY     = 0.f;   // additional Y offset for weapon name
float g_wpnHudNameScale    = 0.85f;  // font scale for weapon name text
int   g_wpnHudNumFontIndex   = -1;
std::string g_wpnHudNumFontFile  = "";
bool  g_wpnHudNumFontFlags[4]   = {};
int   g_wpnHudNameFontIndex  = -1;
std::string g_wpnHudNameFontFile = "";
bool  g_wpnHudNameFontFlags[4]  = {};
// Weapon name text effects
bool  g_wpnHudNameShadowEnabled    = true;
float g_wpnHudNameShadowColor[4]   = { 0.f, 0.f, 0.f, 0.7f };
float g_wpnHudNameShadowOffX       = 1.5f;
float g_wpnHudNameShadowOffY       = 1.5f;
bool  g_wpnHudNameOutlineEnabled   = false;
float g_wpnHudNameOutlineColor[4]  = { 0.f, 0.f, 0.f, 0.8f };
float g_wpnHudNameOutlineThickness = 1.f;
int   g_wpnHudNameColorMode     = 0;         // 0=Glow color  1=Icon color  2=Custom
float g_wpnHudNameCustomColor[4] = { 1.f, 1.f, 1.f, 1.f };
// Weapon slot number text effects
bool  g_wpnHudNumShadowEnabled    = false;
float g_wpnHudNumShadowColor[4]   = { 0.f, 0.f, 0.f, 0.7f };
float g_wpnHudNumShadowOffX       = 1.f;
float g_wpnHudNumShadowOffY       = 1.f;
bool  g_wpnHudNumOutlineEnabled   = false;
float g_wpnHudNumOutlineColor[4]  = { 0.f, 0.f, 0.f, 0.8f };
float g_wpnHudNumOutlineThickness = 1.f;
int   g_wpnHudNumColorMode      = 0;         // 0=Icon color  1=Glow color  2=Custom
float g_wpnHudNumCustomColor[4]  = { 1.f, 1.f, 1.f, 1.f };
float g_wpnHudActiveScale        = 1.f;   // scale multiplier applied to the selected weapon icon
bool  g_wpnHudActiveScaleSmooth  = false; // smoothly animate scale transitions
float g_wpnHudActiveScaleSpeed   = 8.f;   // lerp speed (units/sec)
float g_wpnHudIconRotation       = 0.f;   // icon rotation in degrees (-180..180)
bool  g_wpnHudSepEnabled         = false; // separators between icons (centered/horiz layout only)
float g_wpnHudSepColor[4]        = { 1.f, 1.f, 1.f, 0.5f };
float g_wpnHudSepWidth           = 2.f;   // separator line width in px
float g_wpnHudSepHeight          = 0.7f;  // separator height as fraction of icon height
bool  g_wpnHudCustomNames        = false; // replace displayed names with per-slot custom strings
char  g_wpnHudCustomName[5][64]  = {};    // custom name for visual slots 0-4
bool  g_wpnHudFade               = false; // fade out after weapon switch
float g_wpnHudFadeDelay          = 1.5f;  // seconds visible before fade begins
float g_wpnHudFadeSpeed          = 2.f;   // alpha units per second (1/speed = fade duration)
bool  g_wpnHudBgEnabled              = false;
float g_wpnHudBgWidth                = 200.f;
float g_wpnHudBgColorNear[4]         = { 0.f, 0.f, 0.f, 0.70f };
float g_wpnHudBgColorFar[4]          = { 0.f, 0.f, 0.f, 0.f   };
float g_wpnHudBgFadeStart            = 0.5f;   // fraction of width (from near edge) that stays solid before far fade
bool  g_wpnHudBgFadeNear             = false;  // also fade the near (origin) side
float g_wpnHudBgFadeNearStart        = 0.3f;   // fraction of width used for near-side fade-in
float g_wpnHudBgHeightH              = 60.f;   // fixed background height for horizontal layout (px)
float g_wpnHudBgExtraH               = 0.f;    // extra height padding per side in vertical layout (px)
float g_wpnHudBgOffX                 = 0.f;    // X position offset (positive = away from screen edge)
float g_wpnHudBgOffY                 = 0.f;    // Y position offset (positive = down)
bool  g_wpnHudBgScreenEdge           = false;  // pin near side to screen edge instead of weapon anchor
bool  g_wpnHudBgBorderEnabled        = false;
float g_wpnHudBgBorderHeight         = 3.f;
float g_wpnHudBgBorderColorNear[4]   = { 1.f, 1.f, 1.f, 0.80f };
float g_wpnHudBgBorderColorFar[4]    = { 1.f, 1.f, 1.f, 0.f   };

// ---- No hats -----------------------------------------------
bool g_noHats     = false;
int  g_offParticleProp = -2; // -2=uncached, -1=not found, >=0=byte offset of CParticleProperty in entity

// ---- Halloween footsteps -----------------------------------
bool  g_hwFootsteps         = false;
float g_hwFootColor[3]      = { 1.0f, 0.4f, 0.0f }; // orange default
bool  g_hwFootUseSheenColor = false;
bool  g_hwForceHoliday      = false;

// ---- Zombify -----------------------------------------------
bool        g_zombify        = false;
int         g_zombifyMode    = 0; // 0=enemies only, 1=everyone, 2=everyone incl. local
const void* s_zombieModels[10] = {}; // cached model_t* per class index 1-9
int         s_zombieDynIdx[10] = {}; // 0=unregistered, INT_MIN=failed, non-zero=dynamic model index
void*       g_mdlCache         = nullptr; // IMDLCache (datacache.dll MDLCache004+)
void*       s_zombieHWData[10] = {}; // studiohwdata_t* per class, from IMDLCache::GetHardwareData
int8_t      s_boneMap[10][128] = {}; // zombie bone i -- player bone index (-1 = no match)
bool        s_boneMapped[10]   = {}; // true once BuildZombieBoneMap has run for this class

float s_hwStepAccum     = 0.f;
bool  s_hwLastPosValid  = false;
// Particle index for halloween_boss_foot_impact_customcolor is looked up fresh each footstep
// (not cached) because unusual-particle registration can shift indices mid-session.
std::unordered_set<int> g_ragdollKillSet; // entity indices of players killed by local player

// ---- Killstreak eye glow color override --------------------
bool  g_ksEyeUseSheenColorGlobal = false; // any enabled tier-3 skin has ksUseSheenColor
bool  g_hwFootHideStealth = false;

float g_sheenColors[4][3]  = {
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f },
    { 1.0f, 1.0f, 0.0f },
};
int   g_sheenColorCount    = 2;
float g_sheenAnimSpeed     = 2.0f;

bool  g_enableFog          = false;
float g_fogColor[3]        = { 0.5f, 0.5f, 0.5f };
bool  g_fogUseSheenColor   = false;
float g_fogStart           = 0.f;
float g_fogEnd             = 1000.f;
float g_fogDensity         = 1.f;
bool  g_fogLinkSkybox      = true;
float g_fogSkyboxStart     = 0.f;
float g_fogSkyboxEnd       = 1000.f;
float g_fogSkyboxDensity   = 1.f;


bool  g_enableSkybox       = false;
char  g_skyboxName[64]     = "sky_tf2_04";

bool  g_enableModulate     = false;
float g_modulateWorld[3]   = { 1.0f, 1.0f, 1.0f };
float g_modulateSky[3]     = { 1.0f, 1.0f, 1.0f };
float g_modulateProps[3]   = { 1.0f, 1.0f, 1.0f };

// ---- Weather -----------------------------------------------
// Mode 0 = off
// Mode 1 = Rain  -- CPrecipitation sprite entity, 8192-unit world box
// Mode 2 = Snow  -- CPrecipitation sprite entity, 8192-unit world box
// Mode 3 = Custom particle -- CTE_DispatchEffect, map-wide, no CPrecipitation
int   g_rainMode        = 1;
bool  g_weatherEnabled  = false;
char  g_particleLayerName[8][128] = {"","","","","","","",""}; // per-layer particle name

// Separate rain cvars (mode 1)
float g_rainAlpha       = 0.4f;
float g_rainSpeed       = 600.f;
float g_rainWidth       = 0.5f;
float g_rainLength      = 0.1f;
float g_rainRadius      = 1500.f;
float g_rainSideVel     = 130.f;
// Separate snow cvars (mode 2) -- tuned for soft dot-like flakes
float g_snowAlpha       = 1.0f;
float g_snowSpeed       = 80.f;    // slow fall
float g_snowWidth       = 3.0f;    // wide -- dot-like
float g_snowLength      = 0.02f;   // very short -- no streak
float g_snowRadius      = 1500.f;
float g_snowSideVel     = 15.f;    // gentle drift
// Density scalar [0.1, 1.0] -- multiplied into g_snowRadius before pushing the
// cvar.  Halving density quarters active particle count (area ~ radius^2).
float g_snowDensity     = 0.35f;
// Snow sprite override -- replaces the particles/rain streak material with an
// arbitrary VMT path.  Defaults to particle/snow (round soft flake).
char  g_snowSpriteName[64] = "particle/snow";
bool  s_snowSpriteDirty   = false; // set when name changes -- force re-apply

// CPrecipitation entity state (modes 1 & 2 only)
void* g_precipClass       = nullptr;
void* g_precipEntity      = nullptr;
void* g_precipNetworkable = nullptr;
int   g_offPrecipType     = -1;
int   g_offMins           = -1;
int   g_offMaxs           = -1;
int   s_lastAppliedMode   = -99;

// Custom particle state (mode 3 only)
CTE_DispatchEffect_t      g_fnDispatchEffect   = nullptr;
Get_ParticleSystemIndex_t g_fnGetParticleIndex = nullptr;
// Amalgam-style footstep hooks -- call-site addresses resolved at load.
// Both hooks use _ReturnAddress() to only modify behavior when called from inside CTFPlayer::FireEvent.
uintptr_t g_addrFireEvent_IsHolidayCall = 0;
uintptr_t g_addrFireEvent_AttribHookCall = 0;
// Byte offset of the CNewParticleEffect flags bitfield (-1=uncalibrated, -2=failed, >=0=valid).
int   s_bRemoveOffset = -1;

// Custom particle grid settings (mode 3)
int   g_particleLayerCount = 5;
float g_particleLayerZ[8]  = { -3000.f, -1500.f, 0.f, 1500.f, 3000.f, 0.f, 0.f, 0.f };
float g_particleGridRadius = 4000.f; // XY radius from map origin; grid points outside this are skipped
float g_particleSpacingOffset = 0.f; // added to the 1000u base cell spacing; negative = cells overlap

bool  g_enableViewmodelSway   = false;
float g_vmSwayScale           = 0.f;
float g_vmSwayInterp          = 0.f;
bool  g_enableViewmodelOffset = false;
float g_vmOffsetX             = 0.f;
float g_vmOffsetY             = 0.f;
float g_vmOffsetZ             = 0.f;
bool  g_vmOffsetSeparate      = false;
float g_vmOffset2X            = 0.f;
float g_vmOffset2Y            = 0.f;
float g_vmOffset2Z            = 0.f;
bool  g_removeViewmodelBob    = false;
// CS:GO viewmodel (vm_csgo.h) -- defaults = CS:GO cvar defaults
bool  g_vmCsBob               = false;
float g_vmCsBobCycle          = 0.98f;  // cl_bobcycle
float g_vmCsBobUp             = 0.5f;   // cl_bobup
float g_vmCsBobVert           = 0.25f;  // cl_bobamt_vert
float g_vmCsBobLat            = 0.4f;   // cl_bobamt_lat
float g_vmCsBobLower          = 21.f;   // cl_bob_lower_amt
bool  g_vmCsGunLower          = false;
float g_vmCsGunLowerAngle     = 2.f;    // cl_gunlowerangle
float g_vmCsGunLowerSpeed     = 0.1f;   // cl_gunlowerspeed
bool  g_vmCsLandDip           = false;
float g_vmCsLandDipAmt        = 4.f;    // cl_headbob_land_dip_amt
float g_vmCsLandDipTime       = 0.25f;  // seconds to the bottom (CS:GO 0.25)

// ---- Viewmodel Chams ---------------------------------------
// matType: 0 = Flat (UnlitGeneric/engine white), 1 = Shaded (VertexLitGeneric),
//          2 = Wireframe, 3 = Fresnel (additive phong rim)

// Weapon viewmodel chams
bool          g_enableVmWeaponChams                   = false;
int           g_vmWeaponChamCount                     = 1;
VmChamLayer_t g_vmWeaponChamLayers[k_vmMaxLayers]     = {};

// Arms viewmodel chams
bool          g_enableVmArmsChams                     = false;
int           g_vmArmsChamCount                       = 1;
VmChamLayer_t g_vmArmsChamLayers[k_vmMaxLayers]       = {};

// ---- Dynamic light (DLight) on local player ----------------
// IEffects interface (engine.dll VEngineEffects001), vtable slot 4 = CL_AllocDLight.
void*  g_effects        = nullptr;
bool   g_enableDLight        = false;
float  g_dlightColor[3]      = { 1.f, 0.9f, 0.7f }; // warm white default
float  g_dlightRadius        = 200.f;
bool   g_dlightUseSheenColor = false; // track animated sheen color instead of manual pick

// IClientModeShared -- used to hook DoPostScreenSpaceEffects (slot 39)
void*   g_clientModePtr            = nullptr;
bool    g_doPostHookInstalled      = false;
bool    g_drawViewModelsHookInstalled = false;
// CViewRender pointer -- saved from h_DrawViewModels rcx; used to hook LevelInit (slot 1).
void*   g_viewRender               = nullptr;
bool    g_levelInitHookInstalled   = false;

// Player model chams
bool          g_enablePlChams                         = false;
int           g_plChamCount                           = 1;
VmChamLayer_t g_plChamLayers[k_vmMaxLayers]           = {};
bool          g_plChamsLocalPlayer                    = false; // include local player
// On by default: cosmetics already matched the player path test before this
// option existed, so defaulting it off would silently un-cham everyone's hats.
bool          g_plChamsAttachments                    = true;  // also cham held weapons / carried items

// Map textures -- per-category world-brush texture swap (see visuals.cpp)
bool          g_enableMapTextures                      = false;
MapTexCat_t   g_mapTexCats[k_mapTexCatCount]           = {};

// Flip World (World & Players tab): mirror the rendered world frame + controls (see flipworld.cpp)
bool          g_flipWorld                              = false;

// Prop (static prop) chams
bool          g_enablePropChams                        = false;
int           g_propChamCount                          = 1;
VmChamLayer_t g_propChamLayers[k_vmMaxLayers]          = {};

// ---- Viewmodel Glow ----------------------------------------
bool  g_enableVmWeaponGlow       = false;
float g_vmWeaponGlowColor[4]     = { 0.2f, 0.5f, 1.0f, 0.8f };
float g_vmWeaponGlowSigma        = 8.0f;
int   g_vmWeaponGlowStencil      = 0;     // 0=off, 1-10 = pixel-offset halo thickness
bool  g_vmWeaponGlowUseSheen     = false; // track animated killstreak sheen colour

// ---- Transparent viewmodel ---------------------------------
bool  g_vmAlphaEnable            = false;
float g_vmAlpha                  = 0.5f;  // 1 = opaque (no-op), 0 = invisible
bool  g_vmFlatAlpha              = true;  // one alpha over whole weapon, not alpha x phong mask

bool  g_enableVmArmsGlow         = false;
float g_vmArmsGlowColor[4]       = { 0.2f, 0.5f, 1.0f, 0.8f };
float g_vmArmsGlowSigma          = 8.0f;
int   g_vmArmsGlowStencil        = 0;
bool  g_vmArmsGlowUseSheen       = false;


// ---- Crosshair ---------------------------------------------
bool  g_enableCrosshair      = false;
bool  g_chAntialias          = false;
bool  g_chAdditive           = false;  // additive blend for the custom crosshair
int   g_chBlendMode          = 0;      // kChBlend id (ch_blend_modes.h); 0 falls back to g_chAdditive
bool  g_staticAmbyCrosshair  = false;
bool  g_chUseSheenColor      = false;  // crosshair tracks sheen color
float g_chColor[4]           = { 0.0f, 1.0f, 0.0f, 1.0f };
float g_chOutlineColor[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };
float g_chThickness         = 1.0f;   // bar thickness in units (1 unit = 2 px)
float g_chLength             = 4.0f;   // bar length in units
float g_chGap                = 2.0f;   // gap from center in units
bool  g_chSidesOnly          = false;  // restrict which arms are drawn
bool  g_chSides[4]           = { true, true, true, true }; // L R B T (dirs 0-3)
bool  g_chDot                = false;
bool  g_chDotCircle          = false;  // draw dot as circle instead of square
bool  g_chOutline            = false;
float g_chOutlineThick       = 0.5f;   // outline thickness in units
bool  g_chOutlineUseSheenColor = false; // outline tracks sheen color
bool  g_chOutlineSidesOnly   = false;   // restrict outline to specific sides
bool  g_chOutlineSides[4]    = { true, true, true, true }; // L R T B
bool  g_chOutlineOnLines     = true;   // draw outline on arms
bool  g_chOutlineOnDot       = true;   // draw outline on dot
bool  g_chOutlineBlur        = false;  // soft dropshadow-style blur on outline
float g_chOutlineBlurRadius  = 3.0f;   // spread in units
float g_chAspect             = 1.0f;   // <1: vertical arms longer, >1: horizontal arms longer
float g_chGapAspect          = 1.0f;   // >1: horizontal gap wider, <1: vertical gap wider
bool  g_chDynamic            = false;
float g_chDynamicMin         = 0.0f;   // minimum speed (u/s) before any expansion begins
float g_chDynamicMax         = 3.0f;   // max extra gap at full speed (units)
float g_chDynamicRef         = 400.0f; // speed (u/s) that maps to full expansion
bool  g_chGradient           = false;
float g_chGradColorCenter[4] = { 0.f, 1.f, 0.f, 1.f };
float g_chGradColorOuter[4]  = { 0.f, 1.f, 0.f, 0.f };
float g_chGradStart          = 0.0f;
float g_chGradEnd            = 1.0f;
bool  g_chTaper              = false;
float g_chTaperNear          = 1.0f;
float g_chTaperFar           = 0.0f;
bool  g_chDotCustomColor     = false;
float g_chDotColor[4]        = { 0.f, 1.f, 0.f, 1.f };
float g_chRotation           = 0.f;   // rotation in degrees

// ---- CS:GO sniper scope (scope_csgo.inl) -- csgo defaults ----
bool  g_scCsgo            = false;
int   g_scCsgoWeapon      = 0;      // kScCsWeapons: 0 AWP, 1 SSG 08, 2 SCAR-20/G3SG1
int   g_scCsgoZoom        = 0;      // 0 = zoom fov 1, 1 = zoom fov 2
int   g_scCsgoWidth       = 1;      // cl_crosshair_sniper_width
bool  g_scCsgoNormalInacc = false;  // cl_crosshair_sniper_show_normal_inaccuracy
float g_scCsgoMoveSens    = 1.f;    // ground speed inaccuracy, 0 = still scope
float g_scCsgoAirSens     = 1.f;    // jump/air/land inaccuracy
bool  g_scCsgoScopeIn     = true;   // unscoped penalty carries in (blurred scope-in)
bool  g_scCsgoBob         = true;   // scope sways with the csgo bob
float g_scCsgoBobScale    = 1.f;
bool  g_scCsgoLens        = true;   // overlays/scope_lens dust
float g_scCsgoScale       = 1.f;    // arc box + lens size, 1 = csgo
bool  g_scCsgoNoDot       = true;   // hide your own sniper dot
bool  g_scChargeHide      = false;  // skip CHudScopeCharge::Paint
float g_scCsgoOpacity     = 1.f;    // whole scope alpha
float g_scCsgoBobLat      = 1.f;    // sway strength, sideways (x csgo cl_bobamt_lat 0.4)
float g_scCsgoBobVert     = 1.f;    // sway strength, up/down (x csgo cl_bobamt_vert 0.25)

// ---- CSGO crosshair ----------------------------------------
bool  g_chCsgoEnable    = false;
int   g_chCsgoStyle     = 0;
float g_chCsgoColor[4]  = { 130.f/255.f, 177.f/255.f, 22.f/255.f, 1.f };  // crosshairColor1
float g_chCsgoGap       = 0.f;
float g_chCsgoScale     = 1.f;
bool  g_chCsgoCrisp     = true;
bool  g_chCsgoLinear    = true;
float g_chCsgoVelSens   = 1.f;      // 1 = csgo's own step sizes, 0 = movement moves nothing
float g_chCsgoSmooth    = 0.f;      // seconds to ease into a new gap/spread, 0 = snap (csgo)
bool  g_chCsgoArcs      = true;     // the 4 thin spread lines
bool  g_chCsgoPips      = true;     // the 4 green gap arms

float g_csXSize         = 5.f;      // cl_crosshairsize
float g_csXThick        = 0.5f;     // cl_crosshairthickness
float g_csXGap          = 1.f;      // cl_crosshairgap
bool  g_csXDot          = true;     // cl_crosshairdot
bool  g_csXOutline      = true;     // cl_crosshair_drawoutline
float g_csXOutlineTh    = 1.f;      // cl_crosshair_outlinethickness
bool  g_csXT            = false;    // cl_crosshair_t
int   g_csXAlpha        = 200;      // cl_crosshairalpha
bool  g_csXUseAlpha     = true;     // cl_crosshairusealpha
float g_csXSplitDist    = 7.f;      // cl_crosshair_dynamic_splitdist
float g_csXSplitRatio   = 0.35f;    // cl_crosshair_dynamic_maxdist_splitratio
float g_csXInnerMod     = 1.f;      // cl_crosshair_dynamic_splitalpha_innermod
float g_csXOuterMod     = 0.5f;     // cl_crosshair_dynamic_splitalpha_outermod
bool  g_speedoPreDoctrineParens = false;

void ResetFSNState()
{
    g_fsn_wasEnabled      = false;
    g_fsn_lastLocalEnt    = nullptr;
    g_fsn_entityChangeAge = 0;
    g_fsn_offMyWeapons    = 0;
    g_fsn_offAttrList     = 0;
    g_fsn_offItemDefIndex = 0;
    g_offHViewModel0      = 0;
    g_fsn_skinWasEnabled  = false;
    o_CTFRevolver_GetWeaponCrosshairScale = nullptr;
    g_sheenSigsResolved   = false;
    g_fnGetItemSchema            = nullptr;
    g_fnGetAttributeDefinition   = nullptr;
    g_fnSetRuntimeAttributeValue = nullptr;
    // Reset all weather state
    g_precipClass       = nullptr;
    g_precipEntity      = nullptr;
    g_precipNetworkable = nullptr;
    g_offPrecipType     = -1;
    g_offMins           = -1;
    g_offMaxs           = -1;
    s_lastAppliedMode   = -99;
    g_fnDispatchEffect  = nullptr;
    g_fnGetParticleIndex = nullptr;
    o_CParticlePropCreate   = nullptr;
    o_CParticlePropCreatePt = nullptr;
    for (int i = 0; i < 8; i++) s_trackedWeather[i].clear();
    s_hwLastPosValid   = false;
    s_hwStepAccum      = 0.f;
    g_ragdollKillSet.clear();
    s_bRemoveOffset    = -1; // recalibrate next session
    memset(s_zombieModels, 0, sizeof(s_zombieModels));
    memset(s_zombieDynIdx, 0, sizeof(s_zombieDynIdx));
    memset(s_zombieHWData, 0, sizeof(s_zombieHWData));
    memset(s_boneMapped,   0, sizeof(s_boneMapped));
    g_mdlCache = nullptr;
    g_offParticleProp = -2; // re-scan on next cosmetic encounter
}


float       g_watermarkFade     = 0.0f;
float       g_wmSmoothW         = 0.0f; // animated current width (spring)
float       g_wmSmoothWV        = 0.0f; // velocity for spring
float       g_wmSmoothItemX[k_wmMaxItems] = {}; // per-item smooth X (spring)
float       g_wmSmoothItemV[k_wmMaxItems] = {}; // per-item velocity
std::string g_customLabel       = "infovisual";
int        g_wmLabelSource      = 0; // 0=custom, 1=ingame, 2=windows
bool       g_wmShowUsername     = true; // show username in watermark
std::string g_wmUsernameLabel    = "";   // custom label for username (overrides default)
std::string g_windowsUsername    = "";
bool        g_drawWatermarkBg   = true;

int   g_labelAnimMode    = 0;
float g_labelScrollSpeed = 4.0f;   // chars/sec for scroll
float g_labelTypeSpeed   = 8.0f;   // chars/sec for type-out
float g_labelEraseSpeed  = 16.0f;  // chars/sec for erase
float g_labelHoldTime    = 1.5f;   // seconds to hold full text before erasing
// Title cycle: list of titles to rotate through on a timer (independent of anim mode)
TitleEntry_t  g_titleList[k_maxTitleCycle] = {};
int           g_titleCount      = 0;
float         g_titleInterval   = 3.0f;  // seconds per title
int           g_titleIdx        = 0;
ULONGLONG     g_titleLastChange = 0;
bool          g_titleCycleEnable = false; // independent of labelAnimMode
bool        g_drawWatermarkText = true;
bool        g_drawFPS           = true;
bool        g_drawPing          = true;
bool        g_drawLoss          = true;
bool        g_drawTime          = true;
float       g_watermarkScale    = 1.0f;
char        g_wmSeparator[16]   = " | ";
bool        g_wmUseIcons        = false; // replace separators with field icons
float       g_watermarkYOffset  = 0.0f;
float       g_watermarkXOffset  = 0.0f; // px offset added to anchor X (positive = right/inward, negative = left/outward)
int         g_wmFontWeight      = 0; // kept for config back-compat, ignored when custom font selected

// ---- Menu accent color (RGB 0-1, default blue) -------------
float g_accentColor[3]    = { 110/255.f, 175/255.f, 255/255.f };
float g_menuBgColor[3]    = { 0.035f, 0.035f, 0.043f };
float g_menuTextColor[3]  = { 0.863f, 0.863f, 0.863f }; // #DCDCDC base menu text
bool  g_menuShowTabIcons = true; // show SVG icons next to tab names

// ---- Font enumeration --------------------------------------
std::vector<FontEntry> g_fontList;
int                    g_wmFontIndex  = -1;  // -1 = use built-in segoeui
std::string            g_wmFontFile   = "";  // filename only, for config persistence
// Font style flags for the watermark font (stb_truetype backend)
// 0=Bold (RasterizerMultiply*1.4) 1=Monochrome (OversampleH/V=1, PixelSnap) 2=Sharp (OversampleH=2)  3=PixelSnap
// 4=Drop Shadow (render text twice with offset)  5=Outline (render text stroke in black)
bool g_wmFontFlags[6]      = {};
bool g_wmFontFlagsBuilt[6] = {}; // what was last built, for dirty detection
bool g_mpFontFlagsBuilt[6] = {};


int g_threshFPS  = 60;
int g_threshPing = 120;
int g_threshLoss = 6;

float g_wmBgColor[4]      = { 0.00f, 0.00f, 0.00f, 0.82f };
float g_wmDimColor[3]     = { 0.51f, 0.51f, 0.57f };
float g_wmTextColor[3]    = { 0.86f, 0.86f, 0.90f };
float g_wmAlertColor[3]   = { 0.86f, 0.24f, 0.24f };
float g_wmTitleColor[3]   = { 0.86f, 0.86f, 0.90f };
float g_wmBgRounding      = 6.0f;
int   g_wmBgCornerFlags   = 0;    // 0 = all corners; bit 0=TL,1=TR,2=BL,3=BR
OutlineLayer g_wmOutlineLayers[k_maxOutlineLayers];
int          g_wmOutlineLayerCount = 1; // always >= 1; layer[0] is the primary

// Legacy single-outline shims -- map to layer[0] so old config round-trips cleanly
bool&   g_wmOutline          = g_wmOutlineLayers[0].enable;
float&  g_wmOutlineThick     = g_wmOutlineLayers[0].thick;
int&    g_wmOutlineSideFlags = g_wmOutlineLayers[0].sideFlags;
float*  g_wmOutlineColor     = g_wmOutlineLayers[0].color;
// Watermark blob gradient
bool  g_wmBlobEnable      = false;
bool  g_wmBlobUseSheen    = false;
float g_wmBlobColor[3]    = { 0.55f, 0.35f, 1.0f };
float g_wmBlobAlpha       = 0.55f;  // used when sheen color is on (sheen has no alpha)
float g_wmBlobSpeed       = 1.0f;
float g_wmBlobSize        = 0.7f;   // sigma as fraction of rect short-side

// Watermark precipitation
bool  g_wmPrecipEnable    = false;
int   g_wmPrecipMode      = 0;      // 0=rain, 1=snow
int   g_wmPrecipCount     = 40;     // particle count
float g_wmPrecipSpeed     = 1.0f;   // fall speed multiplier
float g_wmPrecipSize      = 1.0f;   // length/radius multiplier
float g_wmPrecipWind      = 0.0f;   // wind: -1=left .. 1=right
float g_wmPrecipColor[4]  = { 1.0f, 1.0f, 1.0f, 0.35f }; // RGBA

// Watermark kill glow
bool  g_wmKillGlowEnable   = false;
float g_wmKillGlowColor[4] = { 1.0f, 0.35f, 0.1f, 1.0f }; // warm orange-red default
float g_wmKillGlowDuration = 1.5f;  // seconds for glow to fade out
float g_wmKillGlowSpread   = 18.0f; // max expansion in px beyond the outermost outline
float g_wmKillGlowAlpha    = 0.0f;  // runtime only -- not saved
bool  g_wmKillGlowRising   = false; // true = fading in toward 1.0

int   g_watermarkPos     = WM_TOP_RIGHT;

// ---- Media Player overlay settings -------------------------
// mode 0 = card (thumb + title + artist + progress bar)
// mode 1 = plain text ("title - artist" on a watermark-style strip)
int   g_mpMode           = 0;      // 0=card, 1=text strip
bool  g_mpAvoidWatermark = true;   // push watermark away when sharing position
bool  g_mpShowThumb      = true;
bool  g_mpThumbPreserveAspect = true;  // don't stretch to square
bool  g_mpThumbCircle     = false;  // clip album art to circle
bool  g_mpThumbOutline  = false;  // outline around album art
float g_mpThumbOutlineThick = 2.f; // outline thickness
float g_mpThumbOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f }; // outline color
float g_mpThumbSize      = 46.f;  // px before scale, range 16..96
bool  g_mpShowProgress   = true;
bool  g_mpShowDuration   = false; // show track duration next to artist name
float g_mpScale          = 1.0f;
float g_mpYOffset        = 0.5f;
float g_mpXOffset        = 0.0f; // px offset added to anchor X
int   g_mpPos            = WM_BOTTOM_LEFT;
// copied-from-watermark toggle -- when true, reads wm globals at render time
bool  g_mpCopyWmStyle    = false;
// independent style (used when g_mpCopyWmStyle == false)
float g_mpBgColor[4]     = { 0.00f, 0.00f, 0.00f, 0.82f };
float g_mpTitleColor[3]  = { 0.86f, 0.86f, 0.90f };
float g_mpArtistColor[3] = { 0.51f, 0.51f, 0.57f };
float g_mpBgRounding     = 6.0f;
int   g_mpBgCornerFlags  = 0;    // 0 = all corners; bit 0=TL,1=TR,2=BL,3=BR
bool  g_mpBgDraw         = true;
OutlineLayer g_mpOutlineLayers[k_maxOutlineLayers];
int          g_mpOutlineLayerCount = 1;

bool&   g_mpOutline          = g_mpOutlineLayers[0].enable;
float&  g_mpOutlineThick     = g_mpOutlineLayers[0].thick;
int&    g_mpOutlineSideFlags = g_mpOutlineLayers[0].sideFlags;
float*  g_mpOutlineColor     = g_mpOutlineLayers[0].color;
// Media player blob gradient
bool  g_mpBlobEnable     = false;
bool  g_mpBlobUseSheen   = false;
float g_mpBlobColor[3]   = { 0.55f, 0.35f, 1.0f };
float g_mpBlobAlpha      = 0.55f;
float g_mpBlobSpeed      = 1.0f;
float g_mpBlobSize       = 0.7f;

// Media player precipitation
bool  g_mpPrecipEnable   = false;
int   g_mpPrecipMode     = 0;
int   g_mpPrecipCount    = 40;

// Media player waveform dots
bool   g_mpWaveform      = false;
float  g_wfSensitivity   = 4.0f;  // threshold: higher = quieter sounds activate dots
float  g_wfBands[5] = {};          // 0..1 per dot, written by WaveformThread
HANDLE g_wfThread   = nullptr;
bool   g_wfRunning  = false;
float g_mpPrecipSpeed    = 1.0f;
float g_mpPrecipSize     = 1.0f;
float g_mpPrecipWind     = 0.0f;
float g_mpPrecipColor[4] = { 1.0f, 1.0f, 1.0f, 0.35f };
bool  g_mpFontFlags[6]   = {};
int   g_mpFontIndex      = -1;
std::string g_mpFontFile = "";
bool  g_mpShowLyrics     = false;
bool  g_mpLyricsAbove    = false;
bool  g_mpLyricsSide     = false;
float g_mpLyricsXOff     = 0.f;
float g_mpLyricsYOff     = 0.f;
float g_mpLyricsColor[4] = { 0.86f, 0.86f, 0.90f, 1.0f };
float g_mpLyricsScale    = 1.0f;
float g_mpLyricsOffset   = 0.0f;
bool  g_mpLyricsBlob      = true;
bool  g_mpLyricsPrecip    = true;
bool  g_mpLyricsBgDraw    = true;  // draw background behind lyrics box
bool  g_mpLyricsClampWidth = false;
float g_mpLyricsClampW    = 280.f;
bool  g_mpLyricsAvoidWm    = true;   // push lyrics away from watermark box
bool  g_mpLyricsShiftBoxes = false;  // shift MP box (+ WM) up/down so lyrics stay on screen
bool  g_mpLyricsOnly       = false;  // show lyrics without the media player card/strip
int   g_mpLyricsRows       = 3;      // total lines shown (prev + active + next)
// Per-element alignment: 0=left, 1=center, 2=right
int   g_mpTitleAlign    = 0;
int   g_mpArtistAlign   = 0;
int   g_mpLyricsAlign   = 0;
// Waveform dot position: 0=before title, 1=after title, 2=free screen position
int   g_mpWaveformPos   = 1;
float g_mpWfFreeX       = 100.f; // screen X of leftmost dot (free mode)
float g_mpWfFreeY       = 100.f; // screen center Y of dots (free mode)
// Lyrics: truncate with "..." instead of word-wrapping when clamp width is on
bool  g_mpLyricsEllipsis = false;
// Per-element pixel offsets (unscaled; multiplied by sc at render time)
float g_mpTitleXOff     = 0.f;
float g_mpTitleYOff     = 0.f;
float g_mpArtistXOff    = 0.f;
float g_mpArtistYOff    = 0.f;
float g_mpAlbumXOff     = 0.f;
float g_mpAlbumYOff     = 0.f;
// Album cover corner rounding (base px at sc=1, non-circle mode only)
float g_mpThumbRounding  = 4.f;
// Custom album cover (overrides song art, independent of current track)
bool        g_mpCustomThumb     = false;
std::string g_mpCustomThumbPath = "";
// Progress bar adjustments (card mode only)
float g_mpProgXOff    = 0.f;  // x position offset px
float g_mpProgYOff    = 0.f;  // y position offset px
float g_mpProgBarW    = 0.f;  // extra width px (symmetric, can be negative)
float g_mpProgHeight  = 2.f;  // bar height base px (replaces hardcoded 2.f)
// MP width controls
float g_mpMaxWidth    = 0.f;   // 0 = auto; >0 = max box width in base pixels (sc=1)
bool  g_mpStaticWidth = false; // if true, box is always exactly g_mpMaxWidth wide
float g_mpTitleMaxW   = 0.f;   // 0 = auto; >0 = max clip width for title text (base px)
float g_mpArtistMaxW  = 0.f;   // 0 = auto; >0 = max clip width for artist text (base px)
bool  g_mpTitleEllipsis  = false; // truncate title with ... instead of scrolling
bool  g_mpArtistEllipsis = false; // truncate artist with ... instead of clipping
// Per-side BG gradient -- media player  [0]=L [1]=R [2]=T [3]=B
BgSideGrad g_mpBgGrad[4];
// Per-side BG gradient -- watermark
BgSideGrad g_wmBgGrad[4];
float g_mpBgInsetL    = 0.f;   // base px inset from left   (positive = shrink bg)
float g_mpBgInsetR    = 0.f;   // base px inset from right
float g_mpBgInsetT    = 0.f;   // base px inset from top
float g_mpBgInsetB    = 0.f;   // base px inset from bottom
float g_wmBgInsetL    = 0.f;
float g_wmBgInsetR    = 0.f;
float g_wmBgInsetT    = 0.f;
float g_wmBgInsetB    = 0.f;
// Menu UI font (custom font shown in the overlay menu)
int         g_menuFontIndex      = -1;   // -1 = default segoeui
std::string g_menuFontFile       = "";
bool        g_menuFontFlags[6]   = {};
bool        g_menuFontFlagsBuilt[6] = {};
int         g_menuFontBuiltAtIndex = -2; // sentinel: force first build
float       g_menuFontScale        = 1.0f;
float       g_menuFontBuiltAtScale = -1.f;
// ---- Speedometer ------------------------------------------
bool  g_speedoEnable       = false;
// position: screen fraction [0,1]; default bottom-center just above crosshair
float g_speedoPosX         = 0.5f;
float g_speedoPosY         = 0.65f;
float g_speedoScale        = 1.0f;
// number text font
int         g_speedoFontIndex  = -1;   // -1 = default segoeui
std::string g_speedoFontFile   = "";
bool        g_speedoFontFlags[6] = {}; // same semantics as wmFontFlags
bool        g_speedoFontFlagsBuilt[6] = {};
float       g_speedoFontBuiltAtScale = -1.f;
int         g_speedoFontBuiltAtIndex = -2;
// colors -- speed-dependent gradient: below loSpeed--col0, loSpeed..hiSpeed--lerp, above hiSpeed--col1
float g_speedoColorLow[4]  = { 1.f, 1.f, 1.f, 1.f };   // slow color (RGBA)
float g_speedoColorMid[4]  = { 0.4f, 0.9f, 0.4f, 1.f }; // mid color
float g_speedoColorHigh[4] = { 1.f, 0.3f, 0.3f, 1.f };  // fast color
float g_speedoSpeedLow     = 200.f;   // speed at which low--mid starts
float g_speedoSpeedHigh    = 450.f;   // speed at which mid--high completes
bool  g_speedoColorBySpeed = true;    // false = always use colorLow
// outline
bool  g_speedoOutline      = false;
float g_speedoOutlineColor[4] = { 0.f, 0.f, 0.f, 1.f };
float g_speedoOutlineThick    = 1.f;
// drop shadow
bool  g_speedoShadow       = true;
float g_speedoShadowColor[4] = { 0.f, 0.f, 0.f, 0.55f };
float g_speedoShadowOffX   = 1.5f;
float g_speedoShadowOffY   = 1.5f;
bool  g_speedoShadowBlur   = false;   // multi-sample soft shadow
// fade in/out -- shows when speed >= threshold, hides when below
bool  g_speedoFadeIn       = true;
float g_speedoFadeThreshold = 0.f;    // min speed to show (0 = always show when alive)
float g_speedoAlpha        = 0.f;     // runtime: current alpha [0,1]
float g_speedoAlphaV       = 0.f;     // spring velocity for alpha
// smooth number display (spring drives displayed value toward real value)
float g_speedoDisplayVal   = -1.f;    // -1 = uninitialised, snap on first frame
float g_speedoDisplayV     = 0.f;
// animated main-number slot width (springs to natural pixel width -- keeps block centered smoothly)
float g_speedoMainSlotW    = 0.f;     // current animated width; 0 = uninitialised, snap on first frame
float g_speedoMainSlotWV   = 0.f;
// pre-speed (jump takeoff only -- ignores falling off ledges)
bool  g_speedoShowPre      = true;
float g_speedoPreVal       = 0.f;     // last take-off speed -- frozen at jump moment
float g_speedoPreTimer     = 0.f;     // seconds remaining to show it (counts down)
float g_speedoPreAlpha     = 0.f;     // smooth alpha for pre-speed text [0,1]
float g_speedoPreAlphaV    = 0.f;     // spring velocity for pre-speed alpha
float g_speedoPreXOff      = 0.f;     // normalized slide offset: -1=collapsed, 0=expanded
float g_speedoPreXOffV     = 0.f;     // spring velocity for slide
float g_speedoPreSlotW     = 0.f;     // animated slot width in pixels (springs to natural preSz.x+gap)
float g_speedoPreSlotWV    = 0.f;     // spring velocity for slot width
float g_speedoPreHoldTime  = 3.0f;    // seconds to hold before fading out
bool  g_speedoPreAnimCenter = false;  // expand-from-center animation (vs slide-from-right)
bool  g_speedoWasOnGround  = false;
// graph -- independent enable, position, and alpha
bool  g_speedoGraph        = false;
float g_speedoGraphPosX    = 0.5f;
float g_speedoGraphPosY    = 0.72f;
float g_speedoGraphW       = 185.f;
float g_speedoGraphH       = 60.f;
float g_speedoGraphColor[4]  = { 0.4f, 0.9f, 0.4f, 0.8f };
float g_speedoGraphMaxSpeed  = 500.f;   // speed mapped to full graph height
int   g_speedoGraphFade      = 1;        // 0=none,1=both ends,2=by vel,3=end,4=start
float g_speedoGraphLineH     = 1.f;      // line thickness in px (width stays 1px per sample)
float g_speedoGraphAlpha     = 0.f;      // runtime fade alpha for graph
float g_speedoGraphAlphaV    = 0.f;
std::deque<float> g_speedoHistory; // speed samples, newest at front
float g_speedoMainPosX    = -1.f;  // animated left-edge X of main text; -1 = snap next frame
float g_speedoCenterDelay = 0.f;   // >0 = countdown before main re-centers alone after pre-speed fades
// ---- end speedometer globals -------------------------------
// ---- RagdollEffects -- apply visual effects to ragdolls of local player's kills ----
// Toggle g_ragdollEffectEnabled to turn the whole feature on/off.
// Set individual effect bools to choose which effects apply.
bool g_ragdollEffectEnabled  = false;
bool g_ragdollEffectBurning  = false;
bool g_ragdollEffectElectro  = false;
bool g_ragdollEffectAsh      = false;
bool g_ragdollEffectDissolve = false;
bool g_ragdollEffectGold     = false;
bool g_ragdollEffectIce      = false;
bool g_ragdollOnlyKills      = false; // hide ragdolls of players NOT killed by local player


std::atomic<float> g_fps  {0.f};
std::atomic<int>   g_ping {0};
std::atomic<int>   g_loss {0};
std::atomic<bool>  g_conn {false};

// ---- Debug phase tracker -----------------------------------
char               g_phase[128]    = "init";
std::atomic<DWORD> g_phaseThread   {0};
std::atomic<int>   g_presentCount  {0};
std::atomic<int>   g_fsnStage      {-1};

void SetPhase(const char* p)
{
    strncpy(g_phase, p, sizeof(g_phase) - 1);
    g_phase[sizeof(g_phase) - 1] = '\0';
    g_phaseThread.store(GetCurrentThreadId());
}

// The VMT writers used the CWD, which only equals the game folder by launch convention, and a
// file dialog (now open while the game runs) can move it. The exe's folder cannot move.
void GetGameDir(char* out)
{
    const DWORD n     = GetModuleFileNameA(nullptr, out, MAX_PATH);
    char*       slash = (n > 0 && n < MAX_PATH) ? strrchr(out, '\\') : nullptr;
    if (slash) *slash = '\0';
    else       GetCurrentDirectoryA(MAX_PATH, out);
}

void DebugLog(const char* fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)sizeof(buf) - 2;   // truncated: keep the head instead of dropping the line
    if (n == 0) return;
    buf[n]   = '\n';
    buf[n+1] = '\0';

    // Open the log file ONCE and keep the handle for the process lifetime.
    //
    // The old implementation did CreateFileA + WriteFile + CloseHandle on EVERY
    // call. Windows Defender's on-access scanner re-reads the whole file on each
    // open/close, so as debug.log grew over a match each DebugLog call stalled
    // longer -- eventually freezing the render thread for *seconds* mid-frame
    // (the watchdog logged "Present frozen for N seconds"). To the player this
    // looked like the screen randomly going black for a couple of seconds.
    //
    // A single persistent handle is scanned once at open; subsequent WriteFile
    // calls are O(1) regardless of file size and never reopen, so the stalls go
    // away entirely. CREATE_ALWAYS truncates on first use, keeping each session's
    // log fresh. A CRITICAL_SECTION serializes the multiple threads that
    // log (render, watchdog, poll, media player); the file pointer advances
    // naturally so no per-call SetFilePointer is needed. No FlushFileBuffers /
    // CloseHandle: the OS write cache survives a game crash (only an OS crash or
    // power loss would lose the tail). Magic-static init is thread-safe in MSVC.
    struct LogState {
        HANDLE           h;
        CRITICAL_SECTION cs;
        LogState() {
            InitializeCriticalSection(&cs);
            _mkdir("C:\\infovisual");
            h = CreateFileA("C:\\infovisual\\debug.log", GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    };
    static LogState s_log;
    if (s_log.h == INVALID_HANDLE_VALUE) return;

    EnterCriticalSection(&s_log.cs);
    DWORD w = 0;
    WriteFile(s_log.h, buf, (DWORD)(n + 1), &w, nullptr);
    LeaveCriticalSection(&s_log.cs);
}

