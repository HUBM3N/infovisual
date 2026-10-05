#include "config.h"
#include "cfgparse.h"
#include "playerlist.h"

// ---- The simple keys --------------------------------------------------------------------
// One row per scalar setting: saved as key=value, loaded back with the same clamp the old
// hand-written branch applied. Anything with side effects, arrays or legacy names stays in
// the code below the table.
enum class CfgKind : uint8_t { Bool, Int, Float, Str };
struct CfgEntry {
    const char* key;
    CfgKind     kind;
    void*       ptr;
    float       lo = -FLT_MAX;
    float       hi =  FLT_MAX;
};

static const CfgEntry k_cfgTable[] = {
    { "aoEnable", CfgKind::Bool, &g_aoEnable },
    { "aoIntensity", CfgKind::Float, &g_aoIntensity },
    { "aoRadius", CfgKind::Float, &g_aoRadius },
    { "aoSamples", CfgKind::Int, &g_aoSamples, 4.f, 64.f },
    { "aoBias", CfgKind::Float, &g_aoBias },
    { "aoBlur", CfgKind::Float, &g_aoBlur },
    { "aoScale", CfgKind::Float, &g_aoScale, 0.25f, 1.f },
    { "aoFadeStart", CfgKind::Float, &g_aoFadeStart },
    { "aoFadeEnd", CfgKind::Float, &g_aoFadeEnd },
    { "aoViewmodel", CfgKind::Bool, &g_aoViewmodel },
    { "aoFogFade", CfgKind::Bool, &g_aoFogFade },
    { "aoDebug", CfgKind::Bool, &g_aoDebug },
    { "dofEnable", CfgKind::Bool, &g_dofEnable },
    { "dofAutoFocus", CfgKind::Bool, &g_dofAutoFocus },
    { "dofDistance", CfgKind::Float, &g_dofDistance },
    { "dofFocusSpeed", CfgKind::Float, &g_dofFocusSpeed },
    { "dofFocusLimit", CfgKind::Float, &g_dofFocusLimit },
    { "dofRange", CfgKind::Float, &g_dofRange },
    { "dofRamp", CfgKind::Float, &g_dofRamp },
    { "dofMaxBlur", CfgKind::Float, &g_dofMaxBlur },
    { "dofBokehBoost", CfgKind::Float, &g_dofBokehBoost },
    { "dofBokehThresh", CfgKind::Float, &g_dofBokehThreshold },
    { "dofNearBlur", CfgKind::Bool, &g_dofNearBlur },
    { "dofViewmodelSharp", CfgKind::Bool, &g_dofViewmodelSharp },
    { "dofSmooth", CfgKind::Bool, &g_dofSmooth },
    { "fbEnable", CfgKind::Bool, &g_fbEnable },
    { "fbShutter", CfgKind::Float, &g_fbShutter, 0.f, 60.f },
    { "fbAmount", CfgKind::Float, &g_fbAmount, 0.f, 4.f },
    { "fbPlayers", CfgKind::Bool, &g_fbPlayers },
    { "fbViewmodel", CfgKind::Bool, &g_fbViewmodel },
    { "fbVmAmount", CfgKind::Float, &g_fbVmAmount, 0.f, 64.f },
    { "fbSamples", CfgKind::Int, &g_fbSamples, 4.f, 64.f },
    { "fbMaxStreak", CfgKind::Float, &g_fbMaxStreak, 0.01f, 0.2f },
    { "webEnable", CfgKind::Bool, &g_webEnable },
    { "webPanel", CfgKind::Bool, &g_webPanel },
    { "webPanelOpacity", CfgKind::Float, &g_webPanelOpacity, 0.f, 1.f },
    { "webPanelScale", CfgKind::Float, &g_webPanelScale, 0.25f, 4.f },
    { "webResH", CfgKind::Int, &g_webResH, 144.f, 1080.f },
    { "webVolume", CfgKind::Float, &g_webVolume, 0.f, 1.f },
    { "webUrl", CfgKind::Str, &g_webUrl },
    { "webPlaceKey", CfgKind::Int, &g_webPlaceKey, 0.f, 255.f },
    { "webClickKey", CfgKind::Int, &g_webClickKey, 0.f, 255.f },
    { "webScale", CfgKind::Float, &g_webScale, 0.05f, 16.f },
    { "webAspect", CfgKind::Int, &g_webAspect, 0.f, 5.f },
    { "webWorldOpacity", CfgKind::Float, &g_webWorldOpacity, 0.f, 1.f },
    { "webDepthMode", CfgKind::Int, &g_webDepthMode, 0.f, 2.f },
    { "webBackMode", CfgKind::Int, &g_webBackMode, 0.f, 2.f },
    { "webRound", CfgKind::Float, &g_webRound, 0.f, 512.f },
    { "webPanelAlways", CfgKind::Bool, &g_webPanelAlways },
    { "webVolDistance", CfgKind::Bool, &g_webVolDistance },
    { "webVolNear", CfgKind::Float, &g_webVolNear, 0.f, 4096.f },
    { "webVolFar", CfgKind::Float, &g_webVolFar, 1.f, 8192.f },
    { "webAdblock", CfgKind::Bool, &g_webAdblock },
    { "webYtEmbed", CfgKind::Bool, &g_webYtEmbed },
    { "webCrosshairPointer", CfgKind::Bool, &g_webCrosshairPointer },
    { "webClickOnly", CfgKind::Bool, &g_webClickOnly },
    { "webRemoveKey", CfgKind::Int, &g_webRemoveKey, 0.f, 255.f },
    { "webHideSteam", CfgKind::Bool, &g_webHideSteam },
    { "webScrollInput", CfgKind::Bool, &g_webScrollInput },
    { "webRClickKey", CfgKind::Int, &g_webRClickKey, 0.f, 255.f },
    { "webBlockFire", CfgKind::Bool, &g_webBlockFire },
    { "webSmooth", CfgKind::Bool, &g_webSmooth },
    { "webAutoPlay", CfgKind::Bool, &g_webAutoPlay },
    { "webPlayFromStart", CfgKind::Bool, &g_webPlayFromStart },
    { "webAutoRemove", CfgKind::Bool, &g_webAutoRemove },
    { "scCsgo", CfgKind::Bool, &g_scCsgo },
    { "scCsgoWeapon", CfgKind::Int, &g_scCsgoWeapon, 0.f, 2.f },
    { "scCsgoZoom", CfgKind::Int, &g_scCsgoZoom, 0.f, 1.f },
    { "scCsgoWidth", CfgKind::Int, &g_scCsgoWidth, 1.f, 10.f },
    { "scCsgoNormalInacc", CfgKind::Bool, &g_scCsgoNormalInacc },
    { "scCsgoMoveSens", CfgKind::Float, &g_scCsgoMoveSens },
    { "scCsgoAirSens", CfgKind::Float, &g_scCsgoAirSens },
    { "scCsgoScopeIn", CfgKind::Bool, &g_scCsgoScopeIn },
    { "scCsgoBob", CfgKind::Bool, &g_scCsgoBob },
    { "scCsgoBobScale", CfgKind::Float, &g_scCsgoBobScale },
    { "scCsgoLens", CfgKind::Bool, &g_scCsgoLens },
    { "scCsgoScale", CfgKind::Float, &g_scCsgoScale, 0.25f, 2.f },
    { "scCsgoNoDot", CfgKind::Bool, &g_scCsgoNoDot },
    { "scChargeHide", CfgKind::Bool, &g_scChargeHide },
    { "scCsgoOpacity", CfgKind::Float, &g_scCsgoOpacity, 0.f, 1.f },
    { "scCsgoBobLat", CfgKind::Float, &g_scCsgoBobLat, 0.f, 5.f },
    { "scCsgoBobVert", CfgKind::Float, &g_scCsgoBobVert, 0.f, 5.f },
    { "chCsgoEnable", CfgKind::Bool, &g_chCsgoEnable },
    { "chCsgoStyle", CfgKind::Int, &g_chCsgoStyle, 0.f, 4.f },
    { "chCsgoGap", CfgKind::Float, &g_chCsgoGap },
    { "chCsgoScale", CfgKind::Float, &g_chCsgoScale },
    { "chCsgoCrisp", CfgKind::Bool, &g_chCsgoCrisp },
    { "chCsgoLinear", CfgKind::Bool, &g_chCsgoLinear },
    { "chCsgoVelSens", CfgKind::Float, &g_chCsgoVelSens },
    { "chCsgoSmooth", CfgKind::Float, &g_chCsgoSmooth },
    { "chCsgoArcs", CfgKind::Bool, &g_chCsgoArcs },
    { "chCsgoPips", CfgKind::Bool, &g_chCsgoPips },
    { "csXSize", CfgKind::Float, &g_csXSize },
    { "csXThick", CfgKind::Float, &g_csXThick },
    { "csXGap", CfgKind::Float, &g_csXGap },
    { "csXDot", CfgKind::Bool, &g_csXDot },
    { "csXOutline", CfgKind::Bool, &g_csXOutline },
    { "csXOutlineTh", CfgKind::Float, &g_csXOutlineTh },
    { "csXT", CfgKind::Bool, &g_csXT },
    { "csXAlpha", CfgKind::Int, &g_csXAlpha, 0.f, 255.f },
    { "csXUseAlpha", CfgKind::Bool, &g_csXUseAlpha },
    { "csXSplitDist", CfgKind::Float, &g_csXSplitDist },
    { "csXSplitRatio", CfgKind::Float, &g_csXSplitRatio },
    { "csXInnerMod", CfgKind::Float, &g_csXInnerMod },
    { "csXOuterMod", CfgKind::Float, &g_csXOuterMod },
    { "speedoEnable", CfgKind::Bool, &g_speedoEnable },
    { "speedoPosX", CfgKind::Float, &g_speedoPosX, 0.f, 1.f },
    { "speedoPosY", CfgKind::Float, &g_speedoPosY, 0.f, 1.f },
    { "speedoScale", CfgKind::Float, &g_speedoScale, 0.3f, 5.f },
    { "speedoColorBySpeed", CfgKind::Bool, &g_speedoColorBySpeed },
    { "speedoSpeedLow", CfgKind::Float, &g_speedoSpeedLow, 0.f, FLT_MAX },
    { "speedoSpeedHigh", CfgKind::Float, &g_speedoSpeedHigh, 0.f, FLT_MAX },
    { "speedoOutline", CfgKind::Bool, &g_speedoOutline },
    { "speedoOutlineThick", CfgKind::Float, &g_speedoOutlineThick, 0.5f, FLT_MAX },
    { "speedoShadow", CfgKind::Bool, &g_speedoShadow },
    { "speedoShadowOffX", CfgKind::Float, &g_speedoShadowOffX },
    { "speedoShadowOffY", CfgKind::Float, &g_speedoShadowOffY },
    { "speedoShadowBlur", CfgKind::Bool, &g_speedoShadowBlur },
    { "speedoFadeIn", CfgKind::Bool, &g_speedoFadeIn },
    { "speedoFadeThreshold", CfgKind::Float, &g_speedoFadeThreshold, 0.f, FLT_MAX },
    { "speedoShowPre", CfgKind::Bool, &g_speedoShowPre },
    { "speedoPreHoldTime", CfgKind::Float, &g_speedoPreHoldTime, 0.5f, 10.f },
    { "speedoPreAnimCenter", CfgKind::Bool, &g_speedoPreAnimCenter },
    { "speedoPreDoctrineParens", CfgKind::Bool, &g_speedoPreDoctrineParens },
    { "speedoGraph", CfgKind::Bool, &g_speedoGraph },
    { "speedoGraphPosX", CfgKind::Float, &g_speedoGraphPosX, 0.f, 1.f },
    { "speedoGraphPosY", CfgKind::Float, &g_speedoGraphPosY, 0.f, 1.f },
    { "speedoGraphW", CfgKind::Float, &g_speedoGraphW, 20.f, 1000.f },
    { "speedoGraphH", CfgKind::Float, &g_speedoGraphH, 10.f, 400.f },
    { "speedoGraphMaxSpeed", CfgKind::Float, &g_speedoGraphMaxSpeed, 1.f, FLT_MAX },
    { "speedoGraphFade", CfgKind::Int, &g_speedoGraphFade, 0.f, 4.f },
    { "speedoGraphLineH", CfgKind::Float, &g_speedoGraphLineH, 0.5f, 10.f },
    { "kfEnable", CfgKind::Bool, &g_customKillfeedEnable },
    { "kfOrigin", CfgKind::Int, &g_kfOrigin, 0.f, 2.f },
    { "kfOffsetX", CfgKind::Float, &g_kfOffsetX },
    { "kfOffsetY", CfgKind::Float, &g_kfOffsetY },
    { "kfMaxEntries", CfgKind::Int, &g_kfMaxEntries, 1.f, 8.f },
    { "kfEntryPadY", CfgKind::Float, &g_kfEntryPadY, 0.f, 20.f },
    { "kfBoxShadow", CfgKind::Bool, &g_kfBoxShadow },
    { "kfBoxShadowOffX", CfgKind::Float, &g_kfBoxShadowOffX },
    { "kfBoxShadowOffY", CfgKind::Float, &g_kfBoxShadowOffY },
    { "kfBoxShadowBlur", CfgKind::Float, &g_kfBoxShadowBlur, 0.f, 40.f },
    { "kfFontScale", CfgKind::Float, &g_kfFontScale, 0.3f, 5.f },
    { "kfUseIcons", CfgKind::Bool, &g_kfUseIcons },
    { "kfPlainWeaponIcons", CfgKind::Bool, &g_kfPlainWeaponIcons },
    { "kfConsistentIcons", CfgKind::Bool, &g_kfConsistentIcons },
    { "kfIconScale", CfgKind::Float, &g_kfIconScale, 0.25f, 4.f },
    { "kfSlideIn", CfgKind::Bool, &g_kfSlideIn },
    { "kfSlideSpeed", CfgKind::Float, &g_kfSlideSpeed, 1.f, 50.f },
    { "kfSlideDir", CfgKind::Int, &g_kfSlideDir, 0.f, 2.f },
    { "kfFadeIn", CfgKind::Bool, &g_kfFadeIn },
    { "kfFadeSpeed", CfgKind::Float, &g_kfFadeSpeed, 0.5f, 20.f },
    { "kfBgRounding", CfgKind::Float, &g_kfBgRounding, 0.f, 20.f },
    { "kfPadX", CfgKind::Float, &g_kfPadX, 0.f, 40.f },
    { "kfMinHeight", CfgKind::Float, &g_kfMinHeight, 0.f, 80.f },
    { "kfGrowDown", CfgKind::Bool, &g_kfGrowDown },
    { "kfTextDropShadow", CfgKind::Bool, &g_kfTextDropShadow },
    { "kfTextDropShadowOffX", CfgKind::Float, &g_kfTextDropShadowOffX, -20.f, 20.f },
    { "kfTextDropShadowOffY", CfgKind::Float, &g_kfTextDropShadowOffY, -20.f, 20.f },
    { "kfTextDropShadowBlur", CfgKind::Bool, &g_kfTextDropShadowBlur },
    { "kfTextOutline", CfgKind::Bool, &g_kfTextOutline },
    { "kfTextOutlineThickness", CfgKind::Float, &g_kfTextOutlineThickness, 0.5f, 4.f },
    { "kfFadeOut", CfgKind::Bool, &g_kfFadeOut },
    { "kfFadeOutSpeed", CfgKind::Float, &g_kfFadeOutSpeed, 0.5f, 20.f },
    { "kfSlideOut", CfgKind::Bool, &g_kfSlideOut },
    { "kfSlideOutSpeed", CfgKind::Float, &g_kfSlideOutSpeed, 1.f, 50.f },
    { "kfSlideOutDir", CfgKind::Int, &g_kfSlideOutDir, 0.f, 2.f },
    { "kfSmoothShift", CfgKind::Bool, &g_kfSmoothShift },
    { "kfSmoothShiftSpeed", CfgKind::Float, &g_kfSmoothShiftSpeed, 1.f, 50.f },
    { "kfBgCornerFlags", CfgKind::Int, &g_kfBgCornerFlags, 0.f, 15.f },
    { "kfBlobEnable", CfgKind::Bool, &g_kfBlobEnable },
    { "kfBlobAlpha", CfgKind::Float, &g_kfBlobAlpha, 0.f, 1.f },
    { "kfBlobSize", CfgKind::Float, &g_kfBlobSize, 0.1f, 2.f },
    { "kfBlobSpeed", CfgKind::Float, &g_kfBlobSpeed, 0.1f, 5.f },
    { "kfPrecipEnable", CfgKind::Bool, &g_kfPrecipEnable },
    { "kfPrecipMode", CfgKind::Int, &g_kfPrecipMode, 0.f, 1.f },
    { "kfPrecipCount", CfgKind::Int, &g_kfPrecipCount, 1.f, 200.f },
    { "kfPrecipSpeed", CfgKind::Float, &g_kfPrecipSpeed, 0.1f, 5.f },
    { "kfPrecipSize", CfgKind::Float, &g_kfPrecipSize, 0.1f, 5.f },
    { "kfPrecipWind", CfgKind::Float, &g_kfPrecipWind, -1.f, 1.f },
    { "kfKillGlowEnable", CfgKind::Bool, &g_kfKillGlowEnable },
    { "kfKillGlowDuration", CfgKind::Float, &g_kfKillGlowDuration, 0.2f, 5.f },
    { "kfKillGlowSpread", CfgKind::Float, &g_kfKillGlowSpread, 1.f, 60.f },
    { "kfNameLive", CfgKind::Bool, &g_kfNameLive },
    { "wpnHudX", CfgKind::Float, &g_wpnHudX },
    { "wpnHudY", CfgKind::Float, &g_wpnHudY },
    { "wpnHudIconSz", CfgKind::Float, &g_wpnHudIconSz },
    { "wpnHudSlotGap", CfgKind::Float, &g_wpnHudSlotGap },
    { "wpnHudActiveCR", CfgKind::Float, &g_wpnHudActiveColor[0] },
    { "wpnHudActiveCG", CfgKind::Float, &g_wpnHudActiveColor[1] },
    { "wpnHudActiveCB", CfgKind::Float, &g_wpnHudActiveColor[2] },
    { "wpnHudActiveCA", CfgKind::Float, &g_wpnHudActiveColor[3] },
    { "wpnHudInactiveCR", CfgKind::Float, &g_wpnHudInactiveColor[0] },
    { "wpnHudInactiveCG", CfgKind::Float, &g_wpnHudInactiveColor[1] },
    { "wpnHudInactiveCB", CfgKind::Float, &g_wpnHudInactiveColor[2] },
    { "wpnHudInactiveCA", CfgKind::Float, &g_wpnHudInactiveColor[3] },
    { "wpnHudNumScale", CfgKind::Float, &g_wpnHudNumScale },
    { "wpnHudNumPos", CfgKind::Int, &g_wpnHudNumPos },
    { "wpnHudNumOffX", CfgKind::Float, &g_wpnHudNumOffX },
    { "wpnHudNumOffY", CfgKind::Float, &g_wpnHudNumOffY },
    { "wpnHudOriginH", CfgKind::Int, &g_wpnHudOriginH },
    { "wpnHudOriginV", CfgKind::Int, &g_wpnHudOriginV },
    { "wpnHudGlowRadius", CfgKind::Float, &g_wpnHudGlowRadius },
    { "wpnHudGlowBolden", CfgKind::Float, &g_wpnHudGlowBolden },
    { "wpnHudGlowAlpha", CfgKind::Float, &g_wpnHudGlowAlpha },
    { "wpnHudNamePos", CfgKind::Int, &g_wpnHudNamePos },
    { "wpnHudNameOffX", CfgKind::Float, &g_wpnHudNameOffX },
    { "wpnHudNameOffY", CfgKind::Float, &g_wpnHudNameOffY },
    { "wpnHudNameScale", CfgKind::Float, &g_wpnHudNameScale },
    { "wpnHudNameMax", CfgKind::Int, &g_wpnHudNameMaxChars },
    { "wpnHudNameShadowCR", CfgKind::Float, &g_wpnHudNameShadowColor[0] },
    { "wpnHudNameShadowCG", CfgKind::Float, &g_wpnHudNameShadowColor[1] },
    { "wpnHudNameShadowCB", CfgKind::Float, &g_wpnHudNameShadowColor[2] },
    { "wpnHudNameShadowCA", CfgKind::Float, &g_wpnHudNameShadowColor[3] },
    { "wpnHudNameShadowX", CfgKind::Float, &g_wpnHudNameShadowOffX },
    { "wpnHudNameShadowY", CfgKind::Float, &g_wpnHudNameShadowOffY },
    { "wpnHudNameOutlineCR", CfgKind::Float, &g_wpnHudNameOutlineColor[0] },
    { "wpnHudNameOutlineCG", CfgKind::Float, &g_wpnHudNameOutlineColor[1] },
    { "wpnHudNameOutlineCB", CfgKind::Float, &g_wpnHudNameOutlineColor[2] },
    { "wpnHudNameOutlineCA", CfgKind::Float, &g_wpnHudNameOutlineColor[3] },
    { "wpnHudNameOutlineT", CfgKind::Float, &g_wpnHudNameOutlineThickness },
    { "wpnHudNumShadowCR", CfgKind::Float, &g_wpnHudNumShadowColor[0] },
    { "wpnHudNumShadowCG", CfgKind::Float, &g_wpnHudNumShadowColor[1] },
    { "wpnHudNumShadowCB", CfgKind::Float, &g_wpnHudNumShadowColor[2] },
    { "wpnHudNumShadowCA", CfgKind::Float, &g_wpnHudNumShadowColor[3] },
    { "wpnHudNumShadowX", CfgKind::Float, &g_wpnHudNumShadowOffX },
    { "wpnHudNumShadowY", CfgKind::Float, &g_wpnHudNumShadowOffY },
    { "wpnHudNumOutlineCR", CfgKind::Float, &g_wpnHudNumOutlineColor[0] },
    { "wpnHudNumOutlineCG", CfgKind::Float, &g_wpnHudNumOutlineColor[1] },
    { "wpnHudNumOutlineCB", CfgKind::Float, &g_wpnHudNumOutlineColor[2] },
    { "wpnHudNumOutlineCA", CfgKind::Float, &g_wpnHudNumOutlineColor[3] },
    { "wpnHudNumOutlineT", CfgKind::Float, &g_wpnHudNumOutlineThickness },
    { "wpnHudActiveScale", CfgKind::Float, &g_wpnHudActiveScale },
    { "wpnHudActiveScaleSpeed", CfgKind::Float, &g_wpnHudActiveScaleSpeed },
    { "wpnHudIconRotation", CfgKind::Float, &g_wpnHudIconRotation, -180.f, 180.f },
    { "wpnHudNumColorMode", CfgKind::Int, &g_wpnHudNumColorMode },
    { "wpnHudNumCustCR", CfgKind::Float, &g_wpnHudNumCustomColor[0] },
    { "wpnHudNumCustCG", CfgKind::Float, &g_wpnHudNumCustomColor[1] },
    { "wpnHudNumCustCB", CfgKind::Float, &g_wpnHudNumCustomColor[2] },
    { "wpnHudNumCustCA", CfgKind::Float, &g_wpnHudNumCustomColor[3] },
    { "wpnHudNameColorMode", CfgKind::Int, &g_wpnHudNameColorMode },
    { "wpnHudNameCustCR", CfgKind::Float, &g_wpnHudNameCustomColor[0] },
    { "wpnHudNameCustCG", CfgKind::Float, &g_wpnHudNameCustomColor[1] },
    { "wpnHudNameCustCB", CfgKind::Float, &g_wpnHudNameCustomColor[2] },
    { "wpnHudNameCustCA", CfgKind::Float, &g_wpnHudNameCustomColor[3] },
    { "wpnHudSepCR", CfgKind::Float, &g_wpnHudSepColor[0] },
    { "wpnHudSepCG", CfgKind::Float, &g_wpnHudSepColor[1] },
    { "wpnHudSepCB", CfgKind::Float, &g_wpnHudSepColor[2] },
    { "wpnHudSepCA", CfgKind::Float, &g_wpnHudSepColor[3] },
    { "wpnHudSepWidth", CfgKind::Float, &g_wpnHudSepWidth },
    { "wpnHudSepHeight", CfgKind::Float, &g_wpnHudSepHeight },
    { "wpnHudFadeDelay", CfgKind::Float, &g_wpnHudFadeDelay, 0.f, 5.f },
    { "wpnHudFadeSpeed", CfgKind::Float, &g_wpnHudFadeSpeed, 0.1f, 10.f },
    { "wpnHudBgWidth", CfgKind::Float, &g_wpnHudBgWidth },
    { "wpnHudBgFadeStart", CfgKind::Float, &g_wpnHudBgFadeStart, 0.f, 1.f },
    { "wpnHudBgFadeNearStart", CfgKind::Float, &g_wpnHudBgFadeNearStart, 0.f, 1.f },
    { "wpnHudBgHeightH", CfgKind::Float, &g_wpnHudBgHeightH },
    { "wpnHudBgExtraH", CfgKind::Float, &g_wpnHudBgExtraH },
    { "wpnHudBgOffX", CfgKind::Float, &g_wpnHudBgOffX },
    { "wpnHudBgOffY", CfgKind::Float, &g_wpnHudBgOffY },
    { "wpnHudBgNearCR", CfgKind::Float, &g_wpnHudBgColorNear[0] },
    { "wpnHudBgNearCG", CfgKind::Float, &g_wpnHudBgColorNear[1] },
    { "wpnHudBgNearCB", CfgKind::Float, &g_wpnHudBgColorNear[2] },
    { "wpnHudBgNearCA", CfgKind::Float, &g_wpnHudBgColorNear[3] },
    { "wpnHudBgFarCR", CfgKind::Float, &g_wpnHudBgColorFar[0] },
    { "wpnHudBgFarCG", CfgKind::Float, &g_wpnHudBgColorFar[1] },
    { "wpnHudBgFarCB", CfgKind::Float, &g_wpnHudBgColorFar[2] },
    { "wpnHudBgFarCA", CfgKind::Float, &g_wpnHudBgColorFar[3] },
    { "wpnHudBgBorderH", CfgKind::Float, &g_wpnHudBgBorderHeight },
    { "wpnHudBgBorderNearCR", CfgKind::Float, &g_wpnHudBgBorderColorNear[0] },
    { "wpnHudBgBorderNearCG", CfgKind::Float, &g_wpnHudBgBorderColorNear[1] },
    { "wpnHudBgBorderNearCB", CfgKind::Float, &g_wpnHudBgBorderColorNear[2] },
    { "wpnHudBgBorderNearCA", CfgKind::Float, &g_wpnHudBgBorderColorNear[3] },
    { "wpnHudBgBorderFarCR", CfgKind::Float, &g_wpnHudBgBorderColorFar[0] },
    { "wpnHudBgBorderFarCG", CfgKind::Float, &g_wpnHudBgBorderColorFar[1] },
    { "wpnHudBgBorderFarCB", CfgKind::Float, &g_wpnHudBgBorderColorFar[2] },
    { "wpnHudBgBorderFarCA", CfgKind::Float, &g_wpnHudBgBorderColorFar[3] },
    { "enableOverlay", CfgKind::Bool, &g_enableOverlay },
    { "enableMediaPlayer", CfgKind::Bool, &g_enableMediaPlayer },
    { "mpWaveform", CfgKind::Bool, &g_mpWaveform },
    { "wfSensitivity", CfgKind::Float, &g_wfSensitivity, 1.0f, 12.0f },
    { "mpWaveformPos", CfgKind::Int, &g_mpWaveformPos, 0.f, 2.f },
    { "mpWfFreeX", CfgKind::Float, &g_mpWfFreeX, 0.f, 3840.f },
    { "mpWfFreeY", CfgKind::Float, &g_mpWfFreeY, 0.f, 2160.f },
    { "minesweeperOpen", CfgKind::Bool, &g_minesweeperOpen },
    { "msDarkMode", CfgKind::Bool, &g_msDarkMode },
    { "mpMode", CfgKind::Int, &g_mpMode, 0.f, 1.f },
    { "mpAvoidWatermark", CfgKind::Bool, &g_mpAvoidWatermark },
    { "mpPos", CfgKind::Int, &g_mpPos, 0.f, 5.f },
    { "mpScale", CfgKind::Float, &g_mpScale, 0.3f, 3.0f },
    { "mpYOffset", CfgKind::Float, &g_mpYOffset, 0.0f, 1.0f },
    { "mpXOffset", CfgKind::Float, &g_mpXOffset, -800.f, 800.f },
    { "mpShowThumb", CfgKind::Bool, &g_mpShowThumb },
    { "mpThumbPreserveAspect", CfgKind::Bool, &g_mpThumbPreserveAspect },
    { "mpThumbCircle", CfgKind::Bool, &g_mpThumbCircle },
    { "mpThumbOutline", CfgKind::Bool, &g_mpThumbOutline },
    { "mpThumbOutlineThick", CfgKind::Float, &g_mpThumbOutlineThick, 0.5f, 8.f },
    { "mpThumbOutlineR", CfgKind::Float, &g_mpThumbOutlineColor[0], 0.f, 1.f },
    { "mpThumbOutlineG", CfgKind::Float, &g_mpThumbOutlineColor[1], 0.f, 1.f },
    { "mpThumbOutlineB", CfgKind::Float, &g_mpThumbOutlineColor[2], 0.f, 1.f },
    { "mpThumbOutlineA", CfgKind::Float, &g_mpThumbOutlineColor[3], 0.f, 1.f },
    { "mpThumbSize", CfgKind::Float, &g_mpThumbSize, 16.f, 96.f },
    { "mpShowProgress", CfgKind::Bool, &g_mpShowProgress },
    { "mpShowDuration", CfgKind::Bool, &g_mpShowDuration },
    { "mpShowLyrics", CfgKind::Bool, &g_mpShowLyrics },
    { "mpLyricsAbove", CfgKind::Bool, &g_mpLyricsAbove },
    { "mpLyricsSide", CfgKind::Bool, &g_mpLyricsSide },
    { "mpLyricsXOff", CfgKind::Float, &g_mpLyricsXOff, -500.f, 500.f },
    { "mpLyricsYOff", CfgKind::Float, &g_mpLyricsYOff, -500.f, 500.f },
    { "mpLyricsR", CfgKind::Float, &g_mpLyricsColor[0], 0.f, 1.f },
    { "mpLyricsG", CfgKind::Float, &g_mpLyricsColor[1], 0.f, 1.f },
    { "mpLyricsB", CfgKind::Float, &g_mpLyricsColor[2], 0.f, 1.f },
    { "mpLyricsA", CfgKind::Float, &g_mpLyricsColor[3], 0.f, 1.f },
    { "mpLyricsScale", CfgKind::Float, &g_mpLyricsScale, 0.5f, 2.0f },
    { "mpLyricsOffset", CfgKind::Float, &g_mpLyricsOffset, -5.f, 5.f },
    { "mpLyricsBlob", CfgKind::Bool, &g_mpLyricsBlob },
    { "mpLyricsPrecip", CfgKind::Bool, &g_mpLyricsPrecip },
    { "mpLyricsClampWidth", CfgKind::Bool, &g_mpLyricsClampWidth },
    { "mpLyricsClampW", CfgKind::Float, &g_mpLyricsClampW, 80.f, 600.f },
    { "mpLyricsEllipsis", CfgKind::Bool, &g_mpLyricsEllipsis },
    { "mpLyricsAvoidWm", CfgKind::Bool, &g_mpLyricsAvoidWm },
    { "mpLyricsShiftBoxes", CfgKind::Bool, &g_mpLyricsShiftBoxes },
    { "mpLyricsOnly", CfgKind::Bool, &g_mpLyricsOnly },
    { "mpLyricsRows", CfgKind::Int, &g_mpLyricsRows, 1.f, 9.f },
    { "mpLyricsBgDraw", CfgKind::Bool, &g_mpLyricsBgDraw },
    { "mpTitleAlign", CfgKind::Int, &g_mpTitleAlign, 0.f, 2.f },
    { "mpArtistAlign", CfgKind::Int, &g_mpArtistAlign, 0.f, 2.f },
    { "mpLyricsAlign", CfgKind::Int, &g_mpLyricsAlign, 0.f, 2.f },
    { "mpTitleXOff", CfgKind::Float, &g_mpTitleXOff, -800.f, 800.f },
    { "mpTitleYOff", CfgKind::Float, &g_mpTitleYOff, -400.f, 400.f },
    { "mpArtistXOff", CfgKind::Float, &g_mpArtistXOff, -800.f, 800.f },
    { "mpArtistYOff", CfgKind::Float, &g_mpArtistYOff, -400.f, 400.f },
    { "mpAlbumXOff", CfgKind::Float, &g_mpAlbumXOff, -400.f, 400.f },
    { "mpAlbumYOff", CfgKind::Float, &g_mpAlbumYOff, -400.f, 400.f },
    { "mpThumbRounding", CfgKind::Float, &g_mpThumbRounding, 0.f, 48.f },
    { "mpCustomThumb", CfgKind::Bool, &g_mpCustomThumb },
    { "mpCustomThumbPath", CfgKind::Str, &g_mpCustomThumbPath },
    { "mpProgXOff", CfgKind::Float, &g_mpProgXOff, -400.f, 400.f },
    { "mpProgYOff", CfgKind::Float, &g_mpProgYOff, -400.f, 400.f },
    { "mpProgBarW", CfgKind::Float, &g_mpProgBarW, -200.f, 400.f },
    { "mpProgHeight", CfgKind::Float, &g_mpProgHeight, 0.5f, 16.f },
    { "mpMaxWidth", CfgKind::Float, &g_mpMaxWidth, 0.f, 2000.f },
    { "mpStaticWidth", CfgKind::Bool, &g_mpStaticWidth },
    { "mpTitleMaxW", CfgKind::Float, &g_mpTitleMaxW, 0.f, FLT_MAX },
    { "mpArtistMaxW", CfgKind::Float, &g_mpArtistMaxW, 0.f, FLT_MAX },
    { "mpTitleEllipsis", CfgKind::Bool, &g_mpTitleEllipsis },
    { "mpArtistEllipsis", CfgKind::Bool, &g_mpArtistEllipsis },
    { "mpBgInsetL", CfgKind::Float, &g_mpBgInsetL, -50.f, 200.f },
    { "mpBgInsetR", CfgKind::Float, &g_mpBgInsetR, -50.f, 200.f },
    { "mpBgInsetT", CfgKind::Float, &g_mpBgInsetT, -50.f, 200.f },
    { "mpBgInsetB", CfgKind::Float, &g_mpBgInsetB, -50.f, 200.f },
    { "wmBgInsetL", CfgKind::Float, &g_wmBgInsetL, -50.f, 200.f },
    { "wmBgInsetR", CfgKind::Float, &g_wmBgInsetR, -50.f, 200.f },
    { "wmBgInsetT", CfgKind::Float, &g_wmBgInsetT, -50.f, 200.f },
    { "wmBgInsetB", CfgKind::Float, &g_wmBgInsetB, -50.f, 200.f },
    { "mpBgRounding", CfgKind::Float, &g_mpBgRounding, 0.f, 11.f },
    { "mpBgR", CfgKind::Float, &g_mpBgColor[0], 0.f, 1.f },
    { "mpBgG", CfgKind::Float, &g_mpBgColor[1], 0.f, 1.f },
    { "mpBgB", CfgKind::Float, &g_mpBgColor[2], 0.f, 1.f },
    { "mpBgA", CfgKind::Float, &g_mpBgColor[3], 0.f, 1.f },
    { "mpTitleR", CfgKind::Float, &g_mpTitleColor[0], 0.f, 1.f },
    { "mpTitleG", CfgKind::Float, &g_mpTitleColor[1], 0.f, 1.f },
    { "mpTitleB", CfgKind::Float, &g_mpTitleColor[2], 0.f, 1.f },
    { "mpArtistR", CfgKind::Float, &g_mpArtistColor[0], 0.f, 1.f },
    { "mpArtistG", CfgKind::Float, &g_mpArtistColor[1], 0.f, 1.f },
    { "mpArtistB", CfgKind::Float, &g_mpArtistColor[2], 0.f, 1.f },
    { "mpBlobEnable", CfgKind::Bool, &g_mpBlobEnable },
    { "mpBlobUseSheen", CfgKind::Bool, &g_mpBlobUseSheen },
    { "mpBlobR", CfgKind::Float, &g_mpBlobColor[0], 0.f, 1.f },
    { "mpBlobG", CfgKind::Float, &g_mpBlobColor[1], 0.f, 1.f },
    { "mpBlobB", CfgKind::Float, &g_mpBlobColor[2], 0.f, 1.f },
    { "mpBlobAlpha", CfgKind::Float, &g_mpBlobAlpha, 0.f, 1.f },
    { "mpBlobSize", CfgKind::Float, &g_mpBlobSize, 0.1f, 2.f },
    { "mpBlobSpeed", CfgKind::Float, &g_mpBlobSpeed, 0.1f, 5.f },
    { "mpPrecipEnable", CfgKind::Bool, &g_mpPrecipEnable },
    { "mpPrecipMode", CfgKind::Int, &g_mpPrecipMode, 0.f, 1.f },
    { "mpPrecipCount", CfgKind::Int, &g_mpPrecipCount, 1.f, 200.f },
    { "mpPrecipSpeed", CfgKind::Float, &g_mpPrecipSpeed, 0.1f, 5.f },
    { "mpPrecipSize", CfgKind::Float, &g_mpPrecipSize, 0.1f, 5.f },
    { "mpPrecipWind", CfgKind::Float, &g_mpPrecipWind, -1.f, 1.f },
    { "mpPrecipR", CfgKind::Float, &g_mpPrecipColor[0], 0.f, 1.f },
    { "mpPrecipG", CfgKind::Float, &g_mpPrecipColor[1], 0.f, 1.f },
    { "mpPrecipB", CfgKind::Float, &g_mpPrecipColor[2], 0.f, 1.f },
    { "mpPrecipA", CfgKind::Float, &g_mpPrecipColor[3], 0.f, 1.f },
    { "menuFontScale", CfgKind::Float, &g_menuFontScale, 0.5f, 2.0f },
    { "enablePureBypass", CfgKind::Bool, &g_enablePureBypass },
    { "menuSnow", CfgKind::Bool, &g_menuSnow },
    { "enableCheatsbypass", CfgKind::Bool, &g_enableCheatsbypass },
    { "ngColorEnable", CfgKind::Bool, &g_ngColorEnable },
    { "wmLabelSource", CfgKind::Int, &g_wmLabelSource, 0.f, 2.f },
    { "wmShowUsername", CfgKind::Bool, &g_wmShowUsername },
    { "wmUsernameLabel", CfgKind::Str, &g_wmUsernameLabel },
    { "labelAnimMode", CfgKind::Int, &g_labelAnimMode, 0.f, 3.f },
    { "labelScrollSpeed", CfgKind::Float, &g_labelScrollSpeed },
    { "labelTypeSpeed", CfgKind::Float, &g_labelTypeSpeed },
    { "labelEraseSpeed", CfgKind::Float, &g_labelEraseSpeed },
    { "labelHoldTime", CfgKind::Float, &g_labelHoldTime },
    { "titleCycleEnable", CfgKind::Bool, &g_titleCycleEnable },
    { "titleCycleInterval", CfgKind::Float, &g_titleInterval, 0.1f, 60.f },
    { "drawWatermarkBg", CfgKind::Bool, &g_drawWatermarkBg },
    { "drawWatermarkText", CfgKind::Bool, &g_drawWatermarkText },
    { "drawFPS", CfgKind::Bool, &g_drawFPS },
    { "drawPing", CfgKind::Bool, &g_drawPing },
    { "drawLoss", CfgKind::Bool, &g_drawLoss },
    { "drawTime", CfgKind::Bool, &g_drawTime },
    { "wmUseIcons", CfgKind::Bool, &g_wmUseIcons },
    { "menuShowTabIcons", CfgKind::Bool, &g_menuShowTabIcons },
    { "watermarkScale", CfgKind::Float, &g_watermarkScale },
    { "wmFontWeight", CfgKind::Int, &g_wmFontWeight, 0.f, 2.f },
    { "watermarkPos", CfgKind::Int, &g_watermarkPos },
    { "watermarkYOffset", CfgKind::Float, &g_watermarkYOffset },
    { "watermarkXOffset", CfgKind::Float, &g_watermarkXOffset, -800.f, 800.f },
    { "wmShowLogo", CfgKind::Bool, &g_wmShowLogo },
    { "wmLogoGhost", CfgKind::Bool, &g_wmLogoGhost },
    { "wmLogoGhostAlpha", CfgKind::Float, &g_wmLogoGhostAlpha, 0.f, 1.f },
    { "wmLogoImgScale", CfgKind::Float, &g_wmLogoImgScale, 0.05f, 5.f },
    { "wmLogoImgRotation", CfgKind::Float, &g_wmLogoImgRotation },
    { "wmLogoImgColorize", CfgKind::Bool, &g_wmLogoImgColorize },
    { "threshFPS", CfgKind::Int, &g_threshFPS },
    { "threshPing", CfgKind::Int, &g_threshPing },
    { "threshLoss", CfgKind::Int, &g_threshLoss },
    { "wmBgR", CfgKind::Float, &g_wmBgColor[0] },
    { "wmBgG", CfgKind::Float, &g_wmBgColor[1] },
    { "wmBgB", CfgKind::Float, &g_wmBgColor[2] },
    { "wmBgA", CfgKind::Float, &g_wmBgColor[3] },
    { "wmDimR", CfgKind::Float, &g_wmDimColor[0] },
    { "wmDimG", CfgKind::Float, &g_wmDimColor[1] },
    { "wmDimB", CfgKind::Float, &g_wmDimColor[2] },
    { "wmTextR", CfgKind::Float, &g_wmTextColor[0] },
    { "wmTextG", CfgKind::Float, &g_wmTextColor[1] },
    { "wmTextB", CfgKind::Float, &g_wmTextColor[2] },
    { "wmTitleR", CfgKind::Float, &g_wmTitleColor[0] },
    { "wmTitleG", CfgKind::Float, &g_wmTitleColor[1] },
    { "wmTitleB", CfgKind::Float, &g_wmTitleColor[2] },
    { "wmAlertR", CfgKind::Float, &g_wmAlertColor[0] },
    { "wmAlertG", CfgKind::Float, &g_wmAlertColor[1] },
    { "wmAlertB", CfgKind::Float, &g_wmAlertColor[2] },
    { "wmBgRounding", CfgKind::Float, &g_wmBgRounding },
    { "wmBlobEnable", CfgKind::Bool, &g_wmBlobEnable },
    { "wmBlobUseSheen", CfgKind::Bool, &g_wmBlobUseSheen },
    { "wmBlobR", CfgKind::Float, &g_wmBlobColor[0], 0.f, 1.f },
    { "wmBlobG", CfgKind::Float, &g_wmBlobColor[1], 0.f, 1.f },
    { "wmBlobB", CfgKind::Float, &g_wmBlobColor[2], 0.f, 1.f },
    { "wmBlobAlpha", CfgKind::Float, &g_wmBlobAlpha, 0.f, 1.f },
    { "wmBlobSize", CfgKind::Float, &g_wmBlobSize, 0.1f, 2.f },
    { "wmBlobSpeed", CfgKind::Float, &g_wmBlobSpeed, 0.1f, 5.f },
    { "wmPrecipEnable", CfgKind::Bool, &g_wmPrecipEnable },
    { "wmPrecipMode", CfgKind::Int, &g_wmPrecipMode, 0.f, 1.f },
    { "wmPrecipCount", CfgKind::Int, &g_wmPrecipCount, 1.f, 200.f },
    { "wmPrecipSpeed", CfgKind::Float, &g_wmPrecipSpeed, 0.1f, 5.f },
    { "wmPrecipSize", CfgKind::Float, &g_wmPrecipSize, 0.1f, 5.f },
    { "wmPrecipWind", CfgKind::Float, &g_wmPrecipWind, -1.f, 1.f },
    { "wmPrecipR", CfgKind::Float, &g_wmPrecipColor[0], 0.f, 1.f },
    { "wmPrecipG", CfgKind::Float, &g_wmPrecipColor[1], 0.f, 1.f },
    { "wmPrecipB", CfgKind::Float, &g_wmPrecipColor[2], 0.f, 1.f },
    { "wmPrecipA", CfgKind::Float, &g_wmPrecipColor[3], 0.f, 1.f },
    { "wmKillGlowEnable", CfgKind::Bool, &g_wmKillGlowEnable },
    { "wmKillGlowR", CfgKind::Float, &g_wmKillGlowColor[0], 0.f, 1.f },
    { "wmKillGlowG", CfgKind::Float, &g_wmKillGlowColor[1], 0.f, 1.f },
    { "wmKillGlowB", CfgKind::Float, &g_wmKillGlowColor[2], 0.f, 1.f },
    { "wmKillGlowA", CfgKind::Float, &g_wmKillGlowColor[3], 0.f, 1.f },
    { "wmKillGlowDuration", CfgKind::Float, &g_wmKillGlowDuration, 0.2f, 5.f },
    { "wmKillGlowSpread", CfgKind::Float, &g_wmKillGlowSpread, 1.f, 60.f },
    { "streamerMode", CfgKind::Int, &g_streamerMode },
    { "streamerEveryoneLabel", CfgKind::Str, &g_streamerEveryoneLabel },
    { "streamerTeamLabel", CfgKind::Str, &g_streamerTeamLabel },
    { "streamerEnemyLabel", CfgKind::Str, &g_streamerEnemyLabel },
    { "useCustomUsername", CfgKind::Bool, &g_useCustomUsername },
    { "customUsername", CfgKind::Str, &g_customUsername },
    { "enableNameCycle", CfgKind::Bool, &g_enableNameCycle },
    { "nameTrigger", CfgKind::Int, &g_nameTrigger, 0.f, 1.f },
    { "nameInterval", CfgKind::Float, &g_nameInterval, 0.1f, 60.0f },
    { "enableTeamLabelCycle", CfgKind::Bool, &g_enableTeamLabelCycle },
    { "enableEnemyLabelCycle", CfgKind::Bool, &g_enableEnemyLabelCycle },
    { "enableEveryoneLabelCycle", CfgKind::Bool, &g_enableEveryoneLabelCycle },
    { "enableSheen", CfgKind::Bool, &g_enableSheen },
    { "sheenColorR", CfgKind::Float, &g_sheenColor[0] },
    { "sheenColorG", CfgKind::Float, &g_sheenColor[1] },
    { "sheenColorB", CfgKind::Float, &g_sheenColor[2] },
    { "enableSheenAnim", CfgKind::Bool, &g_enableSheenAnim },
    { "enableCritColor", CfgKind::Bool, &g_enableCritColor },
    { "critColorR", CfgKind::Float, &g_critColor[0] },
    { "critColorG", CfgKind::Float, &g_critColor[1] },
    { "critColorB", CfgKind::Float, &g_critColor[2] },
    { "critUseSheenColor", CfgKind::Bool, &g_critUseSheenColor },
    { "enableDRColor", CfgKind::Bool, &g_enableDRColor },
    { "drColorR", CfgKind::Float, &g_drColor[0] },
    { "drColorG", CfgKind::Float, &g_drColor[1] },
    { "drColorB", CfgKind::Float, &g_drColor[2] },
    { "drUseSheenColor", CfgKind::Bool, &g_drUseSheenColor },
    { "sheenColorCount", CfgKind::Int, &g_sheenColorCount, 2.f, 4.f },
    { "sheenAnimSpeed", CfgKind::Float, &g_sheenAnimSpeed, 0.1f, FLT_MAX },
    { "enableFog", CfgKind::Bool, &g_enableFog },
    { "fogColorR", CfgKind::Float, &g_fogColor[0] },
    { "fogColorG", CfgKind::Float, &g_fogColor[1] },
    { "fogColorB", CfgKind::Float, &g_fogColor[2] },
    { "fogUseSheenColor", CfgKind::Bool, &g_fogUseSheenColor },
    { "fogStart", CfgKind::Float, &g_fogStart },
    { "fogEnd", CfgKind::Float, &g_fogEnd },
    { "fogDensity", CfgKind::Float, &g_fogDensity },
    { "fogSkyboxStart", CfgKind::Float, &g_fogSkyboxStart },
    { "fogSkyboxEnd", CfgKind::Float, &g_fogSkyboxEnd },
    { "fogSkyboxDensity", CfgKind::Float, &g_fogSkyboxDensity },
    { "enableSkybox", CfgKind::Bool, &g_enableSkybox },
    { "weatherEnabled", CfgKind::Bool, &g_weatherEnabled },
    { "rainAlpha", CfgKind::Float, &g_rainAlpha, 0.f, 1.f },
    { "rainSpeed", CfgKind::Float, &g_rainSpeed, 0.f, 2000.f },
    { "rainWidth", CfgKind::Float, &g_rainWidth, 0.f, 5.f },
    { "rainLength", CfgKind::Float, &g_rainLength, 0.f, 5.f },
    { "rainRadius", CfgKind::Float, &g_rainRadius, 100.f, 4000.f },
    { "rainSideVel", CfgKind::Float, &g_rainSideVel, 0.f, 500.f },
    { "snowAlpha", CfgKind::Float, &g_snowAlpha, 0.f, 1.f },
    { "snowSpeed", CfgKind::Float, &g_snowSpeed, 0.f, 2000.f },
    { "snowWidth", CfgKind::Float, &g_snowWidth, 0.f, 5.f },
    { "snowLength", CfgKind::Float, &g_snowLength, 0.f, 5.f },
    { "snowRadius", CfgKind::Float, &g_snowRadius, 100.f, 4000.f },
    { "snowSideVel", CfgKind::Float, &g_snowSideVel, 0.f, 500.f },
    { "snowDensity", CfgKind::Float, &g_snowDensity, 0.1f, 1.f },
    { "particleLayerCount", CfgKind::Int, &g_particleLayerCount, 1.f, 8.f },
    { "particleGridRadius", CfgKind::Float, &g_particleGridRadius, 500.f, 8000.f },
    { "particleSpacingOffset", CfgKind::Float, &g_particleSpacingOffset, -16000.f, 16000.f },
    { "enableModulate", CfgKind::Bool, &g_enableModulate },
    { "modulateWorldR", CfgKind::Float, &g_modulateWorld[0] },
    { "modulateWorldG", CfgKind::Float, &g_modulateWorld[1] },
    { "modulateWorldB", CfgKind::Float, &g_modulateWorld[2] },
    { "modulateSkyR", CfgKind::Float, &g_modulateSky[0] },
    { "modulateSkyG", CfgKind::Float, &g_modulateSky[1] },
    { "modulateSkyB", CfgKind::Float, &g_modulateSky[2] },
    { "modulatePropsR", CfgKind::Float, &g_modulateProps[0] },
    { "modulatePropsG", CfgKind::Float, &g_modulateProps[1] },
    { "modulatePropsB", CfgKind::Float, &g_modulateProps[2] },
    { "enableViewmodelSway", CfgKind::Bool, &g_enableViewmodelSway },
    { "vmSwayScale", CfgKind::Float, &g_vmSwayScale },
    { "vmSwayInterp", CfgKind::Float, &g_vmSwayInterp },
    { "enableViewmodelOffset", CfgKind::Bool, &g_enableViewmodelOffset },
    { "vmOffsetX", CfgKind::Float, &g_vmOffsetX },
    { "vmOffsetY", CfgKind::Float, &g_vmOffsetY },
    { "vmOffsetZ", CfgKind::Float, &g_vmOffsetZ },
    { "vmOffsetSeparate", CfgKind::Bool, &g_vmOffsetSeparate },
    { "vmOffset2X", CfgKind::Float, &g_vmOffset2X },
    { "vmOffset2Y", CfgKind::Float, &g_vmOffset2Y },
    { "vmOffset2Z", CfgKind::Float, &g_vmOffset2Z },
    { "removeViewmodelBob", CfgKind::Bool, &g_removeViewmodelBob },
    { "vmCsBob", CfgKind::Bool, &g_vmCsBob },
    { "vmCsBobCycle", CfgKind::Float, &g_vmCsBobCycle },
    { "vmCsBobUp", CfgKind::Float, &g_vmCsBobUp },
    { "vmCsBobVert", CfgKind::Float, &g_vmCsBobVert },
    { "vmCsBobLat", CfgKind::Float, &g_vmCsBobLat },
    { "vmCsBobLower", CfgKind::Float, &g_vmCsBobLower },
    { "vmCsGunLower", CfgKind::Bool, &g_vmCsGunLower },
    { "vmCsGunLowerAngle", CfgKind::Float, &g_vmCsGunLowerAngle },
    { "vmCsGunLowerSpeed", CfgKind::Float, &g_vmCsGunLowerSpeed },
    { "vmCsLandDip", CfgKind::Bool, &g_vmCsLandDip },
    { "vmCsLandDipAmt", CfgKind::Float, &g_vmCsLandDipAmt },
    { "vmCsLandDipTime", CfgKind::Float, &g_vmCsLandDipTime },
    { "dlightEnable", CfgKind::Bool, &g_enableDLight },
    { "dlightUseSheenColor", CfgKind::Bool, &g_dlightUseSheenColor },
    { "dlightR", CfgKind::Float, &g_dlightColor[0], 0.f, 1.f },
    { "dlightG", CfgKind::Float, &g_dlightColor[1], 0.f, 1.f },
    { "dlightB", CfgKind::Float, &g_dlightColor[2], 0.f, 1.f },
    { "dlightRadius", CfgKind::Float, &g_dlightRadius, 10.f, 1000.f },
    { "vmWeaponChams", CfgKind::Bool, &g_enableVmWeaponChams },
    { "vmArmsChams", CfgKind::Bool, &g_enableVmArmsChams },
    { "vmWeaponGlowEn", CfgKind::Bool, &g_enableVmWeaponGlow },
    { "vmWeaponGlowR", CfgKind::Float, &g_vmWeaponGlowColor[0] },
    { "vmWeaponGlowG", CfgKind::Float, &g_vmWeaponGlowColor[1] },
    { "vmWeaponGlowB", CfgKind::Float, &g_vmWeaponGlowColor[2] },
    { "vmWeaponGlowA", CfgKind::Float, &g_vmWeaponGlowColor[3] },
    { "vmWeaponGlowSig", CfgKind::Float, &g_vmWeaponGlowSigma },
    { "vmWeaponGlowSten", CfgKind::Int, &g_vmWeaponGlowStencil },
    { "vmWeaponGlowSh", CfgKind::Bool, &g_vmWeaponGlowUseSheen },
    { "vmAlphaEnable", CfgKind::Bool, &g_vmAlphaEnable },
    { "vmAlpha", CfgKind::Float, &g_vmAlpha, 0.f, 1.f },
    { "vmFlatAlpha", CfgKind::Bool, &g_vmFlatAlpha },
    { "vmArmsGlowEn", CfgKind::Bool, &g_enableVmArmsGlow },
    { "vmArmsGlowR", CfgKind::Float, &g_vmArmsGlowColor[0] },
    { "vmArmsGlowG", CfgKind::Float, &g_vmArmsGlowColor[1] },
    { "vmArmsGlowB", CfgKind::Float, &g_vmArmsGlowColor[2] },
    { "vmArmsGlowA", CfgKind::Float, &g_vmArmsGlowColor[3] },
    { "vmArmsGlowSig", CfgKind::Float, &g_vmArmsGlowSigma },
    { "vmArmsGlowSten", CfgKind::Int, &g_vmArmsGlowStencil },
    { "vmArmsGlowSh", CfgKind::Bool, &g_vmArmsGlowUseSheen },
    { "plChams", CfgKind::Bool, &g_enablePlChams },
    { "plChamsLocal", CfgKind::Bool, &g_plChamsLocalPlayer },
    { "plChamsAttach", CfgKind::Bool, &g_plChamsAttachments },
    { "mapTextures", CfgKind::Bool, &g_enableMapTextures },
    { "flipWorld", CfgKind::Bool, &g_flipWorld },
    { "propChams", CfgKind::Bool, &g_enablePropChams },
    { "enableCrosshair", CfgKind::Bool, &g_enableCrosshair },
    { "staticAmbyCrosshair", CfgKind::Bool, &g_staticAmbyCrosshair },
    { "chUseSheenColor", CfgKind::Bool, &g_chUseSheenColor },
    { "chColorR", CfgKind::Float, &g_chColor[0] },
    { "chColorG", CfgKind::Float, &g_chColor[1] },
    { "chColorB", CfgKind::Float, &g_chColor[2] },
    { "chColorA", CfgKind::Float, &g_chColor[3] },
    { "chOutlineColorR", CfgKind::Float, &g_chOutlineColor[0] },
    { "chOutlineColorG", CfgKind::Float, &g_chOutlineColor[1] },
    { "chOutlineColorB", CfgKind::Float, &g_chOutlineColor[2] },
    { "chOutlineColorA", CfgKind::Float, &g_chOutlineColor[3] },
    { "chThickness", CfgKind::Float, &g_chThickness },
    { "chLength", CfgKind::Float, &g_chLength },
    { "chGap", CfgKind::Float, &g_chGap },
    { "chSidesOnly", CfgKind::Bool, &g_chSidesOnly },
    { "chDot", CfgKind::Bool, &g_chDot },
    { "chDotCircle", CfgKind::Bool, &g_chDotCircle },
    { "chOutline", CfgKind::Bool, &g_chOutline },
    { "chOutlineThick", CfgKind::Float, &g_chOutlineThick },
    { "chOutlineUseSheenColor", CfgKind::Bool, &g_chOutlineUseSheenColor },
    { "chOutlineSidesOnly", CfgKind::Bool, &g_chOutlineSidesOnly },
    { "chOutlineOnLines", CfgKind::Bool, &g_chOutlineOnLines },
    { "chOutlineOnDot", CfgKind::Bool, &g_chOutlineOnDot },
    { "chOutlineBlur", CfgKind::Bool, &g_chOutlineBlur },
    { "chOutlineBlurRadius", CfgKind::Float, &g_chOutlineBlurRadius },
    { "chAspect", CfgKind::Float, &g_chAspect },
    { "chGapAspect", CfgKind::Float, &g_chGapAspect },
    { "chDynamicMin", CfgKind::Float, &g_chDynamicMin },
    { "chDynamicMax", CfgKind::Float, &g_chDynamicMax },
    { "chDynamicRef", CfgKind::Float, &g_chDynamicRef },
    { "chAntialias", CfgKind::Bool, &g_chAntialias },
    { "chAdditive", CfgKind::Bool, &g_chAdditive },   // kept: old cfgs' additive
    { "chBlendMode", CfgKind::Int, &g_chBlendMode, 0.f, 121.f },   // last kChBlend id
    { "chGradient", CfgKind::Bool, &g_chGradient },
    { "chGradStart", CfgKind::Float, &g_chGradStart },
    { "chGradEnd", CfgKind::Float, &g_chGradEnd },
    { "chTaper", CfgKind::Bool, &g_chTaper },
    { "chTaperNear", CfgKind::Float, &g_chTaperNear },
    { "chTaperFar", CfgKind::Float, &g_chTaperFar },
    { "chDotCustomColor", CfgKind::Bool, &g_chDotCustomColor },
    { "chRotation", CfgKind::Float, &g_chRotation },
    { "enableEquipRegionUnlock", CfgKind::Bool, &g_enableEquipRegionUnlock },
    { "selfKillfeedOnly", CfgKind::Bool, &g_selfKillfeedOnly },
    { "deathNoticeTime", CfgKind::Float, &g_deathNoticeTime, 1.f, 60.f },
    { "noHats", CfgKind::Bool, &g_noHats },
    { "hwFootsteps", CfgKind::Bool, &g_hwFootsteps },
    { "hwFootColorR", CfgKind::Float, &g_hwFootColor[0] },
    { "hwFootColorG", CfgKind::Float, &g_hwFootColor[1] },
    { "hwFootColorB", CfgKind::Float, &g_hwFootColor[2] },
    { "hwFootUseSheenColor", CfgKind::Bool, &g_hwFootUseSheenColor },
    { "hwFootHideStealth", CfgKind::Bool, &g_hwFootHideStealth },
    { "hwForceHoliday", CfgKind::Bool, &g_hwForceHoliday },
    { "zombify", CfgKind::Bool, &g_zombify },
    { "ragdollEffectEnabled", CfgKind::Bool, &g_ragdollEffectEnabled },
    { "ragdollEffectBurning", CfgKind::Bool, &g_ragdollEffectBurning },
    { "ragdollEffectElectro", CfgKind::Bool, &g_ragdollEffectElectro },
    { "ragdollEffectAsh", CfgKind::Bool, &g_ragdollEffectAsh },
    { "ragdollEffectDissolve", CfgKind::Bool, &g_ragdollEffectDissolve },
    { "ragdollEffectGold", CfgKind::Bool, &g_ragdollEffectGold },
    { "ragdollEffectIce", CfgKind::Bool, &g_ragdollEffectIce },
    { "ragdollOnlyKills", CfgKind::Bool, &g_ragdollOnlyKills },
    { "accentColorR", CfgKind::Float, &g_accentColor[0] },
    { "accentColorG", CfgKind::Float, &g_accentColor[1] },
    { "accentColorB", CfgKind::Float, &g_accentColor[2] },
    { "menuBgColorR", CfgKind::Float, &g_menuBgColor[0] },
    { "menuBgColorG", CfgKind::Float, &g_menuBgColor[1] },
    { "menuBgColorB", CfgKind::Float, &g_menuBgColor[2] },
    { "menuTextColorR", CfgKind::Float, &g_menuTextColor[0] },
    { "menuTextColorG", CfgKind::Float, &g_menuTextColor[1] },
    { "menuTextColorB", CfgKind::Float, &g_menuTextColor[2] },
    { "enableSkins", CfgKind::Bool, &g_enableSkins },
    { "bgBlur", CfgKind::Bool, &g_bgBlur },
    { "bgBlurSigma", CfgKind::Float, &g_bgBlurSigma, 0.5f, 40.f },
    { "menuBgBlurSigma", CfgKind::Float, &g_menuBgBlurSigma, 0.5f, 40.f },
    { "bloomEnable", CfgKind::Bool, &g_bloomEnable },
    { "bloomIntensity", CfgKind::Float, &g_bloomIntensity, 0.f, 3.f },
    { "bloomThreshold", CfgKind::Float, &g_bloomThreshold, 0.f, 1.f },
    { "bloomRadius", CfgKind::Float, &g_bloomRadius, 0.f, 1.f },
    { "bloomSat", CfgKind::Float, &g_bloomSat, 0.f, 3.f },
    { "colorCorrEnable", CfgKind::Bool, &g_colorCorrEnable },
    { "colorCorrR", CfgKind::Float, &g_colorCorrR, 0.f, 2.f },
    { "colorCorrG", CfgKind::Float, &g_colorCorrG, 0.f, 2.f },
    { "colorCorrB", CfgKind::Float, &g_colorCorrB, 0.f, 2.f },
    { "colorCorrHue", CfgKind::Float, &g_colorCorrHue, -180.f, 180.f },
    { "colorCorrSat", CfgKind::Float, &g_colorCorrSat, 0.f, 3.f },
    { "colorCorrContrast", CfgKind::Float, &g_colorCorrContrast, 0.5f, 1.5f },
    { "colorCorrTemp", CfgKind::Float, &g_colorCorrTemp, -1.f, 1.f },
    { "colorCorrExposure", CfgKind::Float, &g_colorCorrExposure, -3.f, 3.f },
    { "colorCorrLevInBlack", CfgKind::Float, &g_colorCorrLevInBlack, 0.f, 0.99f },
    { "colorCorrLevInWhite", CfgKind::Float, &g_colorCorrLevInWhite, 0.01f, 1.f },
    { "colorCorrLevGamma", CfgKind::Float, &g_colorCorrLevGamma, 0.1f, 10.f },
    { "colorCorrLevOutBlack", CfgKind::Float, &g_colorCorrLevOutBlack, 0.f, 1.f },
    { "colorCorrLevOutWhite", CfgKind::Float, &g_colorCorrLevOutWhite, 0.f, 1.f },
    { "colorCorrHighlights", CfgKind::Float, &g_colorCorrHighlights, -1.f, 1.f },
    { "colorCorrShadows", CfgKind::Float, &g_colorCorrShadows, -1.f, 1.f },
    { "wmBoxShadow", CfgKind::Bool, &g_wmBoxShadow },
    { "wmBoxShOffX", CfgKind::Float, &g_wmBoxShadowOffX },
    { "wmBoxShOffY", CfgKind::Float, &g_wmBoxShadowOffY },
    { "wmBoxShBlur", CfgKind::Float, &g_wmBoxShadowBlur, 0.f, 40.f },
    { "mpBoxShadow", CfgKind::Bool, &g_mpBoxShadow },
    { "mpBoxShOffX", CfgKind::Float, &g_mpBoxShadowOffX },
    { "mpBoxShOffY", CfgKind::Float, &g_mpBoxShadowOffY },
    { "mpBoxShBlur", CfgKind::Float, &g_mpBoxShadowBlur, 0.f, 40.f },
    { "enableImageOverlay", CfgKind::Bool, &g_enableImageOverlay },
};

