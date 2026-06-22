#include <algorithm>
#include "erquestlog_markers.hpp"
#include "from/params.hpp"
#include "from/paramdef/WORLD_MAP_POINT_PARAM_ST.hpp"
#include <set>
#include <vector>
#include <windows.h>
#include <spdlog/spdlog.h>


using namespace from::paramdef;
using ParamTable = from::params::ParamTable;
using ParamRowInfo = from::params::ParamRowInfo;
using ParamResCap = from::params::ParamResCap;

namespace erquestlog {
namespace markers {

// Currently tracked quest IDs
static std::set<int> g_tracked_quests;

// Injected param state
static uint8_t* g_vanilla_param = nullptr;
static int64_t g_vanilla_size = 0;
static uint8_t* g_expanded_param = nullptr;
static int64_t g_expanded_size = 0;
static uint8_t** g_file_ptr_ref = nullptr;
static int64_t* g_file_size_ref = nullptr;
static void* g_allocation = nullptr;

bool is_quest_tracked(int quest_id) {
    return g_tracked_quests.count(quest_id) > 0;
}

void set_quest_tracked(int quest_id, bool tracked) {
    if (tracked)
        g_tracked_quests.insert(quest_id);
    else
        g_tracked_quests.erase(quest_id);
    update_markers();
}

// Quest step marker definition
struct QuestMarker {
    int quest_id;
    uint32_t enable_flag;   // show marker when this flag is ON
    uint32_t disable_flag;  // hide marker when this flag is ON
    uint8_t areaNo;
    uint8_t gridXNo;
    uint8_t gridZNo;
    float posX;
    float posZ;
};

// Quest step markers: enable_flag = condition for this step, disable_flag = next step's flag.
// Marker shows while tracked and while enable_flag is ON and disable_flag is OFF.
// Flags extracted from ADD_TALK_LIST_IF_DATA_ARGS in erquestlog_quests.hpp.
// For compound/inventory conditions, the primary positive flag is used.
static const QuestMarker QUEST_MARKERS[] = {
    // 8701 - Irina's Quest
    { 8701, 1045349207, 1043319206, 60, 43, 31,    98.820f,   -44.270f }, // q1: Deliver letter to Edgar at Castle Morne
    { 8701, 1043319206, 1045349255, 60, 45, 34,  -104.830f,   -38.670f }, // q2: Return to Irina near Bridge of Sacrifice
    { 8701, 1045349255, 1045349252, 60, 33, 44,    17.880f,    28.050f }, // q3: Edgar invades at Revenger's Shack, Liurnia
    { 8701, 1045349252, 0,          60, 33, 44,    17.880f,    28.050f }, // q4: Defeat Edgar at Revenger's Shack

    // 8702 - Roderika's Quest
    { 8702, 4700,        1041382735, 60, 41, 38,   33.620f,    10.410f }, // q1: Stormhill Shack

    // 8703 - Sellen's Quest
    { 8703, 1044369227,  1044369219, 60, 44, 36,    7.245f,    67.111f }, // q1: Waypoint Ruins, Limgrave
    { 8703, 1044369219,  1044369222, 31, 11,  0,  127.070f,    63.940f }, // q2: Sellia Hideaway (find Lusat)
    { 8703, 1044369222,  1041339256, 60, 41, 33,   48.324f,    -2.566f }, // q3: Witchbane Ruins (break seal)
    { 8703, 1041339256,  1034509255, 60, 35, 46,  -92.910f,   -33.000f }, // q4: Raya Lucaria main gate
    { 8703, 1034509255,  3371,       14,  0,  0,   -7.250f,   -10.240f }, // q5: Raya Lucaria interior
    { 8703, 3371,        14009255,   14,  0,  0,   -7.250f,   -10.240f }, // q6: Raya Lucaria (Sellen transformed)
    { 8703, 14009255,    3469,       14,  0,  0,   -7.250f,   -10.240f }, // q7: Raya Lucaria
    { 8703, 3469,        0,          14,  0,  0,   -7.250f,   -10.240f }, // q8: Raya Lucaria (complete)

    // 8704 - Kenneth Haight's Quest
    { 8704, 1045389205,  1045389222, 60, 45, 38,  -88.600f,   -29.470f }, // q1: South of Summonwater Village
    { 8704, 1045389222,  1046360706, 60, 46, 36,  -25.285f,   -20.259f }, // q2: Fort Haight (clear it)
    { 8704, 1046360706,  1046369207, 60, 46, 36,  -25.285f,   -20.259f }, // q3: Fort Haight (after clearing)
    { 8704, 1046369207,  3587,       60, 46, 36,  -25.285f,   -20.259f }, // q4: Fort Haight (awaiting Nepheli)
    { 8704, 3587,        0,          60, 46, 36,  -25.285f,   -20.259f }, // q5: Fort Haight (crowned)

    // 8705 - Boc's Quest
    { 8705, 1043379355,  31159202,   60, 43, 37,   15.100f,    -5.660f }, // q1: Agheel Lake North (tree form)
    { 8705, 31159202,    31159206,   60, 41, 36,  109.188f,   -99.508f }, // q2: Coastal Cave (recover needle)
    { 8705, 31159206,    1036489208, 60, 39, 39,  -30.410f,   106.710f }, // q3: Church of Irith, Liurnia
    { 8705, 1036489208,  1039409259, 60, 39, 51,   92.640f,    -0.240f }, // q4: Altus Highway Junction
    { 8705, 1039409259,  11109808,   11,  0,  0, -226.140f,  -213.890f }, // q5: Leyndell
    { 8705, 11109808,    11109315,   11,  0,  0, -226.140f,  -213.890f }, // q6: Leyndell
    { 8705, 11109315,    0,          11,  0,  0, -226.140f,  -213.890f }, // q7: Leyndell (complete)

    // 8706 - Blaidd's Quest  (entry: OR(1042369320, 1042369328))
    { 8706, 1042369320,  60830,      60, 45, 37,  -64.827f,  -108.786f }, // q1: Mistwood Ruins, Limgrave
    { 8706, 60830,       1045379205, 12,  2,  0, 1338.470f,  1260.100f }, // q2: Siofra River
    { 8706, 1045379205,  1044349256, 60, 34, 50,  -54.740f,    25.090f }, // q3: Ranni's Rise
    { 8706, 1044349256,  12029155,   12,  2,  0, 1016.200f,  1404.990f }, // q4: Night's Sacred Ground, Nokron
    { 8706, 12029155,    1051369355, 60, 34, 50,  -54.740f,    25.090f }, // q5: Ranni's Rise (after Nokron)
    { 8706, 1051369355,  1052389305, 60, 33, 42,  -31.700f,    67.950f }, // q6: Ringleader's Evergaol
    { 8706, 1052389305,  0,          60, 34, 50,  -54.740f,    25.090f }, // q7: Ranni's Rise (Blaidd fallen)

    // 8707 - Thops's Quest
    { 8707, 1039399215,  1039399220, 60, 39, 39,  -30.410f,   106.710f }, // q1: Church of Irith, Liurnia
    { 8707, 1039399220,  0,          14,  0,  0,   -7.250f,   -10.240f }, // q2: Raya Lucaria interior

    // 8708 - Patches's Quest  (entry: OR(31009206, 16009357))
    { 8708, 60819,       31008522,   60, 43, 37,   75.240f,    61.080f }, // q1: Murkwater Cave, Limgrave
    { 8708, 31008522,    1038419259, 60, 43, 37,   75.240f,    61.080f }, // q2: Murkwater Cave
    { 8708, 1038419259,  14009300,   60, 37, 54,   99.010f,   -84.900f }, // q3: Mt. Gelmir, Seethewater
    { 8708, 14009300,    1037549206, 16,  0,  0,    6.236f,  -136.893f }, // q4: Volcano Manor interior
    { 8708, 1037549206,  3693,       16,  0,  0,    6.236f,  -136.893f }, // q5: Volcano Manor
    { 8708, 3693,        16009355,   16,  0,  0,    6.236f,  -136.893f }, // q6: Volcano Manor
    { 8708, 16009355,    16009357,   16,  0,  0,    6.236f,  -136.893f }, // q7: Volcano Manor
    { 8708, 16009357,    16009364,   16,  0,  0,    6.236f,  -136.893f }, // q8: Volcano Manor
    { 8708, 16009364,    0,          13,  0,  0, -131.740f,   393.740f }, // q9: Crumbling Farum Azula

    // 8709 - Ranni's Quest  (entry: OR(1034509410, 1034509431))
    { 8709, 1034509410,  1034509413, 60, 34, 50,  -54.740f,    25.090f }, // q1: Ranni's Rise
    { 8709, 1034509413,  1034509416, 60, 34, 50,  -54.740f,    25.090f }, // q2: Ranni's Rise
    { 8709, 1034509416,  1044369214, 12,  2,  0, 1016.200f,  1404.990f }, // q3: Night's Sacred Ground (Fingerslayer Blade)
    { 8709, 1044369214,  9130,       60, 34, 50,  -54.740f,    25.090f }, // q4: Ranni's Rise (give blade)
    { 8709, 9130,        1034509420, 34, 11,  0,   -0.480f,     1.910f }, // q6: Carian Study Hall
    { 8709, 1034509420,  1034509421, 12,  1,  0,  162.920f,  -169.220f }, // q7: Ainsel River
    { 8709, 1034509421,  12019257,   12,  1,  0, -531.940f,  -544.060f }, // q8: Lake of Rot / Grand Cloister
    { 8709, 12019257,    1034509406, 12,  1,  0,  -96.590f,   204.280f }, // q11: Nokstella
    { 8709, 1034509406,  0,          60, 35, 42,   31.060f,     4.170f }, // q13: Moonlight Altar

    // 8710 - Rya's Quest
    { 8710, 1037429209,  1037429210, 60, 36, 43,   90.770f,   -58.210f }, // q1: Boilprawn Shack, Liurnia
    { 8710, 1037429210,  1038519205, 60, 37, 42,   49.433f,    43.811f }, // q2: Scenic Isle, Liurnia
    { 8710, 1038519205,  16009305,   60, 36, 53,   11.890f,   109.840f }, // q3: Volcano Manor (exterior)
    { 8710, 16009305,    16009308,   16,  0,  0,    6.236f,  -136.893f }, // q4: Volcano Manor interior
    { 8710, 16009308,    16009307,   16,  0,  0,    6.236f,  -136.893f }, // q5: Volcano Manor
    { 8710, 16009307,    16009315,   16,  0,  0,    6.236f,  -136.893f }, // q6: Volcano Manor
    { 8710, 16009315,    16009318,   16,  0,  0,    6.236f,  -136.893f }, // q7: Volcano Manor
    { 8710, 16009318,    16009319,   16,  0,  0,    6.236f,  -136.893f }, // q8: Volcano Manor
    { 8710, 16009319,    16009222,   16,  0,  0,    6.236f,  -136.893f }, // q9: Volcano Manor
    { 8710, 16009222,    16009326,   16,  0,  0,    6.236f,  -136.893f }, // q10: Volcano Manor
    { 8710, 16009326,    16009327,   16,  0,  0,    6.236f,  -136.893f }, // q11: Volcano Manor
    { 8710, 16009327,    0,          16,  0,  0,    6.236f,  -136.893f }, // q13: Volcano Manor (complete)

    // 8711 - Gowry's Quest
    { 8711, 1050389205,  1050389207, 60, 50, 38,  -87.750f,    45.930f }, // q1: Gowry's Shack, Caelid
    { 8711, 1050389207,  1050389210, 60, 50, 39, -125.780f,   -58.250f }, // q2: Sellia, Town of Sorcery
    { 8711, 1050389210,  1050389257, 60, 50, 38,  -87.750f,    45.930f }, // q3: Gowry's Shack (return)
    { 8711, 1050389257,  1050389219, 60, 50, 38,  -87.750f,    45.930f }, // q4: Gowry's Shack
    { 8711, 1050389219,  1050389221, 60, 50, 38,  -87.750f,    45.930f }, // q5: Gowry's Shack
    { 8711, 1050389221,  1050389227, 60, 50, 38,  -87.750f,    45.930f }, // q6: Gowry's Shack
    { 8711, 1050389227,  1050389230, 60, 50, 38,  -87.750f,    45.930f }, // q7: Gowry's Shack
    { 8711, 1050389230,  1050389234, 60, 50, 38,  -87.750f,    45.930f }, // q8: Gowry's Shack
    { 8711, 1050389234,  1050389235, 60, 50, 38,  -87.750f,    45.930f }, // q9: Gowry's Shack
    { 8711, 1050389235,  0,          60, 50, 38,  -87.750f,    45.930f }, // q10: Gowry's Shack (complete)

    // 8712 - D's Quest  (entry: OR(1044399206, AND(1051439205, 11109617)))
    { 8712, 1044399206,  11109625,   60, 44, 39,    5.380f,    18.030f }, // q1: Summonwater Village outskirts
    { 8712, 12029015,    12039005,   12,  2,  0, 1016.200f,  1404.990f }, // q6: Night's Sacred Ground

    // 8713 - Gurranq's Quest  (counter-based flags; location in quest text)

    // 8714 - Diallos's Quest
    { 8714, 11109406,    1037449205, 60, 37, 44,  -61.140f,    56.690f }, // q1: Gate Town SE, Liurnia
    { 8714, 1037449205,  11109430,   60, 37, 44,  -61.140f,    56.690f }, // q2: Gate Town SE
    { 8714, 11109430,    16009405,   16,  0,  0,    6.236f,  -136.893f }, // q3: Volcano Manor
    { 8714, 16009405,    16002730,   16,  0,  0,    6.236f,  -136.893f }, // q4: Volcano Manor
    { 8714, 16002730,    1039449305, 60, 42, 54,  -77.830f,    96.800f }, // q5: Dominula Windmill Village
    { 8714, 1039449305,  1039449315, 60, 42, 54,  -77.830f,    96.800f }, // q6: Dominula Windmill Village
    { 8714, 1039449315,  1039449285, 60, 39, 44,   43.750f,    -6.680f }, // q7: Jarburg, Liurnia
    { 8714, 1039449285,  0,          60, 39, 44,   43.750f,    -6.680f }, // q8: Jarburg (complete)

    // 8715 - Seluvis's Quest
    { 8715, 1034509312,  11109919,   60, 34, 50,   67.790f,   -89.510f }, // q1: Seluvis's Rise
    { 8715, 11109919,    35009326,   60, 34, 50,   67.790f,   -89.510f }, // q2: Seluvis's Rise
    { 8715, 35009326,    11109553,   60, 34, 50,   67.790f,   -89.510f }, // q3: Seluvis's Rise
    { 8715, 11109553,    1034509313, 60, 34, 50,   67.790f,   -89.510f }, // q4: Seluvis's Rise
    { 8715, 1034509313,  1034509328, 60, 34, 50,   67.790f,   -89.510f }, // q5: Seluvis's Rise
    { 8715, 1034509328,  1034509333, 60, 34, 50,   67.790f,   -89.510f }, // q6: Seluvis's Rise
    { 8715, 1034509333,  1034509335, 60, 34, 50,   67.790f,   -89.510f }, // q7: Seluvis's Rise
    { 8715, 1034509335,  1034509316, 60, 34, 50,   67.790f,   -89.510f }, // q8: Seluvis's Rise
    { 8715, 1034509316,  1034509426, 60, 34, 50,   67.790f,   -89.510f }, // q9: Seluvis's Rise
    { 8715, 1034509426,  0,          60, 34, 50,   67.790f,   -89.510f }, // q10: Seluvis's Rise (complete)

    // 8716 - Dung Eater's Quest
    { 8716, 11109957,    35009306,   35,  0,  0, -196.720f,  -125.876f }, // q2: Subterranean Shunning-Grounds
    { 8716, 35009306,    4248,       35,  0,  0, -196.720f,  -125.876f }, // q3: Shunning-Grounds (rescue)
    { 8716, 11109959,    35009334,   35,  0,  0, -196.720f,  -125.876f }, // q5: Shunning-Grounds (Seedbed Curses)
    { 8716, 35009334,    0,          35,  0,  0, -196.720f,  -125.876f }, // q6: Shunning-Grounds (complete)

    // 8717 - Rogier's Quest  (entry: OR(10009617, 10009616, 10009619))
    { 8717, 10009617,    11109505,   10,  0,  0, -273.040f,   177.860f }, // q1: Stormveil Castle
    { 8717, 1034509432,  11109518,   60, 34, 50,  -54.740f,    25.090f }, // q7: Ranni's Rise area
    { 8717, 1034509410,  11109532,   60, 34, 50,  -54.740f,    25.090f }, // q9: Ranni's Rise

    // 8718 - Nepheli's Quest  (entry: OR(10009706, 11109905))
    { 8718, 10009706,    11109905,   10,  0,  0, -188.070f,   210.780f }, // q1: Stormveil Castle
    { 8718, 1034429205,  11109923,   60, 34, 42,  118.950f,   -87.690f }, // q3: Village of Albinaurics
    { 8718, 10009716,    0,          10,  0,  0, -188.070f,   210.780f }, // q8: Stormveil (Nepheli crowned)

    // 8719 - Hyetta's Quest
    { 8719, 1039409205,  1038439206, 60, 38, 41,   80.850f,    95.820f }, // q1: Laskyar Ruins, Liurnia
    { 8719, 1038439206,  1036499206, 60, 38, 43,   67.000f,    57.010f }, // q2: Gate Town Bridge, Liurnia
    { 8719, 1036499206,  35009205,   60, 41, 33,   82.840f,    68.910f }, // q4: Fourth Church of Marika
    { 8719, 35009205,    35000500,   35,  0,  0, -196.720f,  -125.876f }, // q5: Subterranean Shunning-Grounds
    { 8719, 35000500,    35000701,   35,  0,  0, -196.720f,  -125.876f }, // q6: Shunning-Grounds (deeper)
    { 8719, 35000701,    0,          35,  0,  0, -196.720f,  -125.876f }, // q7: Shunning-Grounds (Frenzied Flame)

    // 8720 - Alexander's Quest  (entry: OR(1043399306, 1051369255, 1051369265))
    { 8720, 1043399306,  32009206,   60, 43, 39,  -17.630f,   -15.510f }, // q1: Saintsbridge, Stormhill
    { 8720, 32009206,    1051369255, 60, 46, 39,  -59.190f,  -124.470f }, // q2: Gael Tunnel entrance, Caelid
    { 8720, 1051369255,  1051369265, 60, 47, 40,  -78.560f,   110.010f }, // q3: Wailing Dunes (Radahn festival)
    { 8720, 1051369265,  1043399306, 60, 39, 44,   -6.290f,   -99.030f }, // q4: Jarburg, Liurnia
    { 8720, 1039449206,  1035539205, 60, 37, 54,   99.010f,   -84.900f }, // q5: Mt. Gelmir, Seethewater
    { 8720, 1035539205,  13009255,   13,  0,  0, -131.740f,   393.740f }, // q6: Crumbling Farum Azula
    { 8720, 13009255,    13009257,   13,  0,  0, -131.740f,   393.740f }, // q7: Farum Azula (fight Alexander)
    { 8720, 13009257,    0,          13,  0,  0, -131.740f,   393.740f }, // q8: Farum Azula (complete)

    // 8721 - Yura's Quest
    { 8721, 1043379260,  1043359257, 60, 43, 35,   37.580f,    69.610f }, // q1: Seaside Ruins, Limgrave
    { 8721, 1043359257,  1043379262, 60, 44, 38, -121.610f,   -46.370f }, // q2: Artist's Shack area, Limgrave
    { 8721, 1043379262,  1035469207, 60, 35, 46,  -59.249f,    32.033f }, // q3: Main Academy Gate, Liurnia
    { 8721, 1035469207,  1039529205, 60, 49, 53,   78.930f,   100.960f }, // q4: Mountaintops of the Giants
    { 8721, 1039529205,  1049539209, 60, 49, 54,  -97.720f,    -3.730f }, // q5: Consecrated Snowfield
    { 8721, 1049539209,  0,          60, 48, 57,   61.280f,    -3.420f }, // q6: Ordina Liturgical Town

    // 8722 - Fia's Quest  (all steps in RTH/Deeproot; location in quest text)

    // 8723 - Varré's Quest
    { 8723, 1042369206,  1035449206, 60, 35, 44,  -88.360f,  -112.470f }, // q1: Rose Church, Liurnia
    { 8723, 1035449206,  1035449216, 60, 35, 44,  -88.360f,  -112.470f }, // q2: Rose Church
    { 8723, 1035449216,  1035449225, 60, 35, 44,  -88.360f,  -112.470f }, // q3: Rose Church
    { 8723, 1035449225,  12059155,   60, 35, 44,  -88.360f,  -112.470f }, // q4: Rose Church
    { 8723, 12059155,    12059165,   12,  5,  0, 1614.660f,  1237.590f }, // q5: Mohgwyn Palace
    { 8723, 12059165,    0,          12,  5,  0, 1614.660f,  1237.590f }, // q6: Mohgwyn Palace (complete)

    // 8724 - Millicent's Quest (step-specific markers with real enable/disable flags)
    { 8724, 1045349207,  1043319206, 60, 38, 51,    3.770f,   114.240f }, // q1: Gowry's Shack (deliver Unalloyed Gold Needle)
    { 8724, 1043319206,  1045349255, 60, 39, 54,  -25.700f,    13.080f }, // q2: Windmill Village / Shaded Castle area
    { 8724, 1045349255,  1045349252, 60, 41, 54,   76.830f,    74.000f }, // q3: Elphael, Haligtree

    // 8725 - Jar-Bairn's Quest  (entry: OR(1039449255, 1039449256))
    { 8725, 1039449255,  1039449263, 60, 39, 44,   43.750f,    -6.680f }, // q1: Jarburg, Liurnia
    { 8725, 1039449263,  1039449270, 60, 39, 44,   43.750f,    -6.680f }, // q2: Jarburg
    { 8725, 1039449270,  1039449289, 60, 39, 44,   43.750f,    -6.680f }, // q3: Jarburg
    { 8725, 1039449289,  0,          60, 39, 44,   43.750f,    -6.680f }, // q4: Jarburg (complete)

    // 8726 - Corhyn's Quest
    { 8726, 11109859,    1040529255, 60, 40, 52, -117.460f,   -78.100f }, // q2: Forest-Spanning Greatbridge
    { 8726, 1040529255,  1040529258, 60, 40, 52, -117.460f,   -78.100f }, // q3: Forest-Spanning Greatbridge
    { 8726, 1040529258,  1040529259, 60, 39, 52,   29.140f,    55.770f }, // q4: Second Church of Marika
    { 8726, 1040529259,  1040549205, 60, 39, 52,   29.140f,    55.770f }, // q5: Second Church of Marika
    { 8726, 11109881,    11009555,   11,  0,  0, -248.040f,  -459.600f }, // q7: Leyndell (Goldmask location)
    { 8726, 11009555,    1051569355, 60, 49, 53,   78.930f,   100.960f }, // q8: Mountaintops of the Giants
    { 8726, 1051569355,  0,          60, 49, 53,   78.930f,   100.960f }, // q9: Mountaintops (complete)

    // 8727 - Latenna's Quest
    { 8727, 1035429209,  1036419207, 60, 36, 41,  136.300f,   -62.700f }, // q1: Slumbering Wolf's Shack
    { 8727, 1036419207,  1051569301, 60, 49, 54,  -97.720f,    -3.730f }, // q2: Consecrated Snowfield (toward Apostate)
    { 8727, 1051569301,  1047589206, 60, 47, 58,   48.130f,   -27.070f }, // q3: Apostate Derelict
    { 8727, 1047589206,  0,          60, 47, 58,   48.130f,   -27.070f }, // q4: Apostate Derelict (complete)

    // 8728 - Bernahl's Quest  (entry: OR(1042382713, 16009455, 16009456))
    { 8728, 1042382713,  16009455,   60, 42, 38,   10.388f,    87.536f }, // q1: Warmaster's Shack, Limgrave
    { 8728, 16009455,    16009457,   16,  0,  0,    6.236f,  -136.893f }, // q2: Volcano Manor interior
    { 8728, 16009457,    16009458,   16,  0,  0,    6.236f,  -136.893f }, // q3: Volcano Manor
    { 8728, 16009458,    7605,       16,  0,  0,    6.236f,  -136.893f }, // q4: Volcano Manor
    { 8728, 7605,        16009461,   16,  0,  0,    6.236f,  -136.893f }, // q5: Volcano Manor
    { 8728, 16009461,    16009460,   16,  0,  0,    6.236f,  -136.893f }, // q6: Volcano Manor
    { 8728, 16009460,    3883,       16,  0,  0,    6.236f,  -136.893f }, // q7: Volcano Manor
    { 8728, 3883,        0,          16,  0,  0,    6.236f,  -136.893f }, // q8: Volcano Manor (complete)

    // 8729 - Ansbach's Quest  (entry: OR(2046429355, 2045429206))
    { 8729, 2046429355,  2045429206, 21,  1,  0,  225.700f,   315.590f }, // q1: Specimen Storehouse, Shadow Keep
    { 8729, 2045429206,  2046429365, 21,  1,  0,  225.700f,   315.590f }, // q2: Specimen Storehouse
    { 8729, 2046429365,  2045429213, 21,  1,  0,  225.700f,   315.590f }, // q3: Specimen Storehouse
    { 8729, 2045429213,  21019305,   21,  1,  0,  225.700f,   315.590f }, // q4: Specimen Storehouse
    { 8729, 21019305,    21019316,   21,  0,  0,  282.810f,   200.280f }, // q5: Shadow Keep inner
    { 8729, 21019316,    21019360,   21,  0,  0,  282.810f,   200.280f }, // q6: Shadow Keep inner
    { 8729, 21019360,    21019310,   21,  0,  0,  282.810f,   200.280f }, // q7: Shadow Keep inner
    { 8729, 21019310,    21019323,   20,  1,  0, -206.190f,  -170.660f }, // q9: Enir-Ilim
    { 8729, 21019323,    21019363,   20,  1,  0, -206.190f,  -170.660f }, // q10: Enir-Ilim
    { 8729, 21019363,    21019321,   20,  1,  0, -206.190f,  -170.660f }, // q11: Enir-Ilim
    { 8729, 21019321,    20019211,   20,  1,  0, -206.190f,  -170.660f }, // q12: Enir-Ilim
    { 8729, 20019211,    0,          20,  1,  0, -206.190f,  -170.660f }, // q13: Enir-Ilim (complete)

    // 8731 - Hornsent's Quest  (entry: OR(2048459278, AND(...)))
    { 8731, 2046429210,  2048459278, 20,  0,  0,  -16.540f,   100.380f }, // q1: Belurat, Tower Settlement
    { 8731, 2048459278,  2048459261, 61, 51, 47,   18.890f,    35.280f }, // q2: Shaman Village, Scaduview
    { 8731, 2048459261,  21012703,   61, 51, 47,   18.890f,    35.280f }, // q3: Shaman Village
    { 8731, 21012703,    21019207,   21,  0,  0,  282.810f,   200.280f }, // q4: Shadow Keep
    { 8731, 21019207,    0,          20,  1,  0, -206.190f,  -170.660f }, // q5: Enir-Ilim (complete)

    // 8732 - Queelign's Quest
    { 8732, 21009212,    21009210,   21,  0,  0,  282.810f,   200.280f }, // q1: Shadow Keep
    { 8732, 21009210,    21009211,   21,  0,  0,  282.810f,   200.280f }, // q2: Shadow Keep
    { 8732, 21009211,    0,          21,  0,  0,  282.810f,   200.280f }, // q3: Shadow Keep (complete)

    // 8733 - Ymir's Quest
    { 8733, 2051459220,  2050400600, 61, 51, 45,  -91.690f,    21.480f }, // q1: Cathedral of Manus Metyr
    { 8733, 2050400600,  2051459235, 61, 51, 45,  -91.690f,    21.480f }, // q2: Cathedral of Manus Metyr
    { 8733, 2051459235,  2053460600, 61, 51, 45,  -91.690f,    21.480f }, // q3: Cathedral of Manus Metyr
    { 8733, 2053460600,  2051459208, 61, 51, 45,  -91.690f,    21.480f }, // q4: Cathedral of Manus Metyr
    { 8733, 2051459208,  4326,       61, 51, 45,  -91.690f,    21.480f }, // q5: Cathedral of Manus Metyr
    { 8733, 4326,        2051459743, 61, 51, 45,  -91.690f,    21.480f }, // q6: Cathedral of Manus Metyr
    { 8733, 2051459743,  2051459744, 61, 51, 45,  -91.690f,    21.480f }, // q7: Cathedral of Manus Metyr
    { 8733, 2051459744,  0,          61, 51, 45,  -91.690f,    21.480f }, // q8: Cathedral of Manus Metyr (complete)

    // 8734 - Igon's Quest
    { 8734, 2048429208,  2049399706, 61, 48, 42,  -80.310f,    60.880f }, // q1: Jagged Peak (foot)
    { 8734, 2049399706,  2052409207, 61, 52, 40,   -6.410f,   -73.250f }, // q2: Jagged Peak (higher)
    { 8734, 2052409207,  2054399208, 61, 52, 40,   -6.410f,   -73.250f }, // q3: Jagged Peak (summit)
    { 8734, 2054399208,  0,          61, 52, 40,   -6.410f,   -73.250f }, // q4: Jagged Peak (complete)

    // 8735 - Thiollier's Quest (St. Trina)
    { 8735, 2048439223,  22009255,   61, 51, 47,   18.890f,    35.280f }, // q1: Shaman Village, Scaduview
    { 8735, 22009255,    2048439228, 20,  1,  0, -206.190f,  -170.660f }, // q2: Enir-Ilim
    { 8735, 2048439228,  22009218,   20,  1,  0, -206.190f,  -170.660f }, // q3: Enir-Ilim
    { 8735, 22009218,    22000710,   20,  1,  0, -206.190f,  -170.660f }, // q4: Enir-Ilim
    { 8735, 22000710,    22009221,   20,  1,  0, -206.190f,  -170.660f }, // q5: Enir-Ilim
    { 8735, 22009221,    22009222,   20,  1,  0, -206.190f,  -170.660f }, // q6: Enir-Ilim
    { 8735, 22009222,    22009231,   20,  1,  0, -206.190f,  -170.660f }, // q7: Enir-Ilim
    { 8735, 22009231,    22009235,   20,  1,  0, -206.190f,  -170.660f }, // q8: Enir-Ilim
    { 8735, 22009235,    22009239,   20,  1,  0, -206.190f,  -170.660f }, // q9: Enir-Ilim
    { 8735, 22009239,    0,          20,  1,  0, -206.190f,  -170.660f }, // q10: Enir-Ilim (complete)
};

static ParamResCap* find_world_map_point_param()
{
    auto param_list = *from::params::param_list_address;
    if (!param_list) return nullptr;
    for (int i = 0; i < 186; i++)
    {
        auto prc = param_list->entries[i].param_res_cap;
        if (!prc) continue;
        std::wstring_view name = from::params::dlw_c_str(&prc->param_name);
        if (name == L"WorldMapPointParam") return prc;
    }
    return nullptr;
}

void update_markers()
{
    auto prc = find_world_map_point_param();
    if (!prc) {
        spdlog::warn("[QUESTMARKER] WorldMapPointParam not found");
        return;
    }

    auto* rescap = reinterpret_cast<uint8_t*>(prc->param_header);
    auto*& file_ptr_ref = *reinterpret_cast<uint8_t**>(rescap + 0x80);
    auto& file_size_ref = *reinterpret_cast<int64_t*>(rescap + 0x78);

    // If we have a vanilla snapshot, restore it before re-injecting
    if (g_vanilla_param) {
        file_ptr_ref = g_vanilla_param;
        file_size_ref = g_vanilla_size;
    }

    // Free previous allocation
    if (g_allocation) {
        HeapFree(GetProcessHeap(), 0, g_allocation);
        g_allocation = nullptr;
        g_expanded_param = nullptr;
    }

    // Build list of active markers (tracked quest + flag conditions)
    std::vector<WORLD_MAP_POINT_PARAM_ST> active_markers;
    std::set<int> seen_quests;
    for (const auto& qm : QUEST_MARKERS) {
        if (!is_quest_tracked(qm.quest_id)) continue;
        bool is_first = seen_quests.find(qm.quest_id) == seen_quests.end();
        seen_quests.insert(qm.quest_id);
        WORLD_MAP_POINT_PARAM_ST row{};
        row.dispMask00 = true;
        row.isEnableNoText = true;  // show icon even without text
        row.iconId = 349;           // quest marker icon frame in sprite 171
        row.textId1 = -1;
        row.areaNo = qm.areaNo;
        row.gridXNo = qm.gridXNo;
        row.gridZNo = qm.gridZNo;
        row.posX = qm.posX;
        row.posZ = qm.posZ;
        // First step always visible (flag 0 = always on); later steps show only when their flag is set
        row.textEnableFlagId1 = is_first ? 0 : qm.enable_flag;
        row.textDisableFlagId1 = qm.disable_flag;
        active_markers.push_back(row);
    }

    if (active_markers.empty())
        return;

    // Inject into WorldMapPointParam
    auto* old_file = file_ptr_ref;
    auto* old_table = reinterpret_cast<ParamTable*>(old_file);
    uint16_t orig_rows = old_table->num_rows;


    // Save vanilla state once
    if (!g_vanilla_param) {
        g_vanilla_param = old_file;
        g_vanilla_size = file_size_ref;
        g_file_ptr_ref = &file_ptr_ref;
        g_file_size_ref = &file_size_ref;
    }

    uint32_t new_count = static_cast<uint32_t>(active_markers.size());
    uint32_t total_rows = orig_rows + new_count;

    constexpr size_t WRAPPER_HEADER = 0x10;
    constexpr size_t HEADER_SIZE = 0x40;
    constexpr size_t ROW_LOC_SIZE = sizeof(from::params::ParamRowInfo);
    constexpr size_t DATA_SIZE = sizeof(WORLD_MAP_POINT_PARAM_ST);

    struct WrapperRowLoc { int32_t row; int32_t index; };

    const char* type_str = reinterpret_cast<const char*>(old_file + old_table->param_type_offset);
    size_t type_len = strlen(type_str) + 1;

    size_t locs_start = HEADER_SIZE;
    size_t data_start = locs_start + total_rows * ROW_LOC_SIZE;
    size_t data_end = data_start + total_rows * DATA_SIZE;
    size_t type_start = data_end;
    size_t after_type = type_start + type_len;
    size_t wrap_start = (after_type + 0xf) & ~(size_t)0xf;
    size_t wrap_end = wrap_start + total_rows * sizeof(WrapperRowLoc);
    size_t param_size = wrap_end;
    size_t total_alloc = WRAPPER_HEADER + param_size;

    g_allocation = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total_alloc);
    if (!g_allocation) {
        spdlog::error("[QUESTMARKER] HeapAlloc failed");
        return;
    }

