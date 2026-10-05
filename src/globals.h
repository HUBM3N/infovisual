#pragma once
#include "types.h"
#include "features/cccurve.h"

// Extern declarations for all file-scope globals defined in dllmain.cpp.
// Include this header in any feature .cpp file that needs access to shared state.

extern bool    g_showMenu;
extern int     g_menuKey;
extern bool    g_menuKeyCapture;
extern bool    g_minesweeperOpen;
extern bool    g_msDarkMode;
extern ImFont* g_msFont;

extern ICvar*             g_cvar;
extern HMODULE            g_hSelf;
extern HANDLE             g_hPollThread;
extern HANDLE             g_hWatchdogThread;
extern IEngineClient*     g_engine;
extern void*              g_gameEventManager;
extern void*              g_matSystem;
extern void*              g_studioRender;
extern void*              g_renderView;
extern void*              g_modelRender;
extern void*              g_modelInfo;
extern std::atomic<bool>  g_drawingProps;
extern HWND               g_hwnd;
extern bool               g_imguiInit;
extern DWORD              g_loadingCircleStartMs;
extern std::atomic<bool>  g_fullyInjected;
extern std::atomic<bool>  g_run;
extern std::atomic<bool>  g_eject;
extern HANDLE             g_hWatchdogExitEvent;
extern std::atomic<int>   g_hookDepth;
extern std::atomic<int>   g_fsnDepth;
extern std::atomic<int>   g_miscHookDepth;
extern std::atomic<bool>  g_inGame;
extern CRITICAL_SECTION   g_attrLock;
extern bool               g_attrLockInit;
extern bool g_enableOverlay;
extern bool  g_enableMediaPlayer;
extern float g_mpLastBoxH;
extern float g_mpLastBoxY;
extern int   g_mpLastBoxPos;
extern float g_wmLastBoxX;
extern float g_wmLastBoxY2;
extern float g_wmLastBoxW;
extern float g_wmLastBoxH2;
extern int   g_wmLastBoxPos2;
extern bool  g_wmBoxShadow;
extern float g_wmBoxShadowOffX;
extern float g_wmBoxShadowOffY;
extern float g_wmBoxShadowColor[4];
extern float g_wmBoxShadowBlur;
extern bool  g_mpBoxShadow;
extern float g_mpBoxShadowOffX;
extern float g_mpBoxShadowOffY;
extern float g_mpBoxShadowColor[4];
extern float g_mpBoxShadowBlur;
extern bool  g_bgBlur;
extern float g_bgBlurSigma;
extern float g_menuBgBlurSigma;
extern IDirect3DPixelShader9* g_blurShaderX;
extern IDirect3DPixelShader9* g_blurShaderY;
extern IDirect3DTexture9*     g_blurTexA;
extern IDirect3DSurface9*     g_blurSurfA;
extern IDirect3DTexture9*     g_blurTexB;
extern IDirect3DSurface9*     g_blurSurfB;
extern int                    g_blurShaderW;
extern int                    g_blurShaderH;
extern bool                   g_menuBgBlur;
extern IDirect3DSurface9* g_blurBBSurf;
extern int                g_blurBBW;
extern int                g_blurBBH;
// Bloom (features/bloom.cpp) -- the bright part of the frame bled into a glow. Reads the
// finished frame and nothing else: no depth, no view setup, any card that takes ps_2_0.
extern bool  g_bloomEnable;
extern float g_bloomIntensity;   // how much glow is added, 0..3
extern float g_bloomThreshold;   // luma a pixel has to reach before it glows, 0..1
extern float g_bloomRadius;      // how far it spreads, 0..1 (1..6 blur passes, widening)
extern float g_bloomSat;         // colour of the glow, 0 = white, 1 = as lit, >1 = pushed

extern bool  g_colorCorrEnable;
extern float g_colorCorrR;
extern float g_colorCorrG;
extern float g_colorCorrB;
extern float g_colorCorrHue;
extern float g_colorCorrSat;
extern float g_colorCorrContrast;
extern float g_colorCorrTemp;
extern float g_colorCorrExposure;
extern float g_colorCorrLevInBlack;
extern float g_colorCorrLevInWhite;
extern float g_colorCorrLevGamma;
extern float g_colorCorrLevOutBlack;
extern float g_colorCorrLevOutWhite;
extern float g_colorCorrHighlights;
extern float g_colorCorrShadows;
extern CcCurve g_ccCurve[4];   // 0 master, 1 R, 2 G, 3 B
extern IDirect3DPixelShader9* g_ccShader;
extern IDirect3DTexture9*     g_ccLutTex;
extern IDirect3DTexture9*     g_ccInputTex;
extern IDirect3DSurface9*     g_ccInputSurf;
extern IDirect3DTexture9*     g_ccOutTex;
extern IDirect3DSurface9*     g_ccOutSurf;
extern OverlayImageSlot g_imgSlots[k_imgMaxSlots];
extern int g_imgSelectedSlot;
extern int g_imgDragSlot;
extern bool g_enableImageOverlay;
extern bool g_enablePureBypass;
extern bool g_enableCheatsbypass;
extern bool g_menuSnow;

// ---- Screen effects off the resolved z buffer (features/screenfx.cpp) --------------
// Ambient occlusion (features/ssao.cpp) -- three fullscreen ps_3_0 passes.
extern bool  g_aoEnable;
extern float g_aoIntensity;    // exponent on the term, 1 = as gathered
extern float g_aoRadius;       // world units the gather reaches
extern int   g_aoSamples;      // taps per pixel, 4..64
extern float g_aoBias;         // angle bias, throws away taps too close to the surface plane
extern float g_aoBlur;         // bilateral blur radius, in occlusion texels
extern float g_aoScale;        // gather resolution, 0.25..1 of the frame
extern float g_aoFadeStart;    // units where the term starts fading out
extern float g_aoFadeEnd;      // and where it is gone
extern bool  g_aoViewmodel;    // keep the gun out of it (raw depth < 0.1 is the viewmodel)
extern bool  g_aoFogFade;      // stop the term where this DLL's own fog override has taken over
extern bool  g_aoDebug;        // draw the term instead of multiplying it into the frame

// Depth of field (features/dof.cpp) -- up to five passes off the same depth.
extern bool  g_dofEnable;
extern bool  g_dofAutoFocus;      // focus on whatever the crosshair is pointing at
extern float g_dofDistance;       // manual focal distance, units, when auto focus is off
extern float g_dofFocusSpeed;     // how fast auto focus pulls, per second
extern float g_dofFocusLimit;     // furthest auto focus will pull to
extern float g_dofRange;          // sharp band, centred on the focus
extern float g_dofRamp;           // units the blur ramps in over past that band
extern float g_dofMaxBlur;        // biggest blur radius, in pixels
extern float g_dofBokehBoost;     // how hard a highlight pulls
extern float g_dofBokehThreshold; // luma a highlight starts pulling at
extern bool  g_dofNearBlur;       // blur what is in FRONT of the focus, with the soft edge
extern bool  g_dofViewmodelSharp; // hold the gun sharp whatever the focus is doing
extern bool  g_dofSmooth;         // the separable gaussian that closes the spiral's gaps

// Frame blending (features/frameblend.cpp) -- real motion blur off the same depth: a velocity
// buffer, a tile chain, and a reconstruction gather. Runs twice a frame -- the world and the
// players in the game's post slot, the gun from ViewDrawFade once it has been drawn.
extern bool  g_fbEnable;
extern float g_fbShutter;     // how long the shutter stays open, ms. The whole look lives here.
extern float g_fbAmount;      // the world's share of it, 0 = camera and players sharp
extern bool  g_fbPlayers;     // other players get their own velocity, not just the camera's
extern bool  g_fbViewmodel;   // the gun's animation, off its bone matrices (features/fbbones.h)
extern float g_fbVmAmount;    // and how much of it, 0..64 -- animation streaks are small
extern int   g_fbSamples;     // tap CAP, 4..64. The streak picks the count; this is the ceiling.
extern float g_fbMaxStreak;   // longest streak, as a fraction of screen height

