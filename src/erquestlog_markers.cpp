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

// Quest markers: one entry per quest (initial NPC location from WorldMapPointParam).
// enable_flag=0 / disable_flag=0: always visible while tracked; tracking system controls display.
// Millicent has 3 step-specific markers with real enable/disable flags.
static const QuestMarker QUEST_MARKERS[] = {
    // Quest 8701 - Irina (Weeping Peninsula: near Bridge of Sacrifice)
    { 8701, 0, 0, 60, 45, 34, -104.83f, -38.67f },
    // Quest 8702 - Roderika (Stormhill Shack)
    { 8702, 0, 0, 60, 41, 38, 33.62f, 10.41f },
    // Quest 8703 - Sellen (Waypoint Ruins, Limgrave)
    { 8703, 0, 0, 60, 44, 36, 7.245f, 67.111f },
    // Quest 8704 - Kenneth Haight (South of Summonwater Village)
    { 8704, 0, 0, 60, 45, 38, -88.60f, -29.47f },
    // Quest 8705 - Boc (Agheel Lake North)
    { 8705, 0, 0, 60, 43, 37, 15.10f, -5.66f },
    // Quest 8706 - Blaidd (Siofra River / Nokron)
    { 8706, 0, 0, 12, 2, 0, 1338.47f, 1260.10f },
    // Quest 8707 - Thops (Church of Irith, Liurnia)
    { 8707, 0, 0, 60, 39, 39, -30.41f, 106.71f },
    // Quest 8708 - Patches (Shaded Castle, Altus Plateau)
    { 8708, 0, 0, 60, 39, 54, -12.12f, 54.17f },
    // Quest 8709 - Ranni (Ranni's Rise, northwest Liurnia)
    { 8709, 0, 0, 60, 34, 50, -54.74f, 25.09f },
    // Quest 8710 - Rya (Scenic Isle, Liurnia)
    { 8710, 0, 0, 60, 37, 42, 49.433f, 43.811f },
    // Quest 8711 - Gowry (Gowry's Shack, Caelid)
    { 8711, 0, 0, 60, 50, 38, -87.75f, 45.93f },
    // Quest 8712 - D (Summonwater Village Outskirts)
    { 8712, 0, 0, 60, 44, 39, 5.38f, 18.03f },
    // Quest 8713 - Gurranq (Bestial Sanctum, Dragonbarrow)
    { 8713, 0, 0, 60, 51, 43, -38.35f, 1.85f },
    // Quest 8714 - Diallos (Gate Town Southeast, Liurnia)
    { 8714, 0, 0, 60, 37, 44, -61.14f, 56.69f },
    // Quest 8715 - Seluvis (Seluvis's Rise, northwest Liurnia)
    { 8715, 0, 0, 60, 34, 50, 67.79f, -89.51f },
    // Quest 8716 - Dung Eater (Subterranean Shunning-Grounds, Leyndell)
    { 8716, 0, 0, 35, 0, 0, -196.72f, -125.876f },
    // Quest 8717 - Rogier (Stormveil Castle)
    { 8717, 0, 0, 10, 0, 0, -273.04f, 177.86f },
    // Quest 8718 - Nepheli (Stormveil Castle)
    { 8718, 0, 0, 10, 0, 0, -188.07f, 210.78f },
    // Quest 8719 - Hyetta (Laskyar Ruins, Liurnia)
    { 8719, 0, 0, 60, 38, 41, 80.85f, 95.82f },
    // Quest 8720 - Alexander (Saintsbridge, Stormhill)
    { 8720, 0, 0, 60, 43, 39, -17.63f, -15.51f },
    // Quest 8721 - Yura (Seaside Ruins, Limgrave)
    { 8721, 0, 0, 60, 43, 35, 37.58f, 69.61f },
    // Quest 8722 - Fia (Roundtable Hold; shown at Leyndell entry on map)
    { 8722, 0, 0, 11, 0, 0, -226.14f, -213.89f },
    // Quest 8723 - Varré (Rose Church, Liurnia)
    { 8723, 0, 0, 60, 35, 44, -88.36f, -112.47f },
    // Quest 8724 - Millicent (3 step-specific markers)
    { 8724, 1045349207, 1043319206, 60, 38, 51, 3.770f, 114.240f },
    { 8724, 1043319206, 1045349255, 60, 39, 54, -25.700f, 13.080f },
    { 8724, 1045349255, 1045349252, 60, 41, 54, 76.830f, 74.000f },
    // Quest 8725 - Jar-Bairn (Jarburg, Liurnia)
    { 8725, 0, 0, 60, 39, 44, 43.75f, -6.68f },
    // Quest 8726 - Corhyn (Forest-Spanning Greatbridge, Altus Plateau)
    { 8726, 0, 0, 60, 40, 52, -117.46f, -78.10f },
    // Quest 8727 - Latenna (Slumbering Wolf's Shack, Liurnia)
    { 8727, 0, 0, 60, 36, 41, 136.30f, -62.70f },
    // Quest 8728 - Bernahl (Warmaster's Shack, Limgrave)
    { 8728, 0, 0, 60, 42, 38, 10.388f, 87.536f },
    // Quest 8729 - Ansbach (Specimen Storehouse, Shadow Keep)
    { 8729, 0, 0, 21, 1, 0, 225.70f, 315.59f },
    // Quest 8731 - Hornsent (Belurat, Tower Settlement)
    { 8731, 0, 0, 20, 0, 0, -16.54f, 100.38f },
    // Quest 8732 - Queelign (Shadow Keep)
    { 8732, 0, 0, 21, 0, 0, 282.81f, 200.28f },
    // Quest 8733 - Ymir (Cathedral of Manus Metyr, Scadu Altus)
    { 8733, 0, 0, 61, 51, 45, -91.69f, 21.48f },
    // Quest 8734 - Igon (Jagged Peak, SotE)
    { 8734, 0, 0, 61, 48, 42, -80.31f, 60.88f },
    // Quest 8735 - Thiollier (Enir-Ilim, SotE)
    { 8735, 0, 0, 20, 1, 0, -206.19f, -170.66f },
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
    for (const auto& qm : QUEST_MARKERS) {
        if (!is_quest_tracked(qm.quest_id)) continue;
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
        row.textEnableFlagId1 = qm.enable_flag;
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