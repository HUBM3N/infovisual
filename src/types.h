#pragma once
#include "common.h"

// Project-specific structs, typedefs, and constants shared between
// dllmain.cpp and extracted feature files via globals.h.

// -- Config paths -------------------------------------------------------------
static constexpr const char* CONFIG_DIR  = "C:\\infovisual";
static constexpr const char* CONFIG_PATH = "C:\\infovisual\\default.cfg";

// -- Watermark / media-player position enum -----------------------------------
enum WatermarkPos : int {
    WM_TOP_LEFT = 0, WM_TOP_CENTER, WM_TOP_RIGHT,
    WM_BOTTOM_LEFT,  WM_BOTTOM_CENTER, WM_BOTTOM_RIGHT,
};

// -- Watermark max items ------------------------------------------------------
static constexpr int k_wmMaxItems = 16;

// -- Font list entry ----------------------------------------------------------
struct FontEntry { std::string displayName; std::string fullPath; };

// -- Image overlay ----------------------------------------------------------
static constexpr int k_imgMaxSlots = 8;

struct OverlayImageSlot {
    bool    enabled    = false;
    char    path[MAX_PATH] = {};
    float   scale      = 1.f;
    float   rotation   = 0.f;
    float   posX       = 100.f;
    float   posY       = 100.f;
    float   alpha      = 1.f;
    int     anchor     = 0;
    float   anchorOffX = 8.f;
    float   anchorOffY = 0.f;
    bool    pixelArt   = false;

    std::vector<IDirect3DTexture9*> frames;
    std::vector<float>              frameDelays;
    int   texW       = 0;
    int   texH       = 0;
    bool  loaded     = false;
    bool  needsLoad  = false;

    int   curFrame   = 0;
    float frameTimer = 0.f;

    void ReleaseTextures() {
        for (auto* t : frames) if (t) t->Release();
        frames.clear();
        frameDelays.clear();
        texW = texH = 0;
        loaded = false;
        curFrame = 0;
        frameTimer = 0.f;
    }
};

// -- Weather particle dispatch -----------------------------------------------
struct CEffectData_Weather {
    float  m_vOrigin[3];
    float  m_vStart[3];
    float  m_vNormal[3];
    float  m_vAngles[3];
    int    m_fFlags;
    int    m_nEntIndex;
    float  m_flScale;
    float  m_flMagnitude;
    float  m_flRadius;
    int    m_nAttachmentIndex;
    short  m_nSurfaceProp;
    short  _pad4A;
    int    m_nMaterial;
    int    m_nDamageType;
    int    m_nHitBox;
    unsigned char m_nColor;
    bool   m_bCustomColors;
    char   _pad5A[2];
    float  m_CustomColors[6];
    bool   m_bControlPoint1;
    char   _pad75[3];
    int    m_eParticleAttachment;
    float  m_vecOffset[3];
};

using CTE_DispatchEffect_t      = int(__fastcall*)(const char*, const CEffectData_Weather&);
using Get_ParticleSystemIndex_t = int(__fastcall*)(const char*);

// -- Viewmodel chams ---------------------------------------------------------
struct VmChamLayer_t {
    int   matType  = 0;
    float color[4] = {1.f, 1.f, 1.f, 1.f};
    bool  useSheen = false;
    bool  ignorez  = false;
};
static const int k_vmMaxLayers = 6;

// -- Map textures ------------------------------------------------------------
// Swap the map's world-brush textures per surface category, the way a texture
// mod does it, but at runtime and without touching tf/custom. Categories are
// decided by MapTexClassify() in visuals.cpp from the material's own name and
// its $basetexture path.
enum MapTexCategory_t {
    k_mtFloorOrganic = 0,   // dirt / grass / snow / mud / sand / natural gravel
    k_mtFloorBuilt,         // concrete / tile / metal plate / road / cobble floors
    k_mtRock,               // rock walls, cliffs, boulders
    k_mtWood,               // anything wood -- floor, wall, beam, shingle
    k_mtWallBuilt,          // concrete / brick / plaster / metal walls and trim
    k_mtCeiling,            // ceilings and roofs
    k_mapTexCatCount
};

struct MapTexCat_t {
    int   preset     = 0;                    // 0 = leave alone, else k_mapTexPresets index
    float tint[4]    = { 1.f, 1.f, 1.f, 1.f };
    char  custom[80] = {};                   // VTF path, used when preset == custom slot
};

// -- Sheen fake IMaterialVar -------------------------------------------------
inline const float s_fakeSheenVec[4] = { 1.f, 0.f, 0.f, 1.f };

struct FakeSheenIndexVar {
    static void*        _noop()      { return nullptr; }
    static const char*  _noopStr()   { return ""; }
    static bool         _noopBool()  { return false; }
    static int          _noopInt()   { return 0; }
    static float        _noopFloat() { return 0.f; }
    static float const* GetVecVal()  { return s_fakeSheenVec; }

    void** vtbl;
    static void* s_vtable[31];
    static bool  s_inited;