// ---- Web on surface (features/websurface.cpp) -------------------------------------
// A Steam browser drawn as a panel and/or pasted onto a world surface. See websurface.h
// for what ISteamHTMLSurface can and cannot do (no volume api, no extensions, no DRM).
extern bool  g_webEnable;
extern bool  g_webPanel;             // the floating panel -- also the "find it first" window
extern float g_webPanelOpacity;
extern float g_webPanelScale;
extern int   g_webResH;             // the browser's pixel size: SHORT side (480/720/1080)
extern float g_webVolume;            // injected javascript, not a steam api
extern std::string g_webUrl;
extern int   g_webPlaceKey;          // paste the screen where the crosshair points
extern int   g_webClickKey;          // click through the crosshair
extern int   g_webKeyCapture;        // menu rebind: 0 none, 1 place, 2 click, 3 remove, 4 right click
extern float g_webScale;             // world size: long side = 128 units * scale
extern int   g_webAspect;            // k_wsAspects index (websurf_math.h): 16:9 9:16 4:3 3:4 21:9 1:1
extern float g_webWorldOpacity;
extern int   g_webDepthMode;         // 0 decal (occluded), 1 under viewmodel+hud, 2 on top
extern int   g_webBackMode;          // 0 mirrored, 1 not rendered, 2 readable
extern float g_webRound;             // corner radius of the screen on the wall, world units
extern bool  g_webPanelAlways;       // window stays up with the menu closed (page only, no chrome)
extern bool  g_webVolDistance;       // fade the volume with distance from the placed screen
extern float g_webVolNear;           // full volume within this many units
extern float g_webVolFar;            // silent past this many units
extern bool  g_webAdblock;
extern bool  g_webYtEmbed;           // youtube watch links -> /embed/, which carries no ads
extern bool  g_webCrosshairPointer;  // the crosshair drives the page's mouse
extern bool  g_webClickOnly;         // page sees the pointer only while clicking, never on hover
extern int   g_webRemoveKey;         // take the placed screen off the wall
extern bool  g_webHideSteam;         // claim current Chrome (headers + navigator)
extern bool  g_webScrollInput;       // menu closed: wheel / Up / Down scroll the placed screen you aim at (never the panel)
extern int   g_webRClickKey;         // right click through the crosshair (default mouse2)
extern bool  g_webBlockFire;         // crosshair on the screen: mouse1/mouse2 don't fire the weapon
extern bool  g_webSmooth;            // cap video quality to view size + internet speed (websurf_js.h)
extern bool  g_webAutoPlay;          // placing the screen presses play
extern bool  g_webPlayFromStart;     // ...from 0:00 (not on live streams)
extern bool  g_webAutoRemove;        // the video ending takes the screen off the wall