static void SaveTable(std::ofstream& file)
{
    for (const CfgEntry& e : k_cfgTable) {
        file << e.key << '=';
        switch (e.kind) {
        case CfgKind::Bool:  file << (*(bool*)e.ptr ? 1 : 0); break;
        case CfgKind::Int:   file << *(int*)e.ptr;            break;
        case CfgKind::Float: file << *(float*)e.ptr;          break;
        case CfgKind::Str:   file << *(std::string*)e.ptr;    break;
        }
        file << '\n';
    }
}

// False = not a table key. A malformed number leaves the setting as it was.
static bool LoadTable(const std::string& key, const std::string& val)
{
    static const std::unordered_map<std::string, const CfgEntry*> s_index = [] {
        std::unordered_map<std::string, const CfgEntry*> m;
        for (const CfgEntry& e : k_cfgTable) m.emplace(e.key, &e);
        return m;
    }();
    const auto it = s_index.find(key);
    if (it == s_index.end()) return false;
    const CfgEntry& e = *it->second;

    switch (e.kind) {
    case CfgKind::Bool:  *(bool*)e.ptr = CfgParseBool(val);                        break;
    case CfgKind::Int:   CfgParseInt(val, e.lo, e.hi, *(int*)e.ptr);                break;
    case CfgKind::Float: CfgParseFloat(val, e.lo, e.hi, *(float*)e.ptr);            break;
    case CfgKind::Str:   *(std::string*)e.ptr = val;                               break;
    }
    return true;
}

