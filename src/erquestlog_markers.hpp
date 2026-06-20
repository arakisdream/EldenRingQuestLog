#pragma once

namespace erquestlog {
namespace markers {

void init_markers();
void update_markers();
void set_quest_tracked(int quest_id, bool tracked);
bool is_quest_tracked(int quest_id);

} // namespace markers
} // namespace erquestlog