// net_graph text recolor (netgraph.cpp)
extern bool  g_ngColorEnable;
extern float g_ngTextColor[4];
extern float g_ngOrangeColor[4];
extern float g_ngRedColor[4];
extern float g_ngYellowColor[4];
extern bool  g_enableSheen;
extern float g_sheenColor[3];
extern bool  g_enableSheenAnim;
extern bool  g_enableCritColor;
extern float g_critColor[3];
extern bool  g_critUseSheenColor;
extern bool  g_enableDRColor;
extern float g_drColor[3];
extern bool  g_drUseSheenColor;
extern bool  g_enableEquipRegionUnlock;
extern bool  g_selfKillfeedOnly;
extern float g_deathNoticeTime;
extern bool  g_customKillfeedEnable;
extern int   g_kfOrigin;
extern float g_kfOffsetX;
extern float g_kfOffsetY;
extern int   g_kfMaxEntries;
extern float g_kfEntryPadY;
extern float g_kfBgColor[4];
extern float g_kfTextColor[4];
extern float g_kfRedColor[4];
extern float g_kfBluColor[4];
extern bool  g_kfBoxShadow;
extern float g_kfBoxShadowOffX;
extern float g_kfBoxShadowOffY;
extern float g_kfBoxShadowColor[4];
extern float g_kfBoxShadowBlur;
extern OutlineLayer g_kfOutlineLayers[k_maxOutlineLayers];
extern int          g_kfOutlineLayerCount;
extern int   g_kfFontIndex;
extern float g_kfFontScale;
extern bool  g_kfFontFlags[4];
extern std::string g_kfFontFile;
extern bool  g_kfUseIcons;
extern bool  g_kfPlainWeaponIcons;
extern bool  g_kfConsistentIcons;
extern float g_kfIconScale;
extern bool  g_kfSlideIn;
extern float g_kfSlideSpeed;
extern int   g_kfSlideDir;
extern bool  g_kfFadeIn;
extern float g_kfFadeSpeed;
extern float g_kfBgRounding;
extern float g_kfPadX;
extern float g_kfMinHeight;
extern bool  g_kfGrowDown;
extern float g_kfIconColor[4];
extern bool  g_kfTextDropShadow;
extern float g_kfTextDropShadowColor[4];
extern float g_kfTextDropShadowOffX;
extern float g_kfTextDropShadowOffY;
extern bool  g_kfTextDropShadowBlur;
extern bool  g_kfTextOutline;
extern float g_kfTextOutlineColor[4];
extern float g_kfTextOutlineThickness;
extern bool  g_kfFadeOut;
extern float g_kfFadeOutSpeed;
extern bool  g_kfSlideOut;
extern float g_kfSlideOutSpeed;
extern int   g_kfSlideOutDir;
extern bool  g_kfSmoothShift;
extern float g_kfSmoothShiftSpeed;
extern int   g_kfBgCornerFlags;
extern bool  g_kfBlobEnable;
extern float g_kfBlobColor[3];
extern float g_kfBlobAlpha;
extern float g_kfBlobSize;
extern float g_kfBlobSpeed;
extern bool  g_kfPrecipEnable;
extern int   g_kfPrecipMode;
extern int   g_kfPrecipCount;
extern float g_kfPrecipSpeed;
extern float g_kfPrecipSize;
extern float g_kfPrecipWind;
extern float g_kfPrecipColor[4];
extern bool  g_kfKillGlowEnable;
extern float g_kfKillGlowColor[4];
extern float g_kfKillGlowDuration;
extern float g_kfKillGlowSpread;
extern bool  g_kfNameLive;
extern bool  g_enableWeaponHud;
extern bool  g_wpnHudEditMode;
extern float g_wpnHudX;
extern float g_wpnHudY;
extern float g_wpnHudIconSz;
extern float g_wpnHudSlotGap;
extern float g_wpnHudActiveColor[4];
extern float g_wpnHudInactiveColor[4];
extern float g_wpnHudNumScale;
extern int   g_wpnHudNumPos;
extern float g_wpnHudNumOffX;
extern float g_wpnHudNumOffY;
extern bool  g_wpnHudFlipIcons;
extern int   g_wpnHudOriginH;
extern int   g_wpnHudOriginV;
extern bool  g_wpnHudGlowEnabled;
extern float g_wpnHudGlowRadius;
extern float g_wpnHudGlowBolden;
extern float g_wpnHudGlowAlpha;
extern float g_wpnHudGlowColors[8][4];
extern bool  g_enableWpnHudName;
extern bool  g_wpnHudShowNum;
extern int   g_wpnHudNameMaxChars;
extern int   g_wpnHudNamePos;
extern float g_wpnHudNameOffX;
extern float g_wpnHudNameOffY;
extern float g_wpnHudNameScale;
extern int   g_wpnHudNumFontIndex;
extern std::string g_wpnHudNumFontFile;
extern bool  g_wpnHudNumFontFlags[4];
extern int   g_wpnHudNameFontIndex;
extern std::string g_wpnHudNameFontFile;
extern bool  g_wpnHudNameFontFlags[4];
extern bool  g_wpnHudNameShadowEnabled;
extern float g_wpnHudNameShadowColor[4];
extern float g_wpnHudNameShadowOffX;
extern float g_wpnHudNameShadowOffY;
extern bool  g_wpnHudNameOutlineEnabled;
extern float g_wpnHudNameOutlineColor[4];
extern float g_wpnHudNameOutlineThickness;
extern int   g_wpnHudNameColorMode;
extern float g_wpnHudNameCustomColor[4];
extern bool  g_wpnHudNumShadowEnabled;
extern float g_wpnHudNumShadowColor[4];
extern float g_wpnHudNumShadowOffX;
extern float g_wpnHudNumShadowOffY;
extern bool  g_wpnHudNumOutlineEnabled;
extern float g_wpnHudNumOutlineColor[4];
extern float g_wpnHudNumOutlineThickness;
extern int   g_wpnHudNumColorMode;
extern float g_wpnHudNumCustomColor[4];
extern float g_wpnHudActiveScale;
extern bool  g_wpnHudActiveScaleSmooth;
extern float g_wpnHudActiveScaleSpeed;
extern float g_wpnHudIconRotation;
extern bool  g_wpnHudSepEnabled;
extern float g_wpnHudSepColor[4];
extern float g_wpnHudSepWidth;
extern float g_wpnHudSepHeight;
extern bool  g_wpnHudCustomNames;
extern char  g_wpnHudCustomName[5][64];
extern bool  g_wpnHudFade;
extern float g_wpnHudFadeDelay;
extern float g_wpnHudFadeSpeed;
extern bool  g_wpnHudBgEnabled;
extern float g_wpnHudBgWidth;
extern float g_wpnHudBgColorNear[4];
extern float g_wpnHudBgColorFar[4];
extern float g_wpnHudBgFadeStart;
extern bool  g_wpnHudBgFadeNear;
extern float g_wpnHudBgFadeNearStart;
extern float g_wpnHudBgHeightH;
extern float g_wpnHudBgExtraH;
extern float g_wpnHudBgOffX;
extern float g_wpnHudBgOffY;
extern bool  g_wpnHudBgScreenEdge;
extern bool  g_wpnHudBgBorderEnabled;
extern float g_wpnHudBgBorderHeight;
extern float g_wpnHudBgBorderColorNear[4];
extern float g_wpnHudBgBorderColorFar[4];
extern bool g_noHats;
extern int  g_offParticleProp;
extern bool  g_hwFootsteps;
extern float g_hwFootColor[3];
extern bool  g_hwFootUseSheenColor;
extern bool  g_hwForceHoliday;
extern bool        g_zombify;
extern int         g_zombifyMode;
extern void*       g_mdlCache;
extern std::unordered_set<int> g_ragdollKillSet;
extern bool  g_ksEyeUseSheenColorGlobal;
extern bool  g_hwFootHideStealth;
extern uintptr_t g_addrGetItemInLoadoutCall;
extern float g_sheenColors[4][3];
extern int   g_sheenColorCount;
extern float g_sheenAnimSpeed;
extern bool  g_enableFog;
extern float g_fogColor[3];
extern bool  g_fogUseSheenColor;
extern float g_fogStart;
extern float g_fogEnd;
extern float g_fogDensity;
extern bool  g_fogLinkSkybox;
extern float g_fogSkyboxStart;
extern float g_fogSkyboxEnd;
extern float g_fogSkyboxDensity;
extern bool  g_enableSkybox;
extern char  g_skyboxName[64];
extern bool  g_enableModulate;
extern float g_modulateWorld[3];
extern float g_modulateSky[3];
extern float g_modulateProps[3];
extern int   g_rainMode;
extern bool  g_weatherEnabled;
extern char  g_particleLayerName[8][128];
extern float g_rainAlpha;
extern float g_rainSpeed;
extern float g_rainWidth;
extern float g_rainLength;
extern float g_rainRadius;
extern float g_rainSideVel;
extern float g_snowAlpha;
extern float g_snowSpeed;
extern float g_snowWidth;
extern float g_snowLength;
extern float g_snowRadius;
extern float g_snowSideVel;
extern float g_snowDensity;
extern char  g_snowSpriteName[64];
extern void* g_precipClass;
extern void* g_precipEntity;
extern void* g_precipNetworkable;
extern int   g_offPrecipType;
extern int   g_offMins;
extern int   g_offMaxs;
extern CTE_DispatchEffect_t      g_fnDispatchEffect;
extern Get_ParticleSystemIndex_t g_fnGetParticleIndex;
extern uintptr_t g_addrFireEvent_IsHolidayCall;
extern uintptr_t g_addrFireEvent_AttribHookCall;
extern int   g_particleLayerCount;
extern float g_particleLayerZ[8];
extern float g_particleGridRadius;
extern float g_particleSpacingOffset;
extern bool  g_enableViewmodelSway;
extern float g_vmSwayScale;
extern float g_vmSwayInterp;
extern bool  g_enableViewmodelOffset;
extern float g_vmOffsetX;
extern float g_vmOffsetY;
extern float g_vmOffsetZ;
extern bool  g_vmOffsetSeparate;   // offhand v_model (vm index 1: spy watch, dead ringer) gets own offset
extern float g_vmOffset2X;
extern float g_vmOffset2Y;
extern float g_vmOffset2Z;
extern bool  g_removeViewmodelBob;
extern bool  g_vmCsBob;           // CS:GO bob replaces TF's (vm_csgo.h)
extern float g_vmCsBobCycle, g_vmCsBobUp, g_vmCsBobVert, g_vmCsBobLat, g_vmCsBobLower;
extern bool  g_vmCsGunLower;      // CS:GO gun drops while airborne
extern float g_vmCsGunLowerAngle, g_vmCsGunLowerSpeed;
extern bool  g_vmCsLandDip;       // CS:GO landing dip, viewmodel only
extern float g_vmCsLandDipAmt, g_vmCsLandDipTime;
extern bool          g_enableVmWeaponChams;
extern int           g_vmWeaponChamCount;
extern VmChamLayer_t g_vmWeaponChamLayers[k_vmMaxLayers];
extern bool          g_enableVmArmsChams;
extern int           g_vmArmsChamCount;
extern VmChamLayer_t g_vmArmsChamLayers[k_vmMaxLayers];
extern bool  g_enableVmWeaponGlow;
extern float g_vmWeaponGlowColor[4];
extern float g_vmWeaponGlowSigma;
extern int   g_vmWeaponGlowStencil;
extern bool  g_vmWeaponGlowUseSheen;
// transparent viewmodel: blend weapon + arms draws with g_vmAlpha (1 = opaque).
// flat alpha: $blendtintbybasealpha on vm materials so shader stops multiplying phong mask into alpha.
extern bool  g_vmAlphaEnable;
extern float g_vmAlpha;
extern bool  g_vmFlatAlpha;
extern bool  g_enableVmArmsGlow;
extern float g_vmArmsGlowColor[4];
extern float g_vmArmsGlowSigma;
extern int   g_vmArmsGlowStencil;
extern bool  g_vmArmsGlowUseSheen;
extern void* g_viewRender;
extern bool  g_levelInitHookInstalled;
// dlight_t layout from Source Engine SDK -- must match engine binary exactly.
struct dlight_t {
    int     flags;
    float   origin[3];
    float   radius;
    struct { uint8_t r, g, b; int8_t exponent; } color;
    float   die;
    float   decay;
    float   minlight;
    int     key;
    int     style;
    float   direction[3];
    float   inner_angle;
    float   outer_angle;
};
extern void*  g_effects;
extern bool   g_enableDLight;
extern float  g_dlightColor[3];
extern float  g_dlightRadius;
extern bool   g_dlightUseSheenColor;
extern void*   g_clientModePtr;
extern bool    g_doPostHookInstalled;
extern bool    g_drawViewModelsHookInstalled;
extern bool          g_enablePlChams;
extern int           g_plChamCount;
extern VmChamLayer_t g_plChamLayers[k_vmMaxLayers];
extern bool          g_plChamsLocalPlayer;
extern bool          g_plChamsAttachments;
extern bool          g_enableMapTextures;
extern MapTexCat_t   g_mapTexCats[k_mapTexCatCount];
// Preset table and category labels live in visuals.cpp; menu.cpp and config.cpp
// only ever need these accessors.
extern const int k_mapTexPresetCount;
extern const int k_mapTexPresetCustom;   // preset index meaning "use cat.custom"
const char* MapTexPresetLabel(int preset);
const char* MapTexCategoryLabel(int cat);
extern bool          g_flipWorld;   // Flip World: horizontally mirror the world view + controls
extern bool          g_enablePropChams;
extern int           g_propChamCount;
extern VmChamLayer_t g_propChamLayers[k_vmMaxLayers];
extern bool  g_enableCrosshair;
extern bool  g_chAntialias;
extern bool  g_chAdditive;   // additive blend (dest factor ONE) for the custom crosshair
extern int   g_chBlendMode;  // kChBlend id; 0 = normal, or additive when an old cfg set g_chAdditive
extern bool  g_staticAmbyCrosshair;
extern bool  g_chUseSheenColor;
extern float g_chColor[4];
extern float g_chOutlineColor[4];
extern float g_chThickness;
extern float g_chLength;
extern float g_chGap;
extern bool  g_chSidesOnly;
extern bool  g_chSides[4];
extern bool  g_chDot;
extern bool  g_chDotCircle;
extern bool  g_chOutline;
extern float g_chOutlineThick;
extern bool  g_chOutlineUseSheenColor;
extern bool  g_chOutlineSidesOnly;
extern bool  g_chOutlineSides[4];
extern bool  g_chOutlineOnLines;
extern bool  g_chOutlineOnDot;
extern bool  g_chOutlineBlur;
extern float g_chOutlineBlurRadius;
extern float g_chAspect;
extern float g_chGapAspect;
extern bool  g_chDynamic;
extern float g_chDynamicMin;
extern float g_chDynamicMax;
extern float g_chDynamicRef;
extern bool  g_chGradient;
extern float g_chGradColorCenter[4];
extern float g_chGradColorOuter[4];
extern float g_chGradStart;
extern float g_chGradEnd;
extern bool  g_chTaper;
extern float g_chTaperNear;
extern float g_chTaperFar;
extern bool  g_chDotCustomColor;
extern float g_chDotColor[4];
extern float g_chRotation;

