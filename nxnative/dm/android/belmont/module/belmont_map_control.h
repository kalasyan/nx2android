// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_inventory.h"
#include <cmath>
#include <set>
namespace belmont_map_control {
using belmont::U64;
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
  R = 9,
  Y = 12
};
struct Item {
  U64 object{};
  std::string guid;
  bool enabled{};
};
struct Control {
  bool ready{}, coherent{}, menu_open{}, can_open{}, map_tab{},
      controls_enabled{}, palette{}, place_palette{}, change_palette{},
      top_layer{}, selection_empty{}, previous_tab{}, next_tab{},
      prevent_unplaceable{}, cursor_ready{};
  U64 entity{}, map_manager{}, level_manager{}, map{}, context{}, controller{},
      markers{}, cursor{}, palette_object{}, position_address{},
      selected_item{}, change_item{};
  std::int32_t current_tab{-1}, map_tab_index{-1}, navigation_index{-1},
      selected_marker_id{-1}, change_marker_id{-1}, selected_index{-1};
  std::string level_guid;
  std::vector<Item> items;
  std::array<float, 2> placement{}, cursor_position{};
  std::string reason{"unavailable"};
};
template <class Read> class Decoder {
  Read &read;
  struct Span {
    U64 at;
    std::vector<std::uint8_t> bytes;
  };
  std::vector<Span> journal;
  // Reuse only exact spans inside this decode. Final coherence validation
  // still reads every unique span from the guest; nothing survives Sample.
  belmont::ReadSpanIndex span_index;
  bool Bytes(U64 at, void *dst, std::size_t n) {
    if (!n || n > 4096 || at < 0x10000 ||
        at > std::numeric_limits<U64>::max() - n || journal.size() >= 16384)
      return false;
    if (auto found = span_index.find({at, n}); found != span_index.end()) {
      const auto &saved = journal[found->second].bytes;
      std::memcpy(dst, saved.data(), n);
      return true;
    }
    if (!read(at, dst, n))
      return false;
    const auto *p = static_cast<const std::uint8_t *>(dst);
    span_index.emplace(belmont::ReadSpanKey{at, n}, journal.size());
    journal.push_back({at, {p, p + n}});
    return true;
  }
  template <class T> bool Get(U64 at, T &value) {
    static_assert(std::is_integral_v<T>);
    std::array<std::uint8_t, sizeof(T)> b{};
    if (!Bytes(at, b.data(), b.size()))
      return false;
    U64 v{};
    for (unsigned i = 0; i < sizeof(T); ++i)
      v |= U64(b[i]) << (8 * i);
    std::memcpy(&value, &v, sizeof(T));
    return true;
  }
  bool Ptr(U64 at, U64 &value) {
    return Get(at, value) && belmont::Pointer(value);
  }
  bool Text(U64 at, const char *expected) {
    std::array<char, 128> b{};
    const auto n = std::strlen(expected) + 1;
    return n <= b.size() && Bytes(at, b.data(), n) &&
           !std::memcmp(b.data(), expected, n);
  }
  bool Klass(U64 k, const char *name, const char *ns = "") {
    U64 n{}, s{};
    return belmont::Pointer(k) && PtrText(k + 0x10, n) &&
           PtrText(k + 0x18, s) && Text(n, name) && Text(s, ns);
  }
  // Metadata C strings need not be pointer-aligned.
  bool PtrText(U64 at, U64 &p) {
    return Get(at, p) && p >= 0x10000 && p < 0x8000000000ULL;
  }
  bool ClassText(U64 at, std::string &text) {
    U64 p{};
    if (!PtrText(at, p))
      return false;
    text.clear();
    for (unsigned i = 0; i < 128; ++i) {
      std::uint8_t b{};
      if (!Bytes(p + i, &b, 1))
        return false;
      if (!b)
        return true;
      if (b < 0x20 || b > 0x7e)
        return false;
      text += char(b);
    }
    return false;
  }
  bool Object(U64 p, const char *name, const char *ns = "") {
    U64 k{};
    return belmont::Pointer(p) && Ptr(p, k) && Klass(k, name, ns);
  }
  bool Alive(U64 p) {
    U64 native{};
    std::uint8_t b{};
    return Ptr(p + 0x10, native) && Bytes(native, &b, 1);
  }
  bool String(U64 p, std::string &out) {
    std::int32_t n{};
    if (!Object(p, "String", "System") || !Get(p + 0x10, n) || n <= 0 ||
        n > 128)
      return false;
    std::vector<std::uint8_t> b(std::size_t(n) * 2);
    if (!Bytes(p + 0x14, b.data(), b.size()))
      return false;
    out.clear();
    // Item IDs are native identifier tokens. Reject controls, non-ASCII and
    // malformed UTF-16 rather than expose ambiguous asset lookup keys.
    for (int i = 0; i < n; ++i) {
      if (b[2 * i + 1] || b[2 * i] < 0x21 || b[2 * i] > 0x7e)
        return false;
      out += char(b[2 * i]);
    }
    return true;
  }
  bool Pin(U64 base, U64 rva, std::initializer_list<std::uint8_t> expected) {
    std::vector<std::uint8_t> b(expected.size());
    return Bytes(base + rva, b.data(), b.size()) &&
           std::equal(b.begin(), b.end(), expected.begin());
  }
  bool Flag(U64 base, U64 root, const char *name, const char *ns) {
    U64 k{}, f{};
    std::uint8_t flag{};
    return Ptr(base + root, k) && Klass(k, name, ns) && Ptr(k + 0xb8, f) &&
           Get(f, flag) && flag == 0;
  }
  bool Current(const belmont::Identity &id, const belmont::Snapshot &s) {
    // Explicit freshness gates start a new read phase; do not satisfy them
    // from the earlier copied phase. Their spans remain in final replay.
    span_index.clear();
    U64 k{}, f{}, m{}, e{}, stack{}, items{}, tuple{}, active{}, initial{},
        loading{}, length{};
    std::int32_t count{}, version{};
    std::uint8_t destroyed{}, enabled{};
    if (!Ptr(id.base + 0x6d42540, k) || !Klass(k, "GameStateManager") ||
        !Ptr(k + 0xb8, f) || !Ptr(f, m) || m != s.manager ||
        !Object(m, "GameStateManager") || !Ptr(m + 0xb0, e) || e != s.entity ||
        !Object(e, "Entity", "MotherBase.EntityFramework") || !Alive(e) ||
        !Get(e + 0x20, destroyed) || destroyed || !Get(e + 0x2c8, enabled) ||
        enabled != 1 || !Get(m + 0x20, initial) || !Get(m + 0x68, loading) ||
        !Ptr(m + 0xa0, stack) || !Ptr(stack + 0x10, items) ||
        !Get(stack + 0x18, count) || !Get(stack + 0x1c, version) ||
        !Get(items + 0x18, length) || count <= 0 || count > 128 ||
        length > 128 || U64(count) > length ||
        !Ptr(items + 0x20 + 8 * U64(count - 1), tuple) ||
        !Ptr(tuple + 0x18, active) || active != s.active ||
        !belmont::Pointer(loading) || active == loading ||
        !Flag(id.base, 0x6d3f780, "ApplicationFlowManager",
              "MotherBase.ApplicationFlowManager") ||
        !Flag(id.base, 0x6d46228, "RoomManagerSystem", "RoomManager"))
      return false;
    if (active == initial && s.initial_identity == 1)
      return true;
    std::array<std::uint8_t, 14> flags{};
    return s.paused == 1 && Object(active, "EditorParameteredGameState") &&
           Alive(active) && Bytes(active + 0x18, flags.data(), flags.size()) &&
           std::all_of(flags.begin(), flags.end(),
                       [](auto b) { return b <= 1; }) &&
           flags[0] == 1 && flags[1] == 1 && flags[2] == 0 && flags[11] == 1;
  }
  bool Manager(U64 base, U64 &ui, U64 &manager) {
    U64 k{}, fields{}, variable{}, vk{}, parent{}, sf{}, reg{};
    if (!Ptr(base + 0x6d47f10, k) || !Klass(k, "UIManager") ||
        !Ptr(k + 0xb8, fields) || !Ptr(fields, ui) ||
        !Object(ui, "UIManager") || !Alive(ui) || !Ptr(ui + 0x20, variable) ||
        !Object(variable, "GameMenuVariable") || !Ptr(variable, vk) ||
        !Ptr(vk + 0x58, parent) ||
        !Klass(parent, "ScriptableVariable`1", "MotherBase.ToolKit") ||
        !Ptr(parent + 0xb8, sf) || !Ptr(sf, reg))
      return false;
    // Prefer current values, then source default values as native getter does.
    for (U64 off : {U64(0x10), U64(0x18)}) {
      U64 dict{}, arr{}, length{};
      std::int32_t count{}, free{}, version{};
      if (!Ptr(reg + off, dict) || !Ptr(dict + 0x18, arr) ||
          !Get(dict + 0x20, count) || !Get(dict + 0x28, free) ||
          !Get(dict + 0x2c, version) || count < 0 || count > 128 || free < 0 ||
          free > count || !Get(arr + 0x18, length) || length > 256 ||
          U64(count) > length)
        return false;
      U64 found{};
      int live = 0;
      for (int i = 0; i < count; ++i) {
        const auto at = arr + 0x20 + 0x18 * U64(i);
        std::int32_t hash{}, next{};
        U64 key{}, value{}, candidate{}, back{};
        if (!Get(at, hash))
          return false;
        if (hash == -1)
          continue;
        if (hash < 0 || !Get(at + 4, next) || next < -1 || next >= count ||
            !Ptr(at + 8, key) || !Ptr(at + 0x10, value) ||
            !Get(value + 0x10, candidate))
          return false;
        ++live;
        if (!candidate)
          continue;
        if (!Object(candidate, "GameMenuManager") || !Alive(candidate) ||
            !Get(candidate + 0x20, back))
          return false;
        if (back == variable) {
          if (found && found != candidate)
            return false;
          found = candidate;
        }
      }
      if (live != count - free)
        return false;
      if (found) {
        manager = found;
        return true;
      }
    }
    return false;
  }
  bool Guid(U64 p, std::string &out) {
    if (!String(p, out) || out.size() != 32)
      return false;
    for (char c : out)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        return false;
    return true;
  }

  bool Bit(U64 at, bool &out) {
    std::uint8_t b{};
    if (!Get(at, b) || b > 1)
      return false;
    out = b;
    return true;
  }
  bool Float(U64 at, float &out) {
    std::uint32_t b{};
    if (!Get(at, b))
      return false;
    std::memcpy(&out, &b, 4);
    return std::isfinite(out);
  }
  bool List(U64 p, std::vector<U64> &out, unsigned cap) {
    U64 a{}, n{};
    std::int32_t count{}, v{};
    if (!Ptr(p + 0x10, a) || !Get(p + 0x18, count) || !Get(p + 0x1c, v) ||
        !Get(a + 0x18, n) || count < 0 || unsigned(count) > cap || n > cap ||
        U64(count) > n)
      return false;
    for (int x = 0; x < count; ++x) {
      U64 o{};
      if (!Ptr(a + 0x20 + 8 * U64(x), o))
        return false;
      out.push_back(o);
    }
    return true;
  }
  bool MapManager(U64 base, U64 &result) {
    // RuntimeServiceLocator.Get<UIMapManager>: canonical TypeInfo 6d463c0 (code
    // relocation slot 6bd61b0), static _instance, inherited variables list;
    // Var<T> runtime value +28.
    U64 k{}, f{}, locator{}, list{};
    std::vector<U64> vars;
    if (!Ptr(base + 0x6d463c0, k) || !Klass(k, "RuntimeServiceLocator") ||
        !Ptr(k + 0xb8, f) || !Ptr(f, locator) ||
        !Object(locator, "RuntimeServiceLocator") || !Alive(locator) ||
        !Ptr(locator + 0x18, list) || !List(list, vars, 128))
      return false;
    for (auto var : vars) {
      U64 value{}, vk{};
      std::string name;
      if (!Ptr(var, vk) || !ClassText(vk + 0x10, name))
        return false;
      if (name != "VarUIMapManager")
        continue;
      if (!Object(var, "VarUIMapManager") || !Get(var + 0x28, value))
        return false;
      if (!value)
        continue;
      if (!Ptr(value, vk) || !ClassText(vk + 0x10, name))
        return false;
      if (name != "UIMapManager")
        continue;
      if (result || !Object(value, "UIMapManager") || !Alive(value))
        return false;
      result = value;
    }
    return result != 0;
  }
  bool MarkerID(U64 marker, std::int32_t &id) {
    U64 data{};
    return Object(marker, "LevelMapItemMarker") && Alive(marker) &&
           Ptr(marker + 0x80, data) && Object(data, "MapMarkerData") &&
           Get(data + 0x10, id) && id >= 0;
  }
  bool BaseTabLayer(U64 top, U64 view, Control &out) {
    U64 aspects{}, len{};
    if (!top || !Object(top, "MenuStackLayer") || !Ptr(top + 0x20, aspects) ||
        !Get(aspects + 0x18, len) || len > 32)
      return false;
    for (U64 i = 0; i < len; ++i) {
      U64 aspect{}, k{};
      bool enabled{};
      if (!Ptr(aspects + 0x20 + 8 * i, aspect) || !Ptr(aspect, k))
        return false;
      if (!Klass(k, "MenuStackLayerInputs"))
        continue;
      U64 list{};
      std::vector<U64> actions;
      if (!Bit(aspect + 0x28, enabled) || !enabled ||
          !Ptr(aspect + 0x20, list) || !List(list, actions, 32))
        return false;
      for (U64 action : actions) {
        U64 event{}, group{}, calls{};
        std::vector<U64> listeners;
        if (!Object(action, "InputActionsToUnityEvent") ||
            !Bit(action + 0x88, enabled))
          return false;
        if (!enabled)
          continue;
        if (!Ptr(action + 0x80, event) ||
            !Object(event, "UnityEvent", "UnityEngine.Events") ||
            !Ptr(event + 0x18, group) ||
            !Object(group, "PersistentCallGroup", "UnityEngine.Events") ||
            !Ptr(group + 0x10, calls) || !List(calls, listeners, 16))
          return false;
        for (U64 call : listeners) {
          U64 target{}, name{};
          std::int32_t state{};
          std::string method;
          if (!Object(call, "PersistentCall", "UnityEngine.Events") ||
              !Get(call + 0x10, target) || !Get(call + 0x38, state) ||
              state < 0 || state > 2)
            return false;
          if (target != view || !state)
            continue;
          if (!Ptr(call + 0x20, name) || !String(name, method))
            return false;
          if (method == "PreviousTab")
            out.previous_tab = true;
          if (method == "NextTab")
            out.next_tab = true;
        }
      }
    }
    return out.previous_tab && out.next_tab;
  }
  bool Decode(U64 base, Control &c) {
    U64 ui{}, manager{}, tab{}, view{}, stack{}, layers{}, array{}, length{},
        top{};
    std::int32_t depth{}, version{};
    bool active{};
    c.reason = "native_menu_unavailable";
    if (!Manager(base, ui, manager) || !Ptr(manager + 0x38, tab) ||
        !Object(tab, "BasicTab") || !Get(tab + 0x74, c.map_tab_index) ||
        !Bit(tab + 0x79, active) || !Ptr(manager + 0x30, view) ||
        !Object(view, "BasicTabView") || !Bit(view + 0x51, c.menu_open) ||
        !Get(view + 0x38, c.current_tab) || !Ptr(ui + 0x30, stack) ||
        !Object(stack, "MenuStack") || !Ptr(stack + 0x20, layers) ||
        !Ptr(layers + 0x10, array) || !Get(layers + 0x18, depth) ||
        !Get(layers + 0x1c, version) || !Get(array + 0x18, length) ||
        depth < 0 || depth > 32 || length > 32 || U64(depth) > length)
      return false;
    c.can_open = !c.menu_open && depth == 0;
    if (c.can_open) {
      c.ready = true;
      c.reason = "map_closed";
      return true;
    }
    if (!c.menu_open || c.current_tab < 0 || c.current_tab > 7 ||
        c.map_tab_index < 0 || c.map_tab_index > 7)
      return false;
    c.map_tab = active && c.current_tab == c.map_tab_index;
    if (!c.map_tab) {
      if (!depth || !Ptr(array + 0x20 + 8 * U64(depth - 1), top) ||
          !BaseTabLayer(top, view, c))
        return false;
      c.reason = "other_native_tab";
      c.ready = true;
      return true;
    }
    if (!depth || !Ptr(array + 0x20 + 8 * U64(depth - 1), top))
      return false;
    c.reason = "map_context_unavailable";
    if (!MapManager(base, c.map_manager) ||
        !Ptr(c.map_manager + 0x30, c.level_manager) ||
        !Object(c.level_manager, "UIMapLevelManager") ||
        !Alive(c.level_manager))
      return false;
    for (U64 off : {U64(0x140), U64(0x148), U64(0x160)}) {
      U64 pending{};
      if (!Get(c.level_manager + off, pending) || pending)
        return false;
    }
    U64 level{}, back{}, g{}, nav{}, settings{}, navs{}, zones{}, n{};
    if (!Ptr(c.level_manager + 0x130, c.map) || !Object(c.map, "LevelMap") ||
        !Alive(c.map) || !Ptr(c.map + 0x20, c.context) ||
        !Object(c.context, "LevelMapContext") ||
        !Ptr(c.map + 0x28, c.controller) ||
        !Object(c.controller, "LevelMapController") || !Alive(c.controller) ||
        !Ptr(c.controller + 0x20, back) || back != c.context ||
        !Ptr(c.controller + 0x30, c.markers) ||
        !Object(c.markers, "LevelMapMarkersController") || !Alive(c.markers) ||
        !Ptr(c.markers + 0x20, back) || back != c.context ||
        !Ptr(c.controller + 0x28, c.cursor) ||
        !Object(c.cursor, "LevelMapCursor") || !Alive(c.cursor) ||
        !Ptr(c.cursor + 0x20, back) || back != c.context ||
        !Bit(c.controller + 0xc8, c.controls_enabled) ||
        !Ptr(c.context + 0x20, level) || !Object(level, "LevelID") ||
        !Ptr(level + 0x18, g) || !Guid(g, c.level_guid) ||
        !Ptr(c.level_manager + 0x168, back) || back != level ||
        !Ptr(c.context + 0xa0, nav) || !Object(nav, "LevelMapNavigationZone") ||
        !Alive(nav) || !Ptr(c.context + 0x50, settings) ||
        !Ptr(settings + 0x60, navs) ||
        !Object(navs, "LevelMapNavigationZonesManager") ||
        !Ptr(navs + 0x20, zones) || !Get(zones + 0x18, n) || n == 0 || n > 128)
      return false;
    for (U64 x = 0; x < n; ++x) {
      U64 z{};
      if (!Get(zones + 0x20 + x * 8, z))
        return false;
      if (z == nav) {
        if (c.navigation_index != -1)
          return false;
        c.navigation_index = int(x);
      }
    }
    if (c.navigation_index < 0 || !Get(c.context + 0x90, c.selected_item))
      return false;
    c.cursor_ready = Float(c.context + 0xb0, c.cursor_position[0]) &&
                     Float(c.context + 0xb4, c.cursor_position[1]);
    c.selection_empty = !c.selected_item;
    if (c.selected_item) {
      std::int32_t pinid{};
      if (MarkerID(c.selected_item, pinid))
        c.selected_marker_id = pinid;
    }
    if (!Ptr(c.level_manager + 0x50, c.palette_object) ||
        !Object(c.palette_object, "MapMarkerSelectionController") ||
        !Alive(c.palette_object) || !Bit(c.palette_object + 0x98, c.palette))
      return false;
    if (!c.palette) {
      if (!BaseTabLayer(top, view, c))
        return false;
      c.ready = true;
      c.reason = "map_open";
      return true;
    }
    U64 layer{}, pm{}, lm{}, callback{}, method{}, target{}, p{}, list{};
    bool selectable{};
    c.reason = "palette_unavailable";
    if (!Ptr(c.palette_object + 0x28, layer) || layer != top ||
        !Object(layer, "MenuStackLayer") || !Ptr(c.palette_object + 0xd8, lm) ||
        lm != c.level_manager || !Ptr(c.palette_object + 0xe0, pm) ||
        pm != c.map || !Bit(c.palette_object + 0x99, selectable) ||
        !Ptr(c.palette_object + 0xa8, callback) ||
        !Ptr(callback + 0x40, target) || target != c.markers ||
        !Get(callback + 0x18, method) ||
        !Get(c.markers + 0x50, c.change_item) ||
        !Get(c.palette_object + 0xb8, p))
      return false;
    c.top_layer = true;
    if (!Bit(c.palette_object + 0x4c, c.prevent_unplaceable))
      return false;
    c.place_palette = method == base + 0x1df01f0 && p == 0;
    c.change_palette = method == base + 0x1df0770 && c.change_item &&
                       MarkerID(c.change_item, c.change_marker_id);
    if (!c.place_palette && !c.change_palette)
      return false;
    c.position_address = c.markers + 0x48;
    if (!Float(c.position_address, c.placement[0]) ||
        !Float(c.position_address + 4, c.placement[1]) ||
        !Get(c.palette_object + 0xd0, c.selected_index) ||
        !Ptr(c.palette_object + 0xc8, list))
      return false;
    std::vector<U64> items;
    if (!List(list, items, 32) || items.empty() || c.selected_index < 0 ||
        std::size_t(c.selected_index) >= items.size())
      return false;
    for (auto obj : items) {
      Item item;
      U64 type{};
      if (!Object(obj, "MapMarkerSelectionItem") || !Alive(obj) ||
          !Ptr(obj + 0x38, type) || !Object(type, "MapMarkerTypeID") ||
          !Alive(type) || !Ptr(type + 0x18, g) || !Guid(g, item.guid) ||
          !Bit(obj + 0x50, item.enabled))
        return false;
      item.object = obj;
      c.items.push_back(item);
    }
    c.ready = selectable;
    c.reason = selectable ? "palette_open" : "no_placeable_markers";
    return true;
  }

public:
  explicit Decoder(Read &r) : read(r) {}
  Control Sample(const belmont::Identity &id, const belmont::Snapshot &s) {
    journal.clear();
    span_index.clear();
    Control c;
    c.entity = s.entity;
    if (!s.supported || !s.coherent || !s.entity_ready ||
        s.application_loading != 0 || s.room_loading != 0) {
      c.reason = "game_unavailable";
      return c;
    }
    if (!Current(id, s)) {
      c.reason = "game_changed";
      return c;
    }
    // Pinned original placement/readback and edit interaction opcodes.
    if (!Pin(id.base, 0x1deff28, {0x69, 0x22, 0x09, 0x2d}) ||
        !Pin(id.base, 0x1df0310, {0x88, 0x26, 0x49, 0x2d})) {
      c.reason = "unsupported_control_code";
      return c;
    }
    if (!Decode(id.base, c))
      return c;
    if (!Current(id, s)) {
      c = Control{};
      c.reason = "game_changed";
      return c;
    }
    std::array<std::uint8_t, 4096> b{};
    for (const auto &span : journal)
      if (!read(span.at, b.data(), span.bytes.size()) ||
          !std::equal(span.bytes.begin(), span.bytes.end(), b.begin())) {
        c = Control{};
        c.reason = "raced_control";
        return c;
      }
    c.coherent = true;
    return c;
  }
};