void SaveConfig(const std::string& path) {
    _mkdir(CONFIG_DIR);
    std::ofstream file(path);
    if (!file) return;
    SaveTable(file);

    file << "mpCopyWmStyle="       << (g_mpCopyWmStyle   ? 1 : 0)       << "\n";
    file << "mpBgDraw="            << (g_mpBgDraw        ? 1 : 0)       << "\n";
    file << "mpBgCornerFlags="     << g_mpBgCornerFlags                 << "\n";
    // Outline layers (layer 0 also written as legacy keys for back-compat)
    file << "mpOutlineLayerCount=" << g_mpOutlineLayerCount             << "\n";
    { char k[64];
      for (int i = 0; i < g_mpOutlineLayerCount; ++i) {
        const auto& L = g_mpOutlineLayers[i];
        sprintf(k,"mpOL%dEn=",  i); file << k << (L.enable?1:0)   << "\n";
        sprintf(k,"mpOL%dT=",   i); file << k << L.thick           << "\n";
        sprintf(k,"mpOL%dSF=",  i); file << k << L.sideFlags       << "\n";
        sprintf(k,"mpOL%dR=",   i); file << k << L.color[0]        << "\n";
        sprintf(k,"mpOL%dG=",   i); file << k << L.color[1]        << "\n";
        sprintf(k,"mpOL%dB=",   i); file << k << L.color[2]        << "\n";
        sprintf(k,"mpOL%dA=",   i); file << k << L.color[3]        << "\n";
        sprintf(k,"mpOL%dBl=",  i); file << k << L.blur            << "\n";
      }
    }
    // Legacy layer-0 keys so old configs round-trip
    file << "mpOutline="           << (g_mpOutline       ? 1 : 0)       << "\n";
    file << "mpOutlineThick="      << g_mpOutlineThick                  << "\n";
    file << "mpOutlineSideFlags="  << g_mpOutlineSideFlags              << "\n";
    file << "mpOutR="              << g_mpOutlineColor[0]               << "\n";
    file << "mpOutG="              << g_mpOutlineColor[1]               << "\n";
    file << "mpOutB="              << g_mpOutlineColor[2]               << "\n";
    file << "mpOutA="              << g_mpOutlineColor[3]               << "\n";
    file << "mpFontFile="          << g_mpFontFile                      << "\n";
    { int ff = 0; for (int i = 0; i < 6; i++) if (g_mpFontFlags[i]) ff |= (1<<i);
      file << "mpFontFlags="       << ff                                << "\n"; }
    { static const char* pfx[4]={"mpBgGradL","mpBgGradR","mpBgGradT","mpBgGradB"};
      for(int i=0;i<4;++i) { const auto& g=g_mpBgGrad[i];
        file<<pfx[i]<<"En="<<(g.enable?1:0)<<"\n";
        file<<pfx[i]<<"S="<<g.start<<"\n"<<pfx[i]<<"E="<<g.end<<"\n"; } }
    { static const char* pfx[4]={"wmBgGradL","wmBgGradR","wmBgGradT","wmBgGradB"};
      for(int i=0;i<4;++i) { const auto& g=g_wmBgGrad[i];
        file<<pfx[i]<<"En="<<(g.enable?1:0)<<"\n";
        file<<pfx[i]<<"S="<<g.start<<"\n"<<pfx[i]<<"E="<<g.end<<"\n"; } }
    file << "menuFontFile="        << g_menuFontFile                    << "\n";
    { int ff = 0; for (int i = 0; i < 6; i++) if (g_menuFontFlags[i]) ff |= (1<<i);
      file << "menuFontFlags="     << ff                                << "\n"; }

    // net graph colours
    { const char* sfx = "RGBA";
      for (int i = 0; i < 4; ++i) file << "ngText"   << sfx[i] << "=" << g_ngTextColor[i]   << "\n";
      for (int i = 0; i < 4; ++i) file << "ngOrange" << sfx[i] << "=" << g_ngOrangeColor[i] << "\n";
      for (int i = 0; i < 4; ++i) file << "ngRed"    << sfx[i] << "=" << g_ngRedColor[i]    << "\n";
      for (int i = 0; i < 4; ++i) file << "ngYellow" << sfx[i] << "=" << g_ngYellowColor[i] << "\n";
    }
    file << "customLabel="          << g_customLabel                    << "\n";
    file << "titleCycleCount="      << g_titleCount                     << "\n";
    for (int i = 0; i < k_maxTitleCycle; ++i)
        file << "titleCycle" << i << "=" << g_titleList[i].text        << "\n";
    file << "wmSeparator="          << g_wmSeparator                     << "\n";
    file << "menuKey="              << g_menuKey                         << "\n";
    file << "wmFontFile="           << g_wmFontFile                      << "\n";
    { int ff = 0; for (int i = 0; i < 6; i++) if (g_wmFontFlags[i]) ff |= (1 << i);
      file << "wmFontFlags="        << ff                                << "\n"; }
    file << "wmLogoImgPath="       << g_wmLogoImgPath                   << "\n";
    file << "wmBgCornerFlags="      << g_wmBgCornerFlags                 << "\n";
    file << "wmOutlineLayerCount="  << g_wmOutlineLayerCount             << "\n";
    { char k[64];
      for (int i = 0; i < g_wmOutlineLayerCount; ++i) {
        const auto& L = g_wmOutlineLayers[i];
        sprintf(k,"wmOL%dEn=",  i); file << k << (L.enable?1:0)   << "\n";
        sprintf(k,"wmOL%dT=",   i); file << k << L.thick           << "\n";
        sprintf(k,"wmOL%dSF=",  i); file << k << L.sideFlags       << "\n";
        sprintf(k,"wmOL%dR=",   i); file << k << L.color[0]        << "\n";
        sprintf(k,"wmOL%dG=",   i); file << k << L.color[1]        << "\n";
        sprintf(k,"wmOL%dB=",   i); file << k << L.color[2]        << "\n";
        sprintf(k,"wmOL%dA=",   i); file << k << L.color[3]        << "\n";
        sprintf(k,"wmOL%dBl=",  i); file << k << L.blur            << "\n";
      }
    }
    // Legacy layer-0 keys
    file << "wmOutline="            << (g_wmOutline            ? 1 : 0)  << "\n";
    file << "wmOutlineThick="       << g_wmOutlineThick                  << "\n";
    file << "wmOutlineSideFlags="   << g_wmOutlineSideFlags              << "\n";
    file << "wmOutlineR="           << g_wmOutlineColor[0]               << "\n";
    file << "wmOutlineG="           << g_wmOutlineColor[1]               << "\n";
    file << "wmOutlineB="           << g_wmOutlineColor[2]               << "\n";
    file << "wmOutlineA="           << g_wmOutlineColor[3]               << "\n";
    file << "rainMode="             << g_rainMode                        << "\n";
    file << "snowSpriteName="       << g_snowSpriteName                  << "\n";
    for (int i = 0; i < 8; ++i) {
        file << "particleLayerZ"    << i << "=" << g_particleLayerZ[i]    << "\n";
        file << "particleLayerName" << i << "=" << g_particleLayerName[i] << "\n";
    }
    file << "nameCycleCount="      << g_nameCount                       << "\n";
    for (int i = 0; i < k_maxNames; ++i)
        file << "nameCycle" << i << "=" << g_nameList[i].text           << "\n";
    file << "teamLabelCycleCount="     << g_teamLabelCount                    << "\n";
    for (int i = 0; i < k_maxNames; ++i)
        file << "teamLabelCycle" << i << "=" << g_teamLabelList[i].text       << "\n";
    file << "enemyLabelCycleCount="    << g_enemyLabelCount                   << "\n";
    for (int i = 0; i < k_maxNames; ++i)
        file << "enemyLabelCycle" << i << "=" << g_enemyLabelList[i].text     << "\n";
    file << "everyoneLabelCycleCount="  << g_everyoneLabelCount               << "\n";
    for (int i = 0; i < k_maxNames; ++i)
        file << "everyoneLabelCycle" << i << "=" << g_everyoneLabelList[i].text << "\n";
    for (int i = 0; i < 4; i++) {
        file << "sheenColor" << i << "R=" << g_sheenColors[i][0] << "\n";
        file << "sheenColor" << i << "G=" << g_sheenColors[i][1] << "\n";
        file << "sheenColor" << i << "B=" << g_sheenColors[i][2] << "\n";
    }
    file << "fogLinkSkybox="        << (int)g_fogLinkSkybox              << "\n";
    file << "skyboxName="           << g_skyboxName                      << "\n";
    // ---- Viewmodel Chams ---------------------------------------
    file << "vmWeaponChamCount="    << g_vmWeaponChamCount                << "\n";
    for (int i = 0; i < k_vmMaxLayers; ++i) {
        char key[48];
        sprintf(key, "vmWC%dType=",    i); file << key << g_vmWeaponChamLayers[i].matType   << "\n";
        sprintf(key, "vmWC%dR=",       i); file << key << g_vmWeaponChamLayers[i].color[0]  << "\n";
        sprintf(key, "vmWC%dG=",       i); file << key << g_vmWeaponChamLayers[i].color[1]  << "\n";
        sprintf(key, "vmWC%dB=",       i); file << key << g_vmWeaponChamLayers[i].color[2]  << "\n";
        sprintf(key, "vmWC%dA=",       i); file << key << g_vmWeaponChamLayers[i].color[3]  << "\n";
        sprintf(key, "vmWC%dSheen=",   i); file << key << (g_vmWeaponChamLayers[i].useSheen ? 1 : 0) << "\n";
        sprintf(key, "vmWC%dIgnoreZ=", i); file << key << (g_vmWeaponChamLayers[i].ignorez  ? 1 : 0) << "\n";
    }
    file << "vmArmsChamCount="      << g_vmArmsChamCount                  << "\n";
    for (int i = 0; i < k_vmMaxLayers; ++i) {
        char key[48];
        sprintf(key, "vmAC%dType=",    i); file << key << g_vmArmsChamLayers[i].matType     << "\n";
        sprintf(key, "vmAC%dR=",       i); file << key << g_vmArmsChamLayers[i].color[0]    << "\n";
        sprintf(key, "vmAC%dG=",       i); file << key << g_vmArmsChamLayers[i].color[1]    << "\n";
        sprintf(key, "vmAC%dB=",       i); file << key << g_vmArmsChamLayers[i].color[2]    << "\n";
        sprintf(key, "vmAC%dA=",       i); file << key << g_vmArmsChamLayers[i].color[3]    << "\n";
        sprintf(key, "vmAC%dSheen=",   i); file << key << (g_vmArmsChamLayers[i].useSheen  ? 1 : 0) << "\n";
        sprintf(key, "vmAC%dIgnoreZ=", i); file << key << (g_vmArmsChamLayers[i].ignorez   ? 1 : 0) << "\n";
    }
    file << "plChamCount="          << g_plChamCount                     << "\n";
    for (int i = 0; i < k_vmMaxLayers; ++i) {
        char key[48];
        sprintf(key, "plC%dType=",  i); file << key << g_plChamLayers[i].matType    << "\n";
        sprintf(key, "plC%dR=",     i); file << key << g_plChamLayers[i].color[0]   << "\n";
        sprintf(key, "plC%dG=",     i); file << key << g_plChamLayers[i].color[1]   << "\n";
        sprintf(key, "plC%dB=",     i); file << key << g_plChamLayers[i].color[2]   << "\n";
        sprintf(key, "plC%dA=",     i); file << key << g_plChamLayers[i].color[3]   << "\n";
        sprintf(key, "plC%dSheen=", i); file << key << (g_plChamLayers[i].useSheen  ? 1 : 0) << "\n";
    }
    for (int i = 0; i < k_mapTexCatCount; ++i) {
        char key[48];
        sprintf(key, "mtC%dPreset=", i); file << key << g_mapTexCats[i].preset  << "\n";
        sprintf(key, "mtC%dR=",      i); file << key << g_mapTexCats[i].tint[0] << "\n";
        sprintf(key, "mtC%dG=",      i); file << key << g_mapTexCats[i].tint[1] << "\n";
        sprintf(key, "mtC%dB=",      i); file << key << g_mapTexCats[i].tint[2] << "\n";
        sprintf(key, "mtC%dA=",      i); file << key << g_mapTexCats[i].tint[3] << "\n";
        sprintf(key, "mtC%dTex=",    i); file << key << g_mapTexCats[i].custom  << "\n";
    }
    file << "propChamCount="        << g_propChamCount                   << "\n";
    for (int i = 0; i < k_vmMaxLayers; ++i) {
        char key[48];
        sprintf(key, "prC%dType=",  i); file << key << g_propChamLayers[i].matType    << "\n";
        sprintf(key, "prC%dR=",     i); file << key << g_propChamLayers[i].color[0]   << "\n";
        sprintf(key, "prC%dG=",     i); file << key << g_propChamLayers[i].color[1]   << "\n";
        sprintf(key, "prC%dB=",     i); file << key << g_propChamLayers[i].color[2]   << "\n";
        sprintf(key, "prC%dA=",     i); file << key << g_propChamLayers[i].color[3]   << "\n";
        sprintf(key, "prC%dSheen=", i); file << key << (g_propChamLayers[i].useSheen  ? 1 : 0) << "\n";
    }
    for (int i = 0; i < 4; i++)
        file << "chSide" << i << "=" << (g_chSides[i] ? 1 : 0) << "\n";
    for (int i = 0; i < 4; ++i)
        file << "chOutlineSide" << i << "=" << (g_chOutlineSides[i] ? 1 : 0) << "\n";
    file << "chDynamic="            << (g_chDynamic ? 1 : 0)            << "\n";
    for (int i=0;i<4;i++) file << "chGradColorCenter" << i << "=" << g_chGradColorCenter[i] << "\n";
    for (int i=0;i<4;i++) file << "chGradColorOuter"  << i << "=" << g_chGradColorOuter[i]  << "\n";
    for (int i=0;i<4;i++) file << "chDotColor" << i << "=" << g_chDotColor[i] << "\n";
    for (int i=0;i<4;i++) file << "chCsgoColor" << i << "=" << g_chCsgoColor[i] << "\n";
    // ---- Custom killfeed ---------------------------------------
    for (int i=0;i<4;i++) file << "kfBgColor"   << i << "=" << g_kfBgColor[i]   << "\n";
    for (int i=0;i<4;i++) file << "kfTextColor" << i << "=" << g_kfTextColor[i] << "\n";
    for (int i=0;i<4;i++) file << "kfRedColor"  << i << "=" << g_kfRedColor[i]  << "\n";
    for (int i=0;i<4;i++) file << "kfBluColor"  << i << "=" << g_kfBluColor[i]  << "\n";
    for (int i=0;i<4;i++) file << "kfBoxShadowColor" << i << "=" << g_kfBoxShadowColor[i] << "\n";
    file << "kfOutlineLayerCount="  << g_kfOutlineLayerCount              << "\n";
    { char k[64];
      for (int i = 0; i < g_kfOutlineLayerCount; ++i) {
        const auto& L = g_kfOutlineLayers[i];
        sprintf(k,"kfOL%dEn=",  i); file << k << (L.enable?1:0) << "\n";
        sprintf(k,"kfOL%dT=",   i); file << k << L.thick         << "\n";
        sprintf(k,"kfOL%dSF=",  i); file << k << L.sideFlags     << "\n";
        sprintf(k,"kfOL%dR=",   i); file << k << L.color[0]      << "\n";
        sprintf(k,"kfOL%dG=",   i); file << k << L.color[1]      << "\n";
        sprintf(k,"kfOL%dB=",   i); file << k << L.color[2]      << "\n";
        sprintf(k,"kfOL%dA=",   i); file << k << L.color[3]      << "\n";
        sprintf(k,"kfOL%dBl=",  i); file << k << L.blur          << "\n";
      }
    }
    file << "kfFontFile="           << g_kfFontFile                       << "\n";
    { int ff=0; for(int i=0;i<4;i++) if(g_kfFontFlags[i]) ff|=(1<<i);
      file << "kfFontFlags="        << ff                                  << "\n"; }
    for (int i=0;i<4;i++) file << "kfIconColor" << i << "=" << g_kfIconColor[i] << "\n";
    for (int i=0;i<4;i++) file << "kfTextDropShadowColor" << i << "=" << g_kfTextDropShadowColor[i] << "\n";
    for (int i=0;i<4;i++) file << "kfTextOutlineColor" << i << "=" << g_kfTextOutlineColor[i] << "\n";
    for (int i=0;i<3;i++) file << "kfBlobColor" << i << "=" << g_kfBlobColor[i] << "\n";
    for (int i=0;i<4;i++) file << "kfPrecipColor" << i << "=" << g_kfPrecipColor[i] << "\n";
    for (int i=0;i<4;i++) file << "kfKillGlowColor" << i << "=" << g_kfKillGlowColor[i] << "\n";
    // ---- end custom killfeed -----------------------------------
    file << "wpnHudEnable="         << (g_enableWeaponHud ? 1 : 0)   << "\n";
    file << "wpnHudShowNum="      << (g_wpnHudShowNum ? 1 : 0)    << "\n";
    file << "wpnHudFlipIcons="    << (g_wpnHudFlipIcons   ? 1 : 0) << "\n";
    file << "wpnHudGlowEnable="   << (g_wpnHudGlowEnabled ? 1 : 0) << "\n";
    { char k[32];
      for (int i = 0; i < 8; ++i) {
        sprintf(k,"wpnHudGlowCR%d=",i); file << k << g_wpnHudGlowColors[i][0] << "\n";
        sprintf(k,"wpnHudGlowCG%d=",i); file << k << g_wpnHudGlowColors[i][1] << "\n";
        sprintf(k,"wpnHudGlowCB%d=",i); file << k << g_wpnHudGlowColors[i][2] << "\n";
        sprintf(k,"wpnHudGlowCA%d=",i); file << k << g_wpnHudGlowColors[i][3] << "\n";
      }
    }
    file << "wpnHudNameEnable="  << (g_enableWpnHudName ? 1 : 0) << "\n";
    file << "wpnHudNumFontFile=" << g_wpnHudNumFontFile           << "\n";
    { int ff=0; for(int i=0;i<4;i++) if(g_wpnHudNumFontFlags[i]) ff|=(1<<i);
      file << "wpnHudNumFontFlags=" << ff                         << "\n"; }
    file << "wpnHudNameFontFile="<< g_wpnHudNameFontFile          << "\n";
    { int ff=0; for(int i=0;i<4;i++) if(g_wpnHudNameFontFlags[i]) ff|=(1<<i);
      file << "wpnHudNameFontFlags=" << ff                        << "\n"; }
    file << "wpnHudNameShadow="   << (g_wpnHudNameShadowEnabled  ? 1 : 0) << "\n";
    file << "wpnHudNameOutline="  << (g_wpnHudNameOutlineEnabled ? 1 : 0) << "\n";
    file << "wpnHudNumShadow="    << (g_wpnHudNumShadowEnabled   ? 1 : 0) << "\n";
    file << "wpnHudNumOutline="   << (g_wpnHudNumOutlineEnabled  ? 1 : 0) << "\n";
    file << "wpnHudActiveScaleSmooth=" << (g_wpnHudActiveScaleSmooth ? 1 : 0)   << "\n";
    file << "wpnHudSepEnable="         << (g_wpnHudSepEnabled   ? 1 : 0)        << "\n";
    file << "wpnHudCustomNames="  << (g_wpnHudCustomNames  ? 1 : 0)       << "\n";
    for (int i = 0; i < 5; ++i) {
        char k[32]; snprintf(k, sizeof(k), "wpnHudCName%d=", i);
        file << k << g_wpnHudCustomName[i] << "\n";
    }
    file << "wpnHudFade="      << (g_wpnHudFade ? 1 : 0)  << "\n";
    file << "wpnHudBgEnable="        << (g_wpnHudBgEnabled ? 1 : 0)          << "\n";
    file << "wpnHudBgFadeNear="     << (g_wpnHudBgFadeNear ? 1 : 0)           << "\n";
    file << "wpnHudBgScreenEdge="   << (g_wpnHudBgScreenEdge ? 1 : 0)         << "\n";
    file << "wpnHudBgBorderEnable="  << (g_wpnHudBgBorderEnabled ? 1 : 0)    << "\n";
    // ---- Speedometer -------------------------------------------
    file << "speedoFontFile="       << g_speedoFontFile                  << "\n";
    { int ff=0; for(int i=0;i<6;i++) if(g_speedoFontFlags[i]) ff|=(1<<i);
      file << "speedoFontFlags="    << ff                                << "\n"; }
    for(int i=0;i<4;i++) file << "speedoColorLow"  << i << "=" << g_speedoColorLow[i]  << "\n";
    for(int i=0;i<4;i++) file << "speedoColorMid"  << i << "=" << g_speedoColorMid[i]  << "\n";
    for(int i=0;i<4;i++) file << "speedoColorHigh" << i << "=" << g_speedoColorHigh[i] << "\n";
    for(int i=0;i<4;i++) file << "speedoOutlineColor" << i << "=" << g_speedoOutlineColor[i] << "\n";
    for(int i=0;i<4;i++) file << "speedoShadowColor" << i << "=" << g_speedoShadowColor[i] << "\n";
    for(int i=0;i<4;i++) file << "speedoGraphColor" << i << "=" << g_speedoGraphColor[i] << "\n";
    // ---- end speedometer ---------------------------------------
    file << "zombifyMode="          << g_zombifyMode                     << "\n";
    for (auto& [id, cfg] : g_skinCfgs) {
        if (SkinCfgIsDefault(cfg)) continue;   // every weapon has an entry; only touched ones are saved
        std::string p = "skin_" + std::to_string(id) + "_";
        file << p << "enabled="    << (cfg.enabled    ? 1 : 0) << "\n";
        file << p << "paintKitId=" << cfg.paintKitId            << "\n";
        file << p << "pattern="    << cfg.pattern               << "\n";
        file << p << "wear="       << cfg.wear                  << "\n";
        file << p << "ksTier="     << cfg.ksTier                << "\n";
        file << p << "ksSheen="    << cfg.ksSheen               << "\n";
        file << p << "ksEffect="   << cfg.ksEffect              << "\n";
        file << p << "ksUseSheenColor=" << (cfg.ksUseSheenColor ? 1 : 0) << "\n";
        file << p << "ksCustomColor=" << (cfg.ksCustomColor ? 1 : 0) << "\n";
        file << p << "ksColorR="   << cfg.ksColor[0]            << "\n";
        file << p << "ksColorG="   << cfg.ksColor[1]            << "\n";
        file << p << "ksColorB="   << cfg.ksColor[2]            << "\n";
        file << p << "unusual="    << cfg.unusual               << "\n";
        file << p << "australium=" << (cfg.australium ? 1 : 0) << "\n";
        file << p << "festivized=" << (cfg.festivized ? 1 : 0) << "\n";
        file << p << "festive="    << (cfg.festive    ? 1 : 0) << "\n";
        file << p << "statClock="      << (cfg.statClock  ? 1 : 0)  << "\n";
        file << p << "statKills="      << cfg.statKills               << "\n";
        file << p << "spellFlags="     << (int)cfg.spellFlags         << "\n";
        file << p << "weaponSkinDef="  << cfg.weaponSkinDef           << "\n";
        file << p << "reskinDef="      << cfg.reskinDef               << "\n";
        file << p << "reskinStyle="    << cfg.reskinStyle             << "\n";
    }

    // ---- Background blur ---------------------------------------
    file << "menuBgBlur="          << (g_menuBgBlur ? 1 : 0) << "\n";

    // ---- Color correction --------------------------------------
    // tone curves as "x,y;x,y;..." (old configs had 5 fixed-x ccCurveOff* offsets, still loaded)
    file << "ccCurveM=" << CcCurve_ToString(g_ccCurve[0]) << "\n";
    file << "ccCurveR=" << CcCurve_ToString(g_ccCurve[1]) << "\n";
    file << "ccCurveG=" << CcCurve_ToString(g_ccCurve[2]) << "\n";
    file << "ccCurveB=" << CcCurve_ToString(g_ccCurve[3]) << "\n";

    // ---- Box shadow --------------------------------------------
    for (int i=0;i<4;i++) file << "wmBoxShColor" << i << "=" << g_wmBoxShadowColor[i] << "\n";
    for (int i=0;i<4;i++) file << "mpBoxShColor" << i << "=" << g_mpBoxShadowColor[i] << "\n";

    // ---- Image overlay -----------------------------------------
    for (int i = 0; i < k_imgMaxSlots; ++i) {
        const auto& sl = g_imgSlots[i];
        char p2[32]; sprintf(p2, "img%d_", i);
        file << p2 << "enabled="    << (sl.enabled ? 1 : 0) << "\n";
        file << p2 << "path="       << sl.path               << "\n";
        file << p2 << "scale="      << sl.scale              << "\n";
        file << p2 << "rotation="   << sl.rotation           << "\n";
        file << p2 << "posX="       << sl.posX               << "\n";
        file << p2 << "posY="       << sl.posY               << "\n";
        file << p2 << "alpha="      << sl.alpha              << "\n";
        file << p2 << "anchor="     << sl.anchor             << "\n";
        file << p2 << "anchorOffX=" << sl.anchorOffX         << "\n";
        file << p2 << "anchorOffY=" << sl.anchorOffY         << "\n";
        file << p2 << "pixelArt="  << (sl.pixelArt ? 1 : 0) << "\n";
    }

    // ---- Alias list --------------------------------------------
    AliasLockGuard aliasLk;
    // Save all groups
    file << "playerGroupCount=" << (int)g_playerGroups.size() << "\n";
    for (int gi = 0; gi < (int)g_playerGroups.size(); ++gi)
        file << "playerGroup" << gi << "=" << g_playerGroups[gi] << "\n";
    // Save alias entries
    file << "aliasCount=" << (int)g_aliases.size() << "\n";
    int aliasIdx = 0;
    for (auto& [s64, ae] : g_aliases) {
        char p[32]; sprintf(p, "alias%d_", aliasIdx++);
        file << p << "steam64="      << s64                                   << "\n";
        file << p << "name="         << ae.displayName                        << "\n";
        file << p << "notify="       << (ae.notifyOnServer          ? 1 : 0)  << "\n";
        file << p << "changeName="   << (ae.changeNameInScoreboard  ? 1 : 0)  << "\n";
        file << p << "groupMask="     << ae.groupMask                          << "\n";
    }

    file.close();
}