// ---- CS:GO sniper scope (scope_csgo.inl, cs_hud_scope.cpp port) ----
extern bool  g_scCsgo;            // replace TF's scope, hides every crosshair while zoomed
extern int   g_scCsgoWeapon;      // inaccuracy stats: 0 AWP, 1 SSG 08, 2 SCAR-20/G3SG1
extern int   g_scCsgoZoom;        // 0 = zoom fov 1, 1 = zoom fov 2 (clamped 25)
extern int   g_scCsgoWidth;       // cl_crosshair_sniper_width
extern bool  g_scCsgoNormalInacc; // cl_crosshair_sniper_show_normal_inaccuracy
extern float g_scCsgoMoveSens;    // ground speed inaccuracy scale, 1 = csgo
extern float g_scCsgoAirSens;     // jump/air/land inaccuracy scale, 1 = csgo
extern bool  g_scCsgoScopeIn;     // unscoped penalty carries into the zoom (csgo)
extern bool  g_scCsgoBob;         // scope offset by csgo raw bob
extern float g_scCsgoBobScale;
extern bool  g_scCsgoLens;        // dust lens overlay
extern float g_scCsgoScale;       // arc box + lens size about the centre, 1 = csgo
extern bool  g_scCsgoNoDot;       // hide your own sniper dot (CSniperDot::GetRenderingPositions)
extern bool  g_scChargeHide;      // hide TF's scope charge meter (any scope, own toggle)
extern float g_scCsgoOpacity;     // whole scope alpha (surface alpha multiplier)
extern float g_scCsgoBobLat, g_scCsgoBobVert;   // sway per axis, 1 = csgo bob amounts

// ---- CSGO crosshair (ported 1:1 from SimpleVisual) ---------
extern bool  g_chCsgoEnable;
extern int   g_chCsgoStyle;    // cl_crosshairstyle: 0/1 panorama reticle, 2/3/4 vgui bars
extern float g_chCsgoColor[4]; // wash-color for pips/arcs/dot -- csgo crosshairColor1 #82b116
extern float g_chCsgoGap;      // gap offset, added to GetReticleCrosshairGap
extern float g_chCsgoScale;    // whole-reticle zoom, 1.0 = csgo's own 1:1 screen pixels
extern bool  g_chCsgoCrisp;    // point-sample the art when zoomed (no bilinear mush)
extern bool  g_chCsgoLinear;   // blend in linear light, like panorama's sRGB render target
extern float g_chCsgoVelSens;  // scales every velocity-driven gap/spread term, 1 = csgo
extern float g_chCsgoSmooth;   // ease time (s) on gap + spread, 0 = csgo's hard snap
extern bool  g_chCsgoArcs;     // draw the 4 animated spread lines (style 0)
extern bool  g_chCsgoPips;     // draw the 4 green gap arms (styles 0/1)

