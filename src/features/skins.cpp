#include "globals.h"
#include "config.h"
#include "features/visuals.h"
#include "misc.h"
#include "skins.h"
#include "playerlist.h"
#include "websurface.h"
#include "features/flipworld.h"
#include "features/demostreak.h"
#include "screenfx.h"

static int Skin_GetFestiveIdx(int redirIdx)
{
    for (int i = 0; i < k_wpnCount; i++)
        if (k_wpnList[i].redir == redirIdx && k_wpnList[i].festiveIdx > 0)
            return k_wpnList[i].festiveIdx;
    return 0;
}

int Skin_FindSpecificDef(int weaponRedir, int pkId)
{
    for (int i = 0; i < k_skinDefCount; i++)
        if (k_skinDefs[i].weaponRedir == weaponRedir && k_skinDefs[i].pkId == (int8_t)pkId)
            return k_skinDefs[i].skinDef;
    return 0;
}

int Skin_FindWeaponRedir(int skinDef)
{
    for (int i = 0; i < k_skinDefCount; i++)
        if (k_skinDefs[i].skinDef == skinDef)
            return k_skinDefs[i].weaponRedir;
    return 0;
}

// Which sheen row this weapon uses -- the row an override color is written into. Defined with the
// killstreak color code further down.
static int KsSheenIndexFor(const SkinCfg& c);

