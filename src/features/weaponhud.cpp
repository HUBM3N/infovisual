#include "weaponhud.h"
#include "../globals.h"
#include "../common.h"
#include "config.h"
#include "jobs.h"
#include "weapon_svgs.h"

#include "../imgui/imgui.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

// ──────────────────────────────────────────────────────────────────────────────
// Weapon names: file-based lookup mirroring TF2's own resolution path
//   items_game.txt  → def_index  + localization token  (e.g. "#TF_Weapon_DragonsFury")
//   tf_english.txt  → token      → English display name (e.g. "Dragon's Fury")
// Stock weapons use prefabs in items_game and have no direct item_name field;
// the static fallback table below covers all of those.
// ──────────────────────────────────────────────────────────────────────────────

// Static fallback: def_index → English display name
static const struct { int def; const char* name; } s_wpnNames[] = {
    {  0,"Bat"},             {  1,"Bottle"},           {  2,"Fire Axe"},
    {  3,"Kukri"},           {  4,"Knife"},            {  5,"Fists"},
    {  6,"Shovel"},          {  7,"Wrench"},           {  8,"Bonesaw"},
    {  9,"Shotgun"},         { 10,"Shotgun"},          { 11,"Shotgun"},
    { 12,"Shotgun"},         { 13,"Scattergun"},       { 14,"Sniper Rifle"},
    { 15,"Minigun"},         { 16,"SMG"},              { 17,"Syringe Gun"},
    { 18,"Rocket Launcher"}, { 19,"Grenade Launcher"}, { 20,"Stickybomb Launcher"},
    { 21,"Flamethrower"},    { 22,"Pistol"},           { 23,"Pistol"},
    { 24,"Revolver"},        { 25,"Build PDA"},        { 26,"Destroy PDA"},
    { 27,"Disguise Kit"},    { 29,"Medi Gun"},
    { 35,"Kritzkrieg"},      { 36,"Blutsauger"},       { 37,"Ubersaw"},
    { 38,"Axtinguisher"},    { 39,"Flare Gun"},        { 40,"Backburner"},
    { 41,"Natascha"},        { 42,"Sandvich"},         { 43,"KGB"},
    { 44,"Sandman"},         { 45,"Force-A-Nature"},   { 46,"Bonk!"},
    { 56,"Huntsman"},        { 57,"Razorback"},        { 58,"Jarate"},
    { 61,"Ambassador"},
    {127,"Direct Hit"},      {128,"Equalizer"},        {129,"Buff Banner"},
    {130,"Scottish Resistance"},{131,"Chargin' Targe"},{132,"Eyelander"},
    {133,"Gunboats"},        {140,"Wrangler"},         {141,"Frontier Justice"},
    {142,"Gunslinger"},      {153,"Homewrecker"},      {154,"Pain Train"},
    {155,"Southern Hospitality"},{159,"Dalokohs Bar"},  {163,"Crit-a-Cola"},
    {171,"Tribalman's Shiv"},{172,"Scotsman's Skullcutter"},{173,"Vita-Saw"},
    {214,"Powerjack"},       {215,"Degreaser"},        {220,"Short Stop"},
    {221,"Holy Mackerel"},   {222,"Mad Milk"},         {224,"L'Etranger"},
    {225,"Your Eternal Reward"},{226,"Battalion's Backup"},{228,"Black Box"},
    {230,"Sydney Sleeper"},  {231,"Darwin's Danger Shield"},{232,"Bushwacka"},
    {237,"Rocket Jumper"},   {239,"GRU"},
    {264,"Frying Pan"},      {265,"Sticky Jumper"},
    {298,"Iron Curtain"},    {304,"Amputator"},        {305,"Crusader's Crossbow"},
    {307,"Ullapool Caber"},  {308,"Loch-n-Load"},      {310,"Warrior's Spirit"},
    {311,"Buffalo Steak Sandvich"},{312,"Brass Beast"},  {317,"Candy Cane"},
    {325,"Boston Basher"},   {326,"Back Scratcher"},   {327,"Claidheamh Mor"},
    {329,"Jag"},             {331,"Steel Fists"},      {348,"Sharpened Volcano Fragment"},
    {349,"Sun-on-a-Stick"},  {351,"Detonator"},        {354,"Concheror"},
    {355,"Fan O'War"},       {356,"Conniver's Kunai"}, {357,"Half-Zatoichi"},
    {401,"Shahanshah"},      {402,"Bazaar Bargain"},   {404,"Persian Persuader"},
    {405,"Ali Baba's Wee Booties"},{406,"Splendid Screen"},{411,"Quick-Fix"},
    {412,"Overdose"},        {413,"Solemn Vow"},       {414,"Liberty Launcher"},
    {415,"Reserve Shooter"}, {416,"Market Gardener"},  {423,"Saxxy"},
    {424,"Tomislav"},        {425,"Family Business"},  {426,"Eviction Notice"},
    {441,"Cow Mangler 5000"},{442,"Righteous Bison"},  {444,"Mantreads"},
    {447,"Disciplinary Action"},{448,"Soda Popper"},   {449,"Winger"},
    {450,"Atomizer"},        {457,"Postal Pummeler"},  {460,"Enforcer"},
    {461,"Big Earner"},      {474,"Conscientious Objector"},
    {482,"Nessie's Nine Iron"},
    {513,"The Original"},    {525,"Diamondback"},      {526,"Machina"},
    {527,"Widowmaker"},      {528,"Short Circuit"},    {574,"Wanga Prick"},
    {587,"Apoco-Fists"},     {588,"Pomson 6000"},      {589,"Eureka Effect"},
    {593,"Third Degree"},    {594,"Phlogistinator"},   {595,"Manmelter"},
    {608,"Bootlegger"},      {609,"Scottish Handshake"},{638,"Sharp Dresser"},
    {642,"Cozy Camper"},     {648,"Wrap Assassin"},    {649,"Spy-cicle"},
    {654,"Festive Minigun"}, {656,"Holiday Punch"},    {658,"Festive Rocket Launcher"},
    {659,"Festive Flamethrower"},{660,"Festive Bat"},  {661,"Festive Stickybomb Launcher"},
    {662,"Festive Wrench"},  {663,"Festive Medi Gun"}, {664,"Festive Sniper Rifle"},
    {665,"Festive Knife"},   {669,"Festive Scattergun"},
    {730,"Beggar's Bazooka"},{735,"Sapper"},           {736,"Sapper"},
    {739,"Lollichop"},       {740,"Scorch Shot"},      {741,"Rainblower"},
    {751,"Cleaner's Carbine"},{752,"Hitman's Heatmaker"},{772,"Baby Face's Blaster"},
    {773,"Pretty Boy's Pocket Pistol"},{775,"Escape Plan"},
    {810,"Red-Tape Recorder"},{811,"Huo-Long Heater"},{812,"Flying Guillotine"},
    {813,"Neon Annihilator"},{851,"AWPer Hand"},       {880,"Freedom Staff"},
    {933,"Ap-Sap"},          {939,"Bat Outta Hell"},   {954,"Memory Maker"},
    {996,"Loose Cannon"},    {997,"Rescue Ranger"},    {998,"Vaccinator"},
    {999,"Festive Holy Mackerel"},
    {1000,"Festive Axtinguisher"},{1001,"Festive Buff Banner"},{1002,"Festive Sandvich"},
    {1003,"Festive Ubersaw"},{1004,"Festive Frontier Justice"},{1005,"Festive Huntsman"},
    {1006,"Festive Ambassador"},{1007,"Festive Grenade Launcher"},
    {1013,"Ham Shank"},      {1071,"Golden Frying Pan"},{1078,"Festive Force-A-Nature"},
    {1079,"Festive Crusader's Crossbow"},{1080,"Festive Sapper"},{1081,"Festive Flare Gun"},
    {1082,"Festive Eyelander"},{1083,"Festive Jarate"},{1084,"Festive GRU"},
    {1085,"Festive Black Box"},{1086,"Festive Wrangler"},
    {1092,"Fortified Compound"},{1098,"Classic"},      {1099,"Tide Turner"},
    {1100,"Bread Bite"},     {1101,"Base Jumper"},     {1102,"Snack Attack"},
    {1103,"Back Scatter"},   {1104,"Air Strike"},      {1123,"Necro Smasher"},
    {1127,"Crossing Guard"}, {1141,"Festive Shotgun"}, {1142,"Festive Revolver"},
    {1143,"Festive Bonesaw"},{1144,"Festive Chargin' Targe"},{1145,"Festive Bonk!"},
    {1146,"Festive Backburner"},{1149,"Festive SMG"},  {1150,"Quickiebomb Launcher"},
    {1151,"Iron Bomber"},    {1153,"Panic Attack"},    {1178,"Dragon's Fury"},
    {1179,"Thermal Thruster"},{1180,"Gas Passer"},     {1181,"Hot Hand"},
    {1190,"Second Banana"},
    {1202,"Reissued Lugermorph"},
    {30474,"Nostromo Napalmer"},{30758,"Prinny Machete"},
    { -1, nullptr }
};

static const char* WpnGetStaticName(int def)
{
    for (int i = 0; s_wpnNames[i].def != -1; ++i)
        if (s_wpnNames[i].def == def) return s_wpnNames[i].name;
    return nullptr;
}

// ─── File-based name map (populated once from TF2's data files) ──────────────
static std::map<int, std::string> s_defNameMap;   // render thread only
static bool s_namesLoaded = false;                 // build requested
static std::atomic<std::map<int, std::string>*> s_defNameBuilt{nullptr};   // worker -> render thread