    auto* new_wrapper = reinterpret_cast<uint8_t*>(g_allocation);
    auto* new_file = new_wrapper + WRAPPER_HEADER;
    auto* new_table = reinterpret_cast<ParamTable*>(new_file);

    *reinterpret_cast<uint32_t*>(new_wrapper + 0x00) = static_cast<uint32_t>(wrap_start);
    *reinterpret_cast<int32_t*>(new_wrapper + 0x04) = static_cast<int32_t>(total_rows);

    memcpy(new_file, old_file, HEADER_SIZE);
    new_table->num_rows = static_cast<uint16_t>(total_rows);
    new_table->param_type_offset = type_start;
    *reinterpret_cast<uint32_t*>(new_file + 0x00) = static_cast<uint32_t>(type_start);
    *reinterpret_cast<uint16_t*>(new_file + 0x04) = static_cast<uint16_t>(data_start);
    *reinterpret_cast<uint64_t*>(new_file + 0x30) = data_start;
    memcpy(new_file + type_start, type_str, type_len);

    // Copy vanilla rows
    auto* locs = reinterpret_cast<from::params::ParamRowInfo*>(new_file + locs_start);
    auto* wrap_locs = reinterpret_cast<WrapperRowLoc*>(new_file + wrap_start);
    size_t file_end = type_start + type_len;