static void ParseOLKey(const std::string& sf, const std::string& val, OutlineLayer& L, bool bv) {
    if      (sf=="En")   L.enable    = bv;
    else if (sf=="T")   L.thick     = std::clamp(std::stof(val),0.5f,12.f);
    else if (sf=="SF")  L.sideFlags = std::stoi(val) & 15;
    else if (sf=="R")   L.color[0]  = std::clamp(std::stof(val),0.f,1.f);
    else if (sf=="G")   L.color[1]  = std::clamp(std::stof(val),0.f,1.f);
    else if (sf=="B")   L.color[2]  = std::clamp(std::stof(val),0.f,1.f);
    else if (sf=="A")   L.color[3]  = std::clamp(std::stof(val),0.f,1.f);
    else if (sf=="Bl")  L.blur      = std::clamp(std::stof(val),0.f,40.f);
}

// "ngText<C>" / "ngOrange<C>" / "ngRed<C>" / "ngYellow<C>" where <C> is one of RGBA.
// Returns false for anything that isn't one of those, so the caller can fall
// through to the rest of the key chain.
static bool ParseNetGraphKey(const std::string& key, const std::string& val) {
    float* dst = nullptr;
    size_t n   = 0;
    if      (key.rfind("ngText",   0) == 0) { dst = g_ngTextColor;   n = 6; }
    else if (key.rfind("ngOrange", 0) == 0) { dst = g_ngOrangeColor; n = 8; }
    else if (key.rfind("ngRed",    0) == 0) { dst = g_ngRedColor;    n = 5; }
    else if (key.rfind("ngYellow", 0) == 0) { dst = g_ngYellowColor; n = 8; }
    if (!dst || key.size() != n + 1) return false;

    int ch;
    switch (key[n]) {
        case 'R': ch = 0; break;
        case 'G': ch = 1; break;
        case 'B': ch = 2; break;
        case 'A': ch = 3; break;
        default:  return false;
    }
    dst[ch] = std::clamp(std::stof(val), 0.f, 1.f);
    return true;
}

