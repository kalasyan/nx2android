// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_reader.h"
#include <cmath>
#include <set>
// Current native saved user pins; never companion-local annotations. Optional
// failure is isolated from the caller's core map snapshot. No guest mutations.
namespace belmont_native_pins {
using belmont::U64;
struct Pin {
  std::string level_guid, type_guid;
  std::int32_t id{}, nav_index{};
  float x{}, y{};
  // Guest objects for the guest helper (CHANGE_PIN/REMOVE_PIN); never written.
  U64 object{}, level{};
};
struct Snapshot {
  bool supported{}, coherent{}, ready{};
  U64 entity{}, component{};
  std::int32_t limit_mode{}, limit{};
  std::vector<Pin> pins;
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
  bool MatchesName(U64 at, const char *expected, bool &matches) {
    U64 p{};
    if (!PtrText(at, p))
      return false;
    matches = false;
    // Metadata is immutable, but remain bounded and journal all consulted
    // bytes. Most unrelated component names differ at their first character;
    // do not decode their full class name and namespace on every map poll.
    for (std::size_t i = 0; i <= std::strlen(expected); ++i) {
      std::uint8_t byte{};
      if (!Bytes(p + i, &byte, 1))
        return false;
      if (byte != std::uint8_t(expected[i]))
        return true;
    }
    matches = true;
    return true;
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
  bool Instruction(U64 base, U64 rva,
                   std::initializer_list<std::uint8_t> expected) {
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
  bool Components(U64 e, U64 &component_out) {
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
      bool matches{};
      if (!MatchesName(k + 0x10, "MapMarkersComponent", matches))
        return false;
      keys.push_back(key);
      ++found;
      if (matches) {
        U64 ns{};
        if (!PtrText(k + 0x18, ns) || !Text(ns, ""))
          return false;
        if (component)
          return false;
        component = p;
      }
    }
    component_out = component;
    return found == count - free && component && Alive(component);
  }
  bool Guid(U64 p, std::string &out) {
    if (!String(p, out) || out.size() != 32)
      return false;
    for (char c : out)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        return false;
    return true;
  }
  bool Pins(U64 b) {
    // WritePlayerSave reads the live component dictionary, LevelMarkersData
    // appends by native list layout, MapMarkerData constructor writes fields.
    return Instruction(b, 0x1c2f4cc, {0xa0, 0x16, 0x40, 0xf9}) &&
           Instruction(b, 0x1c2e8ec,
                       {0x68, 0x22, 0x40, 0xb9, 0x88, 0x12, 0x00, 0xb9, 0x60,
                        0x0e, 0x40, 0xf9}) &&
           Instruction(b, 0x1c311d4,
                       {0xa9, 0x16, 0x00, 0xbd, 0xa8, 0x1a, 0x00, 0xbd}) &&
           Instruction(b, 0x1c311e0,
                       {0xa8, 0x12, 0x00, 0xb9, 0xb4, 0x1e, 0x00, 0xb9, 0xb3,
                        0x12, 0x00, 0xf9}) &&
           Instruction(b, 0x1e0fff0,
                       {0x00, 0x0c, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6});
  }
  bool Decode(U64 c, Snapshot &out) {
    U64 dict{}, entries{}, capacity{};
    std::int32_t count{}, free{}, version{};
    if (!Get(c + 0x20, out.limit_mode) || out.limit_mode < 0 ||
        out.limit_mode > 3 || !Get(c + 0x24, out.limit) || out.limit < 0 ||
        out.limit > 4096 || !Ptr(c + 0x28, dict) ||
        !Object(dict, "Dictionary`2", "System.Collections.Generic") ||
        !Get(dict + 0x20, count) || count < 0 || count > 256 ||
        !Get(dict + 0x28, free) || free < 0 || free > count ||
        !Get(dict + 0x2c, version) || !Get(dict + 0x18, entries))
      return false;
    if (!entries)
      return count == 0 && free == 0;
    if (!belmont::Pointer(entries) || !Get(entries + 0x18, capacity) ||
        capacity > 4096 || U64(count) > capacity)
      return false;
    std::set<std::string> levels;
    int live = 0;
    for (int i = 0; i < count; ++i) {
      U64 at = entries + 0x20 + 0x18 * U64(i), level{}, data{}, g{}, list{},
          items{}, length{};
      std::int32_t hash{}, next{}, size{}, ver{}, next_id{};
      std::string level_guid;
      if (!Get(at, hash))
        return false;
      if (hash == -1)
        continue;
      if (hash < 0 || !Get(at + 4, next) || next < -1 || next >= count ||
          !Ptr(at + 8, level) || !Object(level, "LevelID") || !Alive(level) ||
          !Ptr(level + 0x18, g) || !Guid(g, level_guid) ||
          !levels.insert(level_guid).second || !Ptr(at + 0x10, data) ||
          !Object(data, "LevelMarkersData") || !Get(data + 0x20, next_id) ||
          next_id < 0 || !Ptr(data + 0x18, list) ||
          !Object(list, "List`1", "System.Collections.Generic") ||
          !Get(list + 0x18, size) || size < 0 || size > 1024 ||
          !Get(list + 0x1c, ver) || !Get(list + 0x10, items))
        return false;
      ++live;
      if (!items) {
        if (size)
          return false;
        continue;
      }
      if (!belmont::Pointer(items) || !Get(items + 0x18, length) ||
          length > 4096 || U64(size) > length)
        return false;
      std::set<std::int32_t> ids;
      for (int j = 0; j < size; ++j) {
        U64 marker{};
        Pin pin;
        std::uint32_t x{}, y{};
        pin.level_guid = level_guid;
        pin.level = level;
        if (out.pins.size() >= 4096 ||
            !Ptr(items + 0x20 + 8 * U64(j), marker) ||
            !Object(marker, "MapMarkerData") || !Get(marker + 0x10, pin.id) ||
            pin.id < 0 || pin.id >= next_id || !ids.insert(pin.id).second ||
            !Get(marker + 0x14, x) || !Get(marker + 0x18, y) ||
            !Get(marker + 0x1c, pin.nav_index) || pin.nav_index < 0 ||
            pin.nav_index > 255 || !Ptr(marker + 0x20, g) ||
            !Guid(g, pin.type_guid))
          return false;
        pin.object = marker;
        std::memcpy(&pin.x, &x, 4);
        std::memcpy(&pin.y, &y, 4);
        if (!std::isfinite(pin.x) || !std::isfinite(pin.y) ||
            std::abs(pin.x) > 1000000 || std::abs(pin.y) > 1000000)
          return false;
        out.pins.push_back(std::move(pin));
      }
    }
    return live == count - free;
  }
  bool Coherent() {
    std::array<std::uint8_t, 4096> fresh{};
    for (const auto &s : journal)
      if (!read(s.at, fresh.data(), s.bytes.size()) ||
          std::memcmp(fresh.data(), s.bytes.data(), s.bytes.size()))
        return false;
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
        !belmont::Pointer(id.base) || id.size < 0x6d46230 ||
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
    U64 component{};
    if (!Components(source.entity, component) || !Decode(component, out) ||
        !Coherent()) {
      Snapshot failed;
      failed.supported = true;
      failed.reason = "native_pins_unavailable";
      return failed;
    }
    out.ready = out.coherent = true;
    out.entity = source.entity;
    out.component = component;
    out.reason = "ready";
    return out;
  }
};
} // namespace belmont_native_pins
