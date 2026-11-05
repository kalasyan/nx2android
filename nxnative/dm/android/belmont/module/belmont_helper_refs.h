// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_helper_mailbox.h"
#include "belmont_reader.h"
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Read-only resolution of the guest pointers the Belmont guest helper takes:
// the class_table klass pointers, MapMarkerTypeID objects by GUID (UIManager's
// marker display dictionary) and LevelID objects by GUID
// (LevelManager.mainFlowLevelIDs). All three stay resident with the native
// Map menu closed (HELPER-RESIDENCY 2026-10-23). Every pointer is class-name
// checked; anything unresolved stays 0 and the caller falls back to menu
// steering. The guest helper re-checks every klass itself before calling.
namespace belmont_helper_refs {
using belmont::U64;
using Slot = belmont_helper_mailbox::ClassSlot;

struct Refs {
  bool supported{};
  std::array<U64, 8> klass{};
  std::vector<std::pair<std::string, U64>> marker_types, levels;
  std::string reason{"unavailable"};
  U64 Klass(Slot slot) const { return klass[static_cast<std::size_t>(slot)]; }
  U64 MarkerType(const std::string &guid) const { return Find(marker_types, guid); }
  U64 Level(const std::string &guid) const { return Find(levels, guid); }
  // ADD_PIN initializes and reads MapMarkerData's klass in the guest itself;
  // CHANGE_PIN additionally needs Klass(Slot::MapMarkerData) (from a live pin).
  bool PinsReady() const {
    return Klass(Slot::LevelID) &&
           Klass(Slot::MapMarkerTypeID) && Klass(Slot::MapMarkersComponent) &&
           !marker_types.empty() && !levels.empty();
  }
  bool EquipReady(std::int32_t type) const {
    const auto item = type == 1 ? Slot::WeaponItemData
                      : type == 2 ? Slot::SpellItemData
                                  : Slot::CharmItemData;
    return (type >= 1 && type <= 3) && Klass(Slot::Inventory) && Klass(item);
  }

private:
  static U64 Find(const std::vector<std::pair<std::string, U64>> &list,
                  const std::string &guid) {
    for (const auto &[g, p] : list)
      if (g == guid)
        return p;
    return 0;
  }
};

// TypeInfo slots (main-relative) from the build's IL2CPP script metadata.
// Slots fill lazily on first use; an unfilled slot fails the name check.
inline constexpr U64 MapMarkerDataTypeInfo = 0x6d444d8;
inline constexpr U64 SpellItemDataTypeInfo = 0x6d46d00;
inline constexpr U64 WeaponItemDataTypeInfo = 0x6d488c8;
inline constexpr U64 CharmItemDataTypeInfo = 0x6d404e8;
inline constexpr U64 InventoryTypeInfo = 0x6d43b98;
inline constexpr U64 UIManagerTypeInfo = 0x6d47f10;
inline constexpr U64 RuntimeServiceLocatorTypeInfo = 0x6d463c0;

template <class Read> class Decoder {
  Read &read;
  template <class T> bool Get(U64 at, T &value) {
    static_assert(std::is_integral_v<T>);
    return at >= 0x10000 && read(at, &value, sizeof(T));
  }
  bool Ptr(U64 at, U64 &value) { return Get(at, value) && belmont::Pointer(value); }
  bool Text(U64 at, const char *expected) {
    std::array<char, 64> b{};
    const auto n = std::strlen(expected) + 1;
    return n <= b.size() && read(at, b.data(), n) && !std::memcmp(b.data(), expected, n);
  }
  bool CString(U64 at, U64 &p) {
    return Get(at, p) && p >= 0x10000 && p < 0x8000000000ULL;
  }
  bool KlassNamed(U64 k, const char *name) {
    U64 n{}, ns{};
    return belmont::Pointer(k) && CString(k + 0x10, n) && CString(k + 0x18, ns) &&
           Text(n, name) && Text(ns, "");
  }
  bool Object(U64 p, const char *name, U64 *klass = nullptr) {
    U64 k{};
    if (!belmont::Pointer(p) || !Ptr(p, k) || !KlassNamed(k, name))
      return false;
    if (klass)
      *klass = k;
    return true;
  }
  U64 TypeInfo(U64 base, U64 slot, const char *name) {
    U64 k{};
    return Ptr(base + slot, k) && KlassNamed(k, name) ? k : 0;
  }
  bool Guid(U64 object, std::string &out) {
    U64 s{}, sk{}, n{}, ns{};
    std::int32_t length{};
    if (!Ptr(object + 0x18, s) || !Ptr(s, sk) || !CString(sk + 0x10, n) ||
        !CString(sk + 0x18, ns) || !Text(n, "String") || !Text(ns, "System") ||
        !Get(s + 0x10, length) || length != 32)
      return false;
    std::array<std::uint16_t, 32> wide{};
    if (!read(s + 0x14, wide.data(), sizeof(wide)))
      return false;
    out.clear();
    for (auto c : wide) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        return false;
      out += char(c);
    }
    return true;
  }
  void MarkerTypes(U64 base, Refs &out) {
    U64 k{}, statics{}, ui{}, dict{}, list{}, items{};
    std::int32_t size{};
    U64 length{};
    if (!Ptr(base + UIManagerTypeInfo, k) || !KlassNamed(k, "UIManager") ||
        !Ptr(k + 0xb8, statics) || !Ptr(statics, ui) || !Object(ui, "UIManager") ||
        !Ptr(ui + 0x58, dict) || !Ptr(dict + 0x18, list) || !Ptr(list + 0x10, items) ||
        !Get(list + 0x18, size) || size <= 0 || size > 64 || !Get(items + 0x18, length) ||
        U64(size) > length)
      return;
    for (std::int32_t i = 0; i < size; ++i) {
      U64 element{}, type{}, klass{};
      std::string guid;
      if (!Ptr(items + 0x20 + 8 * U64(i), element) || !Ptr(element + 0x18, type) ||
          !Object(type, "MapMarkerTypeID", &klass) || !Guid(type, guid)) {
        out.marker_types.clear();
        return;
      }
      out.klass[std::size_t(Slot::MapMarkerTypeID)] = klass;
      out.marker_types.emplace_back(std::move(guid), type);
    }
  }
  void Levels(U64 base, Refs &out) {
    U64 k{}, statics{}, locator{}, list{}, items{};
    std::int32_t count{};
    if (!Ptr(base + RuntimeServiceLocatorTypeInfo, k) ||
        !KlassNamed(k, "RuntimeServiceLocator") || !Ptr(k + 0xb8, statics) ||
        !Ptr(statics, locator) || !Object(locator, "RuntimeServiceLocator") ||
        !Ptr(locator + 0x18, list) || !Ptr(list + 0x10, items) ||
        !Get(list + 0x18, count) || count <= 0 || count > 128)
      return;
    U64 manager{};
    for (std::int32_t i = 0; i < count && !manager; ++i) {
      U64 var{}, value{};
      if (!Ptr(items + 0x20 + 8 * U64(i), var))
        return;
      if (Object(var, "VarLevelManager") && Ptr(var + 0x28, value) &&
          Object(value, "LevelManager"))
        manager = value;
    }
    U64 array{}, length{};
    if (!manager || !Ptr(manager + 0x70, array) || !Get(array + 0x18, length) ||
        length == 0 || length > 256)
      return;
    for (U64 i = 0; i < length; ++i) {
      U64 level{}, klass{};
      std::string guid;
      if (!Get(array + 0x20 + 8 * i, level) || !level)
        continue; // unassigned array entries are legal
      if (!Object(level, "LevelID", &klass) || !Guid(level, guid)) {
        out.levels.clear();
        return;
      }
      out.klass[std::size_t(Slot::LevelID)] = klass;
      out.levels.emplace_back(std::move(guid), level);
    }
  }

public:
  explicit Decoder(Read &r) : read(r) {}
  // component: the player's live MapMarkersComponent* (0 when unknown).
  // marker: any live MapMarkerData* (0 when none). Pins loaded from a save do
  // not fill the lazy MapMarkerData TypeInfo slot (live 2026-10-23: still the
  // metadata token), so a live object supplies the klass instead.
  Refs Sample(const belmont::Identity &id, U64 component, U64 marker = 0) {
    Refs out;
    if (id.title != belmont::Title || id.build != belmont::Build ||
        !belmont::Pointer(id.base) || id.size < 0x6d48900) {
      out.reason = "unsupported_build";
      return out;
    }
    out.supported = true;
    const auto set = [&](Slot slot, U64 k) { out.klass[std::size_t(slot)] = k; };
    set(Slot::MapMarkerData, TypeInfo(id.base, MapMarkerDataTypeInfo, "MapMarkerData"));
    U64 marker_klass{};
    if (!out.Klass(Slot::MapMarkerData) && marker &&
        Object(marker, "MapMarkerData", &marker_klass))
      set(Slot::MapMarkerData, marker_klass);
    set(Slot::SpellItemData, TypeInfo(id.base, SpellItemDataTypeInfo, "SpellItemData"));
    set(Slot::WeaponItemData, TypeInfo(id.base, WeaponItemDataTypeInfo, "WeaponItemData"));
    set(Slot::CharmItemData, TypeInfo(id.base, CharmItemDataTypeInfo, "CharmItemData"));
    set(Slot::Inventory, TypeInfo(id.base, InventoryTypeInfo, "Inventory"));
    U64 component_klass{};
    if (component && Object(component, "MapMarkersComponent", &component_klass))
      set(Slot::MapMarkersComponent, component_klass);
    MarkerTypes(id.base, out);
    Levels(id.base, out);
    out.reason = out.PinsReady() ? "ready" : "pins_unresolved";
    return out;
  }
};
} // namespace belmont_helper_refs