    for (uint16_t i = 0; i < orig_rows; i++) {
        size_t offset = data_start + i * DATA_SIZE;
        locs[i].row_id = old_table->rows[i].row_id;
        locs[i].param_offset = offset;
        locs[i].param_end_offset = file_end;
        memcpy(new_file + offset, old_file + old_table->rows[i].param_offset, DATA_SIZE);
        wrap_locs[i].row = static_cast<int32_t>(old_table->rows[i].row_id);
        wrap_locs[i].index = i;
    }

    // Assign new row IDs starting high to avoid collisions
    int32_t next_id = 90000000;
    for (uint32_t i = 0; i < new_count; i++) {
        size_t idx = orig_rows + i;
        size_t offset = data_start + idx * DATA_SIZE;
        locs[idx].row_id = static_cast<uint64_t>(next_id);
        locs[idx].param_offset = offset;
        locs[idx].param_end_offset = file_end;
        memcpy(new_file + offset, &active_markers[i], DATA_SIZE);
        wrap_locs[idx].row = next_id;
        wrap_locs[idx].index = static_cast<int32_t>(idx);
        next_id++;
    }

    // Sort by row ID
    std::sort(locs, locs + total_rows,
        [](const from::params::ParamRowInfo& a, const from::params::ParamRowInfo& b) {
            return a.row_id < b.row_id;
        });

    g_expanded_param = new_file;
    g_expanded_size = static_cast<int64_t>(param_size);
    file_ptr_ref = new_file;
    file_size_ref = static_cast<int64_t>(param_size);

}

void init_markers()
{
    update_markers();
}

} // namespace markers
} // namespace erquestlog