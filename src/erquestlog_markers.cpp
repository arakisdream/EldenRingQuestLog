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

// Millicent's quest markers - one per step
static const QuestMarker QUEST_MARKERS[] = {
    // Quest 8724 - Millicent
    // Step 1: Meet at Erdtree-Gazing Hill (always show for testing)
    { 8724, 0, 1043319206, 60, 40, 53, 50.000f, 50.000f },
    // Step 2: Get Valkyrie's Prosthesis at Shaded Castle
    { 8724, 1043319206, 1045349255, 60, 39, 54, -25.700f, 13.080f },
    // Step 3: Meet at Windmill Heights
    { 8724, 1045349255, 1045349252, 60, 41, 54, 76.830f, 74.000f },
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
        row.iconId = 349;  // quest marker icon
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

    if (active_markers.empty()) {
        spdlog::info("[QUESTMARKER] No active markers");
        return;
    }

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

    spdlog::info("[QUESTMARKER] Injected {} quest markers", new_count);
}

void init_markers()
{
    spdlog::info("[QUESTMARKER] Initializing");
    g_tracked_quests.insert(8724);  // temp: hardcode Millicent for testing
    update_markers();
}

} // namespace markers
} // namespace erquestlog