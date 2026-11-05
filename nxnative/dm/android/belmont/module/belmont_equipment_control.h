// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_inventory.h"
#include <cstring>
#include <deque>
#include <set>

// Produce one native button, then require the caller to wait for and inspect a
// fresh coherent snapshot. No timed multi-button macro, memory write or guest
// function call. The runtime's normal controller action performs each step.
namespace belmont_equipment_control {
enum class Button : std::int32_t {
  None = 0,
  Plus = 1,
  Left = 2,
  Right = 3,
  A = 4,
  B = 5,
  Up = 6,
  Down = 7,
  L = 8,
  R = 9
};
struct Step {
  Button button{Button::None};
  bool target_selected{};
  const char *reason{"unavailable"};
};
inline Step Navigate(const belmont_inventory::Control &c, std::int32_t type,
                     const std::string &id = {}) {
  if (!c.ready)
    return {};
  if (type < 1 || type > 3)
    return {Button::None, false, "invalid_category"};
  if (!c.menu_open)
    return {c.can_open ? Button::Plus : Button::None, false,
            "open_native_equipment"};
  if (!c.equipment_tab) {
    if (c.current_tab < 0 || c.equipment_tab_index < 0)
      return {Button::None, false, "other_native_menu"};
    if (c.current_tab < c.equipment_tab_index && c.next_tab)
      return {Button::R, false, "next_native_tab"};
    if (c.current_tab > c.equipment_tab_index && c.previous_tab)
      return {Button::L, false, "previous_native_tab"};
    return {Button::None, false, "native_tab_navigation_unavailable"};
  }
  if (c.picker && c.category != type)
    return {Button::B, false, "leave_other_picker"};
  if (!c.overview && !c.picker)
    return {};
  if (c.holding_charm)
    return {Button::None, false, "native_relic_slot_choice"};
  if (!c.selected_button)
    return {Button::None, false, "selection_pending"};
  const auto matches = [&](const belmont_inventory::Slot &slot) {
    return slot.enabled && slot.item.type == type &&
           (c.overview || id.empty() || slot.item.id == id);
  };
  for (const auto &slot : c.slots)
    if (slot.selected && matches(slot)) {
      if (c.overview && c.confirm)
        return {Button::A, true, "open_native_picker"};
      // Opening a picker does not itself equip. A target ID is an explicit
      // owner choice; only that item may be confirmed by the next controller
      // action.
      if (c.picker && !id.empty() && c.confirm)
        return {Button::A, true, "confirm_selected_owned_item"};
      return {Button::None, true, "picker_open"};
    }
  // Native explicit navigation graph, including disabled slots and sparse
  // layouts. A target outside this observed graph fails closed.
  struct Node {
    std::uint64_t button;
    Button first;
  };
  std::deque<Node> queue{{c.selected_button, Button::None}};
  std::set<std::uint64_t> visited{c.selected_button};
  const Button buttons[] = {Button::Up, Button::Down, Button::Left,
                            Button::Right};
  while (!queue.empty()) {
    const auto node = queue.front();
    queue.pop_front();
    const belmont_inventory::Slot *current = nullptr;
    for (const auto &slot : c.slots)
      if (slot.button == node.button) {
        current = &slot;
        break;
      }
    if (!current)
      continue;
    if (node.first != Button::None && matches(*current))
      return {node.first, false, "navigate_native_selection"};
    for (unsigned i = 0; i < 4; ++i) {
      const auto next = current->navigation[i];
      if (next && visited.insert(next).second)
        queue.push_back(
            {next, node.first == Button::None ? buttons[i] : node.first});
    }
  }
  return {Button::None, false, "target_not_in_native_navigation"};
}
// Tick-driven controller used by module outputs and runtime button actions.
// Each nonzero return is a single pulse; caller supplies a monotonic
// millisecond clock, and emits a short native controller action only for that
// sample.
class Controller {
  enum class Intent { None, Category, Pick, Back, Page };
  Intent intent{Intent::None};
  std::int32_t category{}, index{};
  std::uint64_t entity{}, began{}, last{}, expected_button{};
  unsigned pulses{};
  std::uint64_t last_pulse{};
  bool pulse_seen{};
  bool focus_waiting{}, page_settling{};
  std::uint64_t page_settle_began{};
  std::uint64_t focus_began{};
  Button previous{Button::None};
  belmont_inventory::Control before;
  std::string target_id;
  bool waiting{}, initialized{}, bound_pick{};
  const char *reason_{"idle"};
  bool Begin(Intent next) {
    if (waiting && busy())
      return false;
    intent = next;
    entity = began = last = expected_button = 0;
    pulses = 0;
    previous = Button::None;
    before = {};
    target_id.clear();
    waiting = initialized = bound_pick = false;
    focus_waiting = page_settling = false;
    page_settle_began = 0;
    focus_began = 0;
    reason_ = "requested";
    return true;
  }
  bool Expected(const belmont_inventory::Control &c) const {
    if (previous == Button::Plus)
      return c.menu_open && (c.equipment_tab || (c.previous_tab && c.next_tab));
    if (previous == Button::L || previous == Button::R) {
      if (before.picker && before.category == 3)
        return c.picker && c.category == 3 &&
               c.native_page ==
                   before.native_page + (previous == Button::R ? 1 : -1);
      auto it = std::find(before.active_tabs.begin(), before.active_tabs.end(),
                          before.current_tab);
      if (it == before.active_tabs.end())
        return false;
      if (previous == Button::L) {
        if (it == before.active_tabs.begin())
          return false;
        --it;
      } else {
        ++it;
        if (it == before.active_tabs.end())
          return false;
      }
      return c.menu_open && c.current_tab == *it;
    }
    if (previous == Button::B)
      return before.holding_charm ? (c.picker && !c.holding_charm)
                                  : (!c.picker && c.overview);
    if (previous == Button::A)
      return before.overview && c.picker && c.category == category;
    return expected_button && c.selected_button == expected_button;
  }

public:
  void RequestCategory(std::int32_t type) {
    if (!Begin(Intent::Category))
      return;
    category = type;
    if (type < 1 || type > 3)
      Cancel("invalid_category");
  }
  void RequestPick(std::int32_t native_index) {
    if (!Begin(Intent::Pick))
      return;
    index = native_index;
  }
  void Back() { Begin(Intent::Back); }
  void RequestPage(std::int32_t direction) {
    if (!Begin(Intent::Page))
      return;
    index = direction;
    category = 3;
    if (direction != -1 && direction != 1)
      Cancel("invalid_page_direction");
  }
  void Cancel(const char *why = "cancelled") {
    intent = Intent::None;
    waiting = false;
    focus_waiting = false;
    reason_ = why;
  }
  bool busy() const { return intent != Intent::None; }
  const char *reason() const { return reason_; }
  Button Update(const belmont_inventory::Snapshot &s, bool gameplay_allowed,
                std::uint64_t now_ms) {
    if (!busy())
      return Button::None;
    if (!gameplay_allowed || !s.supported || !s.coherent || !s.ready ||
        !s.entity) {
      Cancel("world_transition");
      return Button::None;
    }
    if (!initialized) {
      entity = s.entity;
      began = now_ms;
      initialized = true;
    }
    if (s.entity != entity || now_ms < began || now_ms - began > 20000 ||
        pulses >= 40) {
      Cancel("request_expired");
      return Button::None;
    }
    if (pulse_seen && (now_ms < last_pulse || now_ms - last_pulse < 250))
      return Button::None;
    const auto &c = s.control;
    if (!c.ready) {
      if (waiting && now_ms >= last && now_ms - last < 1500)
        return Button::None;
      Cancel("control_unavailable");
      return Button::None;
    }
    if (focus_waiting) {
      if (now_ms < focus_began || now_ms - focus_began >= 1500) {
        Cancel("selection_timeout");
        return Button::None;
      }
      if (!c.menu_open || !c.equipment_tab) {
        Cancel("unexpected_native_focus_context");
        return Button::None;
      }
    }
    if (waiting) {
      if (now_ms < last || now_ms - last < 250)
        return Button::None;
      if (!Expected(c)) {
        if ((previous == Button::L || previous == Button::R) &&
            c.current_tab != before.current_tab) {
          Cancel("unexpected_native_tab");
          return Button::None;
        }
        if (previous == Button::A && c.picker && c.category != category) {
          Cancel("unexpected_native_picker");
          return Button::None;
        }
        // While a direction is pending, another nonzero focus is user input
        // unless it is the unchanged original selection. Never chase it.
        if (expected_button && c.selected_button &&
            c.selected_button != before.selected_button) {
          Cancel("unexpected_selection");
          return Button::None;
        }
        if (now_ms - last >= 1500)
          Cancel("native_action_timeout");
        return Button::None;
      }
      waiting = false;
      if (intent == Intent::Pick && before.picker && before.category == 3 &&
          (previous == Button::L || previous == Button::R)) {
        page_settling = true;
        page_settle_began = now_ms;
      }
      if (intent == Intent::Back || intent == Intent::Page) {
        Cancel(intent == Intent::Page ? "native_page_changed"
                                      : "back_complete");
        return Button::None;
      }
    }
    Step step;
    if (intent == Intent::Category) {
      if (c.picker && c.category == category) {
        Cancel("picker_open");
        return Button::None;
      }
      step = Navigate(c, category);
    } else if (intent == Intent::Page) {
      if (!c.picker || c.category != 3 || c.holding_charm ||
          (index < 0 ? !c.can_prev_page : !c.can_next_page)) {
        Cancel("native_page_unavailable");
        return Button::None;
      }
      step = {index < 0 ? Button::L : Button::R, false,
              "change_native_relic_page"};
    } else if (intent == Intent::Back) {
      if (!c.picker || !c.back) {
        Cancel("no_native_picker");
        return Button::None;
      }
      step = {Button::B, false, "back"};
    } else {
      if (!c.picker) {
        Cancel("native_picker_required");
        return Button::None;
      }
      if (c.holding_charm && index >= 0) {
        Cancel("native_relic_slot_choice");
        return Button::None;
      }
      if (!bound_pick) {
        category = c.category;
        const belmont_inventory::Slot *target = nullptr;
        for (const auto &slot : c.slots)
          if (slot.index == index &&
              (slot.enabled || (c.category == 3 && !c.holding_charm &&
               slot.native_page >= 0 && slot.native_page < c.native_pages &&
               slot.native_page != c.native_page && !slot.item.id.empty())))
            target = &slot;
        if (!target || target->item.type != category ||
            (index < 0 && (!c.holding_charm || category != 3))) {
          Cancel("native_item_unavailable");
          return Button::None;
        }
        target_id = target->item.id;
        bound_pick = true;
      }
      if (c.category != category) {
        Cancel("unexpected_picker");
        return Button::None;
      }
      const belmont_inventory::Slot *target = nullptr;
      for (const auto &slot : c.slots)
        if (slot.index == index && slot.item.id == target_id)
          target = &slot;
      if (!target) {
        Cancel("native_item_changed");
        return Button::None;
      }
      if (category == 3 && index >= 0 && target->native_page != c.native_page) {
        if (target->native_page < 0 || target->native_page >= c.native_pages ||
            c.holding_charm ||
            (target->native_page < c.native_page ? !c.can_prev_page : !c.can_next_page)) {
          Cancel("native_page_unavailable");
          return Button::None;
        }
        step = {target->native_page < c.native_page ? Button::L : Button::R,
                false, "route_owned_relic_page"};
      } else {
      if (!target->enabled) {
        if (page_settling && now_ms >= page_settle_began &&
            now_ms - page_settle_began < 1500) {
          reason_ = "native_page_button_pending";
          return Button::None;
        }
        Cancel(page_settling ? "native_page_button_timeout" : "native_button_disabled");
        return Button::None;
      }
      page_settling = false;
      if (c.selected_button == target->button) {
        if (!c.confirm) {
          Cancel("native_button_disabled");
          return Button::None;
        }
        // A real native submit. Stop the request after this pulse; success is
        // observed from the equipped reader, never inferred from this action.
        Cancel("native_submit_sent");
        last_pulse = now_ms;
        pulse_seen = true;
        return Button::A;
      }
      // Find the target in the exact native navigation graph. Reuse Navigate
      // with a unique temporary ID so a duplicate owned ID cannot select the
      // wrong item or relic slot.
      auto graph = c;
      graph.holding_charm = false;
      for (auto &slot : graph.slots)
        slot.item.id = slot.button == target->button ? "target" : "other";
      step = Navigate(graph, category, "target");
      }
    }
    if (step.button == Button::None) {
      if (std::strcmp(step.reason, "selection_pending") == 0) {
        if (!focus_waiting) {
          focus_waiting = true;
          focus_began = now_ms;
        }
        reason_ = "selection_pending";
        return Button::None;
      }
      Cancel(step.reason);
      return Button::None;
    }
    focus_waiting = false;
    before = c;
    previous = step.button;
    expected_button = 0;
    const int direction = step.button == Button::Up      ? 0
                          : step.button == Button::Down  ? 1
                          : step.button == Button::Left  ? 2
                          : step.button == Button::Right ? 3
                                                         : -1;
    if (direction >= 0) {
      for (const auto &slot : c.slots)
        if (slot.button == c.selected_button)
          expected_button = slot.navigation[unsigned(direction)];
      if (!expected_button) {
        Cancel("navigation_target_unavailable");
        return Button::None;
      }
    }
    last = now_ms;
    last_pulse = now_ms;
    pulse_seen = true;
    ++pulses;
    waiting = true;
    reason_ = step.reason;
    return step.button;
  }
};
} // namespace belmont_equipment_control