static void WpnName_BuildFromFiles(std::map<int, std::string>& out)
{
    // Derive the TF2 game dir by stripping "\bin\client.dll" from the module path.
    char dllPath[MAX_PATH] = {};
    GetModuleFileNameA(GetModuleHandleA("client.dll"), dllPath, MAX_PATH);
    for (int strip = 0; strip < 2; ) {
        int len = (int)strlen(dllPath);
        while (len > 0 && dllPath[len-1] != '\\' && dllPath[len-1] != '/') --len;
        if (len > 0) { dllPath[--len] = '\0'; ++strip; } else break;
    }
    if (!dllPath[0]) return;
    std::string tfDir = std::string(dllPath) + "\\";

    // 1. Parse tf_english.txt (UTF-16 LE): each line  "TOKEN"  "Display Name"
    std::map<std::string, std::string> localMap;
    {
        std::wstring engPath(tfDir.begin(), tfDir.end());
        engPath += L"resource\\tf_english.txt";
        FILE* f = _wfopen(engPath.c_str(), L"r,ccs=UTF-16LE");
        if (f) {
            wchar_t wl[1024];
            while (fgetws(wl, 1024, f)) {
                wchar_t* k0 = wcschr(wl,    L'"'); if (!k0) continue; k0++;
                wchar_t* k1 = wcschr(k0,    L'"'); if (!k1) continue; *k1 = L'\0';
                wchar_t* v0 = wcschr(k1+1,  L'"'); if (!v0) continue; v0++;
                wchar_t* v1 = wcschr(v0,    L'"'); if (!v1) continue; *v1 = L'\0';
                char key[256]={}, val[256]={};
                WideCharToMultiByte(CP_UTF8,0,k0,-1,key,256,nullptr,nullptr);
                WideCharToMultiByte(CP_UTF8,0,v0,-1,val,256,nullptr,nullptr);
                if (key[0] && val[0]) localMap[key] = val;
            }
            fclose(f);
        }
    }

    // 2. Parse items_game.txt (ASCII/UTF-8) with a brace-depth state machine.
    //    At depth 2 inside the "items" block: integer keys are def_indices.
    //    At depth 3 inside a def block: "item_name" "#TOKEN" gives the name token.
    {
        std::string igPath = tfDir + "scripts\\items\\items_game.txt";
        FILE* f = fopen(igPath.c_str(), "r");
        if (f) {
            int  depth = 0, curDef = -1;
            char line[1024];
            while (fgets(line, sizeof(line), f)) {
                // Content check BEFORE brace update (def-index key precedes its '{')
                const char* lp = line;
                while (*lp == ' ' || *lp == '\t') ++lp;
                if (*lp == '"') {
                    const char* ke = strchr(lp+1, '"');
                    if (ke) {
                        int klen = (int)(ke - lp - 1);
                        if (depth == 2 && klen > 0 && klen < 8) {
                            bool ok = true;
                            for (int i = 0; i < klen; ++i)
                                if (lp[1+i] < '0' || lp[1+i] > '9') { ok = false; break; }
                            if (ok) curDef = atoi(lp+1);
                        } else if (depth == 3 && curDef >= 0
                                   && klen == 9 && memcmp(lp+1, "item_name", 9) == 0) {
                            const char* v0 = strchr(ke+1, '"');
                            if (v0) {
                                v0++;
                                const char* v1 = strchr(v0, '"');
                                if (v1) {
                                    const char* tok  = (*v0 == '#') ? v0+1 : v0;
                                    int         tlen = (int)(v1 - tok);
                                    if (tlen > 0 && tlen < 128) {
                                        std::string tokStr(tok, tlen);
                                        auto it = localMap.find(tokStr);
                                        if (it != localMap.end())
                                            out[curDef] = it->second;
                                    }
                                }
                            }
                        }
                    }
                }
                // Quote-aware brace tracking
                bool inQ = false;
                for (const char* c = line; *c; ++c) {
                    if (*c == '"') inQ = !inQ;
                    else if (!inQ) {
                        if (*c == '{') ++depth;
                        else if (*c == '}') {
                            if (depth == 3) curDef = -1;
                            if (depth > 0) --depth;
                        }
                    }
                }
            }
            fclose(f);
        }
    }
}

// items_game.txt is megabytes: parsing it inside Present stalled the first frame the name
// label came on. A worker builds a private map and the render thread adopts it when it lands;
// until then the static table above covers stock weapons.
static void WpnName_Pump()
{
    if (!s_namesLoaded) {
        s_namesLoaded = true;
        Jobs::Submit([] {
            auto* m = new std::map<int, std::string>();
            WpnName_BuildFromFiles(*m);
            delete s_defNameBuilt.exchange(m);
        });
    }
    if (auto* m = s_defNameBuilt.exchange(nullptr)) {
        s_defNameMap = std::move(*m);
        delete m;
    }
}

// Display name for weapon by def_index.
// Primary: file-based map (items_game + tf_english) — covers all current TF2 weapons.
// Fallback: static table — covers stock weapons (prefab-based, no direct item_name).
static void WpnGetDisplayName(int def, char* buf, int bufLen)
{
    buf[0] = '\0';
    auto it = s_defNameMap.find(def);
    if (it != s_defNameMap.end()) {
        strncpy_s(buf, bufLen, it->second.c_str(), _TRUNCATE);
        return;
    }
    const char* sn = WpnGetStaticName(def);
    if (sn) strncpy_s(buf, bufLen, sn, _TRUNCATE);
}

// ──────────────────────────────────────────────────────────────────────────────
// Def-index → SVG mapping (linear scan, ~260 entries)
// ──────────────────────────────────────────────────────────────────────────────
struct WpnSvgEntry { int def; const char* data; int len; };
#define WSVG(d, name) { d, k_WpnSVG_##name, (int)sizeof(k_WpnSVG_##name) - 1 }

static const WpnSvgEntry k_wpnSvgTable[] = {
    // ─── Stock slot 0-5 (default items) ─────────────────────────────────────
    WSVG(  0, bat),                    // Bat (scout melee)
    WSVG(  1, bottle),                 // Bottle (demo melee)
    WSVG(  2, fireaxe),                // Fire Axe (pyro melee)
    WSVG(  3, club),                   // Kukri (sniper melee)
    WSVG(  4, knife),                  // Knife (spy melee)
    WSVG(  5, fists),                  // Fists (heavy melee)
    WSVG(  6, shovel),                 // Shovel (soldier melee)
    WSVG(  7, wrench),                 // Wrench (engi melee)
    WSVG(  8, bonesaw),                // Bonesaw (medic melee)
    WSVG(  9, shotgun),                // Shotgun (engi primary)
    WSVG( 10, shotgun),                // Shotgun (soldier secondary)
    WSVG( 11, shotgun),                // Shotgun (heavy secondary)
    WSVG( 12, shotgun),                // Shotgun (pyro secondary)
    WSVG( 13, scattergun),             // Scattergun (scout primary)
    WSVG( 14, sniperrifle),            // Sniper Rifle
    WSVG( 15, minigun),                // Minigun
    WSVG( 16, smg),                    // SMG
    WSVG( 17, syringegun_medic),       // Syringe Gun
    WSVG( 18, rocketlauncher),         // Rocket Launcher
    WSVG( 19, grenadelauncher),        // Grenade Launcher
    WSVG( 20, stickybomblauncher),     // Stickybomb Launcher
    WSVG( 21, flamethrower),           // Flamethrower
    WSVG( 22, pistol),                 // Pistol (engi secondary)
    WSVG( 23, pistol),                 // Pistol (scout secondary)
    WSVG( 24, revolver),               // Revolver
    WSVG( 25, buildpda),               // Build PDA
    WSVG( 26, destroypda),             // Destroy PDA
    WSVG( 27, disguisekit),            // Disguise Kit
    WSVG( 29, medigun),                // Medi Gun
    // ─── Unlocks 35-99 ──────────────────────────────────────────────────────
    WSVG( 35, kritzkrieg),
    WSVG( 36, blutsauger),
    WSVG( 37, ubersaw_),               // Ubersaw (file: ubersaw-.svg → k_WpnSVG_ubersaw_)
    WSVG( 38, axtinguisher),
    WSVG( 39, flaregun),
    WSVG( 40, backburner),
    WSVG( 41, natascha),
    WSVG( 42, sandwich),
    WSVG( 43, gloves),                 // Killing Gloves of Boxing
    WSVG( 44, sandman),
    WSVG( 45, force_a_nature),
    WSVG( 46, bonkatomicpunch),
    WSVG( 56, huntsman),
    WSVG( 57, razorback),
    WSVG( 58, jarate),
    WSVG( 61, ambassador),
    // ─── 100-299 ────────────────────────────────────────────────────────────
    WSVG(127, rocketlauncher_directhit),
    WSVG(128, pickaxe),                // Equalizer
    WSVG(129, buffbanner),
    WSVG(130, scottishresistance),
    WSVG(131, demoshield),             // Chargin' Targe
    WSVG(132, eyelander),
    WSVG(133, gunboats),
    WSVG(140, wrangler_kill),
    WSVG(141, frontier_kill),          // Frontier Justice
    WSVG(142, robot_arm_kill),         // Gunslinger
    WSVG(153, sledgehammer),           // Homewrecker
    WSVG(154, paintrain),
    WSVG(155, southern_comfort_kill),  // Southern Hospitality
    WSVG(159, dalokohs),
    WSVG(160, maxgun),                  // Lugermorph / Max's Pistol
    WSVG(163, critacola),
    WSVG(171, tribalkukri),            // Tribalman's Shiv
    WSVG(172, battleaxe),              // Scotsman's Skullcutter
    WSVG(173, battleneedle),           // Vita-Saw
    // Upgradeable / redir defs
    WSVG(190, bat),
    WSVG(191, bottle),
    WSVG(192, fireaxe),
    WSVG(193, club),
    WSVG(194, knife),
    WSVG(195, fists),
    WSVG(196, shovel),
    WSVG(197, wrench),
    WSVG(198, bonesaw),
    WSVG(199, shotgun),
    WSVG(200, scattergun),
    WSVG(201, sniperrifle),
    WSVG(202, minigun),
    WSVG(203, smg),
    WSVG(205, rocketlauncher),
    WSVG(206, grenadelauncher),
    WSVG(207, stickybomblauncher),
    WSVG(208, flamethrower),
    WSVG(209, pistol),
    WSVG(210, revolver),
    WSVG(211, medigun),
    WSVG(214, powerjack),
    WSVG(215, degreaser),
    WSVG(220, shortstop),
    WSVG(221, holymackerel),
    WSVG(222, mad_milk),
    WSVG(224, letranger),
    WSVG(225, eternal_reward),
    WSVG(226, buffbanner),             // Battalion's Backup
    WSVG(228, blackbox),
    WSVG(230, sydney_sleeper),
    WSVG(231, darwin_shield),
    WSVG(232, bushwacka),
    WSVG(237, rocketjumperr),
    WSVG(239, gloves_running_urgently),
    WSVG(264, fryingpan),
    WSVG(265, stickyjumper),
    WSVG(266, headtaker),             // Horseless Headless Horseman's Headtaker
    WSVG(294, maxgun),               // Lugermorph (Poker Night variant)
    // ─── 298-399 ────────────────────────────────────────────────────────────
    WSVG(298, iron_curtain),
    WSVG(304, amputator),
    WSVG(305, crusaders_crossbow),
    WSVG(307, ullapool_caber),
    WSVG(308, loch_n_load),
    WSVG(310, warrior_spirit),
    WSVG(311, buffalo),                // Buffalo Steak Sandvich
    WSVG(312, brass_beast),
    WSVG(317, candy_cane),
    WSVG(325, boston_basher),
    WSVG(326, back_scratcher),
    WSVG(327, claidheamohmor),
    WSVG(329, wrench_jag),             // Jag
    WSVG(331, steel_fists),
    WSVG(348, lava_axe),               // Sharpened Volcano Fragment
    WSVG(349, sunonastick),            // Sun-on-a-Stick
    WSVG(351, detonator),
    WSVG(354, buffbanner),             // Concheror
    WSVG(355, fanowar),
    WSVG(356, kunai),
    WSVG(357, demokatana),             // Half-Zatoichi
    // ─── 400-499 ────────────────────────────────────────────────────────────
    WSVG(401, shahanshah),
    WSVG(402, bazaar_bargain),
    WSVG(404, persian_persuader),
    WSVG(405, alibaba),                // Ali Baba's Wee Booties
    WSVG(406, splendid_screen),
    WSVG(411, quickfix),
    WSVG(412, overdose),
    WSVG(413, solemn_vow),
    WSVG(414, liberty_launcher),
    WSVG(415, reserve_shooter),
    WSVG(416, market_gardener),
    WSVG(423, saxxy),
    WSVG(424, tomislav),
    WSVG(425, family_business),
    WSVG(426, eviction_notice),
    WSVG(441, cow_mangler),
    WSVG(442, righteous_bison),
    WSVG(444, mantreads),
    WSVG(447, disciplinary_action),
    WSVG(448, soda_popper),
    WSVG(449, the_winger),
    WSVG(450, atomizer),
    WSVG(452, scout_sword),             // Three-Rune Blade
    WSVG(457, mailbox),                // Postal Pummeler
    WSVG(460, enforcer),
    WSVG(461, big_earner),
    WSVG(474, nonnonviolent_protest),  // Conscientious Objector
    WSVG(482, nessieclub),
    // ─── 500-699 ────────────────────────────────────────────────────────────
    WSVG(513, quake_rl),               // The Original
    WSVG(525, diamondback),
    WSVG(526, machina),
    WSVG(527, widowmaker),
    WSVG(528, short_circuit),
    WSVG(574, voodoo_pin),             // Wanga Prick
    WSVG(587, apocofists),
    WSVG(588, pomson),
    WSVG(589, eureka_effect),
    WSVG(593, thirddegree),
    WSVG(594, phlog),
    WSVG(595, manmelter),
    WSVG(608, bootlegger),
    WSVG(609, scotland_shard),         // Scottish Handshake
    WSVG(638, sharp_dresser),
    WSVG(642, cozy_camper),
    WSVG(648, wrapassasin),
    WSVG(649, spycicle),
    // Festive 654-669
    WSVG(654, minigun),
    WSVG(656, holidaypunch),
    WSVG(658, rocketlauncher),
    WSVG(659, flamethrower),
    WSVG(660, bat),
    WSVG(661, stickybomblauncher),
    WSVG(662, wrench),
    WSVG(663, medigun),
    WSVG(664, sniperrifle),
    WSVG(665, knife),
    WSVG(669, scattergun),
    // ─── 700-999 ────────────────────────────────────────────────────────────
    WSVG(730, beggarsbazooka),
    WSVG(735, sapper),
    WSVG(736, sapper),
    WSVG(739, lollichop),              // Lollichop
    WSVG(740, scorchshot),
    WSVG(741, rainblower),             // Rainblower
    WSVG(751, cleanerscarbine),
    WSVG(752, hitmansheatmaker),
    WSVG(772, babyfacesblaster),
    WSVG(773, prettyboyspocketpistol),
    WSVG(775, pickaxe_escape),         // Escape Plan
    WSVG(810, redtaperecorder),        // Red-Tape Recorder
    WSVG(811, huolongheater),
    WSVG(812, cleaver),                // Flying Guillotine
    WSVG(813, neonannihalator),
    WSVG(851, awper),
    WSVG(880, freedomstaff),
    WSVG(933, apsap),
    WSVG(939, batouttahell),
    WSVG(954, memorymaker),
    WSVG(996, loosecannon),
    WSVG(997, rescueranger),
    WSVG(998, vaccinator),
    WSVG(999, holymackerel),           // Festive Holy Mackerel
    // ─── 1000-1200 ──────────────────────────────────────────────────────────
    WSVG(1000, axtinguisher),          // Festive Axtinguisher
    WSVG(1001, buffbanner),            // Festive Buff Banner
    WSVG(1002, sandwich),              // Festive Sandvich
    WSVG(1003, ubersaw_),              // Festive Ubersaw
    WSVG(1004, frontier_kill),         // Festive Frontier Justice
    WSVG(1005, huntsman),              // Festive Huntsman
    WSVG(1006, ambassador),            // Festive Ambassador
    WSVG(1007, grenadelauncher),       // Festive Grenade Launcher
    WSVG(1013, hamshank),
    WSVG(1071, golden_fryingpan),
    WSVG(1078, force_a_nature),        // Festive FaN
    WSVG(1079, crusaders_crossbow),    // Festive Crusader's Crossbow
    WSVG(1080, sapper),                // Festive Sapper
    WSVG(1081, flaregun),              // Festive Flare Gun
    WSVG(1082, eyelander),             // Festive Eyelander
    WSVG(1083, jarate),                // Festive Jarate
    WSVG(1084, gloves_running_urgently), // Festive GRU
    WSVG(1085, blackbox),              // Festive Black Box
    WSVG(1086, wrangler_kill),         // Festive Wrangler
    WSVG(1092, fortifiedcompund),
    WSVG(1098, classic),
    WSVG(1099, tide_turner),
    WSVG(1100, breadbite),
    WSVG(1101, base_jumper),
    WSVG(1102, snackattack),
    WSVG(1103, backscatter),
    WSVG(1104, airstrike),
    WSVG(1123, necrosmasher),
    WSVG(1127, crossingguard),
    WSVG(1141, shotgun),               // Festive Shotgun
    WSVG(1142, revolver),              // Festive Revolver
    WSVG(1143, bonesaw),               // Festive Bonesaw
    WSVG(1144, demoshield),            // Festive Chargin' Targe
    WSVG(1145, bonkatomicpunch),       // Festive Bonk
    WSVG(1146, backburner),            // Festive Backburner
    WSVG(1149, smg),                   // Festive SMG
    WSVG(1150, quickiebomb),
    WSVG(1151, ironbomber),
    WSVG(1153, panic_attack),
    WSVG(1178, dragons_fury),
    WSVG(1179, thermal_thruster),
    WSVG(1180, gaspasser),
    WSVG(1181, hot_hand),
    WSVG(1190, banana),                // Second Banana
    WSVG(1202, maxgun),               // Reissued Lugermorph (2026 promo)
    // ─── Promos ──────────────────────────────────────────────────────────────
    WSVG(30474, nostromo),
    WSVG(30758, prinnymachete),
};
#undef WSVG