extern float g_csXSize;        // cl_crosshairsize
extern float g_csXThick;       // cl_crosshairthickness
extern float g_csXGap;         // cl_crosshairgap
extern bool  g_csXDot;         // cl_crosshairdot
extern bool  g_csXOutline;     // cl_crosshair_drawoutline
extern float g_csXOutlineTh;   // cl_crosshair_outlinethickness
extern bool  g_csXT;           // cl_crosshair_t
extern int   g_csXAlpha;       // cl_crosshairalpha
extern bool  g_csXUseAlpha;    // cl_crosshairusealpha (off = additive bars, alpha 200)
extern float g_csXSplitDist;   // cl_crosshair_dynamic_splitdist
extern float g_csXSplitRatio;  // cl_crosshair_dynamic_maxdist_splitratio
extern float g_csXInnerMod;    // cl_crosshair_dynamic_splitalpha_innermod
extern float g_csXOuterMod;    // cl_crosshair_dynamic_splitalpha_outermod
extern bool  g_speedoPreDoctrineParens;
extern FakeSheenIndexVar g_fakeSheenIndexVar;
extern bool  g_sheenSigsResolved;
extern GetItemSchema_t             g_fnGetItemSchema;
extern GetAttributeDefinition_t    g_fnGetAttributeDefinition;
extern SetRuntimeAttributeValue_t  g_fnSetRuntimeAttributeValue;
extern bool     g_fsn_wasEnabled;
extern void*    g_fsn_lastLocalEnt;
extern uint32_t g_fsn_entityChangeAge;
extern int      g_fsn_offMyWeapons;
extern int      g_fsn_offAttrList;
extern int      g_fsn_offItemDefIndex;
extern int      g_offHViewModel0;
extern bool     g_fsn_skinWasEnabled;
extern std::unordered_map<int,SkinCfg> g_skinCfgs;   // fixed key set after SkinCfgs_Init: never insert
void SkinCfgs_Init();
extern bool g_enableSkins;
extern bool g_skinNeedsUpdate;
extern bool g_setSeqHookInstalled;
extern bool g_equipRegionHooksInstalled;
extern float       g_watermarkFade;
extern float       g_wmSmoothW;
extern float       g_wmSmoothWV;
extern float       g_wmSmoothItemX[k_wmMaxItems];
extern float       g_wmSmoothItemV[k_wmMaxItems];
extern std::string g_customLabel;
extern int        g_wmLabelSource;
extern bool       g_wmShowUsername;
extern std::string g_wmUsernameLabel;
extern std::string g_windowsUsername;
extern bool        g_drawWatermarkBg;
extern IDirect3DTexture9* g_logoTex;
extern IDirect3DTexture9* g_logoIrisTex;
extern IDirect3DTexture9* g_wmIconFpsTex;
extern IDirect3DTexture9* g_wmIconPingTex;
extern IDirect3DTexture9* g_wmIconLossTex;
extern IDirect3DTexture9* g_wmIconTimeTex;
extern IDirect3DTexture9* g_wmIconUserTex;
extern IDirect3DTexture9* g_tabIconWatermarkTex;
extern IDirect3DTexture9* g_tabIconNamesTex;
extern IDirect3DTexture9* g_tabIconSheenTex;
extern IDirect3DTexture9* g_tabIconVisualsTex;
extern IDirect3DTexture9* g_tabIconCrosshairTex;
extern IDirect3DTexture9* g_tabIconMiscTex;
extern IDirect3DTexture9* g_tabIconSkinsTex;
extern IDirect3DTexture9* g_tabIconImagesTex;
extern IDirect3DTexture9* g_tabIconConfigTex;
extern IDirect3DTexture9* g_decorImageTex;
extern bool               g_wmShowLogo;
extern bool               g_wmLogoGhost;
extern float              g_wmLogoGhostAlpha;
extern char               g_wmLogoImgPath[MAX_PATH];
extern std::vector<IDirect3DTexture9*> g_wmLogoImgFrames;
extern std::vector<float>              g_wmLogoImgDelays;
extern int                g_wmLogoImgCurFrame;
extern float              g_wmLogoImgFrameTimer;
extern int                g_wmLogoImgW;
extern int                g_wmLogoImgH;
extern bool               g_wmLogoImgNeedsLoad;
extern float              g_wmLogoImgScale;
extern float              g_wmLogoImgRotation;
extern bool               g_wmLogoImgColorize;
extern int   g_labelAnimMode;
extern float g_labelScrollSpeed;
extern float g_labelTypeSpeed;
extern float g_labelEraseSpeed;
extern float g_labelHoldTime;
extern TitleEntry_t  g_titleList[k_maxTitleCycle];
extern int           g_titleCount;
extern float         g_titleInterval;
extern int           g_titleIdx;
extern ULONGLONG     g_titleLastChange;
extern bool          g_titleCycleEnable;
extern bool        g_drawWatermarkText;
extern bool        g_drawFPS;
extern bool        g_drawPing;
extern bool        g_drawLoss;
extern bool        g_drawTime;
extern float       g_watermarkScale;
extern char        g_wmSeparator[16];
extern bool        g_wmUseIcons;
extern float       g_watermarkYOffset;
extern float       g_watermarkXOffset;
extern int         g_wmFontWeight;
extern float g_accentColor[3];
extern float g_menuBgColor[3];
extern float g_menuTextColor[3];
extern bool  g_menuShowTabIcons;
extern std::vector<FontEntry> g_fontList;
extern int                    g_wmFontIndex;
extern std::string            g_wmFontFile;
extern bool g_wmFontFlags[6];
extern bool g_wmFontFlagsBuilt[6];
extern bool g_mpFontFlagsBuilt[6];
extern int g_threshFPS;
extern int g_threshPing;
extern int g_threshLoss;
extern float g_wmBgColor[4];
extern float g_wmDimColor[3];
extern float g_wmTextColor[3];
extern float g_wmAlertColor[3];
extern float g_wmTitleColor[3];
extern float g_wmBgRounding;
extern int   g_wmBgCornerFlags;
extern OutlineLayer g_wmOutlineLayers[k_maxOutlineLayers];
extern int          g_wmOutlineLayerCount;
extern bool&   g_wmOutline;
extern float&  g_wmOutlineThick;
extern int&    g_wmOutlineSideFlags;
extern float*  g_wmOutlineColor;
extern bool  g_wmBlobEnable;
extern bool  g_wmBlobUseSheen;
extern float g_wmBlobColor[3];
extern float g_wmBlobAlpha;
extern float g_wmBlobSpeed;
extern float g_wmBlobSize;
extern bool  g_wmPrecipEnable;
extern int   g_wmPrecipMode;
extern int   g_wmPrecipCount;
extern float g_wmPrecipSpeed;
extern float g_wmPrecipSize;
extern float g_wmPrecipWind;
extern float g_wmPrecipColor[4];
extern bool  g_wmKillGlowEnable;
extern float g_wmKillGlowColor[4];
extern float g_wmKillGlowDuration;
extern float g_wmKillGlowSpread;
extern float g_wmKillGlowAlpha;
extern bool  g_wmKillGlowRising;
extern int   g_watermarkPos;
extern int   g_mpMode;
extern bool  g_mpAvoidWatermark;
extern bool  g_mpShowThumb;
extern bool  g_mpThumbPreserveAspect;
extern bool  g_mpThumbCircle;
extern bool  g_mpThumbOutline;
extern float g_mpThumbOutlineThick;
extern float g_mpThumbOutlineColor[4];
extern float g_mpThumbSize;
extern bool  g_mpShowProgress;
extern bool  g_mpShowDuration;
extern float g_mpScale;
extern float g_mpYOffset;
extern float g_mpXOffset;
extern int   g_mpPos;
extern bool  g_mpCopyWmStyle;
extern float g_mpBgColor[4];
extern float g_mpTitleColor[3];
extern float g_mpArtistColor[3];
extern float g_mpBgRounding;
extern int   g_mpBgCornerFlags;
extern bool  g_mpBgDraw;
extern OutlineLayer g_mpOutlineLayers[k_maxOutlineLayers];
extern int          g_mpOutlineLayerCount;
extern bool&   g_mpOutline;
extern float&  g_mpOutlineThick;
extern int&    g_mpOutlineSideFlags;
extern float*  g_mpOutlineColor;
extern bool  g_mpBlobEnable;
extern bool  g_mpBlobUseSheen;
extern float g_mpBlobColor[3];
extern float g_mpBlobAlpha;
extern float g_mpBlobSpeed;
extern float g_mpBlobSize;
extern bool  g_mpPrecipEnable;
extern int   g_mpPrecipMode;
extern int   g_mpPrecipCount;
extern bool   g_mpWaveform;
extern float  g_wfSensitivity;
extern float  g_wfBands[5];
extern HANDLE g_wfThread;
extern bool   g_wfRunning;
extern int    g_mpWaveformPos;
extern float  g_mpWfFreeX;
extern float  g_mpWfFreeY;
extern float g_mpPrecipSpeed;
extern float g_mpPrecipSize;
extern float g_mpPrecipWind;
extern float g_mpPrecipColor[4];
extern bool  g_mpFontFlags[6];
extern int   g_mpFontIndex;
extern std::string g_mpFontFile;
extern bool  g_mpShowLyrics;
extern bool  g_mpLyricsAbove;
extern bool  g_mpLyricsSide;
extern float g_mpLyricsXOff;
extern float g_mpLyricsYOff;
extern float g_mpLyricsColor[4];
extern float g_mpLyricsScale;
extern float g_mpLyricsOffset;
extern bool  g_mpLyricsBlob;
extern bool  g_mpLyricsPrecip;
extern bool  g_mpLyricsBgDraw;
extern bool  g_mpLyricsClampWidth;
extern float g_mpLyricsClampW;
extern bool  g_mpLyricsEllipsis;
extern bool  g_mpLyricsAvoidWm;
extern bool  g_mpLyricsShiftBoxes;
extern bool  g_mpLyricsOnly;
extern int   g_mpLyricsRows;
extern int   g_mpTitleAlign;
extern int   g_mpArtistAlign;
extern int   g_mpLyricsAlign;
extern float g_mpTitleXOff;
extern float g_mpTitleYOff;
extern float g_mpArtistXOff;
extern float g_mpArtistYOff;
extern float g_mpAlbumXOff;
extern float g_mpAlbumYOff;
extern float g_mpThumbRounding;
extern bool        g_mpCustomThumb;
extern std::string g_mpCustomThumbPath;
extern float g_mpProgXOff;
extern float g_mpProgYOff;
extern float g_mpProgBarW;
extern float g_mpProgHeight;
extern float g_mpMaxWidth;
extern bool  g_mpStaticWidth;
extern float g_mpTitleMaxW;
extern float g_mpArtistMaxW;
extern bool  g_mpTitleEllipsis;
extern bool  g_mpArtistEllipsis;
extern BgSideGrad g_mpBgGrad[4];
extern BgSideGrad g_wmBgGrad[4];
extern float g_mpBgInsetL;
extern float g_mpBgInsetR;
extern float g_mpBgInsetT;
extern float g_mpBgInsetB;
extern float g_wmBgInsetL;
extern float g_wmBgInsetR;
extern float g_wmBgInsetT;
extern float g_wmBgInsetB;
extern int         g_menuFontIndex;
extern std::string g_menuFontFile;
extern bool        g_menuFontFlags[6];
extern bool        g_menuFontFlagsBuilt[6];
extern int         g_menuFontBuiltAtIndex;
extern float       g_menuFontScale;
extern float       g_menuFontBuiltAtScale;
extern bool  g_speedoEnable;
extern float g_speedoPosX;
extern float g_speedoPosY;
extern float g_speedoScale;
extern int         g_speedoFontIndex;
extern std::string g_speedoFontFile;
extern bool        g_speedoFontFlags[6];
extern bool        g_speedoFontFlagsBuilt[6];
extern float       g_speedoFontBuiltAtScale;
extern int         g_speedoFontBuiltAtIndex;
extern float g_speedoColorLow[4];
extern float g_speedoColorMid[4];
extern float g_speedoColorHigh[4];
extern float g_speedoSpeedLow;
extern float g_speedoSpeedHigh;
extern bool  g_speedoColorBySpeed;
extern bool  g_speedoOutline;
extern float g_speedoOutlineColor[4];
extern float g_speedoOutlineThick;
extern bool  g_speedoShadow;
extern float g_speedoShadowColor[4];
extern float g_speedoShadowOffX;
extern float g_speedoShadowOffY;
extern bool  g_speedoShadowBlur;
extern bool  g_speedoFadeIn;
extern float g_speedoFadeThreshold;
extern float g_speedoAlpha;
extern float g_speedoAlphaV;
extern float g_speedoDisplayVal;
extern float g_speedoDisplayV;
extern float g_speedoMainSlotW;
extern float g_speedoMainSlotWV;
extern bool  g_speedoShowPre;
extern float g_speedoPreVal;
extern float g_speedoPreTimer;
extern float g_speedoPreAlpha;
extern float g_speedoPreAlphaV;
extern float g_speedoPreXOff;
extern float g_speedoPreXOffV;
extern float g_speedoPreSlotW;
extern float g_speedoPreSlotWV;
extern float g_speedoPreHoldTime;
extern bool  g_speedoPreAnimCenter;
extern bool  g_speedoWasOnGround;
extern bool  g_speedoGraph;
extern float g_speedoGraphPosX;
extern float g_speedoGraphPosY;
extern float g_speedoGraphW;
extern float g_speedoGraphH;
extern float g_speedoGraphColor[4];
extern float g_speedoGraphMaxSpeed;
extern int   g_speedoGraphFade;
extern float g_speedoGraphLineH;
extern float g_speedoGraphAlpha;
extern float g_speedoGraphAlphaV;
extern std::deque<float> g_speedoHistory;
extern float g_speedoMainPosX;
extern float g_speedoCenterDelay;
extern int         g_streamerMode;
extern std::string g_streamerEveryoneLabel;
extern std::string g_streamerTeamLabel;
extern std::string g_streamerEnemyLabel;
extern bool        g_useCustomUsername;
extern std::string g_customUsername;
extern NameEntry_t g_nameList[k_maxNames];
extern int         g_nameCount;
extern bool        g_enableNameCycle;
extern int         g_nameTrigger;
extern float       g_nameInterval;
extern int         g_nameIdx;
extern ULONGLONG   g_nameLastChange;
extern bool        g_enableTeamLabelCycle;
extern NameEntry_t g_teamLabelList[k_maxNames];
extern int         g_teamLabelCount;
extern int         g_teamLabelIdx;
extern ULONGLONG   g_teamLabelLastChange;
extern bool        g_enableEnemyLabelCycle;
extern NameEntry_t g_enemyLabelList[k_maxNames];
extern int         g_enemyLabelCount;
extern int         g_enemyLabelIdx;
extern ULONGLONG   g_enemyLabelLastChange;
extern bool        g_enableEveryoneLabelCycle;
extern NameEntry_t g_everyoneLabelList[k_maxNames];
extern int         g_everyoneLabelCount;
extern int         g_everyoneLabelIdx;
extern ULONGLONG   g_everyoneLabelLastChange;
extern bool g_ragdollEffectEnabled;
extern bool g_ragdollEffectBurning;
extern bool g_ragdollEffectElectro;
extern bool g_ragdollEffectAsh;
extern bool g_ragdollEffectDissolve;
extern bool g_ragdollEffectGold;
extern bool g_ragdollEffectIce;
extern bool g_ragdollOnlyKills;
extern IClientEntityList* g_entityList;
extern int  g_offClass;
extern int  g_offTeam;
extern int  g_offVelocity;
extern int  g_offLifeState;
extern int  g_offOrigin;
extern int  g_offFlags;
extern int  g_offMaxspeed;
extern int  g_offRagdollTeam;
extern int  g_offRagdollClass;
extern int  g_offRagdollPlayer;
extern int  g_offRagdollGib;
extern int  g_offRagdollBurning;
extern int  g_offRagdollElectro;
extern int  g_offRagdollFeignDeath;
extern int  g_offRagdollAsh;
extern int  g_offRagdollGold;
extern int  g_offRagdollIce;
extern int  g_offMoveParent;
extern int  g_offModelIndex;
extern int  g_offMyWeapons;
extern int  g_offActiveWeapon;
extern int  g_offPlayerCond;
extern int  g_offViewModelIndex;
extern bool g_netvarsReady;
extern char g_nameCache[65][128];
extern char g_localRealName[128];
extern std::atomic<float> g_fps;
extern std::atomic<int>   g_ping;
extern std::atomic<int>   g_loss;
extern std::atomic<bool>  g_conn;
extern char               g_phase[128];
extern std::atomic<DWORD> g_phaseThread;

