// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_reader.h"
#include <map>

// Bounded native owned inventory and Equipment menu state. Read-only;
// snapshots clear on races, transitions, unsupported builds and malformed data.
namespace belmont_inventory {
using belmont::U64;
struct Item {
  std::int32_t type{};
  std::string id;
  U64 object{};
  std::int32_t index{};
  bool equipped{};
};
struct Slot {
  Item item;
  U64 object{}, button{};
  std::int32_t index{}, native_page{-1};
  bool selected{}, enabled{};
  std::array<U64, 4> navigation{};
};
struct Control {
  bool ready{}, menu_open{}, can_open{}, equipment_tab{}, overview{}, picker{},
      holding_charm{};
  std::int32_t category{}, selected_index{-1}, selected_type{}, charm_slot{-1};
  U64 selected_button{}, selected_slot{};
  std::string selected_id;
  Item held_item;
  std::vector<Slot> slots;
  U64 picker_object{}, slots_list{}, slots_array{}, slots_capacity{};
  std::int32_t slots_count{}, slots_version{};
  // Native explicit navigation targets. Directions are valid only when these
  // are present and snapshot is current; they are not an inferred grid order.
  bool left{}, right{}, up{}, down{}, confirm{}, back{}, previous_tab{},
      next_tab{};
  std::int32_t current_tab{-1}, equipment_tab_index{-1}, tab_count{};
  std::vector<std::int32_t> active_tabs;
  std::int32_t native_page{}, native_pages{1};
  bool can_prev_page{}, can_next_page{};
  std::string reason{"unavailable"};
};
struct Snapshot {
  bool supported{}, coherent{}, ready{};
  U64 entity{}, inventory{}, owned_list{}, owned_array{}, owned_capacity{};
  std::int32_t owned_count{}, owned_version{};
  U64 inventory_component{}, component_dictionary{}, component_entries{},
      component_entry{}, component_capacity{};
  std::int32_t component_count{}, component_free{}, component_version{},
      component_hash{}, component_key{};
  std::vector<Item> items;
  Control control;
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
    if (auto found=span_index.find({at,n});found!=span_index.end()) {
      const auto& saved=journal[found->second].bytes;
      std::memcpy(dst,saved.data(),n);
      return true;
    }
    if (!read(at,dst,n)) return false;
    const auto *p = static_cast<const std::uint8_t *>(dst);
    span_index.emplace(belmont::ReadSpanKey{at,n},journal.size());
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
  bool CharmPins(U64 b) {
    return Pin(b, 0x3f86180,
               {0x95, 0x4e, 0x40, 0xf9, 0x08, 0xe0, 0x40, 0xb9, 0x95, 0x4e,
                0x09, 0xa9}) &&
           Pin(b, 0x1ef46f4, {0x75, 0xda, 0x00, 0xf9}) &&
           Pin(b, 0x1ef47dc,
               {0x60, 0x62, 0x40, 0xf9, 0xe0, 0x03, 0x00, 0xb4, 0xc8, 0x67,
                0x02, 0xd0, 0x61, 0x9a, 0x41, 0xb9, 0x08, 0xf1, 0x42, 0xf9}) &&
           Pin(b, 0x1ef4800,
               {0x00, 0x1c, 0x40, 0xf9, 0xc0, 0x02, 0x00, 0xb4, 0xe1, 0x03,
                0x1f, 0xaa, 0xd5, 0x9e, 0x00, 0x94}) &&
           Pin(b, 0x1ef3a5c,
               {0x62, 0xa2, 0x41, 0xb9, 0x01, 0x2d, 0x40, 0xf9, 0x87, 0x83,
                0x01, 0x94}) &&
           Pin(b, 0x1ef4ec4,
               {0x60, 0x92, 0x40, 0xf9, 0x7f, 0xda, 0x00, 0xf9}) &&
           Pin(b, 0x1ef4fd0,
               {0x68, 0x62, 0x40, 0xf9, 0x28, 0x01, 0x00, 0xb4, 0x08,
                0x19, 0x40, 0xb9, 0x08, 0x05, 0x00, 0x51, 0x69, 0x9a,
                0x41, 0xb9, 0xf4, 0x4f, 0x41, 0xa9, 0x3f, 0x01, 0x08,
                0x6b, 0xe0, 0xa7, 0x9f, 0x1a, 0xfd, 0x7b, 0xc2, 0xa8}) &&
           Pin(b, 0x1ef52a0,
               {0x08, 0x98, 0x41, 0xb9, 0x1f, 0x01, 0x00, 0x71, 0xe0, 0xd7,
                0x9f, 0x1a, 0xc0, 0x03, 0x5f, 0xd6});
  }
  bool NativePins(U64 b) {
    return Pin(b, 0x1f55ca4, {0x73, 0x12, 0x40, 0xf9}) &&
           Pin(b, 0x1f38488, {0x94, 0xc2, 0x42, 0xf9}) &&
           Pin(b, 0x1f384a4,
               {0x88, 0x02, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x08, 0x5d,
                0x40, 0xf9, 0x00, 0x01, 0x40, 0xf9, 0xfd, 0x7b, 0xc2, 0xa8}) &&
           Pin(b, 0x1f354b4, {0x80, 0x12, 0x40, 0xf9}) &&
           Pin(b, 0x3293994,
               {0x08, 0x5c, 0x40, 0xf9, 0x15, 0x01, 0x40, 0xf9}) &&
           Pin(b, 0x3290ab8, {0x00, 0x08, 0x40, 0xf9}) &&
           Pin(b, 0x3290b20, {0x08, 0x09, 0x40, 0xf9}) &&
           Pin(b, 0x3f85864,
               {0x28, 0x00, 0x80, 0x52, 0xe0, 0x03, 0x13, 0xaa, 0x68, 0x92,
                0x02, 0x39}) &&
           Pin(b, 0x1ef5c0c, {0x74, 0xa2, 0x00, 0xb9}) &&
           Pin(b, 0x51015dc,
               {0x28, 0x00, 0x80, 0x52, 0xf3, 0x03, 0x00, 0xaa, 0x08, 0xc8,
                0x03, 0x39}) &&
           Pin(b, 0x1ef7b94, {0xb4, 0x2e, 0x40, 0xf9});
  }
  bool Pins(U64 b) {
    // Original native accessors prove owner, the four equipped fields, ID,
    // type enum, and InventoryComponent.inventory. No methods are called.
    return CharmPins(b) && NativePins(b) &&
           Pin(b, 0x1f52c90,
               {0x00, 0x28, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(b, 0x1f56f50,
               {0x00, 0x14, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(b, 0x1f56f60,
               {0x00, 0x18, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(b, 0x1f56f70,
               {0x00, 0x1c, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(b, 0x1f56f80,
               {0x00, 0x20, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(b, 0x1cabc40,
               {0x00, 0x10, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(b, 0x1cab950,
               {0x00, 0x28, 0x40, 0xb9, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(b, 0x1c23194,
               {0x88, 0x16, 0x40, 0xf9, 0x88, 0x0e, 0x00, 0xb4, 0x09, 0x15,
                0x40, 0xf9}) &&
           Pin(b, 0x1f570a8,
               {0x75, 0x22, 0x40, 0xf9, 0xf5, 0x04, 0x00, 0xb4, 0xa8, 0x0e,
                0x40, 0xf9});
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
  bool Components(U64 e, U64 &inventory, Snapshot *out = nullptr) {
    U64 dict{}, entries{}, capacity{}, component{};
    std::int32_t count{}, free{}, version{}, found{};
    if (!Ptr(e + 0x30, dict) || !Get(dict + 0x20, count) ||
        !Get(dict + 0x28, free) || !Get(dict + 0x2c, version) || count < 0 ||
        count > 256 || free < 0 || free > count || !Ptr(dict + 0x18, entries) ||
        !Get(entries + 0x18, capacity) || capacity > 4096 ||
        U64(count) > capacity)
      return false;
    std::vector<std::int32_t> keys;
    for (int i = 0; i < count; ++i) {
      const auto at = entries + 0x20 + 0x18 * U64(i);
      std::int32_t hash{}, next{}, key{};
      U64 p{}, k{};
      if (!Get(at, hash))
        return false;
      if (hash == -1)
        continue;
      if (hash < 0 || !Get(at + 4, next) || next < -1 || next >= count ||
          !Get(at + 8, key) ||
          std::find(keys.begin(), keys.end(), key) != keys.end() ||
          !Ptr(at + 0x10, p) || !Ptr(p, k))
        return false;
      std::string name, ns;
      if (!ClassText(k + 0x10, name) || name.empty() ||
          !ClassText(k + 0x18, ns))
        return false;
      keys.push_back(key);
      ++found;
      if (name == "InventoryComponent" && ns.empty()) {
        if (component)
          return false;
        component = p;
        if (out) {
          out->inventory_component = p;
          out->component_dictionary = dict;
          out->component_entries = entries;
          out->component_entry = at;
          out->component_capacity = capacity;
          out->component_count = count;
          out->component_free = free;
          out->component_version = version;
          out->component_hash = hash;
          out->component_key = key;
        }
      }
    }
    U64 owner{};
    return found == count - free && component && Alive(component) &&
           Ptr(component + 0x28, inventory) && Object(inventory, "Inventory") &&
           Get(inventory + 0x50, owner) && owner == e;
  }
  bool CachedComponents(U64 e, const Snapshot &previous, U64 &inventory,
                        Snapshot &out) {
    U64 dict{}, entries{}, capacity{}, p{}, owner{};
    std::int32_t count{}, free{}, version{}, hash{}, next{}, key{};
    if (!Ptr(e + 0x30, dict) || dict != previous.component_dictionary ||
        !Ptr(dict + 0x18, entries) || entries != previous.component_entries ||
        !Get(dict + 0x20, count) || count != previous.component_count ||
        !Get(dict + 0x28, free) || free != previous.component_free ||
        !Get(dict + 0x2c, version) || version != previous.component_version ||
        !Get(entries + 0x18, capacity) ||
        capacity != previous.component_capacity || count < 0 || count > 256 ||
        free < 0 || free > count || capacity > 4096 || U64(count) > capacity)
      return false;
    const auto at = previous.component_entry;
    if (at < entries + 0x20 || (at - entries - 0x20) % 0x18 ||
        (at - entries - 0x20) / 0x18 >= U64(count) || !Get(at, hash) ||
        hash != previous.component_hash || hash < 0 || !Get(at + 4, next) ||
        next < -1 || next >= count || !Get(at + 8, key) ||
        key != previous.component_key || !Ptr(at + 0x10, p) ||
        p != previous.inventory_component || !Object(p, "InventoryComponent") ||
        !Alive(p) || !Ptr(p + 0x28, inventory) ||
        !Object(inventory, "Inventory") || !Get(inventory + 0x50, owner) ||
        owner != e)
      return false;
    out.inventory_component = p;
    out.component_dictionary = dict;
    out.component_entries = entries;
    out.component_entry = at;
    out.component_capacity = capacity;
    out.component_count = count;
    out.component_free = free;
    out.component_version = version;
    out.component_hash = hash;
    out.component_key = key;
    return true;
  }
  bool Equipped(U64 p, int type, Item &out) {
    if (!p) {
      out = {};
      return true;
    }
    U64 k{}, id{};
    std::int32_t actual{};
    if (!belmont::Pointer(p) || !Ptr(p, k))
      return false;
    const bool klass = type == 1   ? (Klass(k, "WeaponItemData") ||
                                      Klass(k, "WeaponFistItemData") ||
                                      Klass(k, "WeaponOneHandedItemData") ||
                                      Klass(k, "WeaponSpearItemData") ||
                                      Klass(k, "WeaponVampireKillerItemData"))
                       : type == 2 ? Klass(k, "SpellItemData")
                       : type == 3 ? Klass(k, "CharmItemData")
                                   : Klass(k, "SkinItemData");
    if (!klass || !Alive(p) || !Get(p + 0x28, actual) || actual != type ||
        !Ptr(p + 0x20, id) || !String(id, out.id))
      return false;
    out.equipped = true;
    out.type = type;
    out.object = p;
    return true;
  }
  bool List(U64 list, std::vector<U64> &out, std::size_t cap = 512) {
    U64 arr{}, len{};
    std::int32_t count{}, version{};
    if (!Ptr(list + 0x10, arr) || !Get(list + 0x18, count) ||
        !Get(list + 0x1c, version) || count < 0 || std::size_t(count) > cap ||
        !Get(arr + 0x18, len) || len > cap || U64(count) > len)
      return false;
    for (int i = 0; i < count; ++i) {
      U64 p{};
      if (!Ptr(arr + 0x20 + 8 * U64(i), p))
        return false;
      out.push_back(p);
    }
    return true;
  }
  bool Header(U64 inventory, Snapshot &out) {
    out.inventory = inventory;
    return Ptr(inventory + 0x20, out.owned_list) &&
           Ptr(out.owned_list + 0x10, out.owned_array) &&
           Get(out.owned_list + 0x18, out.owned_count) &&
           Get(out.owned_list + 0x1c, out.owned_version) &&
           Get(out.owned_array + 0x18, out.owned_capacity) &&
           out.owned_count >= 0 && out.owned_count <= 512 &&
           out.owned_capacity <= 512 &&
           U64(out.owned_count) <= out.owned_capacity;
  }
  bool Decode(U64 inventory, Snapshot &out) {
    U64 list{};
    std::vector<U64> items;
    if (!Ptr(inventory + 0x20, list) || !List(list, items))
      return false;
    for (std::size_t i = 0; i < items.size(); ++i) {
      std::int32_t type{};
      U64 k{};
      bool base = false;
      if (!Ptr(items[i], k))
        return false;
      for (unsigned n = 0; n < 8; ++n) {
        if (Klass(k, "ItemData")) {
          base = true;
          break;
        }
        if (!Ptr(k + 0x58, k))
          return false;
      }
      if (!base || !Alive(items[i]) || !Get(items[i] + 0x28, type) ||
          type < 0 || type > 7)
        return false;
      if (type != 1 && type != 2 && type != 3 && type != 7)
        continue;
      Item item;
      if (!Equipped(items[i], type, item))
        return false;
      item.index = std::int32_t(i);
      item.equipped = false;
      out.items.push_back(std::move(item));
    }
    return true;
  }
  bool Bit(U64 at, bool &out) {
    std::uint8_t v{};
    if (!Get(at, v) || v > 1)
      return false;
    out = v != 0;
    return true;
  }
  // Registry dictionaries map string -> Value<T>. Select only a unique live
  // manager whose own variable is the exact UIManager variable. No heap scan.
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
  bool Button(U64 button, Slot &slot) {
    if (!Object(button, "UIButton", "Doozy.Runtime.UIManager.Components") ||
        !Alive(button) || !Bit(button + 0xf2, slot.selected))
      return false;
    bool active{}, interactable{}, groups{}, cooldown{};
    if (!Bit(button + 0x20, active) || !Bit(button + 0xd8, interactable) ||
        !Bit(button + 0xe8, groups) || !Bit(button + 0x198, cooldown))
      return false;
    slot.button = button;
    slot.enabled = active && interactable && groups && !cooldown;
    std::int32_t mode{};
    if (!Get(button + 0x28, mode))
      return false;
    if (mode == 4)
      for (unsigned i = 0; i < 4; ++i)
        if (!Get(button + 0x30 + 8 * i, slot.navigation[i]))
          return false;
    return true;
  }
  bool Directions(U64 button, Control &out) {
    std::int32_t mode{};
    if (!Get(button + 0x28, mode) || mode != 4)
      return false;
    bool *targets[] = {&out.up, &out.down, &out.left, &out.right};
    for (unsigned i = 0; i < 4; ++i) {
      U64 target{};
      if (!Get(button + 0x30 + 8 * i, target))
        return false;
      *targets[i] = belmont::Pointer(target);
    }
    return true;
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
  bool DecodeControl(U64 base, U64 inventory, Control &out,
                     const Control *cache = nullptr,
                     const std::vector<Item> *owned = nullptr) {
    U64 ui{}, manager{}, equipment{}, tab{}, stack{}, layers{}, array{}, top{},
        length{};
    std::int32_t depth{}, version{};
    bool focused{}, active{};
    out.reason = "manager_or_tab_unavailable";
    if (!Manager(base, ui, manager) || !Ptr(manager + 0x68, equipment) ||
        !Object(equipment, "EquipmentMenuManager") || !Alive(equipment) ||
        !Ptr(manager + 0x40, tab) || !Object(tab, "BasicTab") ||
        !Bit(tab + 0x79, active) || !Bit(tab + 0x7a, focused) ||
        !Ptr(ui + 0x30, stack) || !Object(stack, "MenuStack") ||
        !Ptr(stack + 0x20, layers) || !Ptr(layers + 0x10, array) ||
        !Get(layers + 0x18, depth) || !Get(layers + 0x1c, version) ||
        !Get(array + 0x18, length) || depth < 0 || depth > 32 || length > 32 ||
        U64(depth) > length)
      return false;
    U64 tabView{};
    if (!Ptr(manager + 0x30, tabView) || !Object(tabView, "BasicTabView") ||
        !Bit(tabView + 0x51, out.menu_open))
      return false;
    // Before first open, native currentTab is the valid -1 sentinel. Plus is
    // authorized only by this verified closed view and empty actual stack.
    out.can_open = !out.menu_open && depth == 0;
    out.reason = "native_tab_state_unavailable";
    U64 tabs{}, tabCount{};
    bool navEnabled{};
    if (!Ptr(tabView + 0x28, tabs) || !Get(tabs + 0x18, tabCount) ||
        tabCount == 0 || tabCount > 8 ||
        !Get(tabView + 0x38, out.current_tab) ||
        !Get(tab + 0x74, out.equipment_tab_index) ||
        !Bit(tabView + 0x58, navEnabled) || out.current_tab < -1 ||
        (out.menu_open && out.current_tab < 0) ||
        (out.current_tab >= 0 && U64(out.current_tab) >= tabCount) ||
        out.equipment_tab_index < 0 || U64(out.equipment_tab_index) >= tabCount)
      return false;
    out.tab_count = int(tabCount);
    for (U64 i = 0; i < tabCount; ++i) {
      U64 p{};
      bool enabled{};
      std::int32_t index{};
      if (!Ptr(tabs + 0x20 + 8 * i, p) || !Object(p, "BasicTab") ||
          !Bit(p + 0x79, enabled) || !Get(p + 0x74, index) || index != int(i))
        return false;
      if (enabled)
        out.active_tabs.push_back(int(i));
    }
    U64 equipmentTab{};
    if (!Get(tabs + 0x20 + 8 * U64(out.equipment_tab_index), equipmentTab) ||
        equipmentTab != tab)
      return false;
    out.equipment_tab =
        out.menu_open && active && out.current_tab == out.equipment_tab_index;
    if (depth && !Ptr(array + 0x20 + 8 * U64(depth - 1), top))
      return false;
    if (!out.equipment_tab) {
      if (out.menu_open && (!navEnabled || !BaseTabLayer(top, tabView, out)))
        return false;
      out.ready = true;
      out.reason = out.menu_open ? "other_native_tab" : "equipment_closed";
      return true;
    }
    out.reason = "picker_state_unavailable";
    U64 pickers[3]{};
    const U64 offsets[] = {0x28, 0x38, 0x30};
    const char *names[] = {"InventoryWeaponMenuManager",
                           "InventorySpellMenuManager",
                           "InventoryCharmsMenuManager"};
    int openCount = 0;
    std::map<U64, std::int32_t> charmPages;
    for (int i = 0; i < 3; ++i) {
      bool opened{};
      U64 layer{};
      if (!Ptr(equipment + offsets[i], pickers[i]) ||
          !Object(pickers[i], names[i]) || !Alive(pickers[i]) ||
          !Bit(pickers[i] + 0xa4, opened) || !Ptr(pickers[i] + 0x28, layer))
        return false;
      if (opened) {
        ++openCount;
        if (i == 2) {
          U64 pages{}, currentPage{}, header{}, held{};
          bool legacyFlag{};
          std::vector<U64> pageObjects;
          if (!Bit(pickers[i] + 0x1b8, legacyFlag) ||
              !Get(pickers[i] + 0x1b0, held) ||
              !Ptr(pickers[i] + 0xc0, pages) || !List(pages, pageObjects, 16) ||
              !Get(pickers[i] + 0x198, out.native_page) ||
              out.native_page < 0 || out.native_page >= int(pageObjects.size()))
            return false;
          out.native_pages = int(pageObjects.size());
          // InventoryCharmPage.charmsSlots is the source-owned page membership;
          // disabled buttons alone do not identify an off-page owned item.
          for (std::size_t pageIndex = 0; pageIndex < pageObjects.size(); ++pageIndex) {
            U64 pageSlots{};
            std::vector<U64> members;
            if (!Object(pageObjects[pageIndex], "InventoryCharmPage") ||
                !Alive(pageObjects[pageIndex]) ||
                !Ptr(pageObjects[pageIndex] + 0x30, pageSlots) ||
                !List(pageSlots, members, 256))
              return false;
            for (auto member : members)
              if (!charmPages.emplace(member, int(pageIndex)).second)
                return false;
          }
          currentPage = pageObjects[std::size_t(out.native_page)];
          if (!Object(currentPage, "InventoryCharmPage") ||
              !Alive(currentPage) || !Ptr(currentPage + 0x38, header))
            return false;
          out.holding_charm = held && top == header;
          if (out.holding_charm) {
            U64 item{}, owner{};
            if (!Object(held, "InventoryCharmSlotController") || !Alive(held) ||
                !Get(held + 0x50, owner) || owner != inventory ||
                !Ptr(held + 0x58, item) || !Equipped(item, 3, out.held_item))
              return false;
          }
          if (top != layer && (!out.holding_charm || top != header))
            return false;
          out.can_prev_page =
              !out.holding_charm && top == layer && out.native_page > 0;
          out.can_next_page = !out.holding_charm && top == layer &&
                              out.native_page + 1 < out.native_pages;
        } else if (top != layer)
          return false;
        out.category = i + 1;
      }
    }
    if (openCount > 1)
      return false;
    if (openCount == 1) {
      out.reason = "picker_slots_unavailable";
      const U64 picker = pickers[out.category - 1];
      U64 list{}, selected{}, component{}, owner{};
      if (!Ptr(picker + 0x48, list) || !Get(picker + 0x98, selected) ||
          !Get(picker + 0xa0, out.selected_index) ||
          !Ptr(picker + 0x80, component) ||
          !Object(component, "InventoryComponent") ||
          !Get(component + 0x28, owner) || owner != inventory)
        return false;
      out.picker_object = picker;
      out.slots_list = list;
      if (!Ptr(list + 0x10, out.slots_array) ||
          !Get(list + 0x18, out.slots_count) ||
          !Get(list + 0x1c, out.slots_version) ||
          !Get(out.slots_array + 0x18, out.slots_capacity) ||
          out.slots_count < 0 || out.slots_count > 256 ||
          out.slots_capacity > 256 || U64(out.slots_count) > out.slots_capacity)
        return false;
      bool reuse = cache && cache->ready && cache->picker &&
                   cache->category == out.category &&
                   cache->native_page == out.native_page &&
                   cache->picker_object == picker &&
                   cache->slots_list == list &&
                   cache->slots_array == out.slots_array &&
                   cache->slots_count == out.slots_count &&
                   cache->slots_version == out.slots_version &&
                   cache->slots_capacity == out.slots_capacity;
      const Slot *cachedSelected = nullptr;
      if (reuse)
        for (const auto &slot : cache->slots)
          if (slot.index >= 0 && slot.object == selected)
            cachedSelected = &slot;
      // Native page changes can leave selectedInventoryItemSlot pointing at
      // an old page while the actual native UIButton selection has moved.
      // When that pointer's button is not selected, inspect selection flags
      // only on the unchanged cached slots, then validate the selected slot
      // in full below. Never reread all immutable IDs just to find focus.
      if (reuse && !out.holding_charm) {
        bool selectedFlag = false;
        const Slot *old = cachedSelected;
        if (old && !Bit(old->button + 0xf2, selectedFlag))
          return false;
        if (!selectedFlag) {
          cachedSelected = nullptr;
          for (const auto &slot : cache->slots) {
            if (slot.index < 0 || &slot == old)
              continue;
            bool hasSelection{};
            if (!Bit(slot.button + 0xf2, hasSelection))
              return false;
            if (hasSelection) {
              if (cachedSelected)
                return false;
              cachedSelected = &slot;
            }
          }
        }
      }
      // Refresh the authoritative selected slot, its ownership/item pointer,
      // button/disabled/cooldown flags, and native first-hop navigation. Other
      // immutable IDs/graph nodes are reused only across an unchanged native
      // list identity/version. Submit still requires fresh selected ID/button.
      if (reuse && out.holding_charm) {
        for (const auto &slot : cache->slots)
          if (slot.index >= 0) {
            auto copy = slot;
            copy.selected = false;
            out.slots.push_back(std::move(copy));
          }
      } else if (reuse && cachedSelected) {
        Slot current = *cachedSelected;
        U64 item{}, owner{}, button{};
        if (!Get(current.object + 0x50, owner) || owner != inventory ||
            !Get(current.object + 0x58, item) || item != current.item.object ||
            !Get(current.object + 0x20, button) || button != current.button ||
            !Button(button, current))
          return false;
        if (current.selected || out.holding_charm) {
          for (const auto &slot : cache->slots)
            if (slot.index >= 0) {
              auto copy = slot;
              copy.selected = false;
              out.slots.push_back(std::move(copy));
            }
          for (auto &slot : out.slots)
            if (slot.object == current.object)
              slot = current;
          if (current.selected) {
            out.selected_button = current.button;
            out.selected_slot = current.object;
            out.selected_id = current.item.id;
            out.selected_type = out.category;
            out.confirm = current.enabled;
          }
        } else
          reuse = false;
      } else
        reuse = false;
      std::vector<U64> slots;
      if (!reuse) {
        if (!List(list, slots, 256))
          return false;
        for (std::size_t i = 0; i < slots.size(); ++i) {
          U64 item{}, button{}, slotInventory{}, slotClass{};
          Slot slot;
          slot.object = slots[i];
          slot.index = std::int32_t(i);
          if (!Ptr(slots[i], slotClass) || !Alive(slots[i]) ||
              !(Klass(slotClass, "InventoryItemSlotController`1") ||
                Klass(slotClass, "InventoryWeaponSlotController") ||
                Klass(slotClass, "InventorySpellSlotController") ||
                Klass(slotClass, "InventoryCharmSlotController")) ||
              !Get(slots[i] + 0x50, slotInventory) ||
              !Ptr(slots[i] + 0x20, button) || !Get(slots[i] + 0x58, item) ||
              !Button(button, slot))
            return false;
          out.reason = "picker_item_unavailable";
          if (item) {
            if (slotInventory != inventory ||
                !Equipped(item, out.category, slot.item))
              return false;
          } else {
            if (slot.enabled || (slotInventory && slotInventory != inventory))
              return false;
            slot.item.type = out.category;
          }

          if (slot.selected) {
            if (out.selected_button)
              return false;
            out.selected_button = button;
            out.selected_slot = slots[i];
            out.selected_id = slot.item.id;
            out.selected_type = out.category;
            out.confirm = slot.enabled;
          }
          out.slots.push_back(std::move(slot));
        }
      } else {
        for (const auto &slot : out.slots)
          slots.push_back(slot.object);
      }
      // Page acknowledgment can precede the native canvas-group transition.
      // Immutable item IDs/graph may be cached, but live button availability
      // is never cached: a newly shown page must become usable without an ID
      // rescan, and a disabled button must not remain falsely enabled.
      if (reuse)
        for (auto &slot : out.slots) {
          bool active{}, interactable{}, groups{}, cooldown{};
          if (!Bit(slot.button + 0x20, active) ||
              !Bit(slot.button + 0xd8, interactable) ||
              !Bit(slot.button + 0xe8, groups) ||
              !Bit(slot.button + 0x198, cooldown))
            return false;
          slot.enabled = active && interactable && groups && !cooldown;
        }
      if (out.category == 3)
        for (auto &slot : out.slots) {
          auto page = charmPages.find(slot.object);
          if (page == charmPages.end())
            return false;
          slot.native_page = page->second;
        }
      out.reason = "picker_selection_unavailable";
      // Fields are corroborating; active selection is the native UIButton flag.
      if (out.selected_index < -1 ||
          out.selected_index >= std::int32_t(slots.size()))
        return false;
      if (selected &&
          std::find(slots.begin(), slots.end(), selected) == slots.end())
        return false;
      if (out.category == 3) {
        out.reason = "charm_slot_state_unavailable";
        if (!Get(picker + 0x1a0, out.charm_slot) || out.charm_slot < -1 ||
            out.charm_slot > 2)
          return false;
      }
      if (out.category == 3) {
        out.reason = "charm_equipped_slots_unavailable";
        U64 array{}, len{};
        if (!Ptr(picker + 0xb8, array) || !Get(array + 0x18, len) || len > 3)
          return false;
        U64 actualCharms{}, actualCount{};
        if (!Ptr(inventory + 0x40, actualCharms) ||
            !Get(actualCharms + 0x18, actualCount) || actualCount > 3 ||
            actualCount < len)
          return false;
        for (U64 i = 0; i < len; ++i) {
          U64 p{}, button{}, item{}, actualItem{};
          Slot slot;
          if (!Ptr(array + 0x20 + 8 * i, p) ||
              !Object(p, "EquipmentViewEquippedItemController") ||
              !Ptr(p + 0x70, button) || !Get(p + 0x78, item) ||
              !Button(button, slot))
            return false;
          if (slot.selected) {
            if (out.selected_button)
              return false;
            out.selected_button = button;
            out.selected_slot = p;
            out.selected_type = 3;
            out.charm_slot = int(i);
            out.confirm = slot.enabled;
          }
          // The overview target controller is an inactive UI placeholder until
          // the native hold animation populates it. Actual equipped contents
          // come from Inventory.GetEquippedCharms, never placeholder _item.
          if (!Get(actualCharms + 0x20 + 8 * i, actualItem))
            return false;
          if (actualItem) {
            const Item *match = nullptr;
            if (owned)
              for (const auto &entry : *owned)
                if (entry.object == actualItem && entry.type == 3)
                  match = &entry;
            if (match) {
              slot.item = *match;
              slot.item.equipped = true;
            } else if (!Equipped(actualItem, 3, slot.item))
              return false;
          }
          if (slot.selected)
            out.selected_id = slot.item.id;
          slot.object = p;
          slot.index = -int(i) - 1;
          slot.item.type = 3;
          out.slots.push_back(std::move(slot));
        }
      }
      out.picker = true;
      out.back = true;
    } else {
      out.reason = "overview_layer_covered";
      if (!navEnabled || !BaseTabLayer(top, tabView, out))
        return false;
      out.reason = "overview_slots_unavailable";
      U64 controllers{}, len{};
      if (!Ptr(equipment + 0x50, controllers) ||
          !Get(controllers + 0x18, len) || len > 8)
        return false;
      for (U64 i = 0; i < len; ++i) {
        U64 p{}, button{}, item{};
        Slot slot;
        std::int32_t type{}, charm{};
        if (!Ptr(controllers + 0x20 + 8 * i, p) ||
            !Object(p, "EquipmentViewEquippedItemController") ||
            !Get(p + 0x50, type) || !Get(p + 0x54, charm) ||
            !Ptr(p + 0x70, button) || !Get(p + 0x78, item) ||
            !Button(button, slot))
          return false;
        slot.object = p;
        slot.index = int(i);
        slot.item.type = type;
        if (item && !Equipped(item, type, slot.item))
          return false;
        if (slot.selected) {
          if (out.selected_button)
            return false;
          out.selected_button = button;
          out.selected_type = type;
          out.charm_slot = type == 3 ? charm : -1;
          out.confirm = slot.enabled;
        }
        out.slots.push_back(std::move(slot));
      }
      out.overview = true;
      out.back = true;
    }
    out.reason = "native_navigation_unavailable";
    if (out.selected_button && !Directions(out.selected_button, out))
      return false;
    out.ready = true;
    out.reason = "ready";
    return true;
  }
  bool Coherent() {
    std::array<std::uint8_t,4096> fresh{};
    for (const auto &span : journal) {
      if (span.bytes.size()>fresh.size() ||
          !read(span.at,fresh.data(),span.bytes.size()) ||
          std::memcmp(fresh.data(),span.bytes.data(),span.bytes.size())!=0)
        return false;
    }
    return true;
  }

public:
  explicit Decoder(Read &r) : read(r) {}
  Snapshot Sample(const belmont::Identity &id,
                  const belmont::Snapshot &source) {
    journal.clear();
    span_index.clear();
    Snapshot out;
    if (id.title != belmont::Title || id.build != belmont::Build ||
        !belmont::Pointer(id.base) || id.size < 0x6d47f18 ||
        id.base > std::numeric_limits<U64>::max() - id.size) {
      out.reason = "unsupported_build";
      return out;
    }
    out.supported = true;
    if (!source.supported || !source.coherent || !source.manager_ready ||
        !source.entity_ready || !source.components_ready ||
        source.application_loading != 0 || source.room_loading != 0 ||
        source.loading_identity != 0 ||
        (source.initial_identity != 1 && source.paused != 1)) {
      out.reason = "non_gameplay";
      return out;
    }
    if (!Pins(id.base)) {
      out.reason = "accessor_mismatch";
      return out;
    }
    if (!Current(id, source)) {
      out.reason = "stale_source";
      return out;
    }
    U64 inventory{};
    if (!Components(source.entity, inventory, &out) ||
        !Header(inventory, out) || !Decode(inventory, out)) {
      Snapshot rejected;
      rejected.supported = true;
      rejected.reason = "inventory_unavailable";
      return rejected;
    }
    Control control;
    if (DecodeControl(id.base, inventory, control, nullptr, &out.items))
      out.control = std::move(control);
    else
      out.control.reason = control.reason;
    if (!Current(id, source) || !Coherent()) {
      Snapshot rejected;
      rejected.supported = true;
      rejected.reason = "incoherent";
      return rejected;
    }
    out.coherent = out.ready = true;
    out.entity = source.entity;
    out.reason = "ready";
    return out;
  }
  Snapshot SampleControl(const belmont::Identity &id,
                         const belmont::Snapshot &source,
                         const Snapshot &previous) {
    journal.clear();
    span_index.clear();
    Snapshot out;
    if (id.title != belmont::Title || id.build != belmont::Build ||
        !belmont::Pointer(id.base) || id.size < 0x6d47f18 ||
        id.base > std::numeric_limits<U64>::max() - id.size) {
      out.reason = "unsupported_build";
      return out;
    }
    out.supported = true;
    if (!source.supported || !source.coherent || !source.manager_ready ||
        !source.entity_ready || !source.components_ready ||
        source.application_loading != 0 || source.room_loading != 0 ||
        source.loading_identity != 0 ||
        (source.initial_identity != 1 && source.paused != 1)) {
      out.reason = "non_gameplay";
      return out;
    }
    if (!Pins(id.base)) {
      out.reason = "accessor_mismatch";
      return out;
    }
    if (!Current(id, source)) {
      out.reason = "stale_source";
      return out;
    }
    U64 inventory{};
    if (!previous.ready || !previous.coherent ||
        previous.entity != source.entity ||
        !CachedComponents(source.entity, previous, inventory, out) ||
        !Header(inventory, out) || inventory != previous.inventory ||
        out.owned_list != previous.owned_list ||
        out.owned_array != previous.owned_array ||
        out.owned_count != previous.owned_count ||
        out.owned_version != previous.owned_version ||
        out.owned_capacity != previous.owned_capacity) {
      Snapshot rejected;
      rejected.supported = true;
      rejected.reason = "inventory_changed";
      return rejected;
    }
    out.items = previous.items;
    Control control;
    if (DecodeControl(id.base, inventory, control, &previous.control,
                      &out.items))
      out.control = std::move(control);
    else
      out.control.reason = control.reason;
    if (!Current(id, source) || !Coherent()) {
      Snapshot rejected;
      rejected.supported = true;
      rejected.reason = "incoherent";
      return rejected;
    }
    out.coherent = out.ready = true;
    out.entity = source.entity;
    out.reason = "ready";
    return out;
  }
};
} // namespace belmont_inventory