    static void Init() {
        if (s_inited) return;
        for (int i = 0; i < 31; ++i) s_vtable[i] = (void*)_noop;
        s_vtable[1]  = (void*)_noopStr;
        s_vtable[6]  = (void*)_noopStr;
        s_vtable[17] = (void*)_noopBool;
        s_vtable[21] = (void*)_noopBool;
        s_vtable[26] = (void*)_noopInt;
        s_vtable[27] = (void*)_noopFloat;
        s_vtable[28] = (void*)GetVecVal;
        s_vtable[30] = (void*)_noopInt;
        s_inited = true;
    }

    FakeSheenIndexVar() { Init(); vtbl = s_vtable; }
};

// -- Item schema / attribute hooks -------------------------------------------
using GetItemSchema_t              = void*(__fastcall*)();
using GetAttributeDefinition_t     = void*(__fastcall*)(void*, int);
using SetRuntimeAttributeValue_t   = void (__fastcall*)(void*, void*, float);

// -- Title cycle -------------------------------------------------------------
static constexpr int k_maxTitleCycle = 16;
struct TitleEntry_t { char text[256] = {}; };

// -- Per-side BG gradient (MP + WM) ------------------------------------------
struct BgSideGrad {
    bool  enable  = false;
    float start   = 0.4f;  // fraction from edge where full alpha is reached (inner boundary)
    float end     = 0.0f;  // fraction from edge where alpha is 0 (outer boundary)
};

// -- Outline layers ----------------------------------------------------------
static constexpr int k_maxOutlineLayers = 4;
struct OutlineLayer {
    bool  enable    = false;
    float thick     = 1.0f;
    int   sideFlags = 0;
    float color[4]  = { 1.0f, 1.0f, 1.0f, 0.6f };
    float blur      = 0.0f;
};

// -- Name list ---------------------------------------------------------------
struct NameEntry_t { char text[64] = {}; };
static const int k_maxNames = 16;

// -- Entity list stub --------------------------------------------------------
struct IClientEntityList {};

// -- Crash handler -----------------------------------------------------------
struct PersistCrashData {
    volatile LONG active;
    volatile LONG writing;   // re-entrancy guard while a crash log is being written
    char          logPath[MAX_PATH];
    char          ejectNote[256];
};

// -- Media Player state ------------------------------------------------------
struct MediaPlayerState {
    std::string            title;
    std::string            artist;
    bool                   isPlaying    = false;
    int                    totalTimeMs  = 0;
    int                    positionMs   = 0;
    std::chrono::steady_clock::time_point posTimestamp;
    uint64_t               posGeneration = 0;
    IDirect3DTexture9*     thumbTex     = nullptr;
    int                    thumbW       = 0;
    int                    thumbH       = 0;
    bool                   hasThumb     = false;
};

// -- Attribute injection ------------------------------------------------------
// CEconItemAttribute -- layout verified against SkinChanger.cpp reference.
// The void* pad at offset 0 is CRITICAL: without it every field is misaligned.
struct CEconItemAttribute
{
    void*    pad = nullptr;
    uint32_t m_iAttributeDefinitionIndex = 0;
    uint32_t _align32 = 0;
    union { int m_iRawValue32; float m_flValue; };
    int m_nRefundableCurrency = 0;

    CEconItemAttribute(uint16_t idx, float val)
        : pad(nullptr), m_iAttributeDefinitionIndex(idx), _align32(0), m_flValue(val), m_nRefundableCurrency(0) {}
};

struct CUtlMemory_Attr { CEconItemAttribute* m_pMemory; int m_nAllocationCount; int m_nGrowSize; };
struct CUtlVector_Attr
{
    CUtlMemory_Attr     m_Memory;
    int                 m_Size;
    CEconItemAttribute* m_pElements;
    CEconItemAttribute* begin() const { return m_Memory.m_pMemory; }
    CEconItemAttribute* end()   const { return m_Memory.m_pMemory + m_Size; }
};

struct CAttributeList
{
    void*           pad;
    CUtlVector_Attr m_Attributes;
    void*           m_pManager;

    void AddToTail(uint16_t index, float value)
    {
        if (m_Attributes.m_Size < 0 || m_Attributes.m_Size >= 48) return;
        if (m_Attributes.m_Size >= m_Attributes.m_Memory.m_nAllocationCount) return;
        if (!m_Attributes.m_Memory.m_pMemory) return;
        if (reinterpret_cast<uintptr_t>(m_Attributes.m_Memory.m_pMemory) < 0x10000) return;
        m_Attributes.m_Memory.m_pMemory[m_Attributes.m_Size++] = CEconItemAttribute(index, value);
    }

    void SetOrAdd(uint16_t index, float value)
    {
        if (m_Attributes.m_Memory.m_pMemory &&
            reinterpret_cast<uintptr_t>(m_Attributes.m_Memory.m_pMemory) >= 0x10000)
        {
            for (int i = 0; i < m_Attributes.m_Size; ++i)
            {
                if (m_Attributes.m_Memory.m_pMemory[i].m_iAttributeDefinitionIndex == index)
                {
                    m_Attributes.m_Memory.m_pMemory[i].m_flValue = value;
                    return;
                }
            }
        }
        AddToTail(index, value);
    }
};