// First-chance fault counters -- filled by the VEH in crashhandler.cpp.
// The watchdog logs them so a fault loop shows up in debug.log.
extern std::atomic<uint32_t>  g_vehFaultCount;
extern std::atomic<uintptr_t> g_vehLastAddr;
extern std::atomic<uint32_t>  g_vehDumpCount;
extern std::atomic<int>   g_presentCount;
extern std::atomic<int>   g_fsnStage;
extern PersistCrashData* g_crashData;
extern HANDLE             g_hVEH;
void InstallPersistentCrashHandler();
void WriteEjectLog(const char* phase);
extern std::atomic<uint32_t> g_matEpoch;   // bumped by the level hooks; per-map caches key off it
extern std::atomic<bool> g_modWorldValuesDirty;
extern int g_netInfoIndex;
extern uintptr_t g_skinPreWriteRetAddr;
extern ConVar*        g_cv_sv_skyname;
extern bool g_propHooksInstalled;
extern bool  g_vmChamsHookInstalled;
extern bool  g_weaponHooksInstalled;
extern bool g_vmIsRendering;
extern uintptr_t g_addrDrawOverriddenVMCall;
extern ImFont* g_wmFont;
extern float   g_wmFontBuiltAtScale;
extern int     g_wmFontBuiltAtIndex;
extern ImFont* g_mpFont;
extern float   g_mpFontBuiltAtScale;
extern int     g_mpFontBuiltAtIndex;
extern ImFont* g_mpLyricsFont;
extern float   g_mpLyricsBuiltAtScale;
extern ImFont* g_speedoFont;
extern ImFont* g_menuFont;
extern ImFont* g_doctrineFont;
extern ImFont* g_kfFont;
extern float   g_kfFontBuiltAtScale;
extern int     g_kfFontBuiltAtIndex;
extern bool    g_kfFontFlagsBuilt[4];
extern ImFont* g_wpnHudNumFont;
extern float   g_wpnHudNumFontBuiltAtScale;
extern int     g_wpnHudNumFontBuiltAtIndex;
extern bool    g_wpnHudNumFontFlagsBuilt[4];
extern ImFont* g_wpnHudNameFont;
extern float   g_wpnHudNameFontBuiltAtScale;
extern int     g_wpnHudNameFontBuiltAtIndex;
extern bool    g_wpnHudNameFontFlagsBuilt[4];
extern IDirect3DDevice9* g_d3dDev;
extern DWORD             g_fontDirtyAt;
extern float g_colorClipboard[4];
extern bool  g_colorClipboardFilled;
extern MediaPlayerState                 g_mpState;
extern std::mutex                       g_mpMutex;
extern std::atomic<bool>                g_mpRunning;
extern HANDLE                           g_mpThread;
extern std::vector<uint8_t>             g_mpPendingPixels;
extern WNDPROC g_origWndProc;
extern bool s_snowSpriteDirty;