// The csgo crosshair's colour keys. Split out of LoadConfig's else-if chain -- MSVC caps
// block nesting (C1061) and that chain is already at it. Returns false for anything else
// so the caller falls through to the rest.
static bool ParseCsgoCrosshairKey(const std::string& key, const std::string& val) {
    if (key.rfind("chCsgo", 0) != 0) return false;

    if (key == "chCsgoColor0")   g_chCsgoColor[0] = std::stof(val);
    else if (key == "chCsgoColor1")   g_chCsgoColor[1] = std::stof(val);
    else if (key == "chCsgoColor2")   g_chCsgoColor[2] = std::stof(val);
    else if (key == "chCsgoColor3")   g_chCsgoColor[3] = std::stof(val);
    else return false;
    return true;
}

// The skin config fields, out of line. They used to be an else-if chain inside LoadConfig's
// own else-if chain, and MSVC hit its nesting limit (C1061) when it grew.
static void ParseSkinField(SkinCfg& cfg, const std::string& field,
                           const std::string& val, bool bval)
{
    if      (field == "enabled")    cfg.enabled    = bval;
    else if (field == "paintKitId") cfg.paintKitId = std::stoi(val);
    else if (field == "pattern")    cfg.pattern    = (uint64_t)std::stoull(val);
    else if (field == "wear")       cfg.wear       = std::stof(val);
    else if (field == "ksTier")     cfg.ksTier     = std::stoi(val);
    else if (field == "ksSheen")    cfg.ksSheen    = std::stoi(val);
    else if (field == "ksEffect")   cfg.ksEffect   = std::stoi(val);
    else if (field == "ksUseSheenColor") cfg.ksUseSheenColor = bval;
    else if (field == "ksCustomColor") cfg.ksCustomColor = bval;
    else if (field == "ksColorR")   cfg.ksColor[0] = std::stof(val);
    else if (field == "ksColorG")   cfg.ksColor[1] = std::stof(val);
    else if (field == "ksColorB")   cfg.ksColor[2] = std::stof(val);
    else if (field == "unusual")    cfg.unusual    = std::stoi(val);
    else if (field == "australium") cfg.australium = bval;
    else if (field == "festivized") cfg.festivized = bval;
    else if (field == "festive")    cfg.festive    = bval;
    else if (field == "statClock")     cfg.statClock     = bval;
    else if (field == "statKills")     cfg.statKills     = std::stoi(val);
    else if (field == "spellFlags")    cfg.spellFlags    = (uint8_t)std::stoi(val);
    else if (field == "weaponSkinDef") cfg.weaponSkinDef = std::stoi(val);
    else if (field == "reskinDef")     cfg.reskinDef     = std::stoi(val);
    else if (field == "reskinStyle")   cfg.reskinStyle   = std::stoi(val);
    else if (field == "spell") { // legacy migration
        int old = std::stoi(val);
        if      (old == 1) cfg.spellFlags |= kCfgSpell_Exorcism;
        else if (old == 2) cfg.spellFlags |= kCfgSpell_Pumpkin;
        else if (old == 3) cfg.spellFlags |= kCfgSpell_Fire;
    }
}