static const WpnSvgEntry* WpnSvgFind(int defIdx)
{
    for (const auto& e : k_wpnSvgTable)
        if (e.def == defIdx) return &e;
    return nullptr;
}

static IDirect3DTexture9* Wpn_Rasterize(IDirect3DDevice9* dev, const char* svgData, int svgLen, int kSzH, int* outTexW);

// Exposed for killfeed.cpp: rasterize the SVG for a given defindex at heightPx height.
// Returns null if defindex has no SVG entry. Caller owns the texture (must Release on unload).
IDirect3DTexture9* WpnHud_RasterizeForKf(IDirect3DDevice9* dev, int defIdx, int heightPx, int* outW)
{
    const WpnSvgEntry* e = WpnSvgFind(defIdx);
    if (!e) return nullptr;
    return Wpn_Rasterize(dev, e->data, e->len, heightPx, outW);
}

// ──────────────────────────────────────────────────────────────────────────────
// Width override: def_index → (iconW / iconH) ratio
// ──────────────────────────────────────────────────────────────────────────────
static std::map<int,float> s_widthOverride;
static bool  s_widthsLoaded = false;
static int   s_draggingSlot = -1;
static const char* k_widthFile = "C:\\infovisual\\weapon_widths.txt";

// Baked-in width ratios — applied first so widths are correct even without the file.
// File values (if present) override these.
static const struct { int def; float ratio; } k_bakedWidths[] = {
    {    0, 3.2250f }, {    3, 2.6000f }, {    4, 2.4889f }, {    7, 2.5333f },
    {    8, 2.3111f }, {    9, 2.9111f }, {   10, 2.7333f }, {   11, 2.4000f },
    {   12, 2.8667f }, {   13, 2.4375f }, {   15, 2.1778f }, {   18, 3.0000f },
    {   19, 2.5778f }, {   22, 1.1000f }, {   24, 2.0444f }, {   25, 0.6222f },
    {   26, 0.5778f }, {   27, 1.5111f }, {   29, 2.1556f }, {   35, 2.1333f },
    {   37, 2.4667f }, {   38, 2.2444f }, {   41, 2.5778f }, {   43, 2.1333f },
    {   44, 3.1333f }, {   45, 3.0000f }, {   56, 3.7875f }, {  127, 3.3556f },
    {  128, 2.1556f }, {  129, 1.8444f }, {  132, 3.3556f }, {  153, 2.3556f },
    {  155, 2.3778f }, {  171, 2.9333f }, {  173, 2.0667f }, {  220, 1.4222f },
    {  222, 1.8889f }, {  224, 2.0222f }, {  225, 2.6222f }, {  228, 3.1778f },
    {  230, 3.2444f }, {  232, 2.8444f }, {  237, 3.4000f }, {  239, 2.3333f },
    {  264, 2.2444f }, {  265, 1.6444f }, {  308, 2.6000f }, {  312, 2.4889f },
    {  325, 3.2444f }, {  326, 3.0889f }, {  355, 2.4000f }, {  356, 2.8000f },
    {  357, 3.4889f }, {  401, 2.8444f }, {  404, 2.9333f }, {  411, 2.1778f },
    {  414, 3.0889f }, {  415, 2.5778f }, {  416, 2.7556f }, {  424, 2.6667f },
    {  425, 3.0000f }, {  426, 2.1333f }, {  441, 3.0222f }, {  447, 3.0889f },
    {  450, 3.7111f }, {  460, 1.6444f }, {  461, 2.7333f }, {  482, 3.2000f },
    {  525, 2.0889f }, {  574, 2.3778f }, {  588, 2.9111f }, {  589, 2.6222f },
    {  593, 2.2444f }, {  594, 3.1556f }, {  595, 1.5111f }, {  648, 3.4222f },
    {  642, 1.1265f }, {  649, 2.7333f }, {  656, 1.4222f }, {  664, 3.1111f }, {  730, 3.2222f },
    {  735, 1.3111f }, {  739, 2.7778f }, {  741, 3.1333f }, {  751, 1.6000f },
    {  752, 3.1333f }, {  773, 1.3333f }, {  775, 2.0000f }, {  810, 1.2889f },
    {  811, 2.3111f }, {  813, 3.5750f }, {  933, 1.2000f }, {  996, 2.5778f },
    {  998, 1.9778f }, { 1006, 2.3778f }, { 1080, 1.2444f }, { 1092, 3.3778f },
    { 1101, 1.2222f }, { 1102, 1.2889f }, { 1104, 3.0889f }, { 1123, 1.8667f },
    { 1150, 2.0222f }, { 1151, 2.8889f }, { 1153, 2.8889f }, { 1179, 1.6667f },
    { 1180, 2.1556f }, { 1190, 1.5333f },
};

static void LoadWidthOverrides()
{
    s_widthsLoaded = true;
    // Seed with baked values
    for (const auto& e : k_bakedWidths)
        s_widthOverride[e.def] = e.ratio;
    // File values override baked ones (allows per-machine tuning)
    FILE* f = fopen(k_widthFile, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int def; float ratio;
        if (sscanf_s(line, "%d %f", &def, &ratio) == 2 && ratio > 0.f)
            s_widthOverride[def] = ratio;
    }
    fclose(f);
}