__declspec(noinline) static void Skin_Apply(void* pWeapon, int defIdx)
{
    auto it = g_skinCfgs.find(defIdx);
    if (it == g_skinCfgs.end() || !it->second.enabled) return;
    const SkinCfg& c = it->second;

    CAttributeList* pList = reinterpret_cast<CAttributeList*>(
        reinterpret_cast<uintptr_t>(pWeapon) + g_fsn_offAttrList);

    __try {
        if (c.weaponSkinDef > 0) {
            EngineSetAttribute(pList, 725,  c.wear);
            EngineSetAttribute(pList, 731,  1.0f);
            EngineSetAttribute(pList, 2022, 6.0f);
            uint32_t slo = (uint32_t)(c.pattern & 0xFFFFFFFF);
            uint32_t shi = (uint32_t)(c.pattern >> 32);
            EngineSetAttribute(pList, 866, *reinterpret_cast<float*>(&slo));
            EngineSetAttribute(pList, 867, *reinterpret_cast<float*>(&shi));
        } else if (c.paintKitId > 0) {
            float kitF = *reinterpret_cast<const float*>(&c.paintKitId);
            EngineSetAttribute(pList, 834,  kitF);
            EngineSetAttribute(pList, 725,  c.wear);
            EngineSetAttribute(pList, 731,  1.0f);
            EngineSetAttribute(pList, 2022, 6.0f);
            uint32_t slo = (uint32_t)(c.pattern & 0xFFFFFFFF);
            uint32_t shi = (uint32_t)(c.pattern >> 32);
            EngineSetAttribute(pList, 866, *reinterpret_cast<float*>(&slo));
            EngineSetAttribute(pList, 867, *reinterpret_cast<float*>(&shi));
        }
        if (c.ksTier > 0) {
            EngineSetAttribute(pList, 2025, (float)c.ksTier);
            // KsSheenIndexFor, not c.ksSheen: an override color has to live in a sheen ROW, so
            // "override color with no sheen picked" still writes one (team shine).
            const int sheenIdx = KsSheenIndexFor(c);
            if (sheenIdx > 0)
                EngineSetAttribute(pList, 2014, (float)sheenIdx);
            if (c.ksTier == 3 && c.ksEffect > 0) {
                static const int k_ksEffectIds[] = { 0, 2002, 2003, 2004, 2005, 2006, 2007, 2008 };
                int effectId = (c.ksEffect < (int)(sizeof(k_ksEffectIds)/sizeof(k_ksEffectIds[0])))
                    ? k_ksEffectIds[c.ksEffect] : 0;
                if (effectId > 0)
                    EngineSetAttribute(pList, 2013, (float)effectId);
            }
        }
        if (c.unusual > 0) {
            EngineSetAttribute(pList, 134,  (float)c.unusual);
            EngineSetAttribute(pList, 370,  (float)c.unusual);
        }
        if (c.australium) {
            EngineSetAttribute(pList, 2027, 1.0f);
            EngineSetAttribute(pList, 542,  1.0f);
            EngineSetAttribute(pList, 2022, 6.0f);
        }
        if (c.festivized)  EngineSetAttribute(pList, 2053, 1.0f);
        // Stat clock. Two gates decide whether the clock entity is ever built
        // (CTFWeaponBase::AddStatTrakModel, tf_weaponbase.cpp:6730): a kill-eater score attribute
        // (214 below) and GetStattrak(), which needs a paint kit id AND the string attribute
        // weapon_uses_stattrak_module -- and that string is a STATIC attr on the paintkit_base
        // prefab, so the weapon has to be wearing a 15xxx warpaint def (ResolveSkinTarget puts it
        // in one when no skin was picked). Here the paint that def would otherwise apply is
        // cancelled: 834 gets an id no CPaintKitDefinition answers for, so no composite is built
        // (c_tf_player.cpp:3583 and GetPaintKitMaterialOverride both null-check it) and the
        // weapon keeps its normal material.
        if (c.statClock) {
            float kf = *reinterpret_cast<const float*>(&c.statKills);
            EngineSetAttribute(pList, 214, kf);
            EngineSetAttribute(pList, 292, 0.0f);
            if (c.weaponSkinDef <= 0 && c.paintKitId <= 0) {
                static constexpr int k_noPaintKit = 30000;
                EngineSetAttribute(pList, 834, *reinterpret_cast<const float*>(&k_noPaintKit));
                EngineSetAttribute(pList, 725, 0.2f);   // wear; GetPaintKitWear must answer
            }
        }
        if (c.spellFlags & kCfgSpell_Exorcism) EngineSetAttribute(pList, 1009, 1.0f);
        if (c.spellFlags & kCfgSpell_Pumpkin)  EngineSetAttribute(pList, 1007, 1.0f);
        if (c.spellFlags & kCfgSpell_Fire)      EngineSetAttribute(pList, 1008, 1.0f);
        // The reskin's own item properties, taken from items_game.txt instead of hand-listed
        // def indexes: attr 150 set_turn_to_gold is what a Saxxy / Golden Wrench / Gold Frying
        // Pan carries, 551 special_taunt is the taunt kill, and 542 item_style_override is how a
        // multi-style reskin (the Black Rose's black variant) picks its skin pair. Australium
        // already owns 542, so it wins when both are set.
        if (c.reskinDef > 0 && !c.australium) {
            if (const ReskinInfo* ri = Skin_FindReskinInfo(c.reskinDef)) {
                if (ri->turnToGold)   EngineSetAttribute(pList, 150, 1.0f);
                if (ri->specialTaunt) EngineSetAttribute(pList, 551, 1.0f);
                if (ri->styleCount > 1 && c.reskinStyle > 0 && c.reskinStyle < ri->styleCount)
                    EngineSetAttribute(pList, 542, (float)c.reskinStyle);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- DoPostScreenSpaceEffects hook -------------------------
using DoPostSSE_t = bool(__fastcall*)(void*, const void*);
DoPostSSE_t o_DoPostSSE = nullptr;

bool __fastcall h_DoPostSSE(void* rcx, const void* pSetup)
{
    FlipWorld_WorldPass();   // this frame draws the world: the mirror must follow before the next frame
    bool ret = o_DoPostSSE(rcx, pSetup);
    // The depth passes. Ambient occlusion and the lens blur run over a finished world -- and
    // still before DrawViewModels and the 2D HUD, which is what keeps the gun and the hud
    // sharp. It resolves the z buffer itself; see screenfx.h.
    // The world screen goes in AFTER the depth passes: ambient occlusion and the lens blur never
    // touch it (asked for -- a blurred, darkened page is unreadable). It still depth-tests against
    // the same z buffer, and both queue onto the render thread in this order.
    ScreenFx_OnPostScreenSpaceEffects(pSetup);
    Web_OnPostScreenSpaceEffects(pSetup);
    // Flip World: the world is finished and DrawViewModels is the next line (viewrender.cpp:2160), so
    // the gun draws unmirrored over the mirror. After the depth passes, so they read a z buffer that
    // still matches the colour they blur.
    FlipWorld_Render(pSetup);
    return ret;
}

// ---- FrameStageNotify hook ---------------------------------

uintptr_t g_skinPreWriteRetAddr = 0;

using FrameStageNotify_t = void(__fastcall*)(int);
FrameStageNotify_t o_FrameStageNotify = nullptr;

// ---- Reskin runtime info ------------------------------------------------------------------
// Model path, anim family, style skins, item flags and the killfeed icon name all live in
// k_reskinInfo (data/skindata.h), GENERATED from TF2's own items_game.txt. The hand-typed
// table that used to sit here had 15 wrong model paths, and a path the engine cannot load
// leaves the STOCK model on screen with no error at all.

static const char* GetReskinModelPath(int defindex)
{
    const ReskinInfo* ri = Skin_FindReskinInfo(defindex);
    return ri ? ri->model : nullptr;
}

// ---- IVModelInfo wrappers ----------------------------------------------------------------
// Slots off the SDK's own public/engine/ivmodelinfo.h ("VModelInfoClient006"):
//   1 GetModel, 2 GetModelIndex, 3 GetModelName, 21 FindModel(hdr, cache, name),
//   28 GetStudiomodel, 46 GetCacheHandle, 52 RegisterDynamicModel, 53 IsDynamicModelLoading,
//   54 AddRefDynamicModel.
// Do NOT count these off a trimmed header: six obsolete-but-present stubs are easy to drop and
// everything past FindOrLoadModel then comes out six slots short (that is where
// "RegisterDynamicModel is 46" came from -- 46 is GetCacheHandle).

static const void* ModelInfo_GetModel_ptr(int idx)
{
    if (!g_modelInfo || idx == 0) return nullptr;
    const void* result = nullptr;
    __try {
        using Fn = const void*(__fastcall*)(void*, int);
        result = ((Fn)(*(void***)g_modelInfo)[1])(g_modelInfo, idx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

static const char* ModelInfo_GetModelName_ptr(const void* pModel)
{
    if (!g_modelInfo || !pModel) return nullptr;
    const char* result = nullptr;
    __try {
        using Fn = const char*(__fastcall*)(void*, const void*);
        result = ((Fn)(*(void***)g_modelInfo)[3])(g_modelInfo, pModel);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

// Slot 28: GetStudiomodel(const model_t*) -> studiohdr_t*
static const void* ModelInfo_GetStudiomodel_ptr(const void* pModel)
{
    if (!g_modelInfo || !pModel) return nullptr;
    const void* result = nullptr;
    __try {
        using Fn = const void*(__fastcall*)(void*, const void*);
        result = ((Fn)(*(void***)g_modelInfo)[28])(g_modelInfo, pModel);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

// Slot 22: FindModel(const studiohdr_t*, void** cache, const char* name). The three-argument
// one -- what studiohdr_t::FindModel wraps and what virtualmodel_t::AppendModels calls for every
// $includemodel. It hands the include's studiohdr back DIRECTLY: no model index, no dynamic
// registration, no async wait.
//
// NOT 21: MSVC puts overloaded virtuals in reverse declaration order, so 21 is FindModel(void*)
// (engine.dll disasm: a bare jmp to IMDLCache::GetStudioHdr). Called with a studiohdr, 21 took
// the pointer's low 16 bits as an MDL handle: some other model's header, or an AV in
// datacache.dll when that handle slot was empty (crash.log 2026-09-13, phase fsn_5).
// 22 is `if (!*cache) *cache = GetModelForName(name); return GetExtraData(*cache)`.
//
// The old code walked virtualmodel_t by hand (slots 22/23) to get at the merged sequence list.
// That is dead: TF2's x64 tier1 does not lay CUtlVector out the way the 2013 headers do, so
// every probed offset answered garbage. The merged list is rebuilt from the mdl files instead.
__declspec(noinline) static const void* ModelInfo_FindInclude(const void* pHdr, void** cache,
                                                              const char* name)
{
    if (!g_modelInfo || !pHdr || !cache || !name) return nullptr;
    const void* r = nullptr;
    __try {
        using Fn = const void*(__fastcall*)(void*, const void*, void**, const char*);
        r = ((Fn)(*(void***)g_modelInfo)[22])(g_modelInfo, pHdr, cache, name);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return r;
}

// AddRefDynamicModel, slot 54 -- only meaningful for a dynamic index (< -1). Without a
// reference the engine never finishes (or even starts) the load, which is what
// "include not loaded yet (idx=-17)" was.
__declspec(noinline) static void ModelInfo_AddRefDynamic(int idx)
{
    if (!g_modelInfo || idx >= -1) return;
    __try {
        using Fn = void(__fastcall*)(void*, int);
        ((Fn)(*(void***)g_modelInfo)[54])(g_modelInfo, idx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static int ModelInfo_GetModelIndex(const char* path)
{
    if (!g_modelInfo || !path) return -1;
    int result = -1;
    __try {
        using Fn = int(__fastcall*)(void*, const char*);
        result = ((Fn)(*(void***)g_modelInfo)[2])(g_modelInfo, path);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

static int s_registerSlot = 52;   // switched to 46 only if 52 fails validation, then logged

__declspec(noinline) static int ModelInfo_RegisterDynamicAt(int slot, const char* path)
{
    if (!g_modelInfo || !path) return -1;
    int result = -1;
    __try {
        using Fn = int(__fastcall*)(void*, const char*, bool);
        result = ((Fn)(*(void***)g_modelInfo)[slot])(g_modelInfo, path, true);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

// True when idx names exactly the model we asked for. A dynamic index is < -1, a precached one
// > 0; anything else (0, -1, or an index whose name does not match) is not an answer. A wrong
// slot is SILENT -- it returns a plausible small integer -- so the result is validated by name.
static bool ModelIdxIs(int idx, const char* path)
{
    if (idx == 0 || idx == -1) return false;
    const void* m = ModelInfo_GetModel_ptr(idx);
    const char* n = m ? ModelInfo_GetModelName_ptr(m) : nullptr;
    return n && _stricmp(n, path) == 0;
}

__declspec(noinline) static int ModelInfo_RegisterDynamic(const char* path)
{
    int r = ModelInfo_RegisterDynamicAt(s_registerSlot, path);
    if (ModelIdxIs(r, path)) return r;
    int byName = ModelInfo_GetModelIndex(path);
    if (ModelIdxIs(byName, path)) return byName;

    const int other = (s_registerSlot == 52) ? 46 : 52;
    const int r2 = ModelInfo_RegisterDynamicAt(other, path);
    if (ModelIdxIs(r2, path)) {
        DebugLog("skins: RegisterDynamicModel slot %d failed validation, slot %d works -- "
                 "using %d from here", s_registerSlot, other, other);
        s_registerSlot = other;
        return r2;
    }
    byName = ModelInfo_GetModelIndex(path);
    if (ModelIdxIs(byName, path)) return byName;
    DebugLog("skins: RegisterDynamicModel gave nothing usable for '%s' (slot %d -> %d, "
             "slot %d -> %d)", path, s_registerSlot, r, other, r2);
    return -1;
}

__declspec(noinline) static bool ModelInfo_IsDynLoading(int modelIndex)
{
    if (!g_modelInfo || modelIndex == 0 || modelIndex == -1) return false;
    bool result = false;
    __try {
        using Fn = bool(__fastcall*)(void*, int);
        result = ((Fn)(*(void***)g_modelInfo)[53])(g_modelInfo, modelIndex);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

// A model index for a reskin path. -1/0 = "not precached", the normal answer for a weapon
// nobody in the server owns -- and that is NOT cosmetic bookkeeping:
// CEconEntity::UpdateAttachmentModels builds the weapon in the player's hands with
// InitializeAsClientEntity(pItem->GetPlayerDisplayModel()), which bails on
// GetModelIndex(name) == -1 (c_baseentity.cpp:1136) -- no attachment entity, so the viewmodel
// draws the ARMS AND NOTHING ELSE. Registering the path is what makes GetModelIndex answer, and
// it has to happen before the def index we wrote is unpacked.
struct ReskinIdxEntry { int idx; int tries; };
static constexpr int k_maxRegisterTries = 8;

static int GetReskinViewmodelIdx(int defindex)
{
    static std::unordered_map<int, ReskinIdxEntry> s_cache;
    ReskinIdxEntry& e = s_cache[defindex];
    if (e.idx != 0 || e.tries >= k_maxRegisterTries) return e.idx;

    const char* path = GetReskinModelPath(defindex);
    if (!path) { e.tries = k_maxRegisterTries; return 0; }
    if (!g_modelInfo) return 0;   // not an answer, and must not burn the retry budget

    ++e.tries;
    int raw = ModelInfo_GetModelIndex(path);
    if (!ModelIdxIs(raw, path)) raw = ModelInfo_RegisterDynamic(path);
    if (raw > 0 || raw < -1) e.idx = raw;

    if (e.idx != 0 || e.tries == k_maxRegisterTries)
        DebugLog("skins: reskin def=%d '%s' idx=%d%s loading=%d", defindex, path, e.idx,
                 (e.idx < -1) ? " (dynamic)" : (e.idx == 0 ? " -- NOT FOUND, gave up" : ""),
                 (int)ModelInfo_IsDynLoading(e.idx));
    return e.idx;
}

// ---- Sequence remap ----------------------------------------------------------------------
// A reskin that plays different sequences to the weapon it replaces looks wrong: the base asks
// for ACT_MELEE_VM_IDLE and an all-class model only carries ACT_MELEE_ALLCLASS_VM_IDLE.
//
// WHAT DECIDES THE MAPPING, in TF2's own order
// (CTFWeaponBase::TranslateViewmodelHandActivityInternal, tf_weaponbase.cpp:4415):
//   1. the item's animation_replacement block -- GetActivityOverride is consulted FIRST and
//      returns immediately when it answers. Exact activity->activity, and for 27 of the 44
//      reskins it is the whole story. The Snack Attack has the SAME anim_slot as the stock
//      sapper ("building") and differs only there (ACT_VM_DRAW -> ACT_BREADSAPPER_VM_DRAW),
//      which is why a slot-only remap left a bread monster playing the sapper's animation.
//   2. failing that, s_viewmodelacttable: per-family string surgery on the base activity
//      (ACT_VM_<X> + family ITEM1 -> ACT_ITEM1_VM_<X>), carried here as the family prefix.
// Both halves are generated into skindata.h per reskin AND per base weapon.
//
// WHICH MODEL. Not the weapon's. CTFWeaponBase::GetViewModel returns the CLASS ARMS model for
// any item that ShouldAttachToHands (tf_weaponbase.cpp:651) -- the weapon c_model is a separate
// bonemerged attachment -- so the viewmodel entity's model is c_<class>_arms.mdl and every
// SetSequence index is an index into ITS list. Comparing c_knife.mdl against c_saxxy.mdl (what
// this file used to do) is a pair of models the engine never indexes here; that alone is the
// whole of "mapped=0".
//
// The arms model's own sequences carry NO activity names; the names live in its $includemodel
// (c_spy_animations.mdl), which holds every family a remap wants:
//     knife_idle          ACT_MELEE_VM_IDLE            acr_idle     ACT_ITEM1_VM_IDLE
//     melee_allclass_idle ACT_MELEE_ALLCLASS_VM_IDLE   eternal_idle ACT_ITEM2_VM_IDLE
// so this is a WITHIN-ONE-MODEL family swap and no base c_model is read.
//
// studiohdr_t layout (studio.h, x64 -- every pointer field is an int there, so the ON-DISK
// offsets hold): numlocalseq +0xBC, localseqindex +0xC0, mstudioseqdesc_t stride 0xD4,
// szlabelindex +0x04, szactivitynameindex +0x08, numincludemodels +0x150, includemodelindex
// +0x154, mstudiomodelgroup_t { int szlabelindex; int sznameindex; } stride 8.

static int      g_seqRemap[512];
static bool     g_seqRemapBuilt = false;
static AnimSlot g_activeFrom    = AS_DEFAULT;
static AnimSlot g_activeTo      = AS_DEFAULT;
static int      g_activeReskinDef = 0;
static int      g_activeBaseKey = 0;
static int      g_remapTries    = 0;
static const ReskinInfo*    g_activeReskinInfo = nullptr;
static const BaseAnimEntry* g_activeBaseInfo   = nullptr;
static bool                 g_activeNeedsRemap = false;
void*       g_pLocalViewmodelEnt = nullptr;

// A reskin's model can still be loading (it is registered dynamically), and a studiohdr that is
// not there yet maps nothing. Rebuilding on every SetSequence would walk the tables hundreds of
// times a second, so retries are capped.
static constexpr int k_maxRemapTries = 240;

// The built table is kept beside the live one, keyed by what it was actually built FROM.
// The clear above fires far more often than the ANSWER changes: the held weapon's def index
// flips between the server's value and the one we wrote, so UpdateActiveRemap sees a "change"
// every few frames and wipes a table that was already correct. Rebuilding costs a FindModel
// walk plus a few thousand _stricmp on the game thread -- a friend's log shows 1038 rebuilds in
// one session, all producing the identical mapping. With the signature the rebuild runs once
// per real change and the rest are a 2 KB memcpy.
static int         g_seqRemapCache[512];
static const void* g_builtModel     = nullptr;   // viewmodel model_t the cache was built from
static int         g_builtReskinDef = 0;
static int         g_builtBaseKey   = 0;
static bool        g_cacheValid     = false;

static void ClearSeqRemap()
{
    memset(g_seqRemap, 0xFF, sizeof(g_seqRemap));
    g_seqRemapBuilt = false;
    g_remapTries    = 0;
}

static void ResetActiveRemap()
{
    g_activeReskinDef  = 0;
    g_activeBaseKey    = 0;
    g_activeFrom       = AS_DEFAULT;
    g_activeTo         = AS_DEFAULT;
    g_activeReskinInfo = nullptr;
    g_activeBaseInfo   = nullptr;
    g_activeNeedsRemap = false;
    // Drop the cache too: this runs on a local-entity change (so, every map load) and a model_t
    // address can be recycled by the next map, which would make the signature match a stale table.
    g_cacheValid = false;
    g_builtModel = nullptr;
    ClearSeqRemap();
}

// Where a backstab lands in the target family. All-class models have no backstab at all, so
// the whole ACT_BACKSTAB_VM_* family collapses onto that family's idle.
static const char* BackstabTarget(AnimSlot to, bool& outIsPrefix)
{
    switch (to) {
    case AS_MELEE_ALLCLASS: outIsPrefix = false; return "ACT_MELEE_ALLCLASS_VM_IDLE";
    case AS_ITEM1:          outIsPrefix = true;  return "ACT_ITEM1_BACKSTAB_VM_";
    case AS_ITEM2:          outIsPrefix = true;  return "ACT_ITEM2_BACKSTAB_VM_";
    default:                outIsPrefix = false; return nullptr;
    }
}

// The stun is the one activity named after the family it BELONGS to instead of being prefixed
// by it: knife_stun is ACT_MELEE_VM_STUN but acr_stun is ACT_MELEE_VM_ITEM1_STUN.
static const char* StunTarget(AnimSlot to)
{
    switch (to) {
    case AS_ITEM1: return "ACT_MELEE_VM_ITEM1_STUN";
    case AS_ITEM2: return "ACT_MELEE_VM_ITEM2_STUN";
    default:       return nullptr;
    }
}

__declspec(noinline) static int HDR_SeqCount(const void* pHdr)
{
    __try {
        if (!pHdr) return 0;
        if (*(int*)pHdr != 0x54534449) return 0; // "IDST"
        const int n = *(int*)((char*)pHdr + 0xBC);
        return (n >= 0 && n <= 4096) ? n : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

__declspec(noinline) static const char* HDR_SeqActName(const void* pHdr, int i)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return nullptr;
        const int numSeq = *(int*)((char*)pHdr + 0xBC);
        if (i < 0 || i >= numSeq) return nullptr;
        const int seqOff = *(int*)((char*)pHdr + 0xC0);
        if (seqOff <= 0) return nullptr;
        const char* pD = (char*)pHdr + seqOff + i * 0xD4;
        const int actOff = *(int*)(pD + 0x08);
        if (actOff <= 4 || actOff >= 800000) return nullptr;
        const char* name = pD + actOff;
        return (name[0] >= 'A' && name[0] <= 'Z') ? name : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

__declspec(noinline) static const char* HDR_SeqLabel(const void* pHdr, int i)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return nullptr;
        const int numSeq = *(int*)((char*)pHdr + 0xBC);
        if (i < 0 || i >= numSeq) return nullptr;
        const int seqOff = *(int*)((char*)pHdr + 0xC0);
        if (seqOff <= 0) return nullptr;
        const char* pD = (char*)pHdr + seqOff + i * 0xD4;
        const int lblOff = *(int*)(pD + 0x04);
        if (lblOff <= 4 || lblOff >= 800000) return nullptr;
        return pD + lblOff;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

__declspec(noinline) static int HDR_IncludeCount(const void* pHdr)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return 0;
        const int n = *(int*)((char*)pHdr + 0x150);
        return (n > 0 && n <= 64) ? n : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

__declspec(noinline) static const char* HDR_IncludeName(const void* pHdr, int i)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return nullptr;
        const int n   = *(int*)((char*)pHdr + 0x150);
        const int off = *(int*)((char*)pHdr + 0x154);
        if (i < 0 || i >= n || off <= 0) return nullptr;
        const char* pG = (char*)pHdr + off + i * 8;
        const int nameOff = *(int*)(pG + 4);
        if (nameOff <= 0 || nameOff >= 800000) return nullptr;
        return pG + nameOff;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// studiohdr_t::name, a fixed 64-byte string at +0x0C -- the model in its own words.
__declspec(noinline) static const char* HDR_Name(const void* pHdr)
{
    __try {
        if (!pHdr || *(int*)pHdr != 0x54534449) return nullptr;
        const char* n = (const char*)pHdr + 0x0C;
        return n[0] ? n : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static const char* PathBaseName(const char* p)
{
    const char* s = p;
    for (const char* c = p; *c; ++c)
        if (*c == '/' || *c == '\\') s = c + 1;
    return s;
}

// The name is stored as the studiomdl input path, so only the file name is comparable.
static bool HDR_NamesModel(const void* pHdr, const char* path)
{
    const char* n = HDR_Name(pHdr);
    return n && path && _stricmp(PathBaseName(n), PathBaseName(path)) == 0;
}

// ---- the model's own merged sequence NAME table --------------------------------------------
// The index SetSequence is handed is a VIRTUAL sequence index: for a model with $includemodel
// the engine merges the include's sequences into the parent's list. AppendSequences
// (public/studio_virtualmodel.cpp:142) merges BY LABEL -- group 0 (the model itself) in file
// order, then each include's labels that are not already present -- so the index space is fully
// determined by the label lists in the mdl files. Rebuild that order here and there is nothing
// left to guess, and no runtime container to walk by a guessed offset.
static constexpr int k_maxSeqs = 512;
static const char* g_seqName[k_maxSeqs];    // activity name, null when the sequence has none
static const char* g_seqLabel[k_maxSeqs];
static int         g_seqNameCount = 0;

static int SeqLabelIndex(const char* label)
{
    for (int i = 0; i < g_seqNameCount; ++i)
        if (g_seqLabel[i] && _stricmp(g_seqLabel[i], label) == 0) return i;
    return -1;
}

// Returns false only when an include is named but its studiohdr is not loaded yet -- that is a
// wait, and the caller must keep its retry budget for it.
static bool BuildSeqNames(const void* hdr)
{
    g_seqNameCount = 0;
    const int localCount = HDR_SeqCount(hdr);
    for (int i = 0; i < localCount && g_seqNameCount < k_maxSeqs; ++i) {
        g_seqLabel[g_seqNameCount] = HDR_SeqLabel(hdr, i);
        g_seqName [g_seqNameCount] = HDR_SeqActName(hdr, i);
        ++g_seqNameCount;
    }

    bool complete = true;
    const int incCount = HDR_IncludeCount(hdr);
    for (int k = 0; k < incCount; ++k) {
        const char* path = HDR_IncludeName(hdr, k);
        if (!path || !path[0]) continue;

        // The engine's own include lookup first (slot 22), with a fresh null cache per call
        // exactly as AppendModels does (studio_virtualmodel.cpp:110). A kept cache would hold a
        // model_t* across map changes. The header still has to NAME the model asked for
        // (a missing include can come back as error.mdl); if not, this include alone takes
        // the dynamic path.
        const char* via   = "findmodel";
        void*       cache = nullptr;
        const void* ih    = ModelInfo_FindInclude(hdr, &cache, path);
        if (ih && !HDR_NamesModel(ih, path)) {
            const char* got = HDR_Name(ih);
            DebugLog("skins: anim remap -- FindModel (slot 22) answered for '%s' with "
                     "'%s' (%d seqs) -- not that model, dynamic path for this include",
                     path, got ? got : "?", HDR_SeqCount(ih));
            ih  = nullptr;
            via = "findmodel-wrong";
        }

        // Fallback: register it dynamically, plus the reference the load needs. Slower, and it
        // needs the caller's retries because the load lands asynchronously.
        int idx = 0;
        if (!ih) {
            idx = ModelInfo_GetModelIndex(path);
            if (!ModelIdxIs(idx, path)) idx = ModelInfo_RegisterDynamic(path);
            if (idx < -1) ModelInfo_AddRefDynamic(idx);
            const void* m = (idx > 0 || idx < -1) ? ModelInfo_GetModel_ptr(idx) : nullptr;
            ih  = m ? ModelInfo_GetStudiomodel_ptr(m) : nullptr;
            via = "dynamic";
        }
        if (!ih) {
            DebugLog("skins: anim remap -- include '%s' not loaded yet (idx=%d, %s)",
                     path, idx, via);
            complete = false;
            continue;
        }

        const int n = HDR_SeqCount(ih);
        int added = 0, named = 0;
        for (int j = 0; j < n; ++j) {
            const char* lbl = HDR_SeqLabel(ih, j);
            const char* act = HDR_SeqActName(ih, j);
            if (!lbl) continue;
            const int e = SeqLabelIndex(lbl);
            if (e >= 0) {
                // Same label = the same merged slot. The include's copy overrides the parent's
                // forward declaration, so its activity name is the one that counts.
                if (act) { g_seqName[e] = act; ++named; }
            } else if (g_seqNameCount < k_maxSeqs) {
                g_seqLabel[g_seqNameCount] = lbl;
                g_seqName [g_seqNameCount] = act;
                ++g_seqNameCount;
                ++added;
            }
        }
        DebugLog("skins: anim remap include '%s' via=%s seqs=%d named=%d appended=%d (incl. its "
                 "own includes: %d)", path, via, n, named, added, HDR_IncludeCount(ih));
    }
    return complete;
}

static int SeqNameIndex(const char* want)
{
    for (int i = 0; i < g_seqNameCount; ++i)
        if (g_seqName[i] && strcmp(g_seqName[i], want) == 0) return i;
    return -1;
}

static int SeqPrefixCount(const char* pfx)
{
    const size_t n = strlen(pfx);
    int c = 0;
    for (int i = 0; i < g_seqNameCount; ++i)
        if (g_seqName[i] && strncmp(g_seqName[i], pfx, n) == 0) ++c;
    return c;
}

// The n-th sequence carrying this activity name, or the last one when there are fewer than n.
// A family names several sequences the same way (knife_stab_a/b/c are all
// ACT_MELEE_VM_HITCENTER, and so are acr_stab_a/b/c) and the server picks among them at random,
// so the whole group has to be mapped ordinal for ordinal or two thirds of the swings fall
// through unmapped.
static int SeqNameNth(const char* want, int n)
{
    int last = -1, seen = 0;
    for (int i = 0; i < g_seqNameCount; ++i) {
        if (!g_seqName[i] || _stricmp(g_seqName[i], want) != 0) continue;
        last = i;
        if (seen++ == n) return i;
    }
    return last;
}

// One activity -> one activity, across every sequence that carries the name.
static int MapActName(const char* from, const char* to)
{
    if (!from || !to || _stricmp(from, to) == 0) return 0;
    int mapped = 0, ord = 0;
    for (int i = 0; i < g_seqNameCount; ++i) {
        if (!g_seqName[i] || _stricmp(g_seqName[i], from) != 0) continue;
        const int j = SeqNameNth(to, ord++);
        if (j < 0 || j == i || g_seqRemap[i] >= 0) continue;
        g_seqRemap[i] = j;
        ++mapped;
    }
    return mapped;
}

// What the BASE weapon plays when the code asks for activity `act` -- the index the server will
// have picked, and so the index to catch. TF2's order: the base's own animation_replacement,
// then its family's string surgery on ACT_VM_<X>, then the activity itself. Each candidate is
// only taken if the model actually HAS a sequence with that name, which is what keeps a wrong
// family guess harmless.
static const char* BaseActName(const char* act, const BaseAnimEntry* base)
{
    if (base) {
        for (int i = 0; i < base->actReplCount; ++i)
            if (_stricmp(base->actRepl[i].from, act) == 0) {
                if (SeqNameNth(base->actRepl[i].to, 0) >= 0) return base->actRepl[i].to;
                break;
            }

        static const char k_vm[] = "ACT_VM_";
        if (base->animPfx && strncmp(act, k_vm, sizeof(k_vm) - 1) == 0) {
            static char fam[160];
            _snprintf_s(fam, sizeof(fam), _TRUNCATE, "%s%s", base->animPfx,
                        act + sizeof(k_vm) - 1);
            if (SeqNameNth(fam, 0) >= 0) return fam;
        }
    }
    return act;
}

// The reskin's animation_replacement list, which is the exact answer wherever it has one.
static int MapActPairs(const ReskinInfo* ri, const BaseAnimEntry* base)
{
    int mapped = 0;
    if (!ri || !ri->actRepl) return 0;
    for (int k = 0; k < ri->actReplCount; ++k)
        mapped += MapActName(BaseActName(ri->actRepl[k].from, base), ri->actRepl[k].to);
    return mapped;
}

// Every ACT_<from>_<suffix> -> ACT_<to>_<suffix> in one model. A target family with no entry for
// the suffix keeps the base sequence (the arms play it and the reskin is bonemerged to them, so
// an unmapped index is the ORIGINAL animation, never a missing one) -- except SWINGHARD, which
// every family spells HITCENTER when it has no hard swing of its own.
static int MapFamily(const char* fromPfx, const char* toPfx)
{
    const size_t fromLen = strlen(fromPfx);
    char want[160];
    int mapped = 0;
    for (int i = 0; i < g_seqNameCount; ++i) {
        const char* a = g_seqName[i];
        if (!a || g_seqRemap[i] >= 0) continue;
        if (strncmp(a, fromPfx, fromLen) != 0) continue;
        const char* suffix = a + fromLen;

        _snprintf_s(want, sizeof(want), _TRUNCATE, "%s%s", toPfx, suffix);
        int j = SeqNameIndex(want);
        if (j < 0 && strcmp(suffix, "SWINGHARD") == 0) {
            _snprintf_s(want, sizeof(want), _TRUNCATE, "%sHITCENTER", toPfx);
            j = SeqNameIndex(want);
        }
        if (j < 0 || j == i) continue;
        g_seqRemap[i] = j;
        ++mapped;
    }
    return mapped;
}

// The whole ACT_BACKSTAB_VM_* family onto one sequence -- an all-class model has no backstab.
static int MapFamilyToFixed(const char* fromPfx, const char* fixedName)
{
    const int j = SeqNameIndex(fixedName);
    if (j < 0) return 0;
    const size_t fromLen = strlen(fromPfx);
    int mapped = 0;
    for (int i = 0; i < g_seqNameCount; ++i) {
        const char* a = g_seqName[i];
        if (!a || g_seqRemap[i] >= 0) continue;
        if (strncmp(a, fromPfx, fromLen) != 0 || i == j) continue;
        g_seqRemap[i] = j;
        ++mapped;
    }
    return mapped;
}

__declspec(noinline) static int SEH_ReadEntInt(void* pEnt, int offset)
{
    __try { return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + (unsigned)offset); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

__declspec(noinline) static void SEH_WriteEntInt(void* pEnt, int offset, int val)
{
    __try { *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + (unsigned)offset) = val; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void BuildAnimRemap()
{
    const ReskinInfo*    ri   = g_activeReskinInfo;
    const BaseAnimEntry* base = g_activeBaseInfo;
    if (!ri) { g_remapTries = k_maxRemapTries; return; }

    // The family swap is the fallback half and only runs when both sides name a family. A pair
    // list on its own is enough (the Snack Attack has no family swap at all).
    const char* origPfx = base ? base->animPfx : nullptr;
    const char* acPfx   = ri->animPfx;

    // The viewmodel entity's own model is the only sequence space in play.
    int vmModelIdx = 0;
    if (g_pLocalViewmodelEnt && g_offModelIndex > 0)
        vmModelIdx = SEH_ReadEntInt(g_pLocalViewmodelEnt, g_offModelIndex) & 0xFFFF;
    const void* model = (vmModelIdx > 0) ? ModelInfo_GetModel_ptr(vmModelIdx) : nullptr;
    const void* hdr   = model ? ModelInfo_GetStudiomodel_ptr(model) : nullptr;
    if (!hdr) {
        if (g_remapTries == 1)
            DebugLog("skins: anim remap -- no viewmodel studiohdr (ent=%p modelIdx=%d)",
                     g_pLocalViewmodelEnt, vmModelIdx);
        return;   // the viewmodel entity arrives a few frames after the weapon; keep retrying
    }
    const char* vmName = ModelInfo_GetModelName_ptr(model);

    // Same model, same reskin, same base weapon = same answer. Hand back the copy.
    if (g_cacheValid && model == g_builtModel && g_activeReskinDef == g_builtReskinDef
        && g_activeBaseKey == g_builtBaseKey) {
        memcpy(g_seqRemap, g_seqRemapCache, sizeof(g_seqRemap));
        g_seqRemapBuilt = true;
        g_remapTries    = k_maxRemapTries;
        return;
    }

    if (HDR_SeqCount(hdr) <= 0) {
        DebugLog("skins: anim remap -- '%s' has no sequences (hdr=%p)", vmName ? vmName : "?", hdr);
        g_remapTries = k_maxRemapTries;
        return;
    }
    const bool complete = BuildSeqNames(hdr);
    if (!complete && g_remapTries < k_maxRemapTries - 1) return;   // an include is still loading

    // The item's own list first: it is exact, and TF2 reads it before anything else.
    const int byPair = MapActPairs(ri, base);

    // Then the family, for every activity the list does not name (a Saxxy's list is three
    // INSPECT lines; the swings and the draw are pure ACT_MELEE_VM_ -> ACT_MELEE_ALLCLASS_VM_).
    int mapped = byPair;
    if (origPfx && acPfx && _stricmp(origPfx, acPfx) != 0) {
        mapped += MapFamily(origPfx, acPfx);

        bool bsIsPrefix = false;
        if (const char* bsTarget = BackstabTarget(g_activeTo, bsIsPrefix)) {
            if (bsIsPrefix) mapped += MapFamily("ACT_BACKSTAB_VM_", bsTarget);
            else            mapped += MapFamilyToFixed("ACT_BACKSTAB_VM_", bsTarget);
        }
        if (const char* stun = StunTarget(g_activeTo)) {
            const int si = SeqNameIndex("ACT_MELEE_VM_STUN");
            const int sj = SeqNameIndex(stun);
            if (si >= 0 && sj >= 0 && si != sj && g_seqRemap[si] < 0) {
                g_seqRemap[si] = sj; ++mapped;
            }
        }
    }

    // The census is the measurement: "0 names" and "names but not this family" are different
    // failures and used to print the same line.
    DebugLog("skins: anim remap %s -> %s def=%d model='%s' merged=%d (local=%d complete=%d) "
             "melee=%d allclass=%d item1=%d item2=%d backstab=%d pairs=%d/%d mapped=%d try=%d",
             origPfx ? origPfx : "(none)", acPfx ? acPfx : "(none)", g_activeReskinDef,
             vmName ? vmName : "?", g_seqNameCount, HDR_SeqCount(hdr), (int)complete,
             SeqPrefixCount("ACT_MELEE_VM_"), SeqPrefixCount("ACT_MELEE_ALLCLASS_VM_"),
             SeqPrefixCount("ACT_ITEM1_VM_"), SeqPrefixCount("ACT_ITEM2_VM_"),
             SeqPrefixCount("ACT_BACKSTAB_VM_"), byPair, (int)ri->actReplCount,
             mapped, g_remapTries);

    // The table was read, so this answer will not improve. Stop retrying either way.
    g_remapTries = k_maxRemapTries;
    if (mapped > 0) g_seqRemapBuilt = true;

    // Bank it against the inputs that produced it, so a noisy clear costs a memcpy.
    memcpy(g_seqRemapCache, g_seqRemap, sizeof(g_seqRemap));
    g_builtModel     = model;
    g_builtReskinDef = g_activeReskinDef;
    g_builtBaseKey   = g_activeBaseKey;
    g_cacheValid     = (mapped > 0);
}

static int Skins_RemapSequence(void* pEntity, int nSequence)
{
    if (g_eject || !g_enableSkins) return nSequence;
    if (!g_pLocalViewmodelEnt || pEntity != g_pLocalViewmodelEnt) return nSequence;
    if (!g_activeNeedsRemap) return nSequence;

    if (!g_seqRemapBuilt && g_remapTries < k_maxRemapTries) {
        ++g_remapTries;
        BuildAnimRemap();
    }
    if (g_seqRemapBuilt && nSequence >= 0 && (unsigned)nSequence < 512) {
        const int r = g_seqRemap[nSequence];
        if (r >= 0) return r;
    }
    return nSequence;
}

// ---- Viewmodel entity tracking ----------------------------------------------------------------
// A new viewmodel entity is a new sequence space (a class change swaps the arms model), so the
// table built against the old one has to go with it.
__declspec(noinline) static void SEH_UpdateViewmodelEnt(void* pLocal, int offVM)
{
    __try {
        unsigned int hVM = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<uintptr_t>(pLocal) + (unsigned)offVM);
        void* pVM = FSN_GetEntityFromHandle(hVM);
        if (pVM != g_pLocalViewmodelEnt) { g_pLocalViewmodelEnt = pVM; ClearSeqRemap(); }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- SetSequence hook -------------------------------------------------------------------------
SetSequence_t o_CBaseAnimating_SetSequence = nullptr;

void __fastcall h_CBaseAnimating_SetSequence(void* rcx, int nSequence)
{
    o_CBaseAnimating_SetSequence(rcx, g_eject ? nSequence : Skins_RemapSequence(rcx, nSequence));
}

// The DRAW is the one animation the hook above cannot catch in time. It arrives in the same
// snapshot that switches the weapon (RecvProxy_SequenceNum, baseviewmodel_shared.cpp:622, calls
// SetSequence during unpack) and the unpack runs BEFORE this frame's UpdateActiveRemap has seen
// the new weapon -- so the hook still held the previous weapon's mapping and refused. Every
// later animation is fine, which is exactly the shape of "draw animations are still broken".
//
// So the sequence is swept here too. Re-mapping an already-mapped index is a no-op (the
// reskin's own activities are never on the FROM side), so hook and sweep cannot fight.
//
// It runs TWICE per stage 5: once BEFORE the original FrameStageNotify and once after. The draw
// SOUND is an AE_CL_PLAYSOUND anim event ON THE SEQUENCE (knife_draw carries Weapon_Knife.Draw,
// melee_allclass_draw carries Weapon_Machete.Draw) and it is fired inside that original --
// FrameStageNotify(FRAME_RENDER_START) -> OnRenderStart -> SimulateEntities -> DoAnimationEvents
// (cdll_client_int.cpp:2194/2207) -- so sweeping only afterwards was in time for the picture and
// one call too late for the sound: the Saxxy drew right and still opened a knife.
static int g_offSkinSequence = 0;   // DT_BaseAnimating m_nSequence

static void SweepViewmodelSequence()
{
    if (!g_activeNeedsRemap || !g_pLocalViewmodelEnt || g_offSkinSequence <= 0) return;
    __try {
        const int cur = SEH_ReadEntInt(g_pLocalViewmodelEnt, g_offSkinSequence);
        if (cur < 0 || cur >= 512) return;
        const int r = Skins_RemapSequence(g_pLocalViewmodelEnt, cur);
        if (r == cur) return;
        SEH_WriteEntInt(g_pLocalViewmodelEnt, g_offSkinSequence, r);
        static int s_lastFrom = -1;
        if (cur != s_lastFrom) {
            s_lastFrom = cur;
            DebugLog("skins: anim sweep %d -> %d (the hook was too early for this one)", cur, r);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- Style skins ------------------------------------------------------------------------------
// The skin index a reskin wants for the team we are on. -1 = leave the engine's own value, which
// is already right for every item whose style block is the plain 0/1 team pair.
static int ReskinSkinFor(const SkinCfg& c, int teamNum)
{
    if (c.reskinDef <= 0) return -1;
    const ReskinInfo* ri = Skin_FindReskinInfo(c.reskinDef);
    if (!ri || ri->styleCount == 0) return -1;
    int st = c.reskinStyle;
    if (st < 0 || st >= ri->styleCount) st = 0;
    const int8_t s = (teamNum == 3 /* TF_TEAM_BLUE */) ? ri->skinBlu[st] : ri->skinRed[st];
    return (s < 0) ? -1 : (int)s;
}

// The style skin (a Gold Frying Pan's gold is style 0's skin_red 2 on the ORDINARY pan model) is
// written to the weapon entity, which is the WORLD model. The thing in the player's own hands is
// a separate client-only entity, C_ViewmodelAttachmentModel, whose skin is copied once from
// CTFViewModel::GetSkin when the attachment is built (econ_entity.cpp:1196) -- and for the GFP
// the model never changes (it IS the pan's model), so nothing rebuilds the attachment and it
// keeps the stock pan's skin forever. Hence: find it and write the skin on it directly.
//
// Client-only, so it lives above the networked range, and it is identified by the reskin's own
// model index. The search is capped and cached; a hit is re-verified every frame by that index,
// so a rebuilt attachment is picked up again without another scan.
static int   g_offSkinSkin      = 0;   // DT_BaseAnimating m_nSkin
static void* g_pAttachEnt       = nullptr;
static int   g_attachScanTimer  = 0;
static constexpr int k_maxEdicts     = 2048;   // MAX_EDICTS: everything above is client-only
static constexpr int k_attachScanTop = 2048 + 1024;

static const SkinCfg* ActiveSkinCfg(int* outConfigKey);

static void SweepAttachmentSkin(int teamNum)
{
    if (g_offSkinSkin <= 0 || g_offModelIndex <= 0) return;
    const ReskinInfo* ri = g_activeReskinInfo;
    if (!ri || ri->styleCount == 0) { g_pAttachEnt = nullptr; return; }

    int skin = -1;
    if (const SkinCfg* cfg = ActiveSkinCfg(nullptr)) skin = ReskinSkinFor(*cfg, teamNum);
    if (skin < 0) return;

    const int wantIdx = GetReskinViewmodelIdx(ri->defindex);
    if (wantIdx == 0) return;

    __try {
        if (g_pAttachEnt) {
            const int mi = SEH_ReadEntInt(g_pAttachEnt, g_offModelIndex) & 0xFFFF;
            if (mi != (wantIdx & 0xFFFF)) g_pAttachEnt = nullptr;
        }
        if (!g_pAttachEnt) {
            if (--g_attachScanTimer > 0) return;
            g_attachScanTimer = 30;                 // ~2 scans a second while it is missing
            for (int i = k_maxEdicts; i < k_attachScanTop; ++i) {
                void* p = GetClientEntity(i);
                if (!p || !FSN_VtableSane(p)) continue;
                if ((SEH_ReadEntInt(p, g_offModelIndex) & 0xFFFF) != (wantIdx & 0xFFFF)) continue;
                g_pAttachEnt = p;
                DebugLog("skins: viewmodel attachment for def=%d found at client entity %d "
                         "(model idx %d)", ri->defindex, i, wantIdx);
                break;
            }
            if (!g_pAttachEnt) return;
        }
        if (SEH_ReadEntInt(g_pAttachEnt, g_offSkinSkin) != skin)
            SEH_WriteEntInt(g_pAttachEnt, g_offSkinSkin, skin);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}


// ---- Which def index a weapon should report ----------------------------------------------------
// One resolution, shared by the stage-2 pre-write and the post-update pass, so the two can never
// disagree about what a weapon is wearing.
struct SkinTarget { int configKey; int targetDef; const SkinCfg* cfg; };

// The first weapon-specific warpaint def (15xxx) this weapon has. The stat clock needs a carrier:
// weapon_uses_stattrak_module is a STATIC attribute on the paintkit_base prefab, so it exists only
// on those defs -- writing attr 834 onto a stock def does not bring it along.
static int Skin_FirstSkinDefFor(int weaponRedir)
{
    for (int i = 0; i < k_skinDefCount; i++)
        if (k_skinDefs[i].weaponRedir == weaponRedir) return k_skinDefs[i].skinDef;
    return 0;
}

// Reverse map: every def index an enabled config can WRITE, and the config key that wrote it.
// Our own pre-write is what the next pass reads back, and Skin_RedirectIndex only knows the
// stock/festive/class-specific variants -- it cannot fold a Saxxy back onto the weapon it
// replaced (every melee can wear one) and has no case at all for Nessie's Nine Iron or the AWPer
// Hand. Without this the pass after a pre-write finds no config and the weapon loses every
// attribute on the very next frame.
struct RevEntry { int targetDef; int configKey; };
static RevEntry g_rev[64];
static int      g_revCount = 0;

static void RebuildReverseMap()
{
    g_revCount = 0;
    for (const auto& [key, c] : g_skinCfgs) {
        if (!c.enabled) continue;
        int t = 0;
        if      (c.festive)           t = Skin_GetFestiveIdx(key);
        else if (c.reskinDef > 0)     t = c.reskinDef;
        else if (c.weaponSkinDef > 0) t = c.weaponSkinDef;
        else if (c.statClock)         t = Skin_FirstSkinDefFor(key);   // the clock's carrier def
        if (t > 0 && t != key && g_revCount < 64)
            g_rev[g_revCount++] = { t, key };
    }
}

static SkinTarget ResolveSkinTarget(int origDefIdx)
{
    SkinTarget wt{ 0, 0, nullptr };

    int configKey = origDefIdx;
    Skin_RedirectIndex(configKey);
    if (configKey <= 0) return wt;
    // A 15xxx def is a weapon-specific warpaint item; map it back to the weapon it belongs to.
    if (configKey == origDefIdx && origDefIdx >= 15000 && origDefIdx < 16000) {
        int redir = Skin_FindWeaponRedir(origDefIdx);
        if (redir > 0) configKey = redir;
    }

    auto it = g_skinCfgs.find(configKey);
    if (it == g_skinCfgs.end() || !it->second.enabled) {
        // Not a def index the schema redirects -- is it one WE wrote last pass?
        for (int i = 0; i < g_revCount; ++i) {
            if (g_rev[i].targetDef != origDefIdx) continue;
            it = g_skinCfgs.find(g_rev[i].configKey);
            if (it != g_skinCfgs.end() && it->second.enabled) { configKey = g_rev[i].configKey; break; }
        }
        if (it == g_skinCfgs.end() || !it->second.enabled) return wt;
    }
    const SkinCfg& sc = it->second;

    int targetDef = 0;
    if (sc.festive) {
        int festIdx = Skin_GetFestiveIdx(configKey);
        if (festIdx > 0) targetDef = festIdx;
    } else if (sc.reskinDef > 0) {
        targetDef = sc.reskinDef;
    } else if (sc.weaponSkinDef > 0) {
        targetDef = sc.weaponSkinDef;
    } else if (configKey != origDefIdx &&
               (sc.paintKitId > 0 || sc.australium || sc.festivized)) {
        targetDef = configKey;
    }

    // Stat clock with no skin picked: wear a warpaint def purely for the prefab's static
    // weapon_uses_stattrak_module string. Skin_Apply then writes a paint kit id no definition
    // answers for, so no composite is built and the weapon still looks stock.
    if (targetDef <= 0 && sc.statClock)
        targetDef = Skin_FirstSkinDefFor(configKey);

    wt.configKey = configKey;
    wt.targetDef = targetDef;
    wt.cfg       = &sc;
    return wt;
}

// The config for the weapon currently in the player's hands.
static const SkinCfg* ActiveSkinCfg(int* outConfigKey)
{
    if (g_offActiveWeapon <= 0 || g_fsn_offItemDefIndex <= 0) return nullptr;
    int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1) return nullptr;
    void* pLocal = GetClientEntity(localIdx);
    if (!pLocal) return nullptr;

    unsigned int hAW = FSN_ReadWeaponHandle(pLocal, g_offActiveWeapon, 0);
    void* pAW = FSN_GetEntityFromHandle(hAW);
    if (!pAW || !FSN_VtableSane(pAW)) return nullptr;

    const int def = FSN_ReadItemDefIndex(pAW, g_fsn_offItemDefIndex);
    const SkinTarget wt = ResolveSkinTarget(def);
    if (outConfigKey) *outConfigKey = wt.configKey;
    return wt.cfg;
}

// Which anim families the currently held weapon is being remapped between. Same resolution the
// write pass uses, so a def index we already rewrote still names the weapon it was rewritten
// FROM -- that weapon is the one whose activity family the sequences have to come out of.
static void UpdateActiveRemap(void* pLocal)
{
    AnimSlot from = AS_DEFAULT, to = AS_DEFAULT;
    int reskin = 0, baseKey = 0;
    const ReskinInfo*    riInfo   = nullptr;
    const BaseAnimEntry* baseInfo = nullptr;

    __try {
        if (g_offActiveWeapon > 0 && g_fsn_offItemDefIndex > 0) {
            unsigned int hAW = FSN_ReadWeaponHandle(pLocal, g_offActiveWeapon, 0);
            void* pAW = FSN_GetEntityFromHandle(hAW);
            if (pAW && FSN_VtableSane(pAW)) {
                const int defIdx = FSN_ReadItemDefIndex(pAW, g_fsn_offItemDefIndex);
                const SkinTarget wt = ResolveSkinTarget(defIdx);
                if (wt.cfg && wt.cfg->reskinDef > 0) {
                    if (const ReskinInfo* ri = Skin_FindReskinInfo(wt.cfg->reskinDef)) {
                        reskin   = wt.cfg->reskinDef;
                        baseKey  = wt.configKey;
                        from     = Skin_BaseAnimSlot(wt.configKey);
                        to       = ri->anim;
                        riInfo   = ri;
                        baseInfo = Skin_BaseAnimEntry(wt.configKey);
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (reskin != g_activeReskinDef || from != g_activeFrom || to != g_activeTo
        || baseKey != g_activeBaseKey) {
        g_activeReskinDef  = reskin;
        g_activeBaseKey    = baseKey;
        g_activeFrom       = from;
        g_activeTo         = to;
        g_activeReskinInfo = riInfo;
        g_activeBaseInfo   = baseInfo;

        // Nothing to remap when the reskin names no replacements and sits in the same family as
        // the weapon it replaces -- the overwhelming majority (a Batsaber on a bat).
        const char* bp = baseInfo ? baseInfo->animPfx : nullptr;
        const char* rp = riInfo   ? riInfo->animPfx   : nullptr;
        g_activeNeedsRemap = riInfo && ((riInfo->actRepl && riInfo->actReplCount > 0)
                                        || (bp && rp && _stricmp(bp, rp) != 0));
        ClearSeqRemap();
    }
}

// ---- Override sheen color ----------------------------------------------------------------------
// Both halves of a killstreak's color come out of ONE static table in client.dll,
// g_KillStreakEffectsBase (c_tf_player.cpp:336):
//   sheen -- CProxyAnimatedWeaponSheen::OnBind copies that row's m_sheen_rgba straight into
//            $sheenmaptint on every material bind (c_tf_player.cpp:2883).
//   eyes  -- C_TFPlayer::UpdateKillStreakEffects reads m_color1/m_color2 out of the SAME row
//            through GetVectorColorForParticleSystem into m_vEyeGlowColor1/2
//            (c_tf_player.cpp:10440), and CTFPlayerShared::ClientKillStreakBuffThink pushes those
//            into control point 9 (CUSTOM_COLOR_CP1) of the two eyeglow particles every think
//            (tf_player_shared.cpp:6536). The particle art is white; CP1 IS the color.
// So ONE rewritten row recolors the weapon sheen and the eye glow together, at the source --
// no sheen proxy hook, no particle walk, and it does not care what g_enableSheen or the animated
// sheen tint are doing. The array is dynamically initialised (its ctor divides each byte by 255),
// so it lives in a writable section already.
//
// Row = killstreak_params_t, 64 bytes on x64:
//   +0  int m_iShaderIndex   +4 bool m_bHasTeamColor   +8 const char* m_pTexture
//   +16 float sheen rgba     +32 float color1 rgba     +48 float color2 rgba
// g_KillStreakEffectsBlue is the 2-row array directly behind it (rows 0..7 base, 8 empty,
// 9 blue). Row 1 (team shine) is the one row with m_bHasTeamColor set, and a BLU player reads
// the blue copy of it instead.
static constexpr int k_ksRowSize  = 64;
static constexpr int k_ksRows     = 10;   // the 8 base rows and then the 2 blue ones
static constexpr int k_ksBaseRows = 8;
static constexpr int k_ksBlueRow1 = 9;    // g_KillStreakEffectsBlue[1], the team shine copy

// Rows 1 and 2 as the SDK writes them -- the signature the table is found by. Compared with a
// tolerance because each value is a runtime division whose last bit depends on the compiler.
static const float k_ksRow1[12] = {
    200.f/255.f,  20.f/255.f,  15.f/255.f, 1.f,
    255.f/255.f, 118.f/255.f, 118.f/255.f, 1.f,
    255.f/255.f,  35.f/255.f,  28.f/255.f, 1.f };
static const float k_ksRow2[12] = {
    242.f/255.f, 172.f/255.f,  10.f/255.f, 1.f,
    255.f/255.f, 237.f/255.f, 138.f/255.f, 1.f,
    255.f/255.f, 213.f/255.f,  65.f/255.f, 1.f };
static const float k_ksRowBlue[12] = {
     40.f/255.f,  98.f/255.f, 200.f/255.f, 1.f,
      0.f/255.f,  92.f/255.f, 255.f/255.f, 1.f,
    134.f/255.f, 203.f/255.f, 243.f/255.f, 1.f };

static uint8_t* g_ksTable    = nullptr;        // &g_KillStreakEffectsBase[0]
static bool     g_ksSearched = false;
static bool     g_ksBlueOk   = false;          // the blue array really is behind the base one
static float    g_ksOrig[k_ksRows][12] = {};   // shipped colors, kept for the restore
static int      g_ksPatched  = -1;             // row currently holding a custom color
static float    g_ksPatchedColor[3] = {};
static int      g_ksGlowKick = 0;              // frames of a bumped streak -- see KsColorUpdate

static bool KsRowMatch(const uint8_t* p, const float* want)
{
    const float* f = reinterpret_cast<const float*>(p);
    for (int i = 0; i < 12; ++i) {
        const float d = f[i] - want[i];
        if (!(d > -0.002f && d < 0.002f)) return false;   // false on NaN too
    }
    return true;
}

// Found by its own CONTENTS, not by a code signature: the table is data, so no pattern scan over
// .text can reach it and nothing in it moves between builds except its address.
__declspec(noinline) static void KsFindTable()
{
    if (g_ksSearched) return;
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return;
    g_ksSearched = true;

    __try {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hClient);
        auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS*>(
                        reinterpret_cast<uint8_t*>(hClient) + dos->e_lfanew);
        IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < (int)nt->FileHeader.NumberOfSections; ++i) {
            if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            uint8_t* start = reinterpret_cast<uint8_t*>(hClient) + sec[i].VirtualAddress;
            const size_t size = (size_t)sec[i].Misc.VirtualSize;
            if (size < (size_t)k_ksRowSize * k_ksRows) continue;

            // p walks candidate row 1s; row 0 (the empty one) sits one row in front of it and the
            // blue pair k_ksBlueRow1 rows behind, so both ends have to stay in section.
            uint8_t* last = start + size - (size_t)k_ksRowSize * (k_ksRows - 1);
            for (uint8_t* p = start + k_ksRowSize; p <= last; p += 8) {
                if (!KsRowMatch(p + 16, k_ksRow1)) continue;
                if (!KsRowMatch(p + k_ksRowSize + 16, k_ksRow2)) continue;

                g_ksTable = p - k_ksRowSize;
                for (int r = 0; r < k_ksRows; ++r)
                    memcpy(g_ksOrig[r], g_ksTable + r * k_ksRowSize + 16, sizeof(g_ksOrig[r]));
                g_ksBlueOk = KsRowMatch(g_ksTable + k_ksBlueRow1 * k_ksRowSize + 16, k_ksRowBlue);
                DebugLog("skins: killstreak color table at %p (client+%IX) blue=%d",
                         (void*)g_ksTable,
                         (uintptr_t)(g_ksTable - reinterpret_cast<uint8_t*>(hClient)),
                         g_ksBlueOk ? 1 : 0);
                return;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    DebugLog("skins: killstreak color table NOT FOUND -- override sheen color does nothing this "
             "session, the stock sheen and eye colors stay");
}

// Only the three color channels are written; the alpha of each group is left as shipped (the
// proxy overwrites the sheen alpha in first person anyway, tf_sheen_alpha_firstperson).
__declspec(noinline) static void KsWriteRow(int row, const float rgb[3])
{
    if (!g_ksTable || row < 0 || row >= k_ksRows) return;
    __try {
        float* f = reinterpret_cast<float*>(g_ksTable + row * k_ksRowSize + 16);
        for (int grp = 0; grp < 3; ++grp) {          // sheen, color1, color2
            f[grp * 4 + 0] = rgb[0];
            f[grp * 4 + 1] = rgb[1];
            f[grp * 4 + 2] = rgb[2];
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) static void KsRestoreRow(int row)
{
    if (!g_ksTable || row < 0 || row >= k_ksRows) return;
    __try {
        memcpy(g_ksTable + row * k_ksRowSize + 16, g_ksOrig[row], sizeof(g_ksOrig[row]));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The table is the game's own data, so it has to go back before this dll stops running -- nothing
// else would ever undo it.
void Skins_KsColorRestoreAll()
{
    if (g_ksTable && g_ksPatched >= 0) {
        KsRestoreRow(g_ksPatched);
        if (g_ksPatched == 1 && g_ksBlueOk) KsRestoreRow(k_ksBlueRow1);
        g_ksGlowKick = 4;
    }
    g_ksPatched = -1;
}

// A custom color needs a row to live in. With no sheen picked, attribute 2014 is never written and
// nothing ever reads the table, so "override color" on its own would quietly mean team shine.
static int KsSheenIndexFor(const SkinCfg& c)
{
    if (c.ksTier < 2) return 0;
    if (c.ksSheen > 0) return c.ksSheen;
    return c.ksCustomColor ? 1 : 0;
}

// The count the pass writes: the real streak, or one above it while a kick is pending.
static int KsKickedStreak(int count)
{
    if (g_ksGlowKick <= 0) return count;
    --g_ksGlowKick;
    return count + 1;
}

// Once a frame, off the held weapon's config: paint that weapon's sheen row and put every other
// row back. Only ONE row can be custom at a time because the table is process-wide -- it is the
// held weapon's, which is also the only weapon whose sheen and eyes are on screen.
static void KsColorUpdate(const SkinCfg* held)
{
    KsFindTable();
    if (!g_ksTable) return;

    const bool want = held && held->enabled && held->ksCustomColor && held->ksTier >= 2;
    const int  row  = want ? KsSheenIndexFor(*held) : -1;
    if (row <= 0 || row >= k_ksBaseRows) { Skins_KsColorRestoreAll(); return; }

    const float* rgb = held->ksColor;
    if (row == g_ksPatched && rgb[0] == g_ksPatchedColor[0] &&
        rgb[1] == g_ksPatchedColor[1] && rgb[2] == g_ksPatchedColor[2])
        return;                                   // already painted

    if (g_ksPatched >= 0 && g_ksPatched != row) Skins_KsColorRestoreAll();

    KsWriteRow(row, rgb);
    if (row == 1 && g_ksBlueOk) KsWriteRow(k_ksBlueRow1, rgb);   // team shine's BLU copy
    g_ksPatched = row;
    memcpy(g_ksPatchedColor, rgb, sizeof(g_ksPatchedColor));

    // The sheen picks a table write up on its next bind, but the eye glow does NOT: its color is
    // only re-read inside UpdateKillStreakEffects, which the client's think calls when the streak
    // COUNT changes -- otherwise the think just re-pushes the color the glow was built with.
    // Bumping the written count for a few frames and dropping it back makes the think notice
    // twice. +1 rather than -1 so the count never falls under tf_killstreakeyes_minkills and
    // blinks the glow off.
    g_ksGlowKick = 4;

    static DWORD s_lastLog = 0;
    const DWORD now = GetTickCount();
    if (now - s_lastLog > 1000) {                 // a dragged color picker changes every frame
        s_lastLog = now;
        DebugLog("skins: killstreak color row %d = %.2f %.2f %.2f", row, rgb[0], rgb[1], rgb[2]);
    }
}

// ---- Killstreak count --------------------------------------------------------------------------
// THE ATTRIBUTES ARE NOT ENOUGH ON THEIR OWN. The sheen proxy
// (CProxyAnimatedWeaponSheen::GetTimeBetweenAnims) spaces the sheen by the player's kill streak --
// 5 s apart at streak 0, continuous at 5 -- and the eye glow is refused outright below
// tf_killstreakeyes_minkills, which is 5. That streak is a NETWORKED COUNT the server keeps, and a
// server that never issued a killstreak item never counts one, so nothing lights up. We keep our
// own count and write it into both places the client reads:
//   CTFPlayerShared::m_nStreaks[type]                       on the local player
//   CTFPlayerResource::m_iStreaks[playerIdx * COUNT + type] on the resource entity
// Both are arrays of int, so the byte stride is sizeof(int).
enum ETFStreak { kTFStreak_Kills = 0, kTFStreak_KillsAll, kTFStreak_Ducks,
                 kTFStreak_Duck_levelup, kTFStreak_COUNT };

static int  g_offNStreaks   = 0;   // DT_TFPlayer m_Shared  m_nStreaks[kTFStreak_COUNT]
static int  g_offIStreaks   = 0;   // DT_TFPlayerResource   m_iStreaks[MAX_PLAYERS*COUNT]
static int  g_killStreak    = 0;   // our total, before the glow kick
static int  g_stampedStreak = 0;   // last value ApplyKillStreak wrote (kick included)

// CTFPlayerResource, found by class name off the entity list: IClientEntityList slot 0 is
// GetClientNetworkable and IClientNetworkable slot 2 is GetClientClass. Cached, re-found when the
// pointer goes stale.
__declspec(noinline) static void* FindPlayerResource()
{
    static void* s_cached  = nullptr;
    static int   s_lastIdx = 0;

    __try {
        if (s_cached && s_lastIdx > 0 && GetClientEntity(s_lastIdx) == s_cached) return s_cached;
        s_cached = nullptr; s_lastIdx = 0;
        if (!g_entityList) return nullptr;

        void** listVtbl = *reinterpret_cast<void***>(g_entityList);
        if (reinterpret_cast<uintptr_t>(listVtbl) < 0x10000) return nullptr;
        using GetNetworkable_t = void*(__fastcall*)(void*, int);
        const int highest = GetHighestEntityIndex();
        const int cap     = (highest > 0 && highest < 2048) ? highest : 2048;

        for (int i = 1; i <= cap; ++i) {
            void* pNet = ((GetNetworkable_t)listVtbl[0])(g_entityList, i);
            if (!pNet || !FSN_VtableSane(pNet)) continue;
            void** vtbl = *reinterpret_cast<void***>(pNet);
            using GetClass_t = ClientClass*(__fastcall*)(void*);
            ClientClass* cc = ((GetClass_t)vtbl[2])(pNet);
            if (!cc || !cc->m_pNetworkName) continue;
            if (strcmp(cc->m_pNetworkName, "CTFPlayerResource") != 0) continue;
            s_cached  = GetClientEntity(i);
            s_lastIdx = i;
            return s_cached;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { s_cached = nullptr; s_lastIdx = 0; }
    return nullptr;
}

static void ResolveStreakOffsets()
{
    if (!g_netvarsReady) return;
    // m_nStreaks lives inside m_Shared; FindPropOffset walks the nested table and adds both
    // offsets, so the DT_TFPlayer lookup lands on CTFPlayerShared::m_nStreaks itself.
    if (g_offNStreaks == 0) {
        RecvTable* t = FindNetworkTable("DT_TFPlayer");
        int off = t ? FindPropOffset(t, "m_nStreaks") : -1;
        g_offNStreaks = (off > 0) ? off : -1;
        DebugLog("skins: m_nStreaks off=%d", off);
    }
    if (g_offIStreaks == 0) {
        RecvTable* t = FindNetworkTable("DT_TFPlayerResource");
        int off = t ? FindPropOffset(t, "m_iStreaks") : -1;
        g_offIStreaks = (off > 0) ? off : -1;
        DebugLog("skins: m_iStreaks off=%d", off);
    }
}

__declspec(noinline) static void ApplyKillStreak(int localIdx, int count)
{
    __try {
        if (localIdx < 1) return;
        g_stampedStreak = count;
        if (g_offNStreaks > 0) {
            if (void* pLocal = GetClientEntity(localIdx)) {
                SEH_WriteEntInt(pLocal, g_offNStreaks + kTFStreak_Kills    * (int)sizeof(int), count);
                SEH_WriteEntInt(pLocal, g_offNStreaks + kTFStreak_KillsAll * (int)sizeof(int), count);
            }
        }
        if (g_offIStreaks > 0) {
            if (void* pRes = FindPlayerResource()) {
                const int row = localIdx * kTFStreak_COUNT;
                SEH_WriteEntInt(pRes, g_offIStreaks + (row + kTFStreak_Kills)    * (int)sizeof(int), count);
                SEH_WriteEntInt(pRes, g_offIStreaks + (row + kTFStreak_KillsAll) * (int)sizeof(int), count);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The HUD killstreak meter reads m_nStreaks in C_TFPlayer::ClientThink, which runs INSIDE the
// stage-5 original (OnRenderStart -> SimulateEntities, cdll_client_int.cpp:2005). The regular
// stamp runs after the originals, so a 0 landing in between reached the HUD for a frame -- the
// flicker on weapon switch. Put the last value back before the think, and log which side of the
// original it was caught on so a remaining flicker can be traced.
__declspec(noinline) static void RestampStreakSEH(const char* where)
{
    __try {
        if (g_offNStreaks <= 0 || g_fsn_entityChangeAge < 9 || !IsInGame()) return;
        const int localIdx = GetLocalPlayerIndex();
        void* pLocal = (localIdx >= 1) ? GetClientEntity(localIdx) : nullptr;
        if (!pLocal || pLocal != g_fsn_lastLocalEnt) return;
        const int cur = SEH_ReadEntInt(pLocal, g_offNStreaks + kTFStreak_Kills * (int)sizeof(int));
        if (cur == g_stampedStreak) return;
        static DWORD s_lastLog = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastLog > 1000) {
            s_lastLog = now;
            DebugLog("skins: streak overwritten %d -> %d (%s), restamped", g_stampedStreak, cur, where);
        }
        ApplyKillStreak(localIdx, g_stampedStreak);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Is any enabled config asking for a killstreak? Nothing is stamped or rewritten otherwise.
static bool AnyKsWanted()
{
    for (const auto& [id, c] : g_skinCfgs)
        if (c.enabled && c.ksTier > 0) return true;
    return false;
}

static void RestampStreak(const char* where)
{
    if (AnyKsWanted()) RestampStreakSEH(where);
}

// THE STREAK IS KEPT PER WEAPON, keyed by config key -- the key the menu edits (0 = a real
// killstreak weapon with no config). A kill with no tier, ours or the server's, raises nothing.
// The kill notice shows a weapon's own entry; everything else shows the SUM. The count stamped into the
// networked arrays is that SUM over every weapon, whatever is held -- what the server keeps in kTFStreak_Kills
// (tf_gamerules.cpp:13159), so the HUD count adds up across weapons and never drops to 0 on switch.
static std::unordered_map<int, int>& StreakByKey()
{
    static std::unordered_map<int, int> s_byKey;
    return s_byKey;
}

static int StreakTotal()
{
    int total = 0;
    for (const auto& [key, n] : StreakByKey()) total += n;
    return total;
}

// Streak carry-over across demos -- see demostreak.h.
static ds::Demo g_demo;

// IVEngineClient 51 GetLevelName, 76 IsPlayingDemo, 80 GetDemoPlaybackTick, 83
// GetDemoPlaybackTotalTicks (cdll_int.h:414-421; 76 is flipmath.h engIsPlayingDemo). Split from
// DemoStreakSync so the map copy never sits in a function with __try (C2712).
struct DemoNow { bool playing; const char* level; int length; int tick; };
__declspec(noinline) static DemoNow ReadDemoNow()
{
    DemoNow n{ false, nullptr, 0, 0 };
    __try {
        if (g_engine && VCall<bool>(g_engine, 76)) {
            n.level   = VCall<const char*>(g_engine, 51);
            n.length  = VCall<int>(g_engine, 83);
            n.tick    = VCall<int>(g_engine, 80);
            n.playing = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { n.playing = false; }
    return n;
}

// Before every change to the streak. Carries it into a new demo and puts it back on a rewind.
static ds::Load DemoStreakSync()
{
    const DemoNow n = ReadDemoNow();
    const ds::Load l = ds::Sync(g_demo, n.playing, n.level, n.length, n.tick, StreakByKey());
    if (l == ds::Load::NewDemo)
        DebugLog("skins: streak %d carried into demo %s (%d ticks)", StreakTotal(), g_demo.level, n.length);
    else if (l == ds::Load::Rewind)
        DebugLog("skins: demo rewound to tick %d, streak back to %d", n.tick, StreakTotal());
    return l;
}

// In a demo only the recorded player's death ends a streak; a demo load fires the rest.
void Skins_ResetStreak(const char* why, bool death)
{
    if (!ds::ResetEnds(DemoStreakSync(), death)) {
        if (StreakTotal() > 0) DebugLog("skins: streak %d kept (%s, demo)", StreakTotal(), why);
        return;
    }
    const bool had = (g_killStreak != 0) || !StreakByKey().empty();
    g_killStreak = 0;
    StreakByKey().clear();
    const int localIdx = GetLocalPlayerIndex();
    if (localIdx >= 1) ApplyKillStreak(localIdx, 0);
    if (had) DebugLog("skins: streak reset (%s)", why);
}

// A kill by the local player. The TOTAL spans every killstreak weapon, ours or real: a kill counts
// when our config gives the held weapon a tier OR the server counted it (serverKs = the event's
// kill_streak_wep > 0, only set for a weapon that really has killstreak_tier,
// tf_gamerules.cpp:13179), so 9 on a real kunai plus one with a faked ambassador is a total of 10.
// Returns the new total for kill_streak_total, or 0 to leave the event alone (nothing counted, or
// no config asks for a streak -- stock behavior). *outWep is the PER WEAPON count for
// kill_streak_wep (the number beside the killer's name, as the game keeps it), or 0 to keep the
// server's own for a real killstreak weapon we gave no tier.
int Skins_OnLocalKill(bool serverKs, int* outWep)
{
    *outWep = 0;
    if (!g_enableSkins) return 0;
    int key = 0;
    const SkinCfg* cfg = ActiveSkinCfg(&key);
    const bool ourKs = cfg && cfg->ksTier > 0;
    if (!ourKs && !serverKs) return 0;

    ResolveStreakOffsets();
    DemoStreakSync();   // a rewind's replayed kills count once, not twice
    const int wep = ++StreakByKey()[key];   // key 0 = real ks weapon with no config
    g_killStreak  = StreakTotal();
    DebugLog("skins: streak -> %d total, %d on weapon key %d (%s)",
             g_killStreak, wep, key, ourKs ? "ours" : "server");
    if (!AnyKsWanted()) return 0;
    ApplyKillStreak(GetLocalPlayerIndex(), g_killStreak);
    if (ourKs) *outWep = wep;
    return g_killStreak;
}

// Gold statue. set_turn_to_gold (attr 150) is server-side -- CTFPlayer::Event_Killed reads it and
// we never see the result -- but every effect it drives (the gold material on the body and its
// wearables, the fixed constraints, Saxxy.TurnGold) is read out of m_bGoldRagdoll inside
// C_TFRagdoll::CreateTFRagdoll, which is a CLIENT function this dll already hooks. So the corpse
// of anyone killed with a gilding reskin can be gilded here.
bool Skins_HeldWeaponTurnsToGold()
{
    if (!g_enableSkins) return false;
    const SkinCfg* c = ActiveSkinCfg(nullptr);
    if (!c || c->reskinDef <= 0) return false;
    const ReskinInfo* ri = Skin_FindReskinInfo(c->reskinDef);
    return ri && ri->turnToGold;
}

// ---- FSN pre-write helpers --------------------------------------------------------------------
__declspec(noinline) static void FSN_PreWriteDefIndexes()
{
    __try {
        if (g_fsn_offMyWeapons == 0) {
            RecvTable* pTable = FindNetworkTable("DT_BaseCombatCharacter");
            if (pTable) {
                int off = FindPropOffset(pTable, "m_hMyWeapons");
                g_fsn_offMyWeapons = (off > 0) ? off : -1;
            }
        }
        if (g_fsn_offItemDefIndex == 0) {
            RecvTable* pTbl = FindNetworkTable("DT_EconEntity");
            if (!pTbl) pTbl = FindNetworkTable("DT_TFWeaponBase");
            if (pTbl) {
                int off = FindPropOffset(pTbl, "m_iItemDefinitionIndex");
                g_fsn_offItemDefIndex = (off > 0) ? off : -1;
            }
        }
        if (g_fsn_offMyWeapons <= 0 || g_fsn_offItemDefIndex <= 0) return;

        RebuildReverseMap();

        int localIdx = GetLocalPlayerIndex();
        if (localIdx < 1) return;
        void* pLocal = GetClientEntity(localIdx);
        if (!pLocal) return;

        static int s_preWriteDbgCount = 0;
        int wrote = 0;

        for (int i = 0; i < 48; i++) {
            unsigned int hWeapon = FSN_ReadWeaponHandle(pLocal, g_fsn_offMyWeapons, i);
            if (hWeapon == 0xFFFFFFFF || hWeapon == 0) continue;
            void* pWeapon = FSN_GetEntityFromHandle(hWeapon);
            if (!pWeapon || !FSN_VtableSane(pWeapon)) continue;

            int origDefIdx = FSN_ReadItemDefIndex(pWeapon, g_fsn_offItemDefIndex);
            const SkinTarget wt = ResolveSkinTarget(origDefIdx);
            if (!wt.cfg || wt.targetDef <= 0) continue;

            // Register the reskin's model BEFORE the def index it belongs to is unpacked: the
            // attachment entity is built during that unpack and refuses to exist if
            // GetModelIndex cannot answer for the path.
            //
            // NOTHING WRITES A MODEL INDEX ONTO THE WEAPON any more, and the base-path capture
            // that used to live here is gone with it:
            //   * the weapon in the viewmodel is not the viewmodel. UpdateAttachmentModels
            //     (econ_entity.cpp:1148) creates a SEPARATE client entity and models it off the
            //     item definition -- the thing this pre-write already changes. No field on the
            //     weapon feeds it.
            //   * m_iViewModelIndex is the ARMS model's index; writing the weapon's model there
            //     replaces the hands.
            //   * m_nCustomViewmodelModelIndex > 0 makes SetViewModel
            //     (basecombatweapon_shared.cpp:1195) swap the WHOLE viewmodel and return.
            //   * the DrawOverriddenViewmodel field was never a model index at all -- it climbed
            //     ~2 a second in the log, i.e. a frame counter off a wrong signature.
            if (wt.cfg->reskinDef > 0) GetReskinViewmodelIdx(wt.cfg->reskinDef);

            if (wt.targetDef != origDefIdx) {
                FSN_WriteItemDefIndex(pWeapon, g_fsn_offItemDefIndex, wt.targetDef);
                ++wrote;
                if (s_preWriteDbgCount < 20)
                    DebugLog("PreWrite: slot %d orig=%d -> target=%d (cfg=%d)",
                             i, origDefIdx, wt.targetDef, wt.configKey);
            }
        }
        if (wrote > 0 && s_preWriteDbgCount < 20) {
            DebugLog("PreWrite: wrote %d weapon defindexes", wrote);
            ++s_preWriteDbgCount;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("PreWrite: SEH exception caught");
    }
}

static bool s_reskinStabilized = false;

// Every per-map pointer and cache goes the first FSN call after g_matEpoch moves (the
// level hooks bump it). The engine frees all client entities on LevelShutdown.
static void FSN_OnLevelChange()
{
    ResetMatCache(false);
    g_precipEntity       = nullptr;
    g_precipNetworkable  = nullptr;
    s_lastAppliedMode    = -99;
    for (auto& v : s_trackedWeather) v.clear();
    // Zombify's client-side dynamic models: a level change may purge them (nothing holds a
    // reference), so their model_t / hardware-data pointers are looked up again per map.
    memset(s_zombieModels, 0, sizeof(s_zombieModels));
    memset(s_zombieDynIdx, 0, sizeof(s_zombieDynIdx));
    memset(s_zombieHWData, 0, sizeof(s_zombieHWData));
    memset(s_boneMapped,   0, sizeof(s_boneMapped));
    s_reskinStabilized   = false;
    g_pLocalViewmodelEnt = nullptr;
    g_pAttachEnt         = nullptr;
    ResetActiveRemap();
}

void __fastcall h_FrameStageNotify(int stage)
{
    if (g_eject) {
        o_FrameStageNotify(stage);
        // The killstreak color table is a write into the game's own data, so it has to go back
        // before this hook stops running -- nothing else would ever undo it.
        Skins_KsColorRestoreAll();
        // FRAME_RENDER_START: world materials live, nothing mid-draw. Eject's
        // material/entity teardown runs here, not on the eject thread.
        if (stage == 5) EjectRestore_Service();
        return;
    }

    ++g_fsnDepth;
    struct FsnDec { ~FsnDec() { --g_fsnDepth; } } _fdec;

    g_fsnStage.store(stage);
    static const char* const k_phase[] = { "fsn_0", "fsn_1", "fsn_2", "fsn_3", "fsn_4", "fsn_5", "fsn_6" };
    SetPhase((stage >= 0 && stage < 7) ? k_phase[stage] : "fsn_x");

    {
        static uint32_t s_seenEpoch = 0;
        const uint32_t epoch = g_matEpoch.load();
        if (epoch != s_seenEpoch) { s_seenEpoch = epoch; FSN_OnLevelChange(); }
    }

    // THE DRAW SOUND IS AN ANIM EVENT, so the sweep has to beat the ORIGINAL of stage 5, not
    // just the frame's draw. FrameStageNotify(FRAME_RENDER_START) calls OnRenderStart, which runs
    // ProcessOnDataChangedEvents and then SimulateEntities -- and that is
    // C_BaseAnimating::DoAnimationEvents, the thing that fires the sequence's own events
    // (cdll_client_int.cpp:2194/2207). In c_spy_animations.mdl knife_draw carries
    // AE_CL_PLAYSOUND Weapon_Knife.Draw and melee_allclass_draw carries Weapon_Machete.Draw, so
    // sweeping only after the original was in time for the picture and one call too late for the
    // sound: the Saxxy drew right and still opened a knife. Prediction is NOT inside
    // OnRenderStart, so nothing rewrites m_nSequence between here and the draw.
    if (stage == 5 && g_enableSkins) { SweepViewmodelSequence(); RestampStreak("before think"); }

    o_FrameStageNotify(stage);

    // FRAME_START runs every frame on the main thread, main menu included -- the browser's
    // Steam side needs exactly that (the post slot only runs with a world on screen).
    if (stage == 0) Web_MainTick();

    if (stage == 2 && g_skinPreWriteRetAddr != 0 && g_enableSkins && g_fsn_entityChangeAge >= 9)
        FSN_PreWriteDefIndexes();

    if (stage == 4) {
        PlayerList_FireChatNotify();
        Rain_Tick();
        HwFoot_Tick();
        {
            bool anyActive = false;
            if (g_enableSkins)
                for (auto& [id, cfg] : g_skinCfgs)
                    if (cfg.enabled && cfg.ksTier == 3 && cfg.ksUseSheenColor)
                        { anyActive = true; break; }
            g_ksEyeUseSheenColorGlobal = anyActive;
        }
        KsEyeTick();
    }

    if (stage == 1 && g_enableCheatsbypass && g_cvar) {
        static ConVar* sv_cheats = nullptr;
        if (!sv_cheats) sv_cheats = g_cvar->FindVarWalk("sv_cheats");
        if (sv_cheats) sv_cheats->ForceValue(1);
    }

    // FRAME_RENDER_START: world materials live, material system not mid-queue, and
    // unlike Present it does not tick through map loads. All material work lives here.
    if (stage == 5) {
        FlipWorld_Tick();   // cl_flipviewmodels hold/release also runs here, outside CreateMove
        FlipWorld_RenderStart();   // RenderView follows: W2S stays unmirrored for the 3D pass
        ApplyMapTextures();
        VmFlatAlpha_Tick();
        Sky_Tick();
        Modulate_Tick();
        ApplyMatColorModulate();
        // Entity-list walks the draw hooks read back. The engine creates and frees entities
        // on this thread, so they run here rather than on Present.
        if (IsInGame()) {
            BodyRules_Tick();
            NoHats_Tick();
            if (g_enablePlChams && g_plChamsAttachments) RebuildAttachmentOwners();
        }
    }

    if (stage != 5 && stage != 3) return;

    if (!g_engine || !g_entityList) return;

    bool enabling  = g_enableSheen && !g_fsn_wasEnabled;
    bool disabling = !g_enableSheen && g_fsn_wasEnabled;
    g_fsn_wasEnabled = g_enableSheen;

    bool skinEnabling  = g_enableSkins && !g_fsn_skinWasEnabled;
    bool skinDisabling = !g_enableSkins && g_fsn_skinWasEnabled;
    g_fsn_skinWasEnabled = g_enableSkins;

    if (!g_enableSheen && !disabling && !g_enableSkins && !skinDisabling) return;

    bool inGame = FSN_IsInGame();
    if (!inGame) return;

    int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1) return;
    void* pLocal = GetClientEntity(localIdx);
    if (!pLocal) return;


    if (skinDisabling) Skins_KsColorRestoreAll();

    if (skinEnabling || skinDisabling || g_skinNeedsUpdate) {
        ResetActiveRemap();
        g_pAttachEnt = nullptr;
        if (g_enableSkins)
            for (auto& [id, cfg] : g_skinCfgs)
                if (cfg.enabled && cfg.reskinDef > 0)
                    GetReskinViewmodelIdx(cfg.reskinDef);
        TriggerForceFullUpdate("skin_enable_or_select");
        s_reskinStabilized = false;
        g_skinNeedsUpdate = false;
    }

    if (pLocal != g_fsn_lastLocalEnt) {
        DebugLog("FSN: local entity changed to 0x%IX", (uintptr_t)pLocal);
        g_fsn_lastLocalEnt      = pLocal;
        g_fsn_entityChangeAge   = 0;
        g_pAttachEnt            = nullptr;
        ResetActiveRemap();
        return;
    }

    if (g_offHViewModel0 == 0 && g_netvarsReady) {
        RecvTable* pBP = FindNetworkTable("DT_BasePlayer");
        int off = pBP ? FindPropOffset(pBP, "m_hViewModel[0]") : -1;
        g_offHViewModel0 = (off > 0) ? off : -1;
        DebugLog("FSN: m_hViewModel[0] off=%d", off);
    }
    if (g_offHViewModel0 > 0)
        SEH_UpdateViewmodelEnt(pLocal, g_offHViewModel0);

    if (g_fsn_entityChangeAge < 9) {
        ++g_fsn_entityChangeAge;
        return;
    }


    if (!s_reskinStabilized && g_enableSkins) {
        bool hasReskin = false;
        for (auto& [id, cfg] : g_skinCfgs)
            if (cfg.enabled && cfg.reskinDef > 0) { hasReskin = true; break; }
        if (hasReskin) {
            s_reskinStabilized = true;
            TriggerForceFullUpdate("reskin_stabilized");
        }
    }

    // Which families the held weapon is remapped between, plus the two skin sweeps. The
    // reverse map has to be current first: after a pre-write the weapon reports OUR def index,
    // and only that map can fold it back onto the config that wrote it.
    if (g_enableSkins) {
        if (g_offSkinSequence == 0 && g_netvarsReady) {
            RecvTable* pAnim = FindNetworkTable("DT_BaseAnimating");
            int off = pAnim ? FindPropOffset(pAnim, "m_nSequence") : -1;
            g_offSkinSequence = (off > 0) ? off : -1;
            DebugLog("FSN: m_nSequence off=%d", off);
        }
        if (g_offSkinSkin == 0 && g_netvarsReady) {
            RecvTable* pAnim = FindNetworkTable("DT_BaseAnimating");
            int off = pAnim ? FindPropOffset(pAnim, "m_nSkin") : -1;
            g_offSkinSkin = (off > 0) ? off : -1;
            DebugLog("FSN: m_nSkin off=%d", off);
        }

        RebuildReverseMap();
        UpdateActiveRemap(pLocal);
        // Second sweep of the same frame, and the only one on the frame a reskin is first
        // switched on: the pre-original sweep runs off the state this call has just refreshed,
        // so it is one frame behind whenever that state changed. Re-mapping an already-mapped
        // index is a no-op, so the two cannot fight.
        if (stage == 5) SweepViewmodelSequence();
        SweepAttachmentSkin((g_offTeam > 0) ? FSN_ReadItemDefIndex(pLocal, g_offTeam) : 0);

        // Re-stamp the killstreak count every update: both arrays are networked, so the server's
        // own count (zero, on a server that never issued the item) lands on top of ours in every
        // snapshot. The value is the total over every killstreak weapon, held or not.
        if (AnyKsWanted()) {
            if (stage == 5) RestampStreak("inside render start");   // log only: stamped again below
            ResolveStreakOffsets();
            int heldKey = 0;
            const SkinCfg* held = ActiveSkinCfg(&heldKey);
            g_killStreak = StreakTotal();
            // Paint the held weapon's sheen row before the streak is stamped: the recolor asks
            // for a bumped count for a few frames, and KsKickedStreak is what supplies it.
            KsColorUpdate(held);
            ApplyKillStreak(localIdx, KsKickedStreak(g_killStreak));
        } else {
            Skins_KsColorRestoreAll();
        }
    }

    if (g_fsn_offMyWeapons == 0) {
        RecvTable* pTable = FindNetworkTable("DT_BaseCombatCharacter");
        if (pTable) {
            int off = FindPropOffset(pTable, "m_hMyWeapons");
            g_fsn_offMyWeapons = (off > 0) ? off : -1;
        }
    }
    if (g_fsn_offMyWeapons <= 0) return;

    if (g_fsn_offAttrList == 0) {
        RecvTable* pTable = FindNetworkTable("DT_EconEntity");
        if (!pTable) pTable = FindNetworkTable("DT_TFWeaponBase");
        if (pTable) {
            int off = FindPropOffset(pTable, "m_AttributeList");
            g_fsn_offAttrList = (off > 0) ? off : -1;
            DebugLog("FSN: m_AttributeList off=%d", off);
        } else {
            g_fsn_offAttrList = -1;
        }
        if (g_fsn_offAttrList <= 0) return;
    }


    if (g_enableSheen) {
        for (int i = 0; i < 48; i++) {
            unsigned int hWeapon = FSN_ReadWeaponHandle(pLocal, g_fsn_offMyWeapons, i);
            if (hWeapon == 0xFFFFFFFF || hWeapon == 0) continue;
            void* pWeapon = FSN_GetEntityFromHandle(hWeapon);
            if (!pWeapon) continue;
            if (!FSN_VtableSane(pWeapon)) continue;
            CAttributeList* pAttrList = reinterpret_cast<CAttributeList*>(
                reinterpret_cast<uintptr_t>(pWeapon) + g_fsn_offAttrList);
            FSN_InjectAttribs(pAttrList);
        }
    }

    if (enabling || disabling)
        TriggerForceFullUpdate("sheen_edge");

    if (g_enableSkins) {
        if (g_fsn_offItemDefIndex == 0) {
            RecvTable* pTbl = FindNetworkTable("DT_EconEntity");
            if (!pTbl) pTbl = FindNetworkTable("DT_TFWeaponBase");
            if (pTbl) {
                int off = FindPropOffset(pTbl, "m_iItemDefinitionIndex");
                g_fsn_offItemDefIndex = (off > 0) ? off : -1;
                DebugLog("FSN: m_iItemDefinitionIndex off=%d", off);
            } else {
                g_fsn_offItemDefIndex = -1;
            }
        }
        if (g_offHViewModel0 == 0) {
            RecvTable* pBP = FindNetworkTable("DT_BasePlayer");
            int off = pBP ? FindPropOffset(pBP, "m_hViewModel[0]") : -1;
            g_offHViewModel0 = (off > 0) ? off : -1;
        }
        if (g_fsn_offItemDefIndex > 0) {
            const int teamNum = (g_offTeam > 0) ? FSN_ReadItemDefIndex(pLocal, g_offTeam) : 0;

            for (int i = 0; i < 48; i++) {
                unsigned int hWeapon = FSN_ReadWeaponHandle(pLocal, g_fsn_offMyWeapons, i);
                if (hWeapon == 0xFFFFFFFF || hWeapon == 0) continue;
                void* pWeapon = FSN_GetEntityFromHandle(hWeapon);
                if (!pWeapon || !FSN_VtableSane(pWeapon)) continue;

                int origDefIdx = FSN_ReadItemDefIndex(pWeapon, g_fsn_offItemDefIndex);
                const SkinTarget wt = ResolveSkinTarget(origDefIdx);
                if (!wt.cfg) continue;

                if (wt.targetDef > 0 && wt.targetDef != origDefIdx)
                    FSN_WriteItemDefIndex(pWeapon, g_fsn_offItemDefIndex, wt.targetDef);

                if (wt.cfg->reskinDef > 0) {
                    // The WORLD model's skin. It is set from the item view in OnDataChanged,
                    // which only runs on a data change, so stamp it every frame -- a style pair
                    // like the Gold Frying Pan's 2/3 then survives a team switch or a late full
                    // update. The model in your hands is the attachment entity, done separately
                    // in SweepAttachmentSkin.
                    const int skin = ReskinSkinFor(*wt.cfg, teamNum);
                    if (skin >= 0 && g_offSkinSkin > 0)
                        SEH_WriteEntInt(pWeapon, g_offSkinSkin, skin);
                }

                Skin_Apply(pWeapon, wt.configKey);
            }
        } else {
            static bool s_noIdxDbgOnce = false;
            if (!s_noIdxDbgOnce) {
                DebugLog("FSN skin: offItemDefIndex=%d, skipping loop", g_fsn_offItemDefIndex);
                s_noIdxDbgOnce = true;
            }
        }
    }
}
