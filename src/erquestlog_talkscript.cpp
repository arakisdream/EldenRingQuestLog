/**
 * erquestlog_talkscript.cpp
 *
 * Talkscript patching hook. This inserts the questlog menu in the grace dialogue menu
 */
#include "erquestlog_talkscript.hpp"

#include <spdlog/spdlog.h>

#include "erquestlog_quests.hpp"
#include "erquestlog_markers.hpp"
#include "modutils.hpp"

static constexpr unsigned char get_talk_list_entry_result_function = 23;

static std::array<from::EzState::transition *, 100> patched_transition_array;
static std::array<from::EzState::event, 100> patched_events;
static std::array<from::EzState::transition *, 100> patched_transitions;
static std::array<from::EzState::transition *, 100> test_tr;

// When quest_log_state (id=99000) is entered, swap quest list event args to show
// "(tracked)" text for tracked quests. Uses pointer identity to match events.
static void patch_quest_log_tracked()
{
    using QuestArgs = std::array<ezs::arg, 4>;
    struct QuestArgsPair { int quest_id; QuestArgs* normal; QuestArgs* tracked; };

    static QuestArgsPair pairs[] = {
        { 8701, &irina_quest_args,     &irina_quest_tracked_args     },
        { 8702, &roderika_quest_args,  &roderika_quest_tracked_args  },
        { 8703, &sellen_quest_args,    &sellen_quest_tracked_args    },
        { 8704, &kenneth_quest_args,   &kenneth_quest_tracked_args   },
        { 8705, &boc_quest_args,       &boc_quest_tracked_args       },
        { 8706, &blaidd_quest_args,    &blaidd_quest_tracked_args    },
        { 8707, &thops_quest_args,     &thops_quest_tracked_args     },
        { 8708, &patches_quest_args,   &patches_quest_tracked_args   },
        { 8709, &ranni_quest_args,     &ranni_quest_tracked_args     },
        { 8710, &rya_quest_args,       &rya_quest_tracked_args       },
        { 8711, &gowry_quest_args,     &gowry_quest_tracked_args     },
        { 8712, &d_quest_args,         &d_quest_tracked_args         },
        { 8713, &gurranq_quest_args,   &gurranq_quest_tracked_args   },
        { 8714, &diallos_quest_args,   &diallos_quest_tracked_args   },
        { 8715, &seluvis_quest_args,   &seluvis_quest_tracked_args   },
        { 8716, &dungeater_quest_args, &dungeater_quest_tracked_args },
        { 8717, &rogier_quest_args,    &rogier_quest_tracked_args    },
        { 8718, &nepheli_quest_args,   &nepheli_quest_tracked_args   },
        { 8719, &hyetta_quest_args,    &hyetta_quest_tracked_args    },
        { 8720, &alexander_quest_args, &alexander_quest_tracked_args },
        { 8721, &yura_quest_args,      &yura_quest_tracked_args      },
        { 8722, &fia_quest_args,       &fia_quest_tracked_args       },
        { 8723, &varre_quest_args,     &varre_quest_tracked_args     },
        { 8724, &millicent_quest_args, &millicent_quest_tracked_args },
        { 8725, &jarbairn_quest_args,  &jarbairn_quest_tracked_args  },
        { 8726, &corhyn_quest_args,    &corhyn_quest_tracked_args    },
        { 8727, &latenna_quest_args,   &latenna_quest_tracked_args   },
        { 8728, &bernahl_quest_args,   &bernahl_quest_tracked_args   },
        { 8729, &ansbach_quest_args,   &ansbach_quest_tracked_args   },
        { 8731, &hornsent_quest_args,  &hornsent_quest_tracked_args  },
        { 8732, &queelign_quest_args,  &queelign_quest_tracked_args  },
        { 8733, &ymir_quest_args,      &ymir_quest_tracked_args      },
        { 8734, &igon_quest_args,      &igon_quest_tracked_args      },
        { 8735, &trina_quest_args,     &trina_quest_tracked_args     },
    };

    for (auto& ev : quest_log_events) {
        if (ev.command != talk_comm::add_talk_list_data_if) continue;
        for (auto& p : pairs) {
            if (ev.args.data() == p.normal->data() || ev.args.data() == p.tracked->data()) {
                bool tracked = erquestlog::markers::is_quest_tracked(p.quest_id);
                ev.args = tracked ? *p.tracked : *p.normal;
                break;
            }
        }
    }
}

static bool is_sort_chest_transition(const from::EzState::transition *transition)
{
    auto target_state = transition->target_state;
    return target_state != nullptr && !target_state->entry_events.empty() &&
           target_state->entry_events[0].command == from::talk_command::open_repository;
}

std::string to_hex_string(std::span<const unsigned char> data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char byte : data) {
        if (ss.tellp() > 0) ss << "\\x";  // Add space between bytes
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return "\\x" + ss.str();
}