static void SaveWidthOverrides()
{
    FILE* f = fopen(k_widthFile, "w");
    if (!f) return;
    fprintf(f, "# Weapon HUD width ratios (iconW / iconH) — edit mode output\n");
    for (auto& kv : s_widthOverride)
        fprintf(f, "%d %.4f\n", kv.first, kv.second);
    fclose(f);
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-slot texture cache
// Slots 0-7: weapon slots (from m_hMyWeapons indices 0-7)
// Slots 8-15: wearable slots (from m_hMyWearables UtlVector)
// ──────────────────────────────────────────────────────────────────────────────
static constexpr int k_maxSlots        = 16;
static constexpr int k_maxWeaponSlots  = 8;   // how many m_hMyWeapons indices we scan
static constexpr int k_wearableSlotBase = 8;   // wearables start here in s_slotDef
static IDirect3DTexture9* s_slotTex [k_maxSlots] = {};
static int                s_slotDef [k_maxSlots] = { -2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2 };
static int                s_slotTexW[k_maxSlots] = {};  // actual pixel width of each rasterized texture
// Glow textures (Gaussian-blurred silhouette with padding)
static IDirect3DTexture9* s_slotGlowTex[k_maxSlots] = {};
static int                s_slotGlowW  [k_maxSlots] = {};
static int                s_slotGlowH  [k_maxSlots] = {};
static int                s_glowRadius = 0;  // radius used when glow textures were built
static int                s_glowBolden = -1; // bolden used when glow textures were built
static int                s_glowSzH    = 0;  // icon height they were built at
// Hi-res texture for the active slot (rasterized at iconSz * activeScale for sharpness)
static IDirect3DTexture9* s_activeHiTex   = nullptr;
static int                s_activeHiTexW  = 0;
static int                s_activeHiDef   = -2;
static float              s_activeHiScale = 0.f;
static float              s_activeHiSz    = 0.f;

// Returns the texture; sets *outTexW to the actual pixel width of the created texture.
static IDirect3DTexture9* Wpn_Rasterize(IDirect3DDevice9* dev,
    const char* svgData, int svgLen, int kSzH, int* outTexW)
{
    char* buf = new char[(size_t)svgLen + 1];
    memcpy(buf, svgData, (size_t)svgLen);
    buf[svgLen] = '\0';

    NSVGimage* img = nsvgParse(buf, "px", 96.f);
    delete[] buf;
    if (!img || img->width <= 0.f || img->height <= 0.f) {
        if (img) nsvgDelete(img);
        return nullptr;
    }

    // Scale so height == kSzH; keep natural aspect ratio (no clipping, no padding)
    float scale = (float)kSzH / img->height;
    int   kSzW  = std::max(1, (int)(img->width * scale));

    if (outTexW) *outTexW = kSzW;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) { nsvgDelete(img); return nullptr; }

    auto* px = new uint8_t[(size_t)kSzW * kSzH * 4]();
    nsvgRasterize(rast, img, 0.f, 0.f, scale, px, kSzW, kSzH, kSzW * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);

    IDirect3DTexture9* stage = nullptr;
    if (FAILED(dev->CreateTexture(kSzW, kSzH, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_SYSTEMMEM, &stage, nullptr)))
        { delete[] px; return nullptr; }

    D3DLOCKED_RECT lr{};
    if (FAILED(stage->LockRect(0, &lr, nullptr, 0)))
        { stage->Release(); delete[] px; return nullptr; }

    int rp = lr.Pitch / 4;
    auto* dst = reinterpret_cast<uint32_t*>(lr.pBits);
    for (int y = 0; y < kSzH; ++y)
    for (int x = 0; x < kSzW; ++x) {
        uint8_t a = px[(y * kSzW + x) * 4 + 3];
        dst[y * rp + x] = ((uint32_t)a << 24) | 0x00FFFFFF;
    }
    stage->UnlockRect(0);
    delete[] px;

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(kSzW, kSzH, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT, &tex, nullptr)))
        { stage->Release(); return nullptr; }

    if (FAILED(dev->UpdateTexture(stage, tex)))
        { stage->Release(); tex->Release(); return nullptr; }

    stage->Release();
    return tex;
}

// Glow is a padded (iconW+2*rad) x (iconH+2*rad) sheet of white pixels with a
// blurred alpha channel; the draw tints it per slot. Building one is a rasterize
// plus two separable passes over the whole padded area, which at a large radius
// is milliseconds of pure CPU -- far too much to sit in Present, especially with
// six slots rebuilding at once while the radius slider is being dragged.
//
// So it splits in two: WpnGlow_Build is CPU only and runs on a worker, and the
// render thread only does the CreateTexture/UpdateTexture at the end.
struct WpnGlowPixels {
    std::vector<uint32_t> argb;   // pW*pH, white with blurred alpha
    int w = 0, h = 0;
};

static bool WpnGlow_Build(const char* svgData, int svgLen, int kSzH,
                          int glowRad, int boldenRad, WpnGlowPixels& out)
{
    if (glowRad < 1) glowRad = 1;

    std::vector<char> buf(svgData, svgData + svgLen);
    buf.push_back('\0');
    NSVGimage* img = nsvgParse(buf.data(), "px", 96.f);
    if (!img || img->width <= 0.f || img->height <= 0.f) {
        if (img) nsvgDelete(img);
        return false;
    }

    float scale = (float)kSzH / img->height;
    int iW = std::max(1, (int)(img->width * scale));
    int pW = iW    + 2 * glowRad;
    int pH = kSzH  + 2 * glowRad;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) { nsvgDelete(img); return false; }
    std::vector<uint8_t> px((size_t)pW * pH * 4, 0);
    nsvgRasterize(rast, img, (float)glowRad, (float)glowRad, scale, px.data(), pW, pH, pW * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);

    const size_t n = (size_t)pW * pH;
    std::vector<float> alpha(n);
    for (size_t i = 0; i < n; ++i) alpha[i] = px[i * 4 + 3] / 255.f;
    px.clear(); px.shrink_to_fit();

    std::vector<float> tmp(n);

    // Separable max filter: grows the silhouette by boldenRad before the blur,
    // which is what turns a soft glow into an outline+glow.
    if (boldenRad > 0) {
        for (int y = 0; y < pH; ++y)
        for (int x = 0; x < pW; ++x) {
            float mx = 0.f;
            for (int k = -boldenRad; k <= boldenRad; ++k) {
                int sx = std::clamp(x + k, 0, pW - 1);
                mx = std::max(mx, alpha[(size_t)y * pW + sx]);
            }
            tmp[(size_t)y * pW + x] = mx;
        }
        for (int y = 0; y < pH; ++y)
        for (int x = 0; x < pW; ++x) {
            float mx = 0.f;
            for (int k = -boldenRad; k <= boldenRad; ++k) {
                int sy = std::clamp(y + k, 0, pH - 1);
                mx = std::max(mx, tmp[(size_t)sy * pW + x]);
            }
            alpha[(size_t)y * pW + x] = mx;
        }
    }

    const float sigma = glowRad / 2.5f;
    const int   kSz   = 2 * glowRad + 1;
    std::vector<float> kern(kSz);
    float kSum = 0.f;
    for (int i = 0; i < kSz; ++i) {
        float x = (float)(i - glowRad);
        kern[i] = expf(-(x * x) / (2.f * sigma * sigma));
        kSum += kern[i];
    }
    for (int i = 0; i < kSz; ++i) kern[i] /= kSum;

    for (int y = 0; y < pH; ++y)
    for (int x = 0; x < pW; ++x) {
        float s = 0.f;
        for (int k = -glowRad; k <= glowRad; ++k) {
            int sx = std::clamp(x + k, 0, pW - 1);
            s += alpha[(size_t)y * pW + sx] * kern[k + glowRad];
        }
        tmp[(size_t)y * pW + x] = s;
    }
    for (int y = 0; y < pH; ++y)
    for (int x = 0; x < pW; ++x) {
        float s = 0.f;
        for (int k = -glowRad; k <= glowRad; ++k) {
            int sy = std::clamp(y + k, 0, pH - 1);
            s += tmp[(size_t)sy * pW + x] * kern[k + glowRad];
        }
        alpha[(size_t)y * pW + x] = s;
    }

    out.w = pW;
    out.h = pH;
    out.argb.resize(n);
    for (size_t i = 0; i < n; ++i) {
        uint8_t a = (uint8_t)(std::min(alpha[i], 1.f) * 255.f);
        out.argb[i] = ((uint32_t)a << 24) | 0x00FFFFFF;
    }
    return true;
}

// One in-flight build per slot. `gen` is bumped whenever the parameters change,
// so a result that lands after the user moved the slider again is dropped
// instead of overwriting a newer texture.
struct WpnGlowJob {
    WpnGlowPixels     px;
    std::atomic<bool> done{false};
    int               slot = 0;
    unsigned          gen  = 0;
};
static std::mutex                                     s_glowJobMtx;
static std::vector<std::shared_ptr<WpnGlowJob>>       s_glowJobs;
static unsigned                                       s_glowGen     = 0;
static bool                                           s_glowPending[k_maxSlots] = {};

static void WpnGlow_DropJobs()
{
    std::lock_guard<std::mutex> lk(s_glowJobMtx);
    s_glowJobs.clear();
    ++s_glowGen;
    memset(s_glowPending, 0, sizeof(s_glowPending));
}

// Render-thread half: turn a finished pixel buffer into a texture.
static IDirect3DTexture9* WpnGlow_Upload(IDirect3DDevice9* dev, const WpnGlowPixels& src)
{
    if (!dev || src.w <= 0 || src.h <= 0) return nullptr;

    IDirect3DTexture9* stage = nullptr;
    if (FAILED(dev->CreateTexture(src.w, src.h, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_SYSTEMMEM, &stage, nullptr)))
        return nullptr;

    D3DLOCKED_RECT lr{};
    if (FAILED(stage->LockRect(0, &lr, nullptr, 0))) { stage->Release(); return nullptr; }
    const int rp = lr.Pitch / 4;
    auto* dst = reinterpret_cast<uint32_t*>(lr.pBits);
    for (int y = 0; y < src.h; ++y)
        memcpy(dst + (size_t)y * rp, src.argb.data() + (size_t)y * src.w,
               (size_t)src.w * sizeof(uint32_t));
    stage->UnlockRect(0);

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(src.w, src.h, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT, &tex, nullptr)))
        { stage->Release(); return nullptr; }
    if (FAILED(dev->UpdateTexture(stage, tex)))
        { stage->Release(); tex->Release(); return nullptr; }
    stage->Release();
    return tex;
}

