// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_reader.h"
#include <set>

namespace belmont_objectives {
using belmont::U64;
struct LocalizationReference {
  // GUID bytes use the managed System.Guid memory layout (little-endian
  // uint32/uint16/uint16, followed by eight bytes), not a UTF-8 GUID string.
  std::array<std::uint8_t, 16> table_guid{};
  std::string table_name, entry_key;
  std::int32_t table_type{}, entry_type{};
  std::int64_t entry_id{};
};
struct Snapshot {
  bool supported{}, coherent{}, ready{};
  U64 component{}, objective{}, localized_string{};
  std::string objective_guid;
  LocalizationReference description;
  std::int32_t acquisition_mode{-1};
  std::string reason{"unavailable"};
};

// This decoder has no write/call API and does not retain a last-good value.
// It copies identity/localization references only; resolving installed
// localized text belongs on the RomFS worker, never on the sampling thread.
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
    if (!n || n > 2048 || at < 0x10000 ||
        at > std::numeric_limits<U64>::max() - n || journal.size() >= 4096)
      return false;
    if (auto found=span_index.find({at,n});found!=span_index.end()) {
      const auto& saved=journal[found->second].bytes;
      std::memcpy(dst,saved.data(),n);
      return true;
    }
    if (!read(at,dst,n)) return false;
    auto p = static_cast<const std::uint8_t *>(dst);
    span_index.emplace(belmont::ReadSpanKey{at,n},journal.size());
    journal.push_back({at, {p, p + n}});
    return true;
  }
  template <class T> bool Get(U64 at, T &v) {
    static_assert(std::is_integral_v<T>);
    std::array<std::uint8_t, sizeof(T)> b{};
    if (!Bytes(at, b.data(), b.size()))
      return false;
    U64 n{};
    for (unsigned i = 0; i < b.size(); ++i)
      n |= U64(b[i]) << (i * 8);
    std::memcpy(&v, &n, sizeof(T));
    return true;
  }
  bool Ptr(U64 at, U64 &p) { return Get(at, p) && belmont::Pointer(p); }
  bool Text(U64 at, const char *s) {
    std::array<char, 128> b{};
    const auto n = std::strlen(s) + 1;
    return n <= b.size() && Bytes(at, b.data(), n) &&
           !std::memcmp(b.data(), s, n);
  }
  bool Klass(U64 k, const char *name, const char *ns) {
    U64 n{}, s{};
    return belmont::Pointer(k) && Get(k + 0x10, n) && Get(k + 0x18, s) &&
           Text(n, name) && Text(s, ns);
  }
  bool Object(U64 p, const char *name, const char *ns) {
    U64 k{};
    return belmont::Pointer(p) && Ptr(p, k) && Klass(k, name, ns);
  }
  bool Alive(U64 p) {
    U64 n{};
    std::uint8_t b{};
    return Ptr(p + 0x10, n) && Bytes(n, &b, 1);
  }
  bool Utf16(U64 at, std::string &out, std::size_t cap) {
    std::int32_t n{};
    if (!Object(at, "String", "System") || !Get(at + 0x10, n) || n < 0 ||
        std::size_t(n) > cap)
      return false;
    std::vector<std::uint8_t> bytes(std::size_t(n) * 2);
    if (n && !Bytes(at + 0x14, bytes.data(), bytes.size()))
      return false;
    out.clear();
    for (int i = 0; i < n; ++i) {
      std::uint32_t c = bytes[2 * i] | (std::uint32_t(bytes[2 * i + 1]) << 8);
      if (c >= 0xd800 && c <= 0xdbff) {
        if (++i >= n)
          return false;
        auto lo = bytes[2 * i] | (std::uint32_t(bytes[2 * i + 1]) << 8);
        if (lo < 0xdc00 || lo > 0xdfff)
          return false;
        c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
      } else if (c >= 0xdc00 && c <= 0xdfff)
        return false;
      if (c < 32 || c == 127)
        return false;
      if (c < 0x80)
        out += char(c);
      else if (c < 0x800) {
        out += char(0xc0 | (c >> 6));
        out += char(0x80 | (c & 63));
      } else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 63));
        out += char(0x80 | (c & 63));
      } else {
        out += char(0xf0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 63));
        out += char(0x80 | ((c >> 6) & 63));
        out += char(0x80 | (c & 63));
      }
    }
    return true;
  }
  bool OptionalText(U64 at, std::string &s, std::size_t cap) {
    U64 p{};
    return Get(at, p) && (!p || Utf16(p, s, cap));
  }
  bool Pin(U64 base, U64 offset, std::initializer_list<std::uint8_t> bytes) {
    std::vector<std::uint8_t> got(bytes.size());
    return Bytes(base + offset, got.data(), got.size()) &&
           std::equal(got.begin(), got.end(), bytes.begin(), bytes.end());
  }
  bool CurrentPins(U64 base) {
    // Same native static-root/getter and pause-field pins as the base reader.
    // No exploration, room history or rendered map data is read here.
    if (!Pin(base, 0x1d4bc74,
             {0x88, 0x02, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x08, 0x5d, 0x40,
              0xf9, 0x00, 0x01, 0x40, 0xf9}) ||
        !Pin(base, 0x40d4b44,
             {0x88, 0x02, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x08, 0x5d, 0x40,
              0xf9, 0x00, 0x01, 0x40, 0x39}) ||
        !Pin(base, 0x4107104,
             {0x88, 0x02, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x08, 0x5d, 0x40,
              0xf9, 0x00, 0x01, 0x40, 0x39}) ||
        !Pin(base, 0x40e2d90,
             {0x60, 0x1a, 0x40, 0xf9, 0x00, 0x01, 0x00, 0xb4, 0xe8,
              0x58, 0x01, 0xb0, 0x08, 0x5d, 0x40, 0xf9, 0x02, 0x01,
              0x40, 0xf9, 0xf4, 0x4f, 0x42, 0xa9, 0xf6, 0x57, 0x41,
              0xa9, 0xfd, 0x7b, 0xc3, 0xa8, 0xa8, 0x6b, 0xdd, 0x17}))
      return false;
    for (const auto &[rva, offset] :
         std::array<std::pair<U64, std::uint8_t>, 4>{{{0x1d4ef20, 0x60},
                                                      {0x1d4ef30, 0x64},
                                                      {0x1d4ef40, 0x68},
                                                      {0x1d4efd0, 0x8c}}})
      if (!Pin(base, rva, {0x00, offset, 0x40, 0x39, 0xc0, 0x03, 0x5f, 0xd6}))
        return false;
    return true;
  }
  bool ClearLoading(U64 base, U64 root, const char *name, const char *ns) {
    U64 klass{}, fields{};
    std::uint8_t flag{};
    return Ptr(base + root, klass) && Klass(klass, name, ns) &&
           Ptr(klass + 0xb8, fields) && Get(fields, flag) && flag == 0;
  }
  bool Current(const belmont::Identity &id, const belmont::Snapshot &state) {
    // Explicit freshness gates start a new read phase; do not satisfy them
    // from the earlier copied phase. Their spans remain in final replay.
    span_index.clear();
    U64 klass{}, manager_class{}, fields{}, manager{}, entity{}, initial{},
        loading{}, stack{}, items{}, length{}, tuple{}, active{};
    std::int32_t count{}, version{};
    std::uint8_t destroyed{}, enabled{};
    if (!Ptr(id.base + 0x6d42540, klass) ||
        !Klass(klass, "GameStateManager", "") || !Ptr(klass + 0xb8, fields) ||
        !Ptr(fields, manager) || manager != state.manager ||
        !Ptr(manager, manager_class) || manager_class != klass ||
        !Ptr(manager + 0xb0, entity) || entity != state.entity ||
        !Object(entity, "Entity", "MotherBase.EntityFramework") ||
        !Alive(entity) || !Get(entity + 0x20, destroyed) || destroyed != 0 ||
        !Get(entity + 0x2c8, enabled) || enabled != 1 ||
        !Get(manager + 0x20, initial) || !Get(manager + 0x68, loading) ||
        !Ptr(manager + 0xa0, stack) || !Ptr(stack + 0x10, items) ||
        !Get(stack + 0x18, count) || !Get(stack + 0x1c, version) ||
        !Get(items + 0x18, length) || count <= 0 || count > 128 ||
        length > 128 || U64(count) > length ||
        !Ptr(items + 0x20 + 8 * U64(count - 1), tuple) ||
        !Ptr(tuple + 0x18, active) || active != state.active ||
        !belmont::Pointer(loading) || active == loading ||
        !ClearLoading(id.base, 0x6d3f780, "ApplicationFlowManager",
                      "MotherBase.ApplicationFlowManager") ||
        !ClearLoading(id.base, 0x6d46228, "RoomManagerSystem", "RoomManager"))
      return false;
    if (state.initial_identity == 1 && active == initial)
      return true;
    std::array<std::uint8_t, 14> flags{};
    return state.paused == 1 &&
           Object(active, "EditorParameteredGameState", "") && Alive(active) &&
           Bytes(active + 0x18, flags.data(), flags.size()) &&
           std::all_of(flags.begin(), flags.end(),
                       [](auto b) { return b <= 1; }) &&
           flags[0] == 1 && flags[1] == 1 && flags[2] == 0 && flags[11] == 1;
  }
  bool Component(U64 entity, U64 &component) {
    U64 dict{}, entries{}, capacity{};
    std::int32_t count{}, free{}, version{};
    if (!Ptr(entity + 0x30, dict) || !Get(dict + 0x20, count) || count < 0 ||
        count > 256 || !Get(dict + 0x28, free) || free < 0 || free > count ||
        !Get(dict + 0x2c, version) || !Ptr(dict + 0x18, entries) ||
        !Get(entries + 0x18, capacity) || capacity > 4096 ||
        U64(count) > capacity)
      return false;
    std::int32_t found{};
    std::set<std::int32_t> keys;
    for (std::int32_t i = 0; i < count; ++i) {
      U64 at = entries + 0x20 + 0x18 * U64(i), value{}, klass{};
      std::int32_t hash{}, next{}, key{};
      if (!Get(at, hash) || !Get(at + 4, next) || !Get(at + 8, key) ||
          !Get(at + 0x10, value))
        return false;
      if (hash == -1)
        continue;
      if (hash < 0 || next < -1 || next >= count || !keys.insert(key).second ||
          !Ptr(value, klass))
        return false;
      ++found;
      if (Klass(klass, "ProgressionComponent", "")) {
        if (component)
          return false;
        component = value;
      }
    }
    return found == count - free && component;
  }
  static bool Gameplay(const belmont::Snapshot &s) {
    return s.supported && s.coherent && s.manager_ready && s.entity_ready &&
           s.components_ready && (s.initial_identity == 1 || s.paused == 1) &&
           s.loading_identity == 0 && s.application_loading == 0 &&
           s.room_loading == 0;
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
  Snapshot Sample(const belmont::Identity &id) {
    belmont::Decoder state(read);
    return Sample(id, state.Sample(id));
  }
  Snapshot Sample(const belmont::Identity &id, const belmont::Snapshot &state) {
    journal.clear();
    span_index.clear();
    Snapshot out;
    if (id.title != belmont::Title || id.build != belmont::Build ||
        !belmont::Pointer(id.base) || id.size < 0x6d46230 ||
        id.base > std::numeric_limits<U64>::max() - id.size) {
      out.reason = "unsupported";
      return out;
    }
    out.supported = true;
    if (!Gameplay(state)) {
      out.reason = "non_gameplay";
      return out;
    }
    // Native stores/getters: current objective, cached entity, description,
    // SavableScriptableObject GUID, table and entry reference structures.
    if (!CurrentPins(id.base) ||
        !Pin(id.base, 0x1c33c64, {0x93, 0x26, 0x00, 0xf9}) ||
        !Pin(id.base, 0x1c33a6c, {0x60, 0x2a, 0x40, 0xf9}) ||
        !Pin(id.base, 0x1f1ef84, {0x21, 0x10, 0x40, 0xf9}) ||
        !Pin(id.base, 0x1e0fff0, {0x00, 0x0c, 0x40, 0xf9}) ||
        !Pin(id.base, 0x4af6f70, {0x00, 0x84, 0x40, 0xad}) ||
        !Pin(id.base, 0x4af72c0, {0x00, 0x0c, 0xc0, 0x3d}) ||
        !Pin(id.base, 0x4b03990, {0x08, 0x04, 0x41, 0xa9}) ||
        !Pin(id.base, 0x4b052d0, {0x00, 0x00, 0x40, 0xf9})) {
      out.reason = "instruction_mismatch";
      return out;
    }
    if (!Current(id, state)) {
      out.reason = "stale_state";
      return out;
    }
    U64 entity{}, guid{};
    Snapshot value;
    value.supported = true;
    if (!Component(state.entity, value.component) || !Alive(value.component) ||
        !Get(value.component + 0x50, entity) || entity != state.entity ||
        !Ptr(value.component + 0x48, value.objective) ||
        !Object(value.objective, "MainObjective", "") ||
        !Alive(value.objective) || !Ptr(value.objective + 0x18, guid) ||
        !Utf16(guid, value.objective_guid, 128) ||
        value.objective_guid.empty() ||
        !Get(value.objective + 0x28, value.acquisition_mode) ||
        value.acquisition_mode < 0 || value.acquisition_mode > 1 ||
        !Ptr(value.objective + 0x20, value.localized_string) ||
        !Object(value.localized_string, "LocalizedString",
                "UnityEngine.Localization")) {
      out.reason = "objective_unavailable";
      return out;
    }
    auto &ref = value.description;
    const auto loc = value.localized_string;
    std::uint8_t table_valid{}, entry_valid{};
    if (!Get(loc + 0x18, table_valid) || table_valid > 1 ||
        !Get(loc + 0x1c, ref.table_type) || ref.table_type < 1 ||
        ref.table_type > 2 ||
        !Bytes(loc + 0x20, ref.table_guid.data(), ref.table_guid.size()) ||
        !OptionalText(loc + 0x10, ref.table_name, 256) ||
        !Get(loc + 0x30, ref.entry_id) ||
        !OptionalText(loc + 0x38, ref.entry_key, 256) ||
        !Get(loc + 0x40, entry_valid) || entry_valid > 1 ||
        !Get(loc + 0x44, ref.entry_type) || ref.entry_type < 1 ||
        ref.entry_type > 2 ||
        (ref.table_type == 1 &&
         std::all_of(ref.table_guid.begin(), ref.table_guid.end(),
                     [](auto b) { return b == 0; })) ||
        (ref.table_type == 2 && ref.table_name.empty()) ||
        (ref.entry_type == 2 && ref.entry_id <= 0) ||
        (ref.entry_type == 1 && ref.entry_key.empty())) {
      out.reason = "localization_unavailable";
      return out;
    }
    // Only the bounded native gameplay/entity chain is revalidated. The
    // supplied coherent base snapshot already decoded exploration history;
    // objective sampling must never traverse it a second time.
    if (!Current(id, state) || !Coherent()) {
      out.reason = "incoherent";
      return out;
    }
    value.coherent = true;
    value.ready = true;
    value.reason = "ready";
    return value;
  }
};
} // namespace belmont_objectives