static void LoadConfigImpl(const std::string& path) {
    SkinCfgs_Init();
    std::ifstream file(path);
    if (!file) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        bool keepWS = (key == "customLabel" || key == "wmSeparator");
        if (!keepWS) {
            val.erase(0, val.find_first_not_of(" \t"));
            size_t last = val.find_last_not_of(" \t");
            if (last != std::string::npos) val.erase(last + 1);
            else val.clear();
        }

        auto b = [&](){ return val == "1" || val == "true"; };

        if (LoadTable(key, val)) continue;

        try {

        // ---- Speedometer keys -- handled early to avoid else-if depth limit ----
        if (key.size() > 6 && key.compare(0, 6, "speedo") == 0) {
            auto cf = [&](float& f, float lo, float hi){ f = std::clamp(std::stof(val), lo, hi); };
            auto cf0 = [&](float& f){ f = std::clamp(std::stof(val), 0.f, 1.f); };
            if (key == "speedoFontFile") {
                g_speedoFontFile  = val;
                g_speedoFontIndex = FontIndexForFile(val);
            }
            else if (key == "speedoFontFlags")     { int ff=std::stoi(val); for(int i=0;i<6;i++) g_speedoFontFlags[i]=(ff>>i)&1; }
            else if (key == "speedoColorLow0")     cf0(g_speedoColorLow[0]);
            else if (key == "speedoColorLow1")     cf0(g_speedoColorLow[1]);
            else if (key == "speedoColorLow2")     cf0(g_speedoColorLow[2]);
            else if (key == "speedoColorLow3")     cf0(g_speedoColorLow[3]);
            else if (key == "speedoColorMid0")     cf0(g_speedoColorMid[0]);
            else if (key == "speedoColorMid1")     cf0(g_speedoColorMid[1]);
            else if (key == "speedoColorMid2")     cf0(g_speedoColorMid[2]);
            else if (key == "speedoColorMid3")     cf0(g_speedoColorMid[3]);
            else if (key == "speedoColorHigh0")    cf0(g_speedoColorHigh[0]);
            else if (key == "speedoColorHigh1")    cf0(g_speedoColorHigh[1]);
            else if (key == "speedoColorHigh2")    cf0(g_speedoColorHigh[2]);
            else if (key == "speedoColorHigh3")    cf0(g_speedoColorHigh[3]);
            else if (key == "speedoOutlineColor0") cf0(g_speedoOutlineColor[0]);
            else if (key == "speedoOutlineColor1") cf0(g_speedoOutlineColor[1]);
            else if (key == "speedoOutlineColor2") cf0(g_speedoOutlineColor[2]);
            else if (key == "speedoOutlineColor3") cf0(g_speedoOutlineColor[3]);
            else if (key == "speedoShadowColor0")  cf0(g_speedoShadowColor[0]);
            else if (key == "speedoShadowColor1")  cf0(g_speedoShadowColor[1]);
            else if (key == "speedoShadowColor2")  cf0(g_speedoShadowColor[2]);
            else if (key == "speedoShadowColor3")  cf0(g_speedoShadowColor[3]);
            else if (key == "speedoGraphColor0")   cf0(g_speedoGraphColor[0]);
            else if (key == "speedoGraphColor1")   cf0(g_speedoGraphColor[1]);
            else if (key == "speedoGraphColor2")   cf0(g_speedoGraphColor[2]);
            else if (key == "speedoGraphColor3")   cf0(g_speedoGraphColor[3]);
            continue; // skip main else-if chain
        }
        // ---- end speedometer keys ----------------------------------
        // ---- Custom killfeed keys -- handled early to avoid else-if depth limit ----
        if (key.size() > 2 && key.compare(0, 2, "kf") == 0) {
            auto cf0 = [&](float& f){ f = std::clamp(std::stof(val), 0.f, 1.f); };
            if (key == "kfBgColor0")          cf0(g_kfBgColor[0]);
            else if (key == "kfBgColor1")          cf0(g_kfBgColor[1]);
            else if (key == "kfBgColor2")          cf0(g_kfBgColor[2]);
            else if (key == "kfBgColor3")          cf0(g_kfBgColor[3]);
            else if (key == "kfTextColor0")        cf0(g_kfTextColor[0]);
            else if (key == "kfTextColor1")        cf0(g_kfTextColor[1]);
            else if (key == "kfTextColor2")        cf0(g_kfTextColor[2]);
            else if (key == "kfTextColor3")        cf0(g_kfTextColor[3]);
            else if (key == "kfRedColor0")         cf0(g_kfRedColor[0]);
            else if (key == "kfRedColor1")         cf0(g_kfRedColor[1]);
            else if (key == "kfRedColor2")         cf0(g_kfRedColor[2]);
            else if (key == "kfRedColor3")         cf0(g_kfRedColor[3]);
            else if (key == "kfBluColor0")         cf0(g_kfBluColor[0]);
            else if (key == "kfBluColor1")         cf0(g_kfBluColor[1]);
            else if (key == "kfBluColor2")         cf0(g_kfBluColor[2]);
            else if (key == "kfBluColor3")         cf0(g_kfBluColor[3]);
            else if (key == "kfBoxShadowColor0")   cf0(g_kfBoxShadowColor[0]);
            else if (key == "kfBoxShadowColor1")   cf0(g_kfBoxShadowColor[1]);
            else if (key == "kfBoxShadowColor2")   cf0(g_kfBoxShadowColor[2]);
            else if (key == "kfBoxShadowColor3")   cf0(g_kfBoxShadowColor[3]);
            else if (key == "kfOutlineLayerCount") {
                g_kfOutlineLayerCount = std::clamp(std::stoi(val), 0, (int)k_maxOutlineLayers);
                for (int i = 0; i < g_kfOutlineLayerCount; ++i) g_kfOutlineLayers[i] = OutlineLayer{};
            }
            else if (key.size() > 4 && key.compare(0, 4, "kfOL") == 0) {
                size_t nEnd = key.find_first_not_of("0123456789", 4);
                if (nEnd != std::string::npos) {
                    int li = std::stoi(key.substr(4, nEnd - 4));
                    if (li >= 0 && li < g_kfOutlineLayerCount)
                        ParseOLKey(key.substr(nEnd), val, g_kfOutlineLayers[li], b());
                }
            }
            else if (key == "kfFontFile") {
                g_kfFontFile  = val;
                g_kfFontIndex = FontIndexForFile(val);
            }
            else if (key == "kfFontFlags")  { int ff=std::stoi(val); for(int i=0;i<4;i++) g_kfFontFlags[i]=(ff>>i)&1; }
            else if (key == "kfIconColor0")  cf0(g_kfIconColor[0]);
            else if (key == "kfIconColor1")  cf0(g_kfIconColor[1]);
            else if (key == "kfIconColor2")  cf0(g_kfIconColor[2]);
            else if (key == "kfIconColor3")  cf0(g_kfIconColor[3]);
            else if (key == "kfTextDropShadowColor0") cf0(g_kfTextDropShadowColor[0]);
            else if (key == "kfTextDropShadowColor1") cf0(g_kfTextDropShadowColor[1]);
            else if (key == "kfTextDropShadowColor2") cf0(g_kfTextDropShadowColor[2]);
            else if (key == "kfTextDropShadowColor3") cf0(g_kfTextDropShadowColor[3]);
            else if (key == "kfTextOutlineColor0")   cf0(g_kfTextOutlineColor[0]);
            else if (key == "kfTextOutlineColor1")   cf0(g_kfTextOutlineColor[1]);
            else if (key == "kfTextOutlineColor2")   cf0(g_kfTextOutlineColor[2]);
            else if (key == "kfTextOutlineColor3")   cf0(g_kfTextOutlineColor[3]);
            else if (key == "kfBlobColor0")       cf0(g_kfBlobColor[0]);
            else if (key == "kfBlobColor1")       cf0(g_kfBlobColor[1]);
            else if (key == "kfBlobColor2")       cf0(g_kfBlobColor[2]);
            else if (key == "kfPrecipColor0")     cf0(g_kfPrecipColor[0]);
            else if (key == "kfPrecipColor1")     cf0(g_kfPrecipColor[1]);
            else if (key == "kfPrecipColor2")     cf0(g_kfPrecipColor[2]);
            else if (key == "kfPrecipColor3")     cf0(g_kfPrecipColor[3]);
            else if (key == "kfKillGlowColor0")   cf0(g_kfKillGlowColor[0]);
            else if (key == "kfKillGlowColor1")   cf0(g_kfKillGlowColor[1]);
            else if (key == "kfKillGlowColor2")   cf0(g_kfKillGlowColor[2]);
            else if (key == "kfKillGlowColor3")   cf0(g_kfKillGlowColor[3]);
            continue; // skip main else-if chain
        }
        // ---- end custom killfeed keys ------------------------------
        // ---- wpnHud keys — handled early to avoid else-if depth limit ----
        if (key.size() > 6 && key.compare(0, 6, "wpnHud") == 0) {
            if      (key == "wpnHudEnable")         g_enableWeaponHud          = (val != "0");
            else if (key == "wpnHudActiveAlpha")    g_wpnHudActiveColor[3]     = std::stof(val);
            else if (key == "wpnHudInactiveAlpha")  g_wpnHudInactiveColor[3]   = std::stof(val);
            else if (key == "wpnHudShowNum")        g_wpnHudShowNum            = (val != "0");
            else if (key == "wpnHudFlipIcons")      g_wpnHudFlipIcons          = (val != "0");
            else if (key == "wpnHudGlowEnable")     g_wpnHudGlowEnabled        = (val != "0");
            else if (key.size() == 13 && key.compare(0, 11, "wpnHudGlowC") == 0) {
                int comp = (key[11]=='R') ? 0 : (key[11]=='G') ? 1 : (key[11]=='B') ? 2 : (key[11]=='A') ? 3 : -1;
                int idx  = key[12] - '0';
                if (comp >= 0 && idx >= 0 && idx < 8)
                    g_wpnHudGlowColors[idx][comp] = std::stof(val);
            }
            else if (key == "wpnHudNameEnable")    g_enableWpnHudName    = (val != "0");
            else if (key == "wpnHudNumFontFile") {
                g_wpnHudNumFontFile  = val;
                g_wpnHudNumFontIndex = FontIndexForFile(val);
            }
            else if (key == "wpnHudNumFontFlags")  { int ff=std::stoi(val); for(int i=0;i<4;i++) g_wpnHudNumFontFlags[i]=(ff>>i)&1; }
            else if (key == "wpnHudNameFontFile") {
                g_wpnHudNameFontFile  = val;
                g_wpnHudNameFontIndex = FontIndexForFile(val);
            }
            else if (key == "wpnHudNameFontFlags") { int ff=std::stoi(val); for(int i=0;i<4;i++) g_wpnHudNameFontFlags[i]=(ff>>i)&1; }
            else if (key == "wpnHudNameShadow")    g_wpnHudNameShadowEnabled    = (val != "0");
            else if (key == "wpnHudNameOutline")   g_wpnHudNameOutlineEnabled   = (val != "0");
            else if (key == "wpnHudNumShadow")     g_wpnHudNumShadowEnabled     = (val != "0");
            else if (key == "wpnHudNumOutline")    g_wpnHudNumOutlineEnabled    = (val != "0");
            else if (key == "wpnHudActiveScaleSmooth") g_wpnHudActiveScaleSmooth  = (val != "0");
            else if (key == "wpnHudSepEnable")     g_wpnHudSepEnabled           = (val != "0");
            else if (key == "wpnHudCustomNames")   g_wpnHudCustomNames          = (val != "0");
            else if (key.size() == 12 && key.compare(0, 11, "wpnHudCName") == 0) {
                int idx = key[11] - '0';
                if (idx >= 0 && idx < 5) strncpy_s(g_wpnHudCustomName[idx], val.c_str(), _TRUNCATE);
            }
            else if (key == "wpnHudFade")      g_wpnHudFade      = (val != "0");
            else if (key == "wpnHudBgEnable")       g_wpnHudBgEnabled           = (val != "0");
            else if (key == "wpnHudBgFadeNear")      g_wpnHudBgFadeNear           = (val != "0");
            else if (key == "wpnHudBgScreenEdge")    g_wpnHudBgScreenEdge         = (val != "0");
            else if (key == "wpnHudBgBorderEnable") g_wpnHudBgBorderEnabled     = (val != "0");
            continue;
        }
        // ---- end wpnHud keys ---------------------------------------
        if      (key == "mpBgDraw")        g_mpBgDraw      = b();
        else if (key == "mpCopyWmStyle")   g_mpCopyWmStyle = b();
        else if (key == "mpBgCornerFlags")   g_mpBgCornerFlags  = std::stoi(val) & 15;
        else if (key == "mpOutlineLayerCount") g_mpOutlineLayerCount = std::clamp(std::stoi(val),1,k_maxOutlineLayers);
        else if (key.size()>4 && key.substr(0,4)=="mpOL") {
            int idx = key[4]-'0';
            if (idx>=0 && idx<k_maxOutlineLayers)
                ParseOLKey(key.substr(5), val, g_mpOutlineLayers[idx], b());
        }
        else if (key == "mpOutline")         g_mpOutline        = b();
        else if (key == "mpOutlineThick")    g_mpOutlineThick   = std::clamp(std::stof(val), 0.5f, 6.f);
        else if (key == "mpOutlineSideFlags")g_mpOutlineSideFlags= std::stoi(val) & 15;
        else if (key == "mpOutR")  g_mpOutlineColor[0] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "mpOutG")  g_mpOutlineColor[1] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "mpOutB")  g_mpOutlineColor[2] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "mpOutA")       g_mpOutlineColor[3] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "mpFontFile") {
            g_mpFontFile  = val;
            g_mpFontIndex = FontIndexForFile(val);
        }
        else if (key == "mpFontFlags") {
            int ff = std::stoi(val);
            for (int i = 0; i < 6; i++) g_mpFontFlags[i] = (ff>>i)&1;
        }
        else if (key == "menuFontFile") {
            g_menuFontFile  = val;
            g_menuFontIndex = FontIndexForFile(val);
        }
        else if (key == "menuFontFlags") {
            int ff = std::stoi(val);
            for (int i = 0; i < 6; i++) g_menuFontFlags[i] = (ff>>i)&1;
        }
        else if (key.rfind("ng", 0) == 0 && ParseNetGraphKey(key, val)) {}
        // One call, not twenty-seven else-ifs: this chain is already at MSVC's block
        // nesting limit (C1061), and every key added to it costs another level.
        else if (key == "customLabel")            g_customLabel          = val;
        else if (key == "titleCycleCount")        g_titleCount           = std::clamp(std::stoi(val), 0, k_maxTitleCycle);
        else if (key.size() >= 11 && key.substr(0, 10) == "titleCycle" && key[10] >= '0' && key[10] <= '9') {
            int idx = key[10] - '0';
            if (key.size() >= 12 && key[11] >= '0' && key[11] <= '9') idx = idx * 10 + (key[11] - '0');
            if (idx >= 0 && idx < k_maxTitleCycle) {
                strncpy(g_titleList[idx].text, val.c_str(), sizeof(g_titleList[idx].text) - 1);
                g_titleList[idx].text[sizeof(g_titleList[idx].text) - 1] = '\0';
            }
        }
        else if (key == "wmSeparator")            { strncpy(g_wmSeparator, val.c_str(), sizeof(g_wmSeparator)-1); g_wmSeparator[sizeof(g_wmSeparator)-1] = '\0'; }
        else if (key == "menuKey")                { int k = std::stoi(val); if (k > 0 && k < 256) g_menuKey = k; }
        else if (key == "wmFontFile")             { g_wmFontFile = val; ResolveFontIndex(); }
        else if (key == "wmFontFlags")            { int ff = std::stoi(val); for (int i = 0; i < 6; i++) g_wmFontFlags[i] = (ff >> i) & 1; }
        else if (key == "wmLogoImgPath")        { strncpy(g_wmLogoImgPath, val.c_str(), MAX_PATH-1); g_wmLogoImgPath[MAX_PATH-1]='\0'; if (g_wmLogoImgPath[0]) g_wmLogoImgNeedsLoad = true; }
        else if (key == "wmBgCornerFlags")        g_wmBgCornerFlags       = std::stoi(val) & 15;
        else if (key == "wmOutlineLayerCount") g_wmOutlineLayerCount = std::clamp(std::stoi(val),1,k_maxOutlineLayers);
        else if (key.size()>4 && key.substr(0,4)=="wmOL") {
            int idx = key[4]-'0';
            if (idx>=0 && idx<k_maxOutlineLayers)
                ParseOLKey(key.substr(5), val, g_wmOutlineLayers[idx], b());
        }
        else if (key == "wmOutline")              g_wmOutline             = b();
        else if (key == "wmOutlineThick")         g_wmOutlineThick        = std::stof(val);
        else if (key == "wmOutlineSideFlags")     g_wmOutlineSideFlags    = std::stoi(val) & 15;
        else if (key == "wmOutlineR")             g_wmOutlineColor[0]    = std::stof(val);
        else if (key == "wmOutlineG")             g_wmOutlineColor[1]    = std::stof(val);
        else if (key == "wmOutlineB")             g_wmOutlineColor[2]    = std::stof(val);
        else if (key == "wmOutlineA")             g_wmOutlineColor[3]    = std::stof(val);


        // ---- break chain to avoid MSVC C1061 (too many nested else-if blocks) ----
        if (key == "nameCycleCount")         g_nameCount            = std::clamp(std::stoi(val), 0, k_maxNames);
        else if (key.size() >= 10 && key.substr(0, 9) == "nameCycle" && key[9] >= '0' && key[9] <= '9')
        {
            int idx = std::stoi(key.substr(9));
            if (idx >= 0 && idx < k_maxNames)
            {
                strncpy(g_nameList[idx].text, val.c_str(), 63);
                g_nameList[idx].text[63] = '\0';
            }
        }
        else if (key == "teamLabelCycleCount")     g_teamLabelCount          = std::clamp(std::stoi(val), 0, k_maxNames);
        else if (key.size() >= 15 && key.substr(0, 14) == "teamLabelCycle" && key[14] >= '0' && key[14] <= '9')
        {
            int idx = std::stoi(key.substr(14));
            if (idx >= 0 && idx < k_maxNames) { strncpy(g_teamLabelList[idx].text, val.c_str(), 63); g_teamLabelList[idx].text[63] = '\0'; }
        }
        else if (key == "enemyLabelCycleCount")    g_enemyLabelCount         = std::clamp(std::stoi(val), 0, k_maxNames);
        else if (key.size() >= 16 && key.substr(0, 15) == "enemyLabelCycle" && key[15] >= '0' && key[15] <= '9')
        {
            int idx = std::stoi(key.substr(15));
            if (idx >= 0 && idx < k_maxNames) { strncpy(g_enemyLabelList[idx].text, val.c_str(), 63); g_enemyLabelList[idx].text[63] = '\0'; }
        }
        else if (key == "everyoneLabelCycleCount")  g_everyoneLabelCount       = std::clamp(std::stoi(val), 0, k_maxNames);
        else if (key.size() >= 19 && key.substr(0, 18) == "everyoneLabelCycle" && key[18] >= '0' && key[18] <= '9')
        {
            int idx = std::stoi(key.substr(18));
            if (idx >= 0 && idx < k_maxNames) { strncpy(g_everyoneLabelList[idx].text, val.c_str(), 63); g_everyoneLabelList[idx].text[63] = '\0'; }
        }

        if (key == "sheenColor0R")           g_sheenColors[0][0]    = std::stof(val);
        else if (key == "sheenColor0G")           g_sheenColors[0][1]    = std::stof(val);
        else if (key == "sheenColor0B")           g_sheenColors[0][2]    = std::stof(val);
        else if (key == "sheenColor1R")           g_sheenColors[1][0]    = std::stof(val);
        else if (key == "sheenColor1G")           g_sheenColors[1][1]    = std::stof(val);
        else if (key == "sheenColor1B")           g_sheenColors[1][2]    = std::stof(val);
        else if (key == "sheenColor2R")           g_sheenColors[2][0]    = std::stof(val);
        else if (key == "sheenColor2G")           g_sheenColors[2][1]    = std::stof(val);
        else if (key == "sheenColor2B")           g_sheenColors[2][2]    = std::stof(val);
        else if (key == "sheenColor3R")           g_sheenColors[3][0]    = std::stof(val);
        else if (key == "sheenColor3G")           g_sheenColors[3][1]    = std::stof(val);
        else if (key == "sheenColor3B")           g_sheenColors[3][2]    = std::stof(val);
        // Legacy 2-color keys
        else if (key == "sheenColorAR")           g_sheenColors[0][0]    = std::stof(val);
        else if (key == "sheenColorAG")           g_sheenColors[0][1]    = std::stof(val);
        else if (key == "sheenColorAB")           g_sheenColors[0][2]    = std::stof(val);
        else if (key == "sheenColorBR")           g_sheenColors[1][0]    = std::stof(val);
        else if (key == "sheenColorBG")           g_sheenColors[1][1]    = std::stof(val);
        else if (key == "sheenColorBB")           g_sheenColors[1][2]    = std::stof(val);
        else if (key == "fogLinkSkybox")          g_fogLinkSkybox        = (val != "0");
        else if (key == "rainMode")               { g_rainMode = std::clamp(std::stoi(val), 0, 3); }
        else if (key == "snowSpriteName")         {
            strncpy(g_snowSpriteName, val.c_str(), sizeof(g_snowSpriteName) - 1);
            g_snowSpriteName[sizeof(g_snowSpriteName) - 1] = '\0';
            s_snowSpriteDirty = true;
        }
        // legacy keys from unified-slider version -- map to rain vars
        else if (key == "weatherAlpha")           g_rainAlpha          = std::clamp(std::stof(val), 0.f,    1.f);
        else if (key == "weatherAlphaPow")        {} // legacy key, no longer used
        else if (key == "weatherDensity")         {} // legacy key, no longer used
        else if (key == "weatherLength")          g_rainLength         = std::clamp(std::stof(val), 0.f,    5.f);
        else if (key == "weatherRadius")          g_rainRadius         = std::clamp(std::stof(val), 100.f, 5000.f);
        else if (key == "weatherSideVel")         g_rainSideVel        = std::clamp(std::stof(val), 0.f,  500.f);
        else if (key == "weatherSpeed")           g_rainSpeed          = std::clamp(std::stof(val), 0.f, 2000.f);
        else if (key == "weatherWidth")           g_rainWidth          = std::clamp(std::stof(val), 0.f,    5.f);
        else if (key == "weatherScale")           {} // legacy key, ignored
        else if (key == "weatherSize")            {} // legacy key, ignored
        else if (key == "weatherUseCustomParticle") { /* legacy: mode is authoritative */ }
        else if (key == "weatherParticleName") {
            // legacy single-name key -- load into layer 0
            strncpy(g_particleLayerName[0], val.c_str(), 127);
            g_particleLayerName[0][127] = '\0';
        }
        // Legacy slot keys -- migrate to per-layer names
        else if (key == "weatherParticleName0") { strncpy(g_particleLayerName[0], val.c_str(), 127); g_particleLayerName[0][127]='\0'; }
        else if (key == "weatherParticleName1") { strncpy(g_particleLayerName[1], val.c_str(), 127); g_particleLayerName[1][127]='\0'; }
        else if (key == "weatherParticleName2") { strncpy(g_particleLayerName[2], val.c_str(), 127); g_particleLayerName[2][127]='\0'; }
        else if (key == "weatherParticleName3") { strncpy(g_particleLayerName[3], val.c_str(), 127); g_particleLayerName[3][127]='\0'; }
        // Wide clamp: the slider stops at +-2500 but typed values are allowed past it.
        else if (key == "particleLayerZ0")    { g_particleLayerZ[0] = std::stof(val); }
        else if (key == "particleLayerZ1")    { g_particleLayerZ[1] = std::stof(val); }
        else if (key == "particleLayerZ2")    { g_particleLayerZ[2] = std::stof(val); }
        else if (key == "particleLayerZ3")    { g_particleLayerZ[3] = std::stof(val); }
        else if (key == "particleLayerZ4")    { g_particleLayerZ[4] = std::stof(val); }
        else if (key == "particleLayerZ5")    { g_particleLayerZ[5] = std::stof(val); }
        else if (key == "particleLayerZ6")    { g_particleLayerZ[6] = std::stof(val); }
        else if (key == "particleLayerZ7")    { g_particleLayerZ[7] = std::stof(val); }
        else if (key == "particleLayerName0") { strncpy(g_particleLayerName[0], val.c_str(), 127); g_particleLayerName[0][127]='\0'; }
        else if (key == "particleLayerName1") { strncpy(g_particleLayerName[1], val.c_str(), 127); g_particleLayerName[1][127]='\0'; }
        else if (key == "particleLayerName2") { strncpy(g_particleLayerName[2], val.c_str(), 127); g_particleLayerName[2][127]='\0'; }
        else if (key == "particleLayerName3") { strncpy(g_particleLayerName[3], val.c_str(), 127); g_particleLayerName[3][127]='\0'; }
        else if (key == "particleLayerName4") { strncpy(g_particleLayerName[4], val.c_str(), 127); g_particleLayerName[4][127]='\0'; }
        else if (key == "particleLayerName5") { strncpy(g_particleLayerName[5], val.c_str(), 127); g_particleLayerName[5][127]='\0'; }
        else if (key == "particleLayerName6") { strncpy(g_particleLayerName[6], val.c_str(), 127); g_particleLayerName[6][127]='\0'; }
        else if (key == "particleLayerName7") { strncpy(g_particleLayerName[7], val.c_str(), 127); g_particleLayerName[7][127]='\0'; }
        else if (key == "skyboxName")             { strncpy(g_skyboxName, val.c_str(), sizeof(g_skyboxName)-1); g_skyboxName[sizeof(g_skyboxName)-1]=0; }

        // Per-side BgGrad load (mp and wm)
        if (key.size() > 9 && (key.substr(0,8) == "mpBgGrad" || key.substr(0,8) == "wmBgGrad")) {
            bool isWm = key[0] == 'w';
            static const char sides[4] = {'L','R','T','B'};
            for (int i = 0; i < 4; ++i) {
                if (key[8] != sides[i]) continue;
                BgSideGrad& g = isWm ? g_wmBgGrad[i] : g_mpBgGrad[i];
                std::string sf = key.substr(9);
                if      (sf == "En") g.enable = b();
                else if (sf == "S")  g.start  = std::clamp(std::stof(val),0.f,1.f);
                else if (sf == "E")  g.end    = std::clamp(std::stof(val),0.f,1.f);
                break;
            }
        }

        // ---- viewmodel ---------------------------------------------
        // ---- Viewmodel chams load ----------------------------------
        if (key == "vmWeaponChamCount")      g_vmWeaponChamCount    = std::clamp(std::stoi(val), 1, k_vmMaxLayers);
        else if (key == "vmArmsChamCount")        g_vmArmsChamCount      = std::clamp(std::stoi(val), 1, k_vmMaxLayers);
        else if (key.size() >= 6 && key.substr(0,4) == "vmWC") {
            int idx = key[4] - '0';
            if (idx >= 0 && idx < k_vmMaxLayers) {
                std::string suffix = key.substr(5);
                if      (suffix == "Type")    g_vmWeaponChamLayers[idx].matType   = std::clamp(std::stoi(val), 0, 7);
                else if (suffix == "R")       g_vmWeaponChamLayers[idx].color[0]  = std::stof(val);
                else if (suffix == "G")       g_vmWeaponChamLayers[idx].color[1]  = std::stof(val);
                else if (suffix == "B")       g_vmWeaponChamLayers[idx].color[2]  = std::stof(val);
                else if (suffix == "A")       g_vmWeaponChamLayers[idx].color[3]  = std::stof(val);
                else if (suffix == "Sheen")   g_vmWeaponChamLayers[idx].useSheen  = (std::stoi(val) != 0);
                else if (suffix == "IgnoreZ") g_vmWeaponChamLayers[idx].ignorez   = (std::stoi(val) != 0);
            }
        }
        else if (key.size() >= 6 && key.substr(0,4) == "vmAC") {
            int idx = key[4] - '0';
            if (idx >= 0 && idx < k_vmMaxLayers) {
                std::string suffix = key.substr(5);
                if      (suffix == "Type")    g_vmArmsChamLayers[idx].matType     = std::clamp(std::stoi(val), 0, 7);
                else if (suffix == "R")       g_vmArmsChamLayers[idx].color[0]    = std::stof(val);
                else if (suffix == "G")       g_vmArmsChamLayers[idx].color[1]    = std::stof(val);
                else if (suffix == "B")       g_vmArmsChamLayers[idx].color[2]    = std::stof(val);
                else if (suffix == "A")       g_vmArmsChamLayers[idx].color[3]    = std::stof(val);
                else if (suffix == "Sheen")   g_vmArmsChamLayers[idx].useSheen    = (std::stoi(val) != 0);
                else if (suffix == "IgnoreZ") g_vmArmsChamLayers[idx].ignorez     = (std::stoi(val) != 0);
            }
        }
        // ---- Player chams load -------------------------------------
        else if (key == "plChamCount")        g_plChamCount          = std::clamp(std::stoi(val), 1, k_vmMaxLayers);
        else if (key.size() >= 5 && key.substr(0,3) == "plC") {
            int idx = key[3] - '0';
            if (idx >= 0 && idx < k_vmMaxLayers) {
                std::string suffix = key.substr(4);
                if      (suffix == "Type")  g_plChamLayers[idx].matType  = std::clamp(std::stoi(val), 0, 7);
                else if (suffix == "R")     g_plChamLayers[idx].color[0] = std::stof(val);
                else if (suffix == "G")     g_plChamLayers[idx].color[1] = std::stof(val);
                else if (suffix == "B")     g_plChamLayers[idx].color[2] = std::stof(val);
                else if (suffix == "A")     g_plChamLayers[idx].color[3] = std::stof(val);
                else if (suffix == "Sheen") g_plChamLayers[idx].useSheen = (std::stoi(val) != 0);
            }
        }
        // ---- Map textures load -------------------------------------
        else if (key.size() >= 5 && key.substr(0,3) == "mtC") {
            int idx = key[3] - '0';
            if (idx >= 0 && idx < k_mapTexCatCount) {
                std::string suffix = key.substr(4);
                if      (suffix == "Preset") g_mapTexCats[idx].preset =
                                                 std::clamp(std::stoi(val), 0, k_mapTexPresetCount - 1);
                else if (suffix == "R")      g_mapTexCats[idx].tint[0] = std::stof(val);
                else if (suffix == "G")      g_mapTexCats[idx].tint[1] = std::stof(val);
                else if (suffix == "B")      g_mapTexCats[idx].tint[2] = std::stof(val);
                else if (suffix == "A")      g_mapTexCats[idx].tint[3] = std::stof(val);
                else if (suffix == "Tex")    strncpy_s(g_mapTexCats[idx].custom, val.c_str(), _TRUNCATE);
            }
        }
        // ---- Prop chams load ---------------------------------------
        else if (key == "propChamCount")  g_propChamCount   = std::clamp(std::stoi(val), 1, k_vmMaxLayers);
        else if (key.size() >= 5 && key.substr(0,3) == "prC") {
            int idx = key[3] - '0';
            if (idx >= 0 && idx < k_vmMaxLayers) {
                std::string suffix = key.substr(4);
                if      (suffix == "Type")  g_propChamLayers[idx].matType   = std::clamp(std::stoi(val), 0, 7);
                else if (suffix == "R")     g_propChamLayers[idx].color[0]  = std::stof(val);
                else if (suffix == "G")     g_propChamLayers[idx].color[1]  = std::stof(val);
                else if (suffix == "B")     g_propChamLayers[idx].color[2]  = std::stof(val);
                else if (suffix == "A")     g_propChamLayers[idx].color[3]  = std::stof(val);
                else if (suffix == "Sheen") g_propChamLayers[idx].useSheen  = (std::stoi(val) != 0);
            }
        }
        // ---- crosshair ---------------------------------------------
        if (key == "chSide0")                g_chSides[0]           = b();
        else if (key == "chSide1")                g_chSides[1]           = b();
        else if (key == "chSide2")                g_chSides[2]           = b();
        else if (key == "chSide3")                g_chSides[3]           = b();
        else if (key == "chOutlineSide0")         g_chOutlineSides[0]    = b();
        else if (key == "chOutlineSide1")         g_chOutlineSides[1]    = b();
        else if (key == "chOutlineSide2")         g_chOutlineSides[2]    = b();
        else if (key == "chOutlineSide3")         g_chOutlineSides[3]    = b();
        else if (key == "chDynamic")              g_chDynamic            = std::stoi(val) != 0;
        else if (key == "chGradColorCenter0")     g_chGradColorCenter[0] = std::stof(val);
        else if (key == "chGradColorCenter1")     g_chGradColorCenter[1] = std::stof(val);
        else if (key == "chGradColorCenter2")     g_chGradColorCenter[2] = std::stof(val);
        else if (key == "chGradColorCenter3")     g_chGradColorCenter[3] = std::stof(val);
        else if (key == "chGradColorOuter0")      g_chGradColorOuter[0]  = std::stof(val);
        else if (key == "chGradColorOuter1")      g_chGradColorOuter[1]  = std::stof(val);
        else if (key == "chGradColorOuter2")      g_chGradColorOuter[2]  = std::stof(val);
        else if (key == "chGradColorOuter3")      g_chGradColorOuter[3]  = std::stof(val);
        else if (key == "chDotColor0")            g_chDotColor[0]        = std::stof(val);
        else if (key == "chDotColor1")            g_chDotColor[1]        = std::stof(val);
        else if (key == "chDotColor2")            g_chDotColor[2]        = std::stof(val);
        else if (key == "chDotColor3")            g_chDotColor[3]        = std::stof(val);
        else if (ParseCsgoCrosshairKey(key, val)) {}
        else if (key == "zombifyMode")         g_zombifyMode         = val.empty() ? 0 : std::stoi(val);
        else if (key.rfind("skin_", 0) == 0) {
            // key format: skin_<defIdx>_<field>
            size_t u1 = key.find('_', 5);
            if (u1 != std::string::npos) {
                int defIdx = std::stoi(key.substr(5, u1 - 5));
                std::string field = key.substr(u1 + 1);
                // Fill existing entries only: an insert here can rehash the map under the
                // game thread's FSN loop. Keys outside the weapon list have no menu entry anyway.
                auto it = g_skinCfgs.find(defIdx);
                if (it != g_skinCfgs.end())
                    ParseSkinField(it->second, field, val, b());
            }
        }

        // ---- Background blur ---------------------------------------
        if (key == "menuBgBlur")          g_menuBgBlur          = b();
        // legacy keys -- OR into g_bgBlur so any old per-panel enable migrates forward
        else if (key == "wmBgBlur"   || key == "mpBgBlur" || key == "menuBgBlur")  { if (b()) g_bgBlur = true; }
        else if (key == "wmBgBlurSigma" || key == "mpBgBlurSigma")
            g_bgBlurSigma = std::clamp(std::stof(val), 0.5f, 40.f);

        // ---- Color correction --------------------------------------
        else if (key.size() == 8 && key.compare(0, 7, "ccCurve") == 0) {
            // ccCurveM / R / G / B = "x,y;x,y;..."; malformed leaves the curve as it was
            const size_t ch = std::string("MRGB").find(key[7]);
            if (ch != std::string::npos && !CcCurve_Parse(val, g_ccCurve[ch]))
                DebugLog("config: bad %s, curve kept", key.c_str());
        }
        else if (key.size() >= 11 && key.compare(0, 10, "ccCurveOff") == 0) {
            // legacy keys: ccCurveOff0..4 (master), ccCurveOffR0..4, ccCurveOffG0..4, ccCurveOffB0..4
            size_t ch = std::string::npos;
            int    idx = -1;
            if (key.size() == 11) {
                ch = 0; idx = key[10] - '0';
            } else if (key.size() == 12) {
                ch = std::string("RGB").find(key[10]);
                if (ch != std::string::npos) ch += 1;
                idx = key[11] - '0';
            }
            float off = 0.f;
            if (ch != std::string::npos && CfgParseFloat(val, -1.f, 1.f, off))
                CcCurve_ApplyLegacyOffset(g_ccCurve[ch], idx, off);   // checks idx
        }

        // ---- Box shadow --------------------------------------------
        if (key == "wmBoxShColor0") g_wmBoxShadowColor[0] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "wmBoxShColor1") g_wmBoxShadowColor[1] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "wmBoxShColor2") g_wmBoxShadowColor[2] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "wmBoxShColor3") g_wmBoxShadowColor[3] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "mpBoxShColor0") g_mpBoxShadowColor[0] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "mpBoxShColor1") g_mpBoxShadowColor[1] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "mpBoxShColor2") g_mpBoxShadowColor[2] = std::clamp(std::stof(val),0.f,1.f);
        else if (key == "mpBoxShColor3") g_mpBoxShadowColor[3] = std::clamp(std::stof(val),0.f,1.f);

        // ---- Image overlay -----------------------------------------
        else if (key.size() > 4 && key.rfind("img", 0) == 0) {
            size_t us = key.find('_', 3);
            if (us != std::string::npos) {
                int idx = std::stoi(key.substr(3, us - 3));
                if (idx >= 0 && idx < k_imgMaxSlots) {
                    std::string sf = key.substr(us + 1);
                    auto& sl = g_imgSlots[idx];
                    if      (sf == "enabled")    sl.enabled  = b();
                    else if (sf == "path")      { strncpy(sl.path, val.c_str(), MAX_PATH-1); sl.path[MAX_PATH-1]='\0'; sl.needsLoad = sl.path[0] != '\0'; }
                    else if (sf == "scale")      sl.scale      = std::clamp(std::stof(val), 0.05f, 20.f);
                    else if (sf == "rotation")   sl.rotation   = std::stof(val);
                    else if (sf == "posX")       sl.posX       = std::stof(val);
                    else if (sf == "posY")       sl.posY       = std::stof(val);
                    else if (sf == "alpha")      sl.alpha      = std::clamp(std::stof(val), 0.f, 1.f);
                    else if (sf == "anchor")     sl.anchor     = std::clamp(std::stoi(val), 0, 1);
                    else if (sf == "anchorOffX") sl.anchorOffX = std::stof(val);
                    else if (sf == "anchorOffY") sl.anchorOffY = std::stof(val);
                    else if (sf == "pixelArt")   sl.pixelArt   = b();
                }
            }
        }
        } catch (...) {}   // a malformed value skips its line, never the load
    }
    // ---- Alias list load ---------------------------------------
    {
        // Second pass over the file for alias/group keys, under the alias lock.
        std::ifstream af(path);
        AliasLockGuard aliasLk;
        if (af) {
            std::string al;
            while (std::getline(af, al)) {
                if (al.empty() || al[0] == '#') continue;
                size_t ep = al.find('='); if (ep == std::string::npos) continue;
                std::string ak = al.substr(0, ep);
                std::string av = al.substr(ep + 1);
                auto bi = [&]{ return av == "1"; };

                if (ak == "playerGroupCount") {
                    int cnt = std::min(std::stoi(av), 32);   // group masks are 32 bits
                    if (cnt > (int)g_playerGroups.size())
                        g_playerGroups.resize(cnt);
                }
                else if (ak.substr(0, 11) == "playerGroup") {
                    int gi = std::stoi(ak.substr(11));
                    if (gi >= 0 && gi < 32 && !av.empty()) {
                        if (gi >= (int)g_playerGroups.size()) g_playerGroups.resize(gi + 1);
                        g_playerGroups[gi] = av;
                    }
                }
                else if (ak.substr(0, 5) == "alias") {
                    size_t uu = ak.find('_');
                    if (uu == std::string::npos) continue;
                    std::string sf = ak.substr(uu + 1);
                    int idx = std::stoi(ak.substr(5, uu - 5));
                    if (sf == "steam64") {
                        uint64_t s = (uint64_t)std::stoull(av);
                        if (s == 0) continue;
                        if (!g_aliases.count(s)) {
                            AliasEntry e{};
                            e.steam64   = s;
                            e.groupMask = 0;
                            g_aliases[s] = e;
                        }
                    }
                }
            }
            // Second mini-pass: fill non-steam64 fields
            af.clear(); af.seekg(0);
            // Collect (idx, steam64) mapping first
            std::unordered_map<int, uint64_t> idxToSteam;
            while (std::getline(af, al)) {
                if (al.empty() || al[0] == '#') continue;
                size_t ep = al.find('='); if (ep == std::string::npos) continue;
                std::string ak = al.substr(0, ep);
                std::string av = al.substr(ep + 1);
                if (ak.substr(0, 5) != "alias") continue;
                size_t uu = ak.find('_'); if (uu == std::string::npos) continue;
                std::string sf = ak.substr(uu + 1);
                int idx = std::stoi(ak.substr(5, uu - 5));
                if (sf == "steam64") idxToSteam[idx] = (uint64_t)std::stoull(av);
            }
            af.clear(); af.seekg(0);
            while (std::getline(af, al)) {
                if (al.empty() || al[0] == '#') continue;
                size_t ep = al.find('='); if (ep == std::string::npos) continue;
                std::string ak = al.substr(0, ep);
                std::string av = al.substr(ep + 1);
                if (ak.substr(0, 5) != "alias") continue;
                size_t uu = ak.find('_'); if (uu == std::string::npos) continue;
                std::string sf = ak.substr(uu + 1);
                int idx = std::stoi(ak.substr(5, uu - 5));
                auto it2 = idxToSteam.find(idx);
                if (it2 == idxToSteam.end()) continue;
                auto ae_it = g_aliases.find(it2->second);
                if (ae_it == g_aliases.end()) continue;
                AliasEntry& ae = ae_it->second;
                if (sf == "name") {
                    strncpy_s(ae.displayName, av.c_str(), _TRUNCATE);
                } else if (sf == "notify") {
                    ae.notifyOnServer = (av == "1");
                } else if (sf == "changeName") {
                    ae.changeNameInScoreboard = (av == "1");
                } else if (sf == "groupMask") {
                    ae.groupMask = (uint32_t)std::stoul(av);
                } else if (sf == "group") {
                    // backward compat: single-int groupID → set that bit
                    int g = std::stoi(av);
                    if (g >= 0 && g < 32) ae.groupMask |= (1u << g);
                }
            }
        }
    }

    file.close();
}