// Keeps the glow cache in sync with the current settings: retires stale
// textures, uploads whatever a worker finished, and queues what is still
// missing. Lives outside WeaponHud_Tick because that function uses __try, which
// MSVC won't allow next to objects that need unwinding.
//
// Until a build lands the slot simply draws with no glow -- a frame or two,
// versus the multi-millisecond stall that building it inline used to cost.
static void WpnGlow_Pump(IDirect3DDevice9* dev, int texSzH)
{
    const int glowRad   = std::max(1, (int)g_wpnHudGlowRadius);
    const int boldenRad = std::max(0, (int)g_wpnHudGlowBolden);

    auto releaseGlow = [] {
        for (int i = 0; i < k_maxSlots; ++i) {
            if (s_slotGlowTex[i]) { s_slotGlowTex[i]->Release(); s_slotGlowTex[i] = nullptr; }
            s_slotGlowW[i] = s_slotGlowH[i] = 0;
        }
        WpnGlow_DropJobs();
    };

    // Glow off: free its textures and jobs once, then nothing per frame.
    static bool s_glowOn = false;
    if (!g_wpnHudGlowEnabled) {
        if (s_glowOn) { releaseGlow(); s_glowOn = false; }
        return;
    }
    s_glowOn = true;

    if (glowRad != s_glowRadius || boldenRad != s_glowBolden || texSzH != s_glowSzH) {
        releaseGlow();
        s_glowRadius = glowRad;
        s_glowBolden = boldenRad;
        s_glowSzH    = texSzH;
    }

    std::lock_guard<std::mutex> lk(s_glowJobMtx);

    // Collect anything that finished since the last frame.
    for (size_t j = 0; j < s_glowJobs.size(); ) {
        auto& job = s_glowJobs[j];
        if (!job->done.load(std::memory_order_acquire)) { ++j; continue; }
        if (job->gen == s_glowGen && !s_slotGlowTex[job->slot]) {
            s_slotGlowTex[job->slot] = WpnGlow_Upload(dev, job->px);
            s_slotGlowW[job->slot]   = job->px.w;
            s_slotGlowH[job->slot]   = job->px.h;
        }
        s_glowPending[job->slot] = false;
        s_glowJobs.erase(s_glowJobs.begin() + j);
    }

    // Queue what is still missing.
    for (int i = 0; i < k_maxSlots; ++i) {
        if (!s_slotTex[i] || s_slotGlowTex[i] || s_glowPending[i]) continue;
        const WpnSvgEntry* e = WpnSvgFind(s_slotDef[i]);
        if (!e) continue;

        s_glowPending[i] = true;
        auto job  = std::make_shared<WpnGlowJob>();
        job->slot = i;
        job->gen  = s_glowGen;
        s_glowJobs.push_back(job);

        // The SVG bytes are static const in weapon_svgs.h, so the worker can
        // read them straight through without a copy.
        const char* data = e->data;
        const int   len  = e->len;
        Jobs::Submit([job, data, len, texSzH, glowRad, boldenRad] {
            WpnGlow_Build(data, len, texSzH, glowRad, boldenRad, job->px);
            job->done.store(true, std::memory_order_release);
        });
    }
}

// Width override lookup: if def has no direct override, fall back to the first
// def in the table that shares the same SVG data pointer (handles australium,
// festive, festivized variants that all map to the same base SVG).
static float GetWidthRatio(int def, int texW, int texH)
{
    float natR = (texW > 0 && texH > 0) ? (float)texW / (float)texH : 1.f;
    auto direct = s_widthOverride.find(def);
    if (direct != s_widthOverride.end())
        return direct->second;

    // Which override shares this def's SVG only changes when keys are added or removed (values
    // move live during a drag), so memo it per def: the scan is ~110 x 260 compares per slot.
    static std::map<int, int> s_aliasOf;          // def -> override key with the same SVG, -1 none
    static size_t             s_aliasKeys = (size_t)-1;
    if (s_aliasKeys != s_widthOverride.size()) { s_aliasOf.clear(); s_aliasKeys = s_widthOverride.size(); }
    auto ai = s_aliasOf.find(def);
    if (ai == s_aliasOf.end()) {
        int alias = -1;
        if (const WpnSvgEntry* e = WpnSvgFind(def)) {
            for (const auto& kv : s_widthOverride) {
                const WpnSvgEntry* o = WpnSvgFind(kv.first);
                if (o && o->data == e->data) { alias = kv.first; break; }
            }
        }
        ai = s_aliasOf.emplace(def, alias).first;
    }
    if (ai->second >= 0) {
        auto oi = s_widthOverride.find(ai->second);
        if (oi != s_widthOverride.end()) return oi->second;
    }
    return natR;
}

// ──────────────────────────────────────────────────────────────────────────────
// Gradient background helpers
// ──────────────────────────────────────────────────────────────────────────────

// Draw a horizontal gradient strip [x0,x1] x [y0,y1].
// nearOnLeft=true  → nearCol on left edge, farCol on right edge.
// fadeNearFrac     → fraction of width from the near edge used for a fade-in
//                    (nearCol transparent → nearCol solid). 0 = no near fade.
// fadeFarFrac      → fraction of width from the near edge that stays solid
//                    before the far fade starts. Must be >= fadeNearFrac.
static void DrawHGradStrip(ImDrawList* dl,
    float x0, float x1, float y0, float y1,
    ImU32 nearCol, ImU32 farCol,
    float fadeNearFrac, float fadeFarFrac,
    bool nearOnLeft)
{
    float w = x1 - x0;
    if (w <= 0.f || y1 <= y0) return;

    float fn = std::max(0.f, std::min(1.f, fadeNearFrac));
    float ff = std::max(fn,  std::min(1.f, fadeFarFrac));
    // Transparent version of nearCol for the near-side fade-in
    ImU32 nearTrans = nearCol & 0x00FFFFFF;

    if (nearOnLeft) {
        float pNearEnd  = x0 + w * fn;
        float pFarStart = x0 + w * ff;
        if (fn > 0.f)
            dl->AddRectFilledMultiColor(ImVec2(x0, y0),       ImVec2(pNearEnd, y1),  nearTrans, nearCol, nearCol, nearTrans);
        if (pFarStart > pNearEnd)
            dl->AddRectFilled(          ImVec2(pNearEnd, y0),  ImVec2(pFarStart, y1), nearCol);
        if (pFarStart < x1)
            dl->AddRectFilledMultiColor(ImVec2(pFarStart, y0), ImVec2(x1, y1),        nearCol, farCol, farCol, nearCol);
    } else {
        float pNearEnd  = x1 - w * fn;
        float pFarStart = x1 - w * ff;
        if (pFarStart > x0)
            dl->AddRectFilledMultiColor(ImVec2(x0, y0),        ImVec2(pFarStart, y1), farCol, nearCol, nearCol, farCol);
        if (pNearEnd > pFarStart)
            dl->AddRectFilled(          ImVec2(pFarStart, y0),  ImVec2(pNearEnd, y1),  nearCol);
        if (fn > 0.f)
            dl->AddRectFilledMultiColor(ImVec2(pNearEnd, y0),   ImVec2(x1, y1),        nearCol, nearTrans, nearTrans, nearCol);
    }
}

