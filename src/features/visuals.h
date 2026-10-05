#pragma once
#include "globals.h"
#include "config.h"
#include "vmglow.h"

// --- Fog / skybox / modulate state -------------------------------------------
extern char  s_fogColorBuf[32];
extern char* s_fogColorOrigPtr;
extern int   s_fogColorOrigLen;
extern char  s_fogColorSkyBuf[32];
extern char* s_fogColorSkyOrigPtr;
extern int   s_fogColorSkyOrigLen;

void ResetMatCache(bool bUnloading = false);
void ApplyMatColorModulate();   // crit / DR colours, game thread
void Modulate_Tick();           // world + sky modulation, game thread
void Modulate_Restore();
void Sky_Tick();                // custom skybox, game thread
void Sky_Restore();
void UpdateVisualsCvars();
void VisualsCvars_Release();   // hand every held cvar back (eject)
void UpdateNetStats();
void UpdateFPS();

// Level lifecycle (IBaseClientDLL slots 6/7), both bump g_matEpoch.
using LevelFn_t = void(__fastcall*)(void*);
extern LevelFn_t o_LevelInitPostEntity;
extern LevelFn_t o_LevelShutdown;
void __fastcall h_LevelInitPostEntity(void* rcx);
void __fastcall h_LevelShutdown(void* rcx);

// --- Chams / DrawModel hooks -------------------------------------------------
using ForcedMaterialOverride_fwd_t      = void(__fastcall*)(void*, void*, int);
using DrawModelExecute_t                = void(__fastcall*)(void*, const void*, const void*, const void*);
using DrawViewModels_t                  = void(__fastcall*)(void*, const void*, bool);
using CBaseAnimatingDrawModel_t         = int(__fastcall*)(void*, int);
using CBaseAnimatingInternalDraw_t      = int(__fastcall*)(void*, int);

extern ForcedMaterialOverride_fwd_t      o_ForcedMatOverride_fwd;
extern DrawModelExecute_t                o_DrawModelExecute;
extern DrawViewModels_t                  o_DrawViewModels;
extern CBaseAnimatingDrawModel_t         o_CBaseAnimatingDrawModel;
extern CBaseAnimatingInternalDraw_t      o_CBaseAnimatingInternalDraw;

void __fastcall h_ForcedMatOverride(void* rcx, void* pMat, int nType);
void __fastcall h_DrawModelExecute(void* rcx, const void* pState, const void* pInfo, const void* pBones);
void __fastcall h_DrawViewModels(void* rcx, const void* pViewSetup, bool drawViewmodel);
int  __fastcall h_CBaseAnimatingDrawModel(void* rcx, int flags);
int  __fastcall h_CBaseAnimatingInternalDraw(void* rcx, int flags);
void VmCull_Resolve(HMODULE client, uint8_t* fnInternalDraw);   // viewmodel cull windows (thread.cpp, with the hook)
int  VmGameCull();   // CullMode the game set for the viewmodel being drawn: 1 CW (flipped), 0 CCW / not in one

void* VmGetMat(int type);
void  VmResetMatCache();
void  PlResetMatCache();
// --- Map textures (accessors declared in globals.h) --------------------------
void  ApplyMapTextures();
void  VmFlatAlpha_Tick();       // FSN stage 5: $blendtintbybasealpha flips for transparent viewmodel
void  ResetMapTextures(bool bRefreshMaterials);
bool  MapTexturesOwnWorld();
// Eject teardown that must run on the game thread. Eject thread sets the state
// to 1 and waits for 3; FrameStageNotify drives it.
extern std::atomic<int>  g_ejectRestoreState;
extern std::atomic<bool> g_ejectRestoreDidWork;   // true once the pass really ran
void  EjectRestore_Service();
void  BodyRules_Tick();         // functional items per player, for the No hats / Zombify body
void  NoHats_Tick();            // cosmetic particle suppression
void  RebuildAttachmentOwners();
void  KsEyeTick();

// --- Weather -----------------------------------------------------------------
struct TrackedWeather_t { float x, y, z; void* pEffect; };
extern std::vector<TrackedWeather_t> s_trackedWeather[8];
extern bool s_precipHandleSearched;

using FindMaterial_t = void*(__fastcall*)(void*, const char*, const char*, bool, const char*);
extern FindMaterial_t o_FindMaterial;

void* __fastcall h_FindMaterial(void* rcx, const char* pName, const char* pTextureGroup,
    bool bComplain, const char* pComplainPrefix);

void Precip_TearDown(int kSlot);
void Rain_Tick();

// --- Ragdoll -----------------------------------------------------------------
using CreateTFRagdoll_t = void(__fastcall*)(void*);
extern CreateTFRagdoll_t o_CreateTFRagdoll;
void __fastcall h_CreateTFRagdoll(void* rcx);

// --- Viewmodel / prop hooks --------------------------------------------------
using CalcViewModelView_t   = void(__fastcall*)(void*, void*, void*, void*);
extern CalcViewModelView_t   o_CalcViewModelView;
void __fastcall h_CalcViewModelView(void* rcx, void* owner, void* pEyePosition, void* pEyeAngles);
void VmCsgo_Install(HMODULE client);   // CS:GO bob hook (vm_csgo_hook.inl)
void TryInstallPropHooks();

// --- Particle property hooks -------------------------------------------------
struct PtVec { float x, y, z; };
using CParticlePropCreate_t   = void*(__fastcall*)(void*, const char*, int, const char*);
using CParticlePropCreatePt_t = void*(__fastcall*)(void*, const char*, int, int, PtVec);
extern CParticlePropCreate_t   o_CParticlePropCreate;
extern CParticlePropCreatePt_t o_CParticlePropCreatePt;
void* __fastcall h_CParticlePropCreate(void* pProp, const char* pszParticleName, int iAttachType, const char* pszAttachmentName);
void* __fastcall h_CParticlePropCreatePt(void* pProp, const char* pszParticleName, int iAttachType, int iAttachmentPoint, PtVec vecOriginOffset);