// SEH helpers for h_FrameStageNotify (C2712: __try cannot be in a function with object unwinding)
__declspec(noinline) bool FSN_IsInGame()
{
    bool result = false;
    __try { result = VCall<bool>(g_engine, 26); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

__declspec(noinline) unsigned int FSN_ReadWeaponHandle(void* pLocal, int offset, int i)
{
    __try {
        return *reinterpret_cast<unsigned int*>(
            reinterpret_cast<uintptr_t>(pLocal) + offset + i * sizeof(unsigned int));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

__declspec(noinline) bool FSN_VtableSane(void* pEnt)
{
    __try {
        void** vtbl = *(void***)pEnt;
        return reinterpret_cast<uintptr_t>(vtbl) >= 0x10000;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) void* FSN_GetEntityFromHandle(unsigned int hWeapon)
{
    // The handle is a packed value: lower 14 bits = entity index.
    // Call GetClientEntity(index) at vtable slot 3.
    // Do NOT pass the raw handle to a vtable slot; that was calling the wrong function.
    void* pWeapon = nullptr;
    __try {
        int entIdx = (int)(hWeapon & 0x3FFF);
        using GetClientEntity_t = void*(__thiscall*)(void*, int);
        void** vtbl = *(void***)g_entityList;
        if (reinterpret_cast<uintptr_t>(vtbl) >= 0x10000)
            pWeapon = ((GetClientEntity_t)vtbl[3])(g_entityList, entIdx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return pWeapon;
}
__declspec(noinline) int FSN_ReadItemDefIndex(void* pWeapon, int offset)
{
    int v = 0;
    __try { v = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pWeapon) + offset); }
    __except(EXCEPTION_EXECUTE_HANDLER) { v = -1; }
    return v;
}

__declspec(noinline) void FSN_WriteItemDefIndex(void* pWeapon, int offset, int val)
{
    __try { *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pWeapon) + offset) = val; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) void FSN_InjectAttribs(CAttributeList* pAttrList)
{
    if (!g_attrLockInit) return;
    EnterCriticalSection(&g_attrLock);
    __try {
        int sz    = pAttrList->m_Attributes.m_Size;
        int alloc = pAttrList->m_Attributes.m_Memory.m_nAllocationCount;

        // Basic sanity on the list header.
        if (sz < 0 || sz > 64 || alloc < 0 || alloc > 512) { LeaveCriticalSection(&g_attrLock); return; }
        if (sz > alloc && alloc > 0) { LeaveCriticalSection(&g_attrLock); return; }

        // Do not bail when alloc==0. AddToTail uses malloc+memcpy
        // (never realloc) so it is safe to grow buffers that were originally
        // allocated by the engine's heap. realloc on an engine pointer = crash.

        // Use the engine's schema API (GetItemSchema->GetAttributeDefinition->
        // SetRuntimeAttributeValue) so attributes are visible to the killstreak
        // proxy system.  Raw SetOrAdd is kept as a fallback when sigs fail.
        EngineSetAttribute(pAttrList, 2025, 1.0f); // killstreak_tier
        EngineSetAttribute(pAttrList, 2014, 1.0f); // killstreak_idleeffect
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    LeaveCriticalSection(&g_attrLock);
}
// ---- Skinchanger apply helpers -----------------------------
void Skin_RedirectIndex(int& d)
{
    switch (d) {
        case 13: d=200; break; // Scattergun
        case 23: d=209; break; // Scout Pistol
        case  0: d=190; break; // Bat
        case 18: d=205; break; // Rocket Launcher
        case 10: d=199; break; // Soldier Shotgun
        case  6: d=196; break; // Shovel
        case 21: d=208; break; // Flamethrower
        case 12: d=199; break; // Pyro Shotgun
        case  2: d=192; break; // Fire Axe
        case 19: d=206; break; // Grenade Launcher
        case 20: d=207; break; // Stickybomb Launcher
        case  1: d=191; break; // Bottle
        case 15: d=202; break; // Minigun
        case 11: d=199; break; // Heavy Shotgun
        case  5: d=195; break; // Fists
        case  9: d=199; break; // Engi Shotgun
        case 22: d=209; break; // Engi Pistol
        case  7: d=197; break; // Wrench
        case 29: d=211; break; // Medi Gun
        case  8: d=198; break; // Bonesaw
        case 14: d=201; break; // Sniper Rifle
        case 16: d=203; break; // SMG
        case  3: d=193; break; // Kukri
        case 24: d=210; break; // Revolver
        case  4: d=194; break; // Knife
        // Festive variants -> same config key as their base weapon
        case  669: d=200; break; // Festive Scattergun -> Scattergun
        case  660: d=190; break; // Festive Bat
        case  999: d=221; break; // Festive Holy Mackerel
        case 1078: d= 45; break; // Festive Force-a-Nature
        case 1145: d= 46; break; // Festive Bonk! Atomic Punch
        case  658: d=205; break; // Festive Rocket Launcher
        case 1001: d=129; break; // Festive Buff Banner
        case 1085: d=228; break; // Festive Black Box
        case 1141: d=199; break; // Festive Shotgun (all classes)
        case  659: d=208; break; // Festive Flamethrower
        case 1000: d= 38; break; // Festive Axtinguisher
        case 1081: d= 39; break; // Festive Flare Gun
        case 1146: d= 40; break; // Festive Backburner
        case  661: d=207; break; // Festive Stickybomb Launcher
        case 1007: d=206; break; // Festive Grenade Launcher
        case 1082: d=132; break; // Festive Eyelander
        case 1144: d=131; break; // Festive Chargin' Targe
        case  654: d=202; break; // Festive Minigun
        case 1002: d= 42; break; // Festive Sandvich
        case 1084: d=239; break; // Festive GRU
        case  662: d=197; break; // Festive Wrench
        case 1004: d=141; break; // Festive Frontier Justice
        case 1086: d=140; break; // Festive Wrangler
        case  663: d=211; break; // Festive Medi Gun
        case 1003: d= 37; break; // Festive Ubersaw
        case 1079: d=305; break; // Festive Crusader's Crossbow
        case 1143: d=198; break; // Festive Bonesaw
        case  664: d=201; break; // Festive Sniper Rifle
        case 1005: d= 56; break; // Festive Huntsman
        case 1083: d= 58; break; // Festive Jarate
        case 1149: d=203; break; // Festive SMG
        case  665: d=194; break; // Festive Knife
        case 1006: d= 61; break; // Festive Ambassador
        case 1142: d=210; break; // Festive Revolver
        // Reskin variants -> same config key as their base weapon
        // Melee class-specific reskins
        case 30667: d=190; break; // Batsaber -> Bat
        case   739: d=192; break; // Lollichop -> Fire Axe
        case   727: d=194; break; // Black Rose -> Knife
        case  1100: d=195; break; // Bread Bite -> Fists
        case   169: d=197; break; // Golden Wrench -> Wrench
        case   266: d=132; break; // Horseless Headless Horsemann's Headtaker -> Eyelander
        case   574: d=225; break; // Wanga Prick -> Your Eternal Reward
        case   452: d=325; break; // Three-Rune Blade -> Boston Basher
        // Ranged/utility class-specific reskins
        case   741: d=208; break; // Rainblower -> Flamethrower
        case 30474: d=208; break; // Nostromo Napalmer -> Flamethrower
        case   160: d=209; break; // Lugermorph -> Pistol
        case 30666: d=209; break; // C.A.P.P.E.R -> Pistol
        case   161: d=210; break; // Big Kill -> Revolver
        case 30668: d=140; break; // Giger Counter -> Wrangler
        case   863: d= 42; break; // Robo-Sandvich -> Sandvich
        case   433: d=159; break; // Fishcake -> Dalokohs Bar
        case  1121: d=222; break; // Mutated Milk -> Mad Milk
        case   572: d=221; break; // Unarmed Combat -> Holy Mackerel
        case   466: d=153; break; // Maul -> Homewrecker
        case  1105: d= 58; break; // Self-Aware Beauty Mark -> Jarate
        case 30665: d=526; break; // Shooting Star -> Machina
        // Spy gadget stock/upgradeable normalization
        case    30: d=212; break; // Invis Watch stock -> upgradeable redir
        case   735: d=736; break; // Sapper stock -> upgradeable redir
        case  1080: d=736; break; // Festive Sapper -> Sapper redir
        // Spy gadget reskins
        case   297: d=212; break; // Enthusiast's Timepiece -> Invis Watch redir
        case   947: d=212; break; // Quackenbirdt -> Invis Watch redir
        case   933: d=736; break; // Ap-Sap -> Sapper redir
        case  1102: d=736; break; // Snack Attack -> Sapper redir
        // Demo boots reskin
        case   608: d=405; break; // Bootlegger -> Ali Baba's Wee Booties
        default: break;
    }
}


void LoadConfig(const std::string& path)
{
    try { LoadConfigImpl(path); }
    catch (...) { DebugLog("config: %s stopped at a malformed alias entry", path.c_str()); }
}