// Draw the full gradient background for one rect.
// fadeNearFrac / fadeFarFrac forwarded to DrawHGradStrip (ignored in horizMode).
static void DrawWpnBg(ImDrawList* dl,
    float bgX0, float bgX1, float y0, float y1,
    float fadeNearFrac, float fadeFarFrac,
    ImU32 nearCol, ImU32 farCol,
    bool horizMode, bool leftOrigin)
{
    if (bgX1 <= bgX0 || y1 <= y0) return;

    if (horizMode) {
        float w     = bgX1 - bgX0;
        float gradW = w * (1.f - std::max(0.f, std::min(1.f, fadeFarFrac))) * 0.5f;
        float sX0   = bgX0 + gradW;
        float sX1   = bgX1 - gradW;
        if (gradW > 0.f) {
            dl->AddRectFilledMultiColor(ImVec2(bgX0, y0), ImVec2(sX0, y1), farCol, nearCol, nearCol, farCol);
            dl->AddRectFilledMultiColor(ImVec2(sX1, y0), ImVec2(bgX1, y1), nearCol, farCol, farCol, nearCol);
        }
        if (sX1 > sX0) dl->AddRectFilled(ImVec2(sX0, y0), ImVec2(sX1, y1), nearCol);
    } else {
        DrawHGradStrip(dl, bgX0, bgX1, y0, y1, nearCol, farCol, fadeNearFrac, fadeFarFrac, leftOrigin);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────
void WeaponHud_InvalidateTextures()
{
    for (int i = 0; i < k_maxSlots; ++i) {
        if (s_slotTex[i])     { s_slotTex[i]->Release();     s_slotTex[i]     = nullptr; }
        if (s_slotGlowTex[i]) { s_slotGlowTex[i]->Release(); s_slotGlowTex[i] = nullptr; }
        s_slotDef[i]  = -2;
        s_slotTexW[i] = 0;
        s_slotGlowW[i] = s_slotGlowH[i] = 0;
    }
    s_glowRadius = 0;
    s_glowBolden = -1;
    if (s_activeHiTex) { s_activeHiTex->Release(); s_activeHiTex = nullptr; }
    s_activeHiDef   = -2;
    s_activeHiScale = 0.f;
    s_activeHiSz    = 0.f;
    s_activeHiTexW  = 0;
}

void WeaponHud_Tick(IDirect3DDevice9* dev)
{
    if (!g_enableWeaponHud) return;
    if (!g_netvarsReady)    return;

    // Resolve m_hMyWeapons / m_iItemDefinitionIndex here too. Skins used to be the
    // only resolver, so the HUD drew nothing unless skins were also enabled.
    if (g_fsn_offMyWeapons == 0) {
        // g_netvarsReady: the tables are up, so a missing one stays missing (-1, no retry)
        RecvTable* pTable = FindNetworkTable("DT_BaseCombatCharacter");
        int off = pTable ? FindPropOffset(pTable, "m_hMyWeapons") : -1;
        g_fsn_offMyWeapons = (off > 0) ? off : -1;
    }
    if (g_fsn_offItemDefIndex == 0) {
        RecvTable* pTbl = FindNetworkTable("DT_EconEntity");
        if (!pTbl) pTbl = FindNetworkTable("DT_TFWeaponBase");
        int off = pTbl ? FindPropOffset(pTbl, "m_iItemDefinitionIndex") : -1;
        g_fsn_offItemDefIndex = (off > 0) ? off : -1;
    }
    if (g_fsn_offMyWeapons   <= 0) return;
    if (g_fsn_offItemDefIndex <= 0) return;
    if (!IsInGame() || !IsLocalPlayerAlive()) return;

    // Invalidate texture cache when icon size changes so icons re-rasterize
    static float s_lastIconSz = -1.f;
    if (g_wpnHudIconSz != s_lastIconSz) {
        WeaponHud_InvalidateTextures();
        s_lastIconSz = g_wpnHudIconSz;
    }

    // Fade-out tracking: reset timer on weapon switch, fade after delay elapses
    static int   s_fadeLastSlot  = -2;
    static float s_fadeTimer     = 0.f;
    static float s_fadeAlpha     = 1.f;

    int localIdx = GetLocalPlayerIndex();
    if (localIdx < 1) return;
    void* pLocal = GetClientEntity(localIdx);
    if (!pLocal) return;

    // ── Active weapon ────────────────────────────────────────────────────────
    int activeEntIdx = -1;
    if (g_offActiveWeapon > 0) {
        __try {
            unsigned int h = *reinterpret_cast<unsigned int*>(
                reinterpret_cast<uintptr_t>(pLocal) + g_offActiveWeapon);
            if (h != 0xFFFFFFFF && h != 0)
                activeEntIdx = (int)(h & 0x3FFF);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // ── Per-slot defs ────────────────────────────────────────────────────────────
    int   slotDef[k_maxSlots]  = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };
    int  activeSlot  = -1;
    int   activeDef  = -1;
    void* pActiveWpn = nullptr;

    if (g_enableWpnHudName) WpnName_Pump();

    // Weapons: scan m_hMyWeapons[0..k_maxWeaponSlots-1]
    for (int i = 0; i < k_maxWeaponSlots; ++i) {
        unsigned int hWpn = FSN_ReadWeaponHandle(pLocal, g_fsn_offMyWeapons, i);
        if (hWpn == 0xFFFFFFFF || hWpn == 0) continue;
        void* pWpn = FSN_GetEntityFromHandle(hWpn);
        if (!pWpn) continue;
        int def = FSN_ReadItemDefIndex(pWpn, g_fsn_offItemDefIndex);
        if (def < 0) continue;
        // Only track slots with a known SVG mapping (excludes invis watches etc.)
        if (!WpnSvgFind(def)) continue;
        slotDef[i] = def;
        if ((int)(hWpn & 0x3FFF) == activeEntIdx) {
            activeSlot = i; activeDef = def;
            pActiveWpn = pWpn;
        }
    }

    // Wearables: scan entity list for CTFWearable entities owned by localIdx.
    // Only accept the specific defs we support (prevents spy disguise weapons appearing).
    // Returns the weapon slot (0-indexed) each wearable occupies, or -1 if unknown.
    auto WearableGameSlot = [](int def) -> int {
        switch (def) {
            case 133: case 444: case 1101: return 1; // Gunboats, Mantreads, Base Jumper → Soldier secondary
            case 405: case 608: return 0;  // Ali Baba's, Bootlegger → Demo primary
            case 131: case 406: case 1099: case 1144: return 1; // Chargin' Targe, Splendid Screen, Tide Turner, Festive Targe → Demo secondary
            case  57: case 642: case 231: return 1; // Razorback, Cozy Camper, Darwin's → Sniper secondary
            default: return -1;
        }
    };

    // Wearables: read directly from player's m_hMyWearables CUtlVector.
    // Scanned every frame so layout stays in sync with any loadout change.
    {
        static int s_offWearables = 0;

        if (s_offWearables == 0) {
            RecvTable* pBP = FindNetworkTable("DT_TFPlayer");
            int offPlace = pBP ? FindPropOffset(pBP, "m_szLastPlaceName") : -1;
            if (offPlace <= 0) {
                pBP = FindNetworkTable("DT_BasePlayer");
                offPlace = pBP ? FindPropOffset(pBP, "m_szLastPlaceName") : -1;
            }
            s_offWearables = (offPlace > 0) ? (offPlace + 56) : -1;
        }

        int wearDef[8] = {};
        int wearCount  = 0;
        if (s_offWearables > 0 && g_fsn_offItemDefIndex > 0) {
            __try {
                uintptr_t vecAddr = reinterpret_cast<uintptr_t>(pLocal) + s_offWearables;
                uint32_t* pData   = *reinterpret_cast<uint32_t**>(vecAddr);
                int        count  = *reinterpret_cast<int*>(vecAddr + 16); // 64-bit: m_Size at +16
                if (pData && count > 0 && count < 32) {
                    for (int j = 0; j < count && wearCount < 8; ++j) {
                        __try {
                            unsigned int hWear = pData[j];
                            if (hWear == 0xFFFFFFFF || hWear == 0) continue;
                            void* pWear = FSN_GetEntityFromHandle(hWear);
                            if (!pWear) continue;
                            int def = FSN_ReadItemDefIndex(pWear, g_fsn_offItemDefIndex);
                            if (WearableGameSlot(def) < 0) continue;
                            wearDef[wearCount++] = def;
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        // Sort ascending by gameSlot: lower slots (boots=0) must be inserted before
        // higher ones (shield=1) or the shift cascade puts earlier wearables after later ones.
        for (int a = 1; a < wearCount; ++a) {
            int key = wearDef[a], kgs = WearableGameSlot(key), b = a - 1;
            while (b >= 0 && WearableGameSlot(wearDef[b]) > kgs) { wearDef[b+1] = wearDef[b]; --b; }
            wearDef[b+1] = key;
        }

        // Place each wearable at its correct game slot.
        // Skip if already at the right index (covers the case where the shield entity
        // lives in both m_hMyWeapons and m_hMyWearables at the correct position).
        // If TF2 stored it at a wrong m_hMyWeapons index, remove it from there first,
        // then shift existing weapons right and insert at the correct slot.
        for (int j = 0; j < wearCount; ++j) {
            int gameSlot = WearableGameSlot(wearDef[j]);
            if (gameSlot < 0 || gameSlot >= k_maxWeaponSlots) continue;
            if (slotDef[gameSlot] == wearDef[j]) continue;
            // Remove from wrong position if present
            for (int k = 0; k < k_maxWeaponSlots; ++k) {
                if (k != gameSlot && slotDef[k] == wearDef[j]) {
                    for (int m = k; m < k_maxWeaponSlots - 1; ++m)
                        slotDef[m] = slotDef[m + 1];
                    slotDef[k_maxWeaponSlots - 1] = -1;
                    break;
                }
            }
            // Shift right to make room, then insert
            if (slotDef[gameSlot] >= 0) {
                for (int k = k_maxWeaponSlots - 1; k > gameSlot; --k)
                    slotDef[k] = slotDef[k - 1];
            }
            slotDef[gameSlot] = wearDef[j];
        }

        // Re-map activeSlot in case insertion shifted it.
        if (activeDef >= 0) {
            for (int i = 0; i < k_maxWeaponSlots; ++i) {
                if (slotDef[i] == activeDef) { activeSlot = i; break; }
            }
        }
    }

    // ── Texture cache (rasterize at current icon pixel height) ───────────────
    int texSzH = (int)g_wpnHudIconSz;
    if (texSzH < 8) texSzH = 8;
    for (int i = 0; i < k_maxSlots; ++i) {
        if (slotDef[i] == s_slotDef[i]) continue;
        if (s_slotTex[i])     { s_slotTex[i]->Release();     s_slotTex[i]     = nullptr; }
        if (s_slotGlowTex[i]) { s_slotGlowTex[i]->Release(); s_slotGlowTex[i] = nullptr; }
        s_slotDef[i]   = slotDef[i];
        s_slotTexW[i]  = 0;
        s_slotGlowW[i] = s_slotGlowH[i] = 0;
        if (slotDef[i] >= 0) {
            const WpnSvgEntry* e = WpnSvgFind(slotDef[i]);
            if (e) s_slotTex[i] = Wpn_Rasterize(dev, e->data, e->len, texSzH, &s_slotTexW[i]);
        }
    }

    // ── Hi-res active slot texture (avoids blurry upscale when activeScale > 1) ─
    if (g_wpnHudActiveScale > 1.f && activeDef >= 0) {
        int hiSzH = std::max(texSzH, (int)(g_wpnHudIconSz * g_wpnHudActiveScale + 0.5f));
        if (activeDef   != s_activeHiDef   ||
            g_wpnHudActiveScale != s_activeHiScale ||
            g_wpnHudIconSz      != s_activeHiSz) {
            if (s_activeHiTex) { s_activeHiTex->Release(); s_activeHiTex = nullptr; }
            s_activeHiDef   = activeDef;
            s_activeHiScale = g_wpnHudActiveScale;
            s_activeHiSz    = g_wpnHudIconSz;
            s_activeHiTexW  = 0;
            const WpnSvgEntry* e = WpnSvgFind(activeDef);
            if (e) s_activeHiTex = Wpn_Rasterize(dev, e->data, e->len, hiSzH, &s_activeHiTexW);
        }
    } else {
        // Scale <= 1 or no active weapon: release hi-res tex, base tex is sufficient
        if (s_activeHiTex && (g_wpnHudActiveScale <= 1.f || activeDef < 0)) {
            s_activeHiTex->Release(); s_activeHiTex = nullptr;
            s_activeHiDef = -2; s_activeHiScale = 0.f; s_activeHiSz = 0.f; s_activeHiTexW = 0;
        }
    }

    WpnGlow_Pump(dev, texSzH);

    // ── Load width overrides on first run ─────────────────────────────────────
    if (!s_widthsLoaded) LoadWidthOverrides();

// ── Slot display order ────────────────────────────────────────────────────
    // Spy (class 8): their sapper is stored at the building slot (index 4),
    // which comes after knife (index 2) in natural order — wrong for TF2 HUD.
    // Fix: if spy, swap sapper and knife positions in the visual list.
    bool isSpy = (GetPlayerClass(localIdx) == 8);

    // Build ordered list of visible slots
    int vis[k_maxSlots]; int visN = 0;
    for (int i = 0; i < k_maxSlots; ++i)
        if (s_slotTex[i]) vis[visN++] = i;

    if (isSpy && visN >= 2) {
        // Find sapper and knife in the visible list
        static const int k_sapperDefs[] = { 735, 736, 810, 933, 1080, 1102 };
        int sapperPos = -1, knifePos = -1;
        for (int j = 0; j < visN; ++j) {
            int d = slotDef[vis[j]];
            for (int sd : k_sapperDefs) if (d == sd) { sapperPos = j; break; }
            static const int k_knifeDefs[] = { 4, 194, 225, 356, 461, 574, 638, 649, 665 };
            for (int kd : k_knifeDefs) if (d == kd) { knifePos = j; break; }
        }
        // If sapper appears after knife: move sapper to right before knife
        if (sapperPos > knifePos && sapperPos >= 0 && knifePos >= 0) {
            int sapSlot = vis[sapperPos];
            for (int j = sapperPos; j > knifePos; --j) vis[j] = vis[j-1];
            vis[knifePos] = sapSlot;
        }
    }

    // ── Render (only slots with a loaded texture) ─────────────────────────────
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    float sz  = g_wpnHudIconSz;
    float gap = g_wpnHudSlotGap;

    if (visN == 0) return;

    // Fade-out: track slot changes, tick timer, compute wpnAlpha
    {
        const float dt = AnimDt();   // game time (animclock.h)
        if (activeSlot != s_fadeLastSlot) {
            s_fadeLastSlot = activeSlot;
            s_fadeTimer    = 0.f;
            s_fadeAlpha    = 1.f;
        }
        if (g_wpnHudFade) {
            s_fadeTimer += dt;
            if (s_fadeTimer >= g_wpnHudFadeDelay) {
                s_fadeAlpha -= dt * g_wpnHudFadeSpeed;
                if (s_fadeAlpha < 0.f) s_fadeAlpha = 0.f;
            }
        } else {
            s_fadeAlpha = 1.f;
        }
    }
    const float wpnAlpha = s_fadeAlpha;

    bool  leftOrigin = (g_wpnHudOriginH == 1);
    bool  horizMode  = (g_wpnHudOriginH == 2);
    float anchorX    = leftOrigin ? g_wpnHudX : (sw - g_wpnHudX); // unused in horizMode

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 mouse = io.MousePos;

    // ── Pass 1: compute per-row ratio/iconW/iconH (includes live drag update) ──
    struct RowDim { float ratio, iconW, iconH; };
    RowDim dim[k_maxSlots] = {};

    for (int row = 0; row < visN; ++row) {
        int   slot = vis[row];
        int   def  = slotDef[slot];
        float natR = (s_slotTexW[slot] > 0 && texSzH > 0)
                     ? (float)s_slotTexW[slot] / (float)texSzH : 1.f;
        float ratio = GetWidthRatio(def, s_slotTexW[slot], texSzH);

        if (g_wpnHudEditMode && s_draggingSlot == slot && !horizMode) {
            if (io.MouseDown[0]) {
                float newW = leftOrigin ? (mouse.x - anchorX) : (anchorX - mouse.x);
                // Clamp to 10%..400% of natural size
                float scale = (natR > 0.f) ? (newW / sz / natR) : 1.f;
                scale = std::max(0.1f, std::min(4.0f, scale));
                ratio = natR * scale;
                s_widthOverride[def] = ratio;
            }
            if (io.MouseReleased[0]) {
                s_widthOverride[def] = ratio;
                SaveWidthOverrides();
                s_draggingSlot = -1;
            }
        }

        // All modes use the same ratio so baked/user overrides apply consistently.
        float iconW = sz * ratio;
        float iconH = (s_slotTexW[slot] > 0 && texSzH > 0)
                      ? iconW * (float)texSzH / (float)s_slotTexW[slot]
                      : sz;

        // Scale active icon with optional smooth animation
        {
            static float s_slotScale[k_maxSlots] = {
                1.f,1.f,1.f,1.f,1.f,1.f,1.f,1.f,
                1.f,1.f,1.f,1.f,1.f,1.f,1.f,1.f
            };
            float target = (slot == activeSlot) ? g_wpnHudActiveScale : 1.f;
            if (g_wpnHudActiveScaleSmooth) {
                float dt = AnimDt();
                float t  = std::min(1.f, g_wpnHudActiveScaleSpeed * dt);
                s_slotScale[slot] += (target - s_slotScale[slot]) * t;
            } else {
                s_slotScale[slot] = target;
            }
            iconW *= s_slotScale[slot];
            iconH *= s_slotScale[slot];
        }

        dim[row] = { ratio, iconW, iconH };
    }

    // ── Pass 2: compute start position ───────────────────────────────────────
    float curX = 0.f, curY = 0.f;
    if (horizMode) {
        // Total bar width; all icons are sz tall
        float totalW = (visN > 1) ? (visN - 1) * gap : 0.f;
        for (int row = 0; row < visN; ++row) totalW += dim[row].iconW;
        curX = (sw - totalW) / 2.f;
        if      (g_wpnHudOriginV == 1) curY = g_wpnHudY;
        else if (g_wpnHudOriginV == 2) curY = sh - g_wpnHudY - sz;
        else                           curY = (sh - sz) / 2.f + g_wpnHudY;
    } else {
        float totalH = (visN > 1) ? (visN - 1) * gap : 0.f;
        for (int row = 0; row < visN; ++row) totalH += dim[row].iconH;
        if      (g_wpnHudOriginV == 1) curY = g_wpnHudY;
        else if (g_wpnHudOriginV == 2) curY = sh - g_wpnHudY - totalH;
        else                           curY = (sh - totalH) * 0.5f + g_wpnHudY;
    }

    // ── Pass 3: render ────────────────────────────────────────────────────────
    for (int row = 0; row < visN; ++row) {
        int slot = vis[row];
        float iconW = dim[row].iconW;
        float iconH = dim[row].iconH;

        bool   isActive = (slot == activeSlot);
        float* col      = isActive ? g_wpnHudActiveColor  : g_wpnHudInactiveColor;
        const float colA = col[3] * wpnAlpha;

        float x0, x1, y0, y1;
        if (horizMode) {
            x0 = curX; x1 = x0 + iconW;
            // Center each icon vertically around the bar midline (sz tall) so scaled
            // active icons grow upward and downward equally rather than just downward.
            float barMidY = curY + sz * 0.5f;
            y0 = barMidY - iconH * 0.5f;
            y1 = barMidY + iconH * 0.5f;
            curX += iconW + gap;
        } else {
            if (leftOrigin) { x0 = anchorX; x1 = x0 + iconW; }
            else            { x1 = anchorX; x0 = x1 - iconW; }
            y0 = curY; y1 = y0 + iconH;
            curY += iconH + gap;
        }

        ImU32 iconCol = IM_COL32(
            (int)(col[0] * 255), (int)(col[1] * 255), (int)(col[2] * 255),
            (int)(colA * 255));

        // Gradient background behind active weapon
        if (isActive && g_wpnHudBgEnabled) {
            float bgW  = g_wpnHudBgWidth;
            float offX = g_wpnHudBgOffX;

            // X bounds — screen-edge mode pins near side to screen edge; otherwise to anchor.
            float bgX0, bgX1;
            if (horizMode) {
                float midX = (x0 + x1) * 0.5f + offX;
                bgX0 = midX - bgW * 0.5f;
                bgX1 = midX + bgW * 0.5f;
            } else if (g_wpnHudBgScreenEdge) {
                if (leftOrigin) { bgX0 = offX;        bgX1 = bgW + offX; }
                else            { bgX1 = sw - offX;   bgX0 = sw - bgW - offX; }
            } else {
                if (leftOrigin) { bgX0 = anchorX + offX; bgX1 = bgX0 + bgW; }
                else            { bgX1 = anchorX - offX; bgX0 = bgX1 - bgW; }
            }

            // Y bounds — horiz: fixed height centred on icon midline.
            //            vert:  icon + half-gap pad on each side, plus optional extra.
            float bgY0, bgY1;
            if (horizMode) {
                float midY = (y0 + y1) * 0.5f;
                bgY0 = midY - g_wpnHudBgHeightH * 0.5f;
                bgY1 = midY + g_wpnHudBgHeightH * 0.5f;
            } else {
                bgY0 = y0 - gap * 0.5f - g_wpnHudBgExtraH;
                bgY1 = y1 + gap * 0.5f + g_wpnHudBgExtraH;
            }
            bgY0 += g_wpnHudBgOffY;
            bgY1 += g_wpnHudBgOffY;

            float fn    = g_wpnHudBgFadeNear ? g_wpnHudBgFadeNearStart : 0.f;
            float fs    = g_wpnHudBgFadeStart;
            ImU32 nearC = IM_COL32(
                (int)(g_wpnHudBgColorNear[0]*255), (int)(g_wpnHudBgColorNear[1]*255),
                (int)(g_wpnHudBgColorNear[2]*255), (int)(g_wpnHudBgColorNear[3]*wpnAlpha*255));
            ImU32 farC  = IM_COL32(
                (int)(g_wpnHudBgColorFar[0]*255), (int)(g_wpnHudBgColorFar[1]*255),
                (int)(g_wpnHudBgColorFar[2]*255), (int)(g_wpnHudBgColorFar[3]*wpnAlpha*255));

            DrawWpnBg(dl, bgX0, bgX1, bgY0, bgY1, fn, fs, nearC, farC, horizMode, leftOrigin);

            if (g_wpnHudBgBorderEnabled && g_wpnHudBgBorderHeight > 0.f) {
                float bh   = g_wpnHudBgBorderHeight;
                ImU32 bnC  = IM_COL32(
                    (int)(g_wpnHudBgBorderColorNear[0]*255), (int)(g_wpnHudBgBorderColorNear[1]*255),
                    (int)(g_wpnHudBgBorderColorNear[2]*255), (int)(g_wpnHudBgBorderColorNear[3]*wpnAlpha*255));
                ImU32 bfC  = IM_COL32(
                    (int)(g_wpnHudBgBorderColorFar[0]*255), (int)(g_wpnHudBgBorderColorFar[1]*255),
                    (int)(g_wpnHudBgBorderColorFar[2]*255), (int)(g_wpnHudBgBorderColorFar[3]*wpnAlpha*255));
                DrawWpnBg(dl, bgX0, bgX1, bgY0,        bgY0 + bh, fn, fs, bnC, bfC, horizMode, leftOrigin);
                DrawWpnBg(dl, bgX0, bgX1, bgY1 - bh,  bgY1,      fn, fs, bnC, bfC, horizMode, leftOrigin);
            }
        }

        // Rotation helper: rotate a local offset (lx,ly) around the icon centre
        float iconCx = (x0 + x1) * 0.5f, iconCy = (y0 + y1) * 0.5f;
        float iconRad = g_wpnHudIconRotation * (3.14159265f / 180.f);
        float cosR = cosf(iconRad), sinR = sinf(iconRad);
        auto RotPt = [&](float lx, float ly) -> ImVec2 {
            return { iconCx + lx * cosR - ly * sinR,
                     iconCy + lx * sinR + ly * cosR };
        };
        bool useRot = (g_wpnHudIconRotation != 0.f);

        // Glow behind icon (active slot only)
        if (isActive && g_wpnHudGlowEnabled && s_slotGlowTex[slot]) {
            float* gc = g_wpnHudGlowColors[std::min(row, 7)];
            ImU32 glowCol = IM_COL32(
                (int)(gc[0] * 255), (int)(gc[1] * 255), (int)(gc[2] * 255),
                (int)(gc[3] * g_wpnHudGlowAlpha * wpnAlpha * 255));
            // Pad by what the texture was actually built with, not by what the
            // slider currently reads -- a drag can be a frame ahead of the raster.
            float gPad = (s_glowSzH > 0) ? (float)s_glowRadius * (iconH / (float)s_glowSzH)
                                         : (float)s_glowRadius;
            float gu0 = g_wpnHudFlipIcons ? 1.f : 0.f;
            float gu1 = g_wpnHudFlipIcons ? 0.f : 1.f;
            float gx0 = x0 - gPad, gy0 = y0 - gPad;
            float gx1 = x1 + gPad, gy1 = y1 + gPad;
            if (useRot) {
                float ghw = (gx1 - gx0) * 0.5f, ghh = (gy1 - gy0) * 0.5f;
                float gcx = (gx0 + gx1) * 0.5f,  gcy = (gy0 + gy1) * 0.5f;
                auto GR = [&](float lx, float ly) -> ImVec2 {
                    return { gcx + lx * cosR - ly * sinR,
                             gcy + lx * sinR + ly * cosR };
                };
                dl->AddImageQuad((ImTextureID)(uintptr_t)s_slotGlowTex[slot],
                    GR(-ghw,-ghh), GR(ghw,-ghh), GR(ghw,ghh), GR(-ghw,ghh),
                    ImVec2(gu0,0), ImVec2(gu1,0), ImVec2(gu1,1), ImVec2(gu0,1), glowCol);
            } else {
                dl->AddImage((ImTextureID)(uintptr_t)s_slotGlowTex[slot],
                    ImVec2(gx0,gy0), ImVec2(gx1,gy1),
                    ImVec2(gu0,0), ImVec2(gu1,1), glowCol);
            }
        }

        // Slot number
        if (g_wpnHudShowNum) {
            char num[4];
            snprintf(num, sizeof(num), "%d", row + 1);
            ImFont* numFont = (g_wpnHudNumFont && g_wpnHudNumFont->IsLoaded()) ? g_wpnHudNumFont : ImGui::GetFont();
            float   fsz     = numFont->FontSize;
            ImVec2  tsz     = numFont->CalcTextSizeA(fsz, FLT_MAX, 0.f, num);
            float numX, numY;
            if (horizMode) {
                numX = x0 + (iconW - tsz.x) * 0.5f;
                // Pin top/bottom to the base icon height (sz) so the active slot's
                // larger icon doesn't shift the number up or down.
                numY = (g_wpnHudNumPos == 0) ? (curY - tsz.y - 2.f)
                     : (g_wpnHudNumPos == 2) ? (curY + sz + 2.f)
                     :                         (y0 + (iconH - tsz.y) * 0.5f);
            } else {
                numX = leftOrigin ? (x0 - tsz.x - 8.f) : (x1 + 8.f);
                numY = (g_wpnHudNumPos == 0) ? y0
                     : (g_wpnHudNumPos == 2) ? (y1 - tsz.y)
                     :                         (y0 + (iconH - tsz.y) * 0.5f);
            }
            numX += g_wpnHudNumOffX;
            numY += g_wpnHudNumOffY;
            float numTextAlpha;
            ImU32 numCol;
            if (g_wpnHudNumColorMode == 1) {
                // Glow color
                float* gc = g_wpnHudGlowColors[std::min(row, 7)];
                numTextAlpha = gc[3] * g_wpnHudGlowAlpha * wpnAlpha;
                numCol = IM_COL32((int)(gc[0]*255),(int)(gc[1]*255),(int)(gc[2]*255),(int)(numTextAlpha*255));
            } else if (g_wpnHudNumColorMode == 2) {
                // Custom color
                float* cc = g_wpnHudNumCustomColor;
                numTextAlpha = cc[3] * wpnAlpha;
                numCol = IM_COL32((int)(cc[0]*255),(int)(cc[1]*255),(int)(cc[2]*255),(int)(numTextAlpha*255));
            } else {
                // Icon color (default)
                numTextAlpha = colA * (isActive ? (220.f/255.f) : (160.f/255.f));
                numCol = IM_COL32(
                    (int)(col[0]*200),(int)(col[1]*200),(int)(col[2]*200),(int)(numTextAlpha*255));
            }
            if (g_wpnHudNumShadowEnabled) {
                float ox = g_wpnHudNumShadowOffX, oy = g_wpnHudNumShadowOffY;
                ImU32 sc = IM_COL32(
                    (int)(g_wpnHudNumShadowColor[0]*255), (int)(g_wpnHudNumShadowColor[1]*255),
                    (int)(g_wpnHudNumShadowColor[2]*255), (int)(g_wpnHudNumShadowColor[3]*numTextAlpha*255));
                dl->AddText(numFont, fsz, ImVec2(numX+ox, numY+oy), sc, num);
            }
            if (g_wpnHudNumOutlineEnabled) {
                float ot = g_wpnHudNumOutlineThickness;
                ImU32 oc = IM_COL32(
                    (int)(g_wpnHudNumOutlineColor[0]*255), (int)(g_wpnHudNumOutlineColor[1]*255),
                    (int)(g_wpnHudNumOutlineColor[2]*255), (int)(g_wpnHudNumOutlineColor[3]*numTextAlpha*255));
                dl->AddText(numFont, fsz, ImVec2(numX-ot, numY),    oc, num);
                dl->AddText(numFont, fsz, ImVec2(numX+ot, numY),    oc, num);
                dl->AddText(numFont, fsz, ImVec2(numX,    numY-ot), oc, num);
                dl->AddText(numFont, fsz, ImVec2(numX,    numY+ot), oc, num);
                dl->AddText(numFont, fsz, ImVec2(numX-ot, numY-ot), oc, num);
                dl->AddText(numFont, fsz, ImVec2(numX+ot, numY-ot), oc, num);
                dl->AddText(numFont, fsz, ImVec2(numX-ot, numY+ot), oc, num);
                dl->AddText(numFont, fsz, ImVec2(numX+ot, numY+ot), oc, num);
            }
            dl->AddText(numFont, fsz, ImVec2(numX, numY), numCol, num);
        } // end g_wpnHudShowNum

        float u0 = g_wpnHudFlipIcons ? 1.f : 0.f;
        float u1 = g_wpnHudFlipIcons ? 0.f : 1.f;
        // Use hi-res texture for active slot when available (avoids blurry upscale)
        IDirect3DTexture9* drawTex = (isActive && s_activeHiTex) ? s_activeHiTex : s_slotTex[slot];
        if (useRot) {
            float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
            dl->AddImageQuad(
                (ImTextureID)(uintptr_t)drawTex,
                RotPt(-hw,-hh), RotPt(hw,-hh), RotPt(hw,hh), RotPt(-hw,hh),
                ImVec2(u0,0), ImVec2(u1,0), ImVec2(u1,1), ImVec2(u0,1), iconCol);
        } else {
            dl->AddImage(
                (ImTextureID)(uintptr_t)drawTex,
                ImVec2(x0, y0), ImVec2(x1, y1),
                ImVec2(u0, 0), ImVec2(u1, 1), iconCol);
        }

        // Separator after this icon (horizMode only, not after the last icon)
        if (horizMode && g_wpnHudSepEnabled && row < visN - 1) {
            float barMidY  = curY + sz * 0.5f;
            float sepHpx   = sz * g_wpnHudSepHeight;
            float sepMidX  = x1 + gap * 0.5f;
            float sw2      = g_wpnHudSepWidth * 0.5f;
            ImU32 sepCol   = IM_COL32(
                (int)(g_wpnHudSepColor[0]*255), (int)(g_wpnHudSepColor[1]*255),
                (int)(g_wpnHudSepColor[2]*255), (int)(g_wpnHudSepColor[3]*wpnAlpha*255));
            dl->AddRectFilled(
                ImVec2(sepMidX - sw2, barMidY - sepHpx * 0.5f),
                ImVec2(sepMidX + sw2, barMidY + sepHpx * 0.5f),
                sepCol);
        }

        // Weapon name label (active slot only)
        if (isActive && g_enableWpnHudName) {
            char nameBuf[256] = {};
            // Custom name overrides real name when non-empty
            if (g_wpnHudCustomNames && row < 5 && g_wpnHudCustomName[row][0] != '\0') {
                strncpy_s(nameBuf, g_wpnHudCustomName[row], _TRUNCATE);
            } else {
                WpnGetDisplayName(slotDef[slot], nameBuf, (int)sizeof(nameBuf));
                if (g_wpnHudNameMaxChars > 0) {
                    int len = (int)strlen(nameBuf);
                    if (len > g_wpnHudNameMaxChars) {
                        int cut = g_wpnHudNameMaxChars > 3 ? g_wpnHudNameMaxChars - 3 : 0;
                        nameBuf[cut] = '\0';
                        strncat_s(nameBuf, sizeof(nameBuf), "...", _TRUNCATE);
                    }
                }
            }
            if (nameBuf[0]) {
                ImFont* nameFont = (g_wpnHudNameFont && g_wpnHudNameFont->IsLoaded()) ? g_wpnHudNameFont : ImGui::GetFont();
                float   fsz      = nameFont->FontSize;
                ImVec2  tsz      = nameFont->CalcTextSizeA(fsz, FLT_MAX, 0.f, nameBuf);
                float textX = horizMode ? (x0 + (iconW - tsz.x) * 0.5f)
                            : leftOrigin ? x0 : (x1 - tsz.x);
                float textY;
                if (horizMode)
                    textY = (g_wpnHudNamePos == 0) ? (curY - tsz.y - 3.f) : (curY + sz + 3.f);
                else
                    textY = (g_wpnHudNamePos == 0) ? (y0 - tsz.y - 3.f)   : (y1 + 3.f);
                textX += g_wpnHudNameOffX;
                textY += g_wpnHudNameOffY;
                float nameTextAlpha;
                ImU32 nameCol;
                if (g_wpnHudNameColorMode == 1) {
                    // Icon color
                    nameTextAlpha = colA;
                    nameCol = IM_COL32(
                        (int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255),(int)(nameTextAlpha*255));
                } else if (g_wpnHudNameColorMode == 2) {
                    // Custom color
                    float* cc = g_wpnHudNameCustomColor;
                    nameTextAlpha = cc[3] * wpnAlpha;
                    nameCol = IM_COL32(
                        (int)(cc[0]*255),(int)(cc[1]*255),(int)(cc[2]*255),(int)(nameTextAlpha*255));
                } else {
                    // Glow color (default)
                    float* gc    = g_wpnHudGlowColors[std::min(row, 7)];
                    nameTextAlpha = gc[3] * g_wpnHudGlowAlpha * wpnAlpha;
                    nameCol = IM_COL32(
                        (int)(gc[0]*255),(int)(gc[1]*255),(int)(gc[2]*255),(int)(nameTextAlpha*255));
                }
                if (g_wpnHudNameShadowEnabled) {
                    float ox = g_wpnHudNameShadowOffX, oy = g_wpnHudNameShadowOffY;
                    ImU32 sc = IM_COL32(
                        (int)(g_wpnHudNameShadowColor[0]*255), (int)(g_wpnHudNameShadowColor[1]*255),
                        (int)(g_wpnHudNameShadowColor[2]*255), (int)(g_wpnHudNameShadowColor[3]*nameTextAlpha*255));
                    dl->AddText(nameFont, fsz, ImVec2(textX+ox, textY+oy), sc, nameBuf);
                }
                if (g_wpnHudNameOutlineEnabled) {
                    float ot = g_wpnHudNameOutlineThickness;
                    ImU32 oc = IM_COL32(
                        (int)(g_wpnHudNameOutlineColor[0]*255), (int)(g_wpnHudNameOutlineColor[1]*255),
                        (int)(g_wpnHudNameOutlineColor[2]*255), (int)(g_wpnHudNameOutlineColor[3]*nameTextAlpha*255));
                    dl->AddText(nameFont, fsz, ImVec2(textX-ot, textY),    oc, nameBuf);
                    dl->AddText(nameFont, fsz, ImVec2(textX+ot, textY),    oc, nameBuf);
                    dl->AddText(nameFont, fsz, ImVec2(textX,    textY-ot), oc, nameBuf);
                    dl->AddText(nameFont, fsz, ImVec2(textX,    textY+ot), oc, nameBuf);
                    dl->AddText(nameFont, fsz, ImVec2(textX-ot, textY-ot), oc, nameBuf);
                    dl->AddText(nameFont, fsz, ImVec2(textX+ot, textY-ot), oc, nameBuf);
                    dl->AddText(nameFont, fsz, ImVec2(textX-ot, textY+ot), oc, nameBuf);
                    dl->AddText(nameFont, fsz, ImVec2(textX+ot, textY+ot), oc, nameBuf);
                }
                dl->AddText(nameFont, fsz, ImVec2(textX, textY), nameCol, nameBuf);
            }
        }

        // Edit mode: drag handle on the outer edge (vertical modes only)
        if (g_wpnHudEditMode && !horizMode) {
            float hEdge = leftOrigin ? x1 : x0;
            bool onHandle = (mouse.x >= hEdge - 7.f && mouse.x <= hEdge + 7.f &&
                             mouse.y >= y0 && mouse.y <= y1);
            if (onHandle && io.MouseClicked[0]) s_draggingSlot = slot;

            bool isDragging = (s_draggingSlot == slot);
            ImU32 hcol = isDragging ? IM_COL32(255,200, 30,255) :
                         onHandle   ? IM_COL32(255,255,100,220) :
                                      IM_COL32(255,255,255,100);
            float hx = isDragging ? mouse.x : hEdge;
            dl->AddLine(ImVec2(hx, y0 + 2.f), ImVec2(hx, y1 - 2.f), hcol, 3.f);
            float tip  = leftOrigin ? (hx + 5.f) : (hx - 5.f);
            float base = leftOrigin ? (hx - 4.f) : (hx + 4.f);
            dl->AddTriangleFilled(
                ImVec2(base, y0 + (y1-y0)*0.5f - 5.f),
                ImVec2(tip,  y0 + (y1-y0)*0.5f),
                ImVec2(base, y0 + (y1-y0)*0.5f + 5.f),
                hcol);
        }
    }
}