void SetPhase(const char* p);
void DebugLog(const char* fmt, ...);
void GetGameDir(char* out);   // out: MAX_PATH chars. Folder of tf_win64.exe (holds tf\materials).
void ConsoleColorPrint(uint8_t r, uint8_t g, uint8_t b, const char* msg);
void ResetFSNState();
void GetAnimatedSheenColor(float out[3]);
using SheenOnBind_t = void(__fastcall*)(void*, void*);
extern SheenOnBind_t o_SheenOnBind;
void __fastcall h_SheenOnBind(void* rcx, void* pEntity);
using CTFRevolver_GetWeaponCrosshairScale_t = void(__fastcall*)(void*, float&);
extern CTFRevolver_GetWeaponCrosshairScale_t o_CTFRevolver_GetWeaponCrosshairScale;
void __fastcall h_CTFRevolver_GetWeaponCrosshairScale(void* rcx, float& flScale);
bool EngineSetAttribute(CAttributeList* pList, uint32_t index, float value);
void TriggerForceFullUpdate(const char* reason = "");
int  Skin_FindWeaponRedir(int skinDef);
void ResolveSheenSigs();
void InstallEquipRegionHook();
using CreateTFRagdoll_t = void(__fastcall*)(void*);
extern CreateTFRagdoll_t o_CreateTFRagdoll;
void __fastcall h_CreateTFRagdoll(void* rcx);
inline int GetPlayerForUserID(int userID)
{
    if (!g_engine || userID <= 0) return 0;
    int result = 0;
    __try {
        using Fn = int(__fastcall*)(void*, int);
        void** vtbl = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            result = ((Fn)vtbl[9])(g_engine, userID);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}
struct PlayerInfo_t {
    char     name[128];
    int      userID;
    char     guid[33];
    uint32_t friendsID;
    char     friendsName[128];
    bool     fakeplayer;
    bool     ishltv;
    uint32_t customFiles[4];
    uint8_t  filesDownloaded;
};

// Matches TF2's actual player_info_t layout (name[32], not name[128]).
// Use only for reading friendsID — do not use name field for display.
struct PlayerInfo_SDK_t {
    char     name[32];
    int      userID;
    char     guid[33];
    uint32_t friendsID;
    char     friendsName[32];
    bool     fakeplayer;
    bool     ishltv;
    uint32_t customFiles[4];
    uint8_t  filesDownloaded;
};
inline bool GetPlayerInfo(int iIndex, PlayerInfo_t* pInfo)
{
    if (!g_engine) return false;
    bool result = false;
    __try {
        using GetPlayerInfo_t = bool(__thiscall*)(void*, int, PlayerInfo_t*);
        void** vtbl = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            result = ((GetPlayerInfo_t)vtbl[8])(g_engine, iIndex, pInfo);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

// Returns the Steam account ID (lower 32 bits of steam64) for a player slot.
// Uses the SDK-correct name[32] layout so friendsID lands at the right offset.
inline uint32_t GetPlayerFriendsID(int iIndex)
{
    if (!g_engine) return 0;
    PlayerInfo_SDK_t pi = {};
    __try {
        using Fn = bool(__thiscall*)(void*, int, PlayerInfo_SDK_t*);
        void** vtbl = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            ((Fn)vtbl[8])(g_engine, iIndex, &pi);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return pi.friendsID;
}
void ResolveNetvars();
void EnsureEntityList();
struct RecvProp;
struct RecvTable
{
    RecvProp*   m_pProps;
    int         m_nProps;
    void*       m_pDecoder;
    const char* m_pNetTableName;
    bool        m_bInitialized;
    bool        m_bInMainList;
};
struct RecvProp
{
    const char* m_pVarName;
    int         m_RecvType;
    int         m_Flags;
    int         m_StringBufferSize;
    bool        m_bInsideArray;
    const void* m_pExtraData;
    RecvProp*   m_pArrayProp;
    void*       m_ArrayLengthProxy;
    void*       m_ProxyFn;
    void*       m_DataTableProxyFn;
    RecvTable*  m_pDataTable;
    int         m_Offset;
    int         m_ElementStride;
    int         m_nElements;
    const char* m_pParentArrayPropName;
};
int  FindPropOffset(RecvTable* pTable, const char* name, int baseOffset = 0, int depth = 0);
RecvTable* FindNetworkTable(const char* netTableName);

struct ClientClass {
    void*        m_pCreateFn;
    void*        m_pCreateEventFn;
    const char*  m_pNetworkName;
    RecvTable*   m_pRecvTable;
    ClientClass* m_pNext;
    int          m_ClassID;
};
ClientClass* GetAllClasses();
extern int s_lastAppliedMode;

inline constexpr float kLogoAspect = 1911.4941f / 1188.2795f;
inline constexpr int   k_speedoHistMax = 185;

inline void* GetClientEntity(int iIndex)
{
    if (!g_entityList) return nullptr;
    void* result = nullptr;
    __try {
        using GetClientEntity_t = void*(__thiscall*)(void*, int);
        void** vtbl = *(void***)g_entityList;
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            result = ((GetClientEntity_t)vtbl[3])(g_entityList, iIndex);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

// IClientEntityList slot 6. Slot 3 (GetClientEntity) is proven above, and the
// interface order is GetClientNetworkable, ...FromHandle, GetClientUnknownFromHandle,
// GetClientEntity, GetClientEntityFromHandle, NumberOfEntities, GetHighestEntityIndex.
// Bounds entity sweeps to slots actually in use instead of the full 2048.
inline int GetHighestEntityIndex()
{
    if (!g_entityList) return 2048;
    int r = 2048;
    __try {
        using GetHighest_t = int(__thiscall*)(void*);
        void** vtbl = *(void***)g_entityList;
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            r = ((GetHighest_t)vtbl[6])(g_entityList);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (r < 0 || r > 2048) r = 2048;   // bad read -> fall back to full sweep
    return r;
}

inline int GetLocalPlayerIndex()
{
    if (!g_engine) return -1;
    int idx = -1;
    __try { idx = VCall<int>(g_engine, 12); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return idx;
}

inline bool IsInGame()
{
    if (!g_engine) return false;
    bool r = false;
    __try { r = VCall<bool>(g_engine, 26); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

// Overlay animation clock (features/animclock.h, ticked once per Present in render.cpp): game time, so
// host_framerate recordings, host_timescale, demo_timescale and pause carry the overlay animations with them.
// Menu and real-world timers must not use it. AnimTime / AnimTimeMs are safe from any thread.
double   AnimTime();     // seconds since inject, game time
uint64_t AnimTimeMs();
float    AnimDt();       // game seconds this overlay frame (render thread)

inline bool IsLocalPlayerAlive()
{
    if (g_offLifeState <= 0) return false;
    int idx = GetLocalPlayerIndex();
    if (idx <= 0) return false;
    void* pEnt = GetClientEntity(idx);
    if (!pEnt) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(pEnt) + g_offLifeState;
    if (addr < 0x10000) return false;
    uint8_t lifeState = 0;
    __try {
        lifeState = *reinterpret_cast<uint8_t*>(addr);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return lifeState == 0;
}

inline float GetLocalPlayerSpeed()
{
    ResolveNetvars();
    if (g_offVelocity <= 0) return -1.f;
    int idx = GetLocalPlayerIndex();
    if (idx <= 0) return -1.f;
    void* pEnt = GetClientEntity(idx);
    if (!pEnt) return -1.f;
    float vx = 0.f, vy = 0.f;
    __try {
        auto* vel = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pEnt) + g_offVelocity);
        vx = vel[0];
        vy = vel[1];
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1.f; }
    return sqrtf(vx * vx + vy * vy);
}

inline int GetPlayerClass(int iIndex)
{
    ResolveNetvars();
    if (g_offClass <= 0) return 0;
    void* pEnt = GetClientEntity(iIndex);
    if (!pEnt) return 0;
    int cls = 0;
    __try {
        cls = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + g_offClass);
    } __except(EXCEPTION_EXECUTE_HANDLER) { cls = 0; }
    return (cls >= 1 && cls <= 9) ? cls : 0;
}

// Particle system constants
inline constexpr int       k_particleEffectStride = 40;
inline constexpr int       k_particleEffectPtrOff = 32;
inline constexpr int       k_setSimSlot           = 4;
// CP9 (CUSTOM_COLOR_CP1) is where TF2 writes the team color for killstreak eye glows.
// CNewParticleEffect: 8-byte IParticleEffect vtable, then CParticleCollection at the next
// 16-byte boundary (+16, it holds an fltx4). CPC+116 = m_ControlPoints[0];
// + 9 * sizeof(CParticleControlPoint=104) = CPC+1052 = NEF+1068 = CP9 m_Position.
inline constexpr ptrdiff_t k_nefCpcOff     =   16;
inline constexpr ptrdiff_t k_cpcCP9PosOff  = 1052;
inline constexpr ptrdiff_t k_cpcChildHdOff = 8884;
inline constexpr ptrdiff_t k_cpcNextOff    = 8828;

// Material system helpers
inline void MatColorModulate(void* pMat, float r, float g, float b)
{
    if (!pMat) return;
    using Fn = void(__fastcall*)(void*, float, float, float);
    ((Fn)(*(void***)pMat)[28])(pMat, r, g, b);
}
inline bool MatIsError(void* pMat)
{
    if (!pMat) return true;
    using Fn = bool(__fastcall*)(void*);
    return ((Fn)(*(void***)pMat)[42])(pMat);
}
inline void* MatSysFindMaterial(void* pMatSys, const char* pName, bool bComplain = false)
{
    if (!pMatSys) return nullptr;
    using Fn = void*(__fastcall*)(void*, const char*, const char*, bool, const char*);
    return ((Fn)(*(void***)pMatSys)[71])(pMatSys, pName, "", bComplain, nullptr);
}

extern int s_bRemoveOffset;

// Zombie rendering state (defined in dllmain.cpp, used by chams.cpp)
extern const void* s_zombieModels[10];
extern int         s_zombieDynIdx[10];
extern void*       s_zombieHWData[10];
extern int8_t      s_boneMap[10][128];
extern bool        s_boneMapped[10];
extern bool        s_hwLastPosValid;

void* GetInterface(const char* module, const char* name);

inline int GetPlayerTeam(int iIndex)
{
    ResolveNetvars();
    if (g_offTeam <= 0) return 0;
    void* pEnt = GetClientEntity(iIndex);
    if (!pEnt) return 0;
    int team = 0;
    __try {
        team = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + g_offTeam);
    } __except(EXCEPTION_EXECUTE_HANDLER) { team = 0; }
    return (team >= 0 && team <= 3) ? team : 0;
}

inline constexpr int k_pingSubtract = 20;

inline const char* MatGetTextureGroupName(void* pMat)
{
    if (!pMat) return "";
    const char* r = "";
    __try { using Fn = const char*(__fastcall*)(void*); r = ((Fn)(*(void***)pMat)[1])(pMat); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r ? r : "";
}
inline const char* MatGetName(void* pMat)
{
    if (!pMat) return "";
    const char* r = "";
    __try { using Fn = const char*(__fastcall*)(void*); r = ((Fn)(*(void***)pMat)[0])(pMat); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r ? r : "";
}
inline unsigned short MatSysFirstMaterial(void* p)
{
    unsigned short r = 0xFFFF;
    __try { using Fn = unsigned short(__fastcall*)(void*); r = ((Fn)(*(void***)p)[73])(p); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
inline unsigned short MatSysNextMaterial(void* p, unsigned short h)
{
    unsigned short r = 0xFFFF;
    __try { using Fn = unsigned short(__fastcall*)(void*, unsigned short); r = ((Fn)(*(void***)p)[74])(p, h); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
inline unsigned short MatSysInvalidMaterial(void* p)
{
    unsigned short r = 0xFFFF;
    __try { using Fn = unsigned short(__fastcall*)(void*); r = ((Fn)(*(void***)p)[75])(p); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
inline void* MatSysGetMaterial(void* p, unsigned short h)
{
    void* r = nullptr;
    __try { using Fn = void*(__fastcall*)(void*, unsigned short); r = ((Fn)(*(void***)p)[76])(p, h); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}