enum class Operation { Add, Change, Remove };
struct Request {
  Operation operation{Operation::Add};
  std::string level_guid, type_guid;
  std::int32_t navigation_index{-1}, native_id{-1};
  float x{}, y{};
  // Caller joins current native source level/navigation IDs and exact displayed
  // map bounds. Reader never infers map identity from a companion area number.
  bool source_proven{};
  std::array<float, 4> bounds{};
};
struct Step {
  Button button{Button::None};
  bool write_position{}, move_cursor{}, submitted{}, done{};
  U64 address{}, entity{}, markers{}, palette{};
  std::array<float, 2> expected_position{}, position{};
  float stick_x{}, stick_y{};
  const char *reason{"idle"};
};
// Observed state machine, one input per fresh sample; never timed button
// chains. The module owns the actual 8-byte compare-before-write transaction.
class Controller {
  Request request;
  bool busy{}, write_sent{}, written{}, submitted{}, clearing_selection{};
  float clear_stick_x{}, clear_stick_y{};
  U64 entity{}, markers{}, palette{}, began{}, last{}, wait_since{},
      unready_since{};
  unsigned pulses{};
  std::int32_t old_selection{-2}, old_tab{-2};
  enum class Wait { None, Menu, Tab, Palette, Selection, Mutation };
  Wait waiting{Wait::None};
  Step Fail(const char *why) {
    busy = false;
    return {Button::None, false, false, false, true, 0, 0, 0, 0,
            {},           {},    0,     0,     why};
  }
  Step Pulse(Button b, Wait w, U64 now, const char *why) {
    last = now;
    ++pulses;
    waiting = w;
    wait_since = now;
    Step s;
    s.button = b;
    s.reason = why;
    return s;
  }

public:
  bool Busy() const { return busy; }
  bool Start(const Request &r, U64 now) {
    if (busy || !r.source_proven || r.level_guid.size() != 32 ||
        r.navigation_index < 0 || r.navigation_index > 127 ||
        !std::isfinite(r.x) || !std::isfinite(r.y) ||
        r.bounds[2] <= r.bounds[0] || r.bounds[3] <= r.bounds[1] ||
        r.x < r.bounds[0] || r.x > r.bounds[2] || r.y < r.bounds[1] ||
        r.y > r.bounds[3] ||
        (r.operation != Operation::Remove && r.type_guid.size() != 32) ||
        (r.operation != Operation::Add && r.native_id < 0))
      return false;
    for (auto f : r.bounds)
      if (!std::isfinite(f))
        return false;
    request = r;
    busy = true;
    write_sent = written = submitted = clearing_selection = false;
    entity = markers = palette = 0;
    began = now;
    last = unready_since = 0;
    pulses = 0;
    waiting = Wait::None;
    old_selection = -2;
    return true;
  }
  void Cancel() { busy = false; }
  bool CanWrite(const Control &c, const Step &s) const {
    return busy && write_sent && !written && s.write_position && c.ready &&
           c.coherent && c.palette && c.place_palette && c.top_layer &&
           c.entity == entity && s.entity == entity && c.markers == markers &&
           s.markers == markers && c.palette_object == palette &&
           s.palette == palette && c.level_guid == request.level_guid &&
           c.navigation_index == request.navigation_index &&
           c.position_address == s.address && s.address == c.markers + 0x48 &&
           c.placement == s.expected_position &&
           s.position == std::array<float, 2>{request.x, request.y};
  }
  void PositionWriteResult(bool success) {
    if (busy && write_sent) {
      written = success;
      if (!success)
        busy = false;
    }
  }
  Step Update(const Control &c, U64 now, bool mutation_observed = false) {
    if (!busy)
      return {};
    if (now < began || now - began > 20000 || pulses >= 64)
      return Fail("request_timeout");
    if (entity && c.entity && c.entity != entity)
      return Fail("entity_changed");
    if (!c.ready || !c.coherent || !c.entity) {
      if (c.reason == "game_unavailable" || c.reason == "game_changed" ||
          c.reason == "unsupported_control_code")
        return Fail("native_game_unavailable");
      if (!unready_since)
        unready_since = now;
      if (now - unready_since > 1500)
        return Fail("native_control_unavailable");
      Step step;
      step.reason = "native_control_settling";
      return step;
    }
    unready_since = 0;
    if (!entity)
      entity = c.entity;
    if (c.entity != entity)
      return Fail("entity_changed");
    if (submitted) {
      if (mutation_observed) {
        auto s = Fail("native_mutation_observed");
        s.submitted = true;
        return s;
      }
      if (now - wait_since > 2000)
        return Fail("native_mutation_not_observed");
      return {};
    }
    if (last && now - last < 250)
      return {};
    if (waiting != Wait::None) {
      bool ack =
          (waiting == Wait::Menu && c.menu_open) ||
          (waiting == Wait::Tab && c.current_tab != old_tab) ||
          (waiting == Wait::Palette && c.palette) ||
          (waiting == Wait::Selection && c.selected_index != old_selection);
      if (ack)
        waiting = Wait::None;
      else if (now - wait_since > 1500)
        return Fail("native_input_not_acknowledged");
      else
        return {};
    }
    if (!c.menu_open) {
      if (!c.can_open)
        return Fail("native_menu_blocked");
      return Pulse(Button::Plus, Wait::Menu, now, "open_native_menu");
    }
    if (!c.map_tab) {
      old_tab = c.current_tab;
      if (c.current_tab > c.map_tab_index && c.previous_tab)
        return Pulse(Button::L, Wait::Tab, now, "previous_native_tab");
      if (c.current_tab < c.map_tab_index && c.next_tab)
        return Pulse(Button::R, Wait::Tab, now, "next_native_tab");
      return Fail("native_map_tab_unavailable");
    }
    if (c.level_guid != request.level_guid ||
        c.navigation_index != request.navigation_index)
      return Fail("native_map_context_changed");
    if (markers && markers != c.markers)
      return Fail("marker_controller_changed");
    markers = c.markers;
    if (!c.palette) {
      if (write_sent)
        return Fail("palette_closed_unexpectedly");
      if (!c.controls_enabled)
        return Fail("map_controls_disabled");
      if (request.operation == Operation::Add) {
        if (!c.selection_empty) {
          if (!c.cursor_ready)
            return Fail("cursor_position_unavailable");
          if (!clearing_selection) {
            float dx = request.x - c.cursor_position[0];
            float dy = request.y - c.cursor_position[1];
            float distance = std::hypot(dx, dy);
            // The marker action deletes when a pin is selected. Move through
            // native cursor controls until a fresh snapshot proves no
            // selection; placement coordinates are installed only after its
            // palette opens.
            if (distance < 0.02f) {
              dx = 1.0f;
              dy = 0.0f;
              distance = 1.0f;
            }
            const float magnitude = distance < 2 ? 0.35f : 1.0f;
            clear_stick_x = magnitude * dx / distance;
            clear_stick_y = magnitude * dy / distance;
            clearing_selection = true;
          }
          // Keep the escape direction until selection is gone: reversing
          // toward the requested point each tick could oscillate inside the
          // current pin's native snapping radius.
          Step step;
          step.move_cursor = true;
          step.stick_x = clear_stick_x;
          step.stick_y = clear_stick_y;
          step.reason = "clear_native_placement_selection";
          last = now;
          ++pulses;
          return step;
        }
        clearing_selection = false;
        return Pulse(Button::Y, Wait::Palette, now,
                     "open_native_place_palette");
      }
      if (c.selected_marker_id != request.native_id) {
        if (!c.cursor_ready)
          return Fail("cursor_position_unavailable");
        const float dx = request.x - c.cursor_position[0],
                    dy = request.y - c.cursor_position[1];
        const float distance = std::hypot(dx, dy);
        if (distance < 0.02f)
          return Fail("target_pin_not_selectable");
        // Bounded closed loop: a short low-amplitude native cursor pulse, then
        // re-read its displayed position and exact selected native ID. Never
        // A/Y until that ID is observed; native confiner/snapping owns
        // movement.
        Step step;
        step.move_cursor = true;
        const float magnitude = distance < 2 ? 0.35f : 1.0f;
        step.stick_x = magnitude * dx / distance;
        step.stick_y = magnitude * dy / distance;
        step.reason = "seek_exact_native_pin";
        last = now;
        ++pulses;
        return step;
      }
      if (request.operation == Operation::Remove) {
        submitted = true;
        wait_since = now;
        auto s =
            Pulse(Button::Y, Wait::Mutation, now, "remove_selected_native_pin");
        s.submitted = true;
        return s;
      }
      return Pulse(Button::A, Wait::Palette, now, "open_native_change_palette");
    }
    if (!c.top_layer)
      return Fail("palette_covered");
    if (palette && palette != c.palette_object)
      return Fail("palette_changed");
    palette = c.palette_object;
    if (request.operation == Operation::Remove)
      return Fail("unexpected_palette");
    if (request.operation == Operation::Add) {
      if (!c.place_palette)
        return Fail("unexpected_change_palette");
      if (!write_sent) {
        if (c.position_address != c.markers + 0x48)
          return Fail("invalid_position_field");
        Step s;
        s.write_position = true;
        s.address = c.position_address;
        s.entity = entity;
        s.markers = markers;
        s.palette = palette;
        s.expected_position = c.placement;
        s.position = {request.x, request.y};
        s.reason = "guarded_native_placement_position";
        write_sent = true;
        return s;
      }
      if (!written)
        return Fail("position_write_unacknowledged");
      if (c.placement[0] != request.x || c.placement[1] != request.y)
        return Fail("position_readback_mismatch");
    } else if (!c.change_palette || c.change_marker_id != request.native_id)
      return Fail("wrong_native_change_target");
    int target = -1;
    for (std::size_t i = 0; i < c.items.size(); ++i)
      if (c.items[i].guid == request.type_guid) {
        if (target != -1)
          return Fail("ambiguous_marker_type");
        target = int(i);
      }
    if (target < 0 || !c.items[target].enabled)
      return Fail("native_marker_type_unavailable");
    if (c.selected_index < 0 || std::size_t(c.selected_index) >= c.items.size())
      return Fail("palette_selection_unavailable");
    if (c.selected_index == target) {
      submitted = true;
      wait_since = now;
      auto s =
          Pulse(Button::A, Wait::Mutation, now, "confirm_native_marker_type");
      s.submitted = true;
      return s;
    }
    const int n = int(c.items.size()),
              right = (target - c.selected_index + n) % n,
              left = (c.selected_index - target + n) % n;
    old_selection = c.selected_index;
    return Pulse(right <= left ? Button::Right : Button::Left, Wait::Selection,
                 now, "select_native_marker_type");
  }
};
} // namespace belmont_map_control