/**
 * Check if the given state group is the main menu for the grace, and patch it to contain the
 * modded menu options
 */
static bool patch_states(from::EzState::state_group *state_group)
{
    from::EzState::state *add_menu_state = nullptr;
    from::EzState::state *menu_transition_state = nullptr;

    int event_index = -1;
    int transition_index = -1;

    for (auto &state : state_group->states)
    {
        for (int i = 0; i < state.entry_events.size(); i++)
        {
            auto &event = state.entry_events[i];

            /*spdlog::debug("Event id: {0}, bank: {1}", 
                    event.command.id,
                    event.command.bank
                );*/

            if (event.command == from::talk_command::add_talk_list_data)
            {
                auto message_id = get_int_value(event.args[1]);
                if (message_id == 15000395)         // Sort chest
                {
                    spdlog::info("Found sort chest message...");
                    add_menu_state = &state;
                    event_index = i;
                }
                else if (message_id == 87000000)    // Quest Log
                {
                    spdlog::debug("Not patching state group x{}, already patched",
                                  0x7fffffff - state_group->id);
                    return true;
                }
            }
        }

        // Look for the state where we check the chosen menu item and transition to a new state.
        for (int i = 0; i < state.transitions.size(); i++)
        {
            auto &transition = state.transitions[i];
            if (is_sort_chest_transition(transition))
            {
                spdlog::info("Found transition state, id: {}...", 0x7fffffff - state_group->id);
                menu_transition_state = &state;
                transition_index = i;

                /*for (auto &tr : menu_transition_state->transitions)
                {
                    spdlog::debug("Identikit saved transitions [0]: x{0}, evaluator: {1}",
                            tr->target_state->id, 
                            to_hex_string(tr->evaluator)
                        );
                }*/
            }
        }
    }

    if (event_index == -1 || transition_index == -1)
    {
        return false;
    }
    spdlog::info("Patching state group x{}", 0x7fffffff - state_group->id);

    // Add "Ongoing Quests" menu option
    auto &events = add_menu_state->entry_events;

    std::copy(events.begin(), events.end(), patched_events.begin());
    patched_events[events.size()] = quest_log;

    events = {patched_events.data(), events.size() + 1};

    // Add a transition to the "Ongoing Quests" menu
    auto &tr = menu_transition_state->transitions;
    
    std::copy(tr.begin(), tr.begin() + transition_index, patched_transitions.begin());
    std::copy(tr.begin() + transition_index, tr.end(),
              patched_transitions.begin() + transition_index + 1);
    patched_transitions[transition_index] = &quest_log_transition;

    tr = {patched_transitions.data(), tr.size() + 1};
    
    /*spdlog::debug("AFTER PATCH:");
    for (auto &t : tr)
    {
        spdlog::debug("Identikit transition: x{0}, evaluator: {1}",
                t->target_state->id, 
                to_hex_string(t->evaluator)
            );
    }*/

    return true;
}

static void (*ezstate_enter_state)(from::EzState::state *,
                                   from::EzState::detail::EzStateMachineImpl *, void *);

/**
 * Hook for EzState::state::Enter()
 */
static void ezstate_enter_state_detour(from::EzState::state *state,
                                       from::EzState::detail::EzStateMachineImpl *machine,
                                       void *unk)
{
    if (state == machine->state_group->initial_state)
    {
        if (patch_states(machine->state_group))
        {
            main_menu_return_transition.target_state = state;
        }
    }

    // Refresh (tracked) labels each time the quest log list opens
    if (state->id == 99000)
    {
        patch_quest_log_tracked();
    }

    // Tracking toggle states: state_id = quest_id * 100 + 51
    if (state->id % 100 == 51) {
        int quest_id = static_cast<int>(state->id / 100);
        if (quest_id >= 8701 && quest_id <= 8735) {
            bool tracked = erquestlog::markers::is_quest_tracked(quest_id);
            erquestlog::markers::set_quest_tracked(quest_id, !tracked);
            spdlog::info("[QUESTMARKER] Quest {} tracking toggled: {}", quest_id, !tracked);
        }
    }

    ezstate_enter_state(state, machine, unk);
}

void erquestlog::setup_talkscript()
{
    modutils::hook(
        {
            .aob = "80 7e 18 00"     // cmp byte ptr [rsi+0x18], 0
                   "74 15"           // je 27
                   "4c 8d 44 24 40"  // lea r8, [rsp+0x40]
                   "48 8b d6"        // mov rdx, rsi
                   "48 8b 4e 20"     // mov rcx, qword ptr [rsi+0x20]
                   "e8 ?? ?? ?? ??", // call EzState::state::Enter
            .offset = 18,
            .relative_offsets = {{1, 5}},
        },
        ezstate_enter_state_detour, ezstate_enter_state);
}
