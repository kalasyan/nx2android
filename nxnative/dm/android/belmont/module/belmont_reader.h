// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Build-pinned read-only diagnostic prototype. State identity is provisional;
// map/marker output stays unavailable until native liveness and projection are
// verified.
#include "belmont_fast_travel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>
#include <unordered_map>

namespace belmont {
using U64 = std::uint64_t;
struct ReadSpanKey {
  U64 at{};
  std::size_t size{};
  bool operator==(const ReadSpanKey&) const = default;
};
struct ReadSpanHash {
  std::size_t operator()(const ReadSpanKey& key) const {
    return std::hash<U64>{}(key.at) ^ (std::hash<std::size_t>{}(key.size) << 1);
  }
};
using ReadSpanIndex = std::unordered_map<ReadSpanKey,std::size_t,ReadSpanHash>;
inline constexpr U64 Title = 0x0100BA4026E94000ULL;
inline constexpr char BuildHex[] =
    "0F1169AA7D0FA4529C861F96ADEC306E00000000000000000000000000000000";
inline constexpr std::array<std::uint8_t, 32> Build{
    0x0f, 0x11, 0x69, 0xaa, 0x7d, 0x0f, 0xa4, 0x52,
    0x9c, 0x86, 0x1f, 0x96, 0xad, 0xec, 0x30, 0x6e};
inline bool Supports(const char *text) {
  if (!text || std::strlen(text) != 64)
    return false;
  for (unsigned i = 0; i < 64; ++i) {
    char c = text[i];
    if (c >= 'a' && c <= 'f')
      c -= 32;
    if (c != BuildHex[i])
      return false;
  }
  return true;
}
struct Identity {
  U64 title{}, base{}, size{};
  std::array<std::uint8_t, 32> build{};
};
struct ExplorationRoom {
  std::string room_id;
  std::int32_t width{}, height{};
  bool pending_validation{};
  // Optional native savepoint metadata: -1 unknown, 0 false, 1 true.
  int is_save_room{-1}, used_save_point{-1};
  // Exact native enum values: 0 NoChunk, 1 Unexplored, 2 Temporary, 3
  // Validated. An absent/unavailable room has no record, never a fabricated
  // zero mask.
  std::vector<std::int32_t> cells;
  bool entered_zones_ready{};
  std::vector<std::int32_t> entered_zone_ids;
};
struct Snapshot {
  bool supported{}, coherent{}, manager_ready{}, entity_ready{};
  int application_loading{-1}, room_loading{-1}, account_initialized{-1};
  int initial_identity{-1}, loading_identity{-1}, paused{-1};
  U64 manager{}, active{}, entity{};
  bool components_ready{}, explorer_candidate{}, room_candidate{},
      position_candidate{};
  int exploration_count{-1}, room_state{-1};
  std::array<float, 3> target_position{};
  std::string room_id;
  std::string reason{"unavailable"};
  bool exploration_ready{}, exploration_zones_ready{};
  bool landmarks_ready{};
  bool unlocked_fast_travel_ready{};
  std::vector<std::string> unlocked_fast_travel_levels;
  bool last_used_save_room_ready{};
  std::string last_used_save_room;
  std::vector<std::string> discovered_landmarks;
  std::vector<ExplorationRoom> exploration;
  // Bounded fast-path provenance. Populated only by this decoder; the caller
  // must retain an unchanged full sample and cap its age (normally 100 ms).
  struct MotionContract {
    U64 base{}, dictionary{}, entries{}, explorer{}, room{}, position{}, layout_hash{};
    std::int32_t count{}, live{}, version{};
    bool operator==(const MotionContract &) const = default;
  } motion;
  bool motion_only{};
  // Static/synthetic evidence cannot promote a gameplay map to ready.
  bool map_ready{};
  // No map or last-good cache: unavailable never means empty/revealed.
};
inline bool Pointer(U64 p) {
  return p >= 0x10000 && p < 0x8000000000ULL && (p & 7) == 0;
}

// Every successful read is journalled then compared after decoding. This also
// checks TypeInfo resolution, class identity, roots, stack headers and entity
// flags. Policy caps are reader limits, not alleged game maxima.
template <class Read> class Decoder {
  Read &read;
  belmont_fast_travel::OptionalReader<Read> fast_travel_reader;
  U64 base{};
  struct Span {
    U64 at;
    std::vector<std::uint8_t> bytes;
  };
  std::vector<Span> journal;
  // Reuse only exact spans inside this decode. Final coherence validation
  // still reads every unique span from the guest; nothing survives Sample.
  belmont::ReadSpanIndex span_index;
  std::vector<Span> optional_journal;
  bool savepoint_fields_available{};
  bool OptionalBytes(U64 at,void* dst,std::size_t n){
    if(!n||n>4096||at<0x10000||at>std::numeric_limits<U64>::max()-n||
       optional_journal.size()>=8192||!read(at,dst,n))return false;
    const auto* bytes=static_cast<const std::uint8_t*>(dst);
    optional_journal.push_back({at,{bytes,bytes+n}});return true;
  }
  template<class T> bool OptionalGet(U64 at,T& value){
    std::array<std::uint8_t,sizeof(T)> bytes{};
    if(!OptionalBytes(at,bytes.data(),bytes.size()))return false;
    U64 bits{};for(unsigned n=0;n<sizeof(T);++n)bits|=U64(bytes[n])<<(8*n);
    std::memcpy(&value,&bits,sizeof(T));return true;
  }
  bool OptionalPins(){
    const auto pin=[&](U64 rva,std::array<std::uint8_t,4> expected){
      std::array<std::uint8_t,4> actual{};
      return OptionalBytes(base+rva,actual.data(),actual.size())&&actual==expected;
    };
    return pin(0x1c28f7c,{0x88,0x02,0x01,0x39})&&
           pin(0x1c2b574,{0x08,0x05,0x41,0x39})&&
           pin(0x1dedc10,{0x00,0x45,0x40,0xf9});
  }
  bool OptionalCoherent(){
    std::array<std::uint8_t,4096> bytes{};
    for(const auto& span:optional_journal)
      if(!read(span.at,bytes.data(),span.bytes.size())||
         std::memcmp(bytes.data(),span.bytes.data(),span.bytes.size())!=0)return false;
    return true;
  }
  bool OptionalString(U64 address,std::string& value){
    U64 object{},klass{},name{},space{};
    if(!OptionalGet(address,object))return false;
    if(!object){value.clear();return true;}
    if(!Pointer(object)||!OptionalGet(object,klass)||!Pointer(klass)||
       !OptionalGet(klass+0x10,name)||name<0x10000||name>=0x8000000000ULL||
       !OptionalGet(klass+0x18,space)||space<0x10000||space>=0x8000000000ULL)return false;
    std::array<char,7> type{},ns{};
    if(!OptionalBytes(name,type.data(),type.size())||
       !OptionalBytes(space,ns.data(),ns.size())||
       std::memcmp(type.data(),"String",7)||std::memcmp(ns.data(),"System",7))return false;
    return Utf16(object,value,true);
  }
  bool Bytes(U64 at, void *out, std::size_t n) {
    if (!n || n > 4096 || at < 0x10000 ||
        at > std::numeric_limits<U64>::max() - n || journal.size() >= 16384)
      return false;
    if (auto found=span_index.find({at,n});found!=span_index.end()) {
      const auto& saved=journal[found->second].bytes;
      std::memcpy(out,saved.data(),n);
      return true;
    }
    if (!read(at, out, n))
      return false;
    const auto *p = static_cast<const std::uint8_t *>(out);
    span_index.emplace(belmont::ReadSpanKey{at,n},journal.size());
    journal.push_back({at, {p, p + n}});
    return true;
  }
  template <class T> bool Get(U64 at, T &out) {
    static_assert(std::is_integral_v<T>);
    std::array<std::uint8_t, sizeof(T)> bytes{};
    if (!Bytes(at, bytes.data(), bytes.size()))
      return false;
    U64 value = 0;
    for (unsigned i = 0; i < sizeof(T); ++i)
      value |= U64(bytes[i]) << (i * 8);
    std::memcpy(&out, &value, sizeof(T));
    return true;
  }
  bool Ptr(U64 at, U64 &out) { return Get(at, out) && Pointer(out); }
  bool Text(U64 at, const char *expected) {
    const auto n = std::strlen(expected) + 1;
    std::array<char, 128> bytes{};
    return n <= bytes.size() && Bytes(at, bytes.data(), n) &&
           std::memcmp(bytes.data(), expected, n) == 0;
  }
  bool Klass(U64 klass, const char *name, const char *ns) {
    U64 n{}, s{};
    return Pointer(klass) && Get(klass + 0x10, n) && Get(klass + 0x18, s) &&
           Text(n, name) && Text(s, ns);
  }
  bool Object(U64 object, const char *name, const char *ns) {
    U64 klass{};
    return Pointer(object) && Ptr(object, klass) && Klass(klass, name, ns);
  }
  bool NativeAlive(U64 object) {
    U64 native{};
    std::uint8_t readable{};
    // Reading proves the native address is readable through the caller's
    // guest-memory mapping. Its contents are not a Behaviour enabled flag.
    return Ptr(object + 0x10, native) && Bytes(native, &readable, 1);
  }
  bool Static(U64 base, U64 offset, const char *name, const char *ns,
              U64 &fields, U64 *out_class = nullptr) {
    U64 klass{};
    if (!Ptr(base + offset, klass) || !Klass(klass, name, ns) ||
        !Ptr(klass + 0xb8, fields))
      return false;
    if (out_class)
      *out_class = klass;
    return true;
  }
  int Flag(U64 base, U64 offset, const char *name, const char *ns) {
    U64 fields{};
    std::uint8_t value{};
    return Static(base, offset, name, ns, fields) && Get(fields, value) &&
                   value <= 1
               ? value
               : -1;
  }
  bool Utf16(U64 at, std::string &text, bool optional=false) {
    std::int32_t n{};
    if (!Pointer(at) || !(optional?OptionalGet(at+0x10,n):Get(at+0x10,n)) || n < 0 || n > 128)
      return false;
    std::vector<std::uint8_t> bytes(std::size_t(n) * 2);
    if (n && !(optional?OptionalBytes(at+0x14,bytes.data(),bytes.size()):Bytes(at+0x14,bytes.data(),bytes.size())))
      return false;
    text.clear();
    for (int i = 0; i < n; ++i) {
      std::uint32_t cp = bytes[2 * i] | (std::uint32_t(bytes[2 * i + 1]) << 8);
      if (cp >= 0xd800 && cp <= 0xdbff) {
        if (++i >= n)
          return false;
        const auto low = bytes[2 * i] | (std::uint32_t(bytes[2 * i + 1]) << 8);
        if (low < 0xdc00 || low > 0xdfff)
          return false;
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
      } else if (cp >= 0xdc00 && cp <= 0xdfff)
        return false;
      if (cp < 0x80)
        text += char(cp);
      else if (cp < 0x800) {
        text += char(0xc0 | (cp >> 6));
        text += char(0x80 | (cp & 63));
      } else if (cp < 0x10000) {
        text += char(0xe0 | (cp >> 12));
        text += char(0x80 | ((cp >> 6) & 63));
        text += char(0x80 | (cp & 63));
      } else {
        text += char(0xf0 | (cp >> 18));
        text += char(0x80 | ((cp >> 12) & 63));
        text += char(0x80 | ((cp >> 6) & 63));
        text += char(0x80 | (cp & 63));
      }
    }
    return true;
  }
  bool Dictionary(U64 dict, U64 &entries, std::int32_t &count,
                  std::int32_t &live, std::int32_t limit = 256) {
    std::int32_t free{};
    U64 capacity{};
    if (!Pointer(dict) || !Get(dict + 0x20, count) || !Get(dict + 0x28, free) ||
        count < 0 || count > limit || free < 0 || free > count)
      return false;
    // A fresh empty dictionary can legitimately have a null entries array.
    std::int32_t version{};
    span_index.erase(belmont::ReadSpanKey{dict + 0x2c, sizeof(version)});
    if (!Get(dict + 0x2c, version) || !Get(dict + 0x18, entries))
      return false;
    if (entries == 0 && count == 0) {
      live = 0;
      return true;
    }
    if (!Pointer(entries) || !Get(entries + 0x18, capacity) ||
        capacity > 4096 || U64(count) > capacity)
      return false;
    live = count - free;
    return true;
  }
  bool ZoneIds(U64 state, std::vector<std::int32_t> &result) {
    result.clear();
    // Native WorldRoomExplorationState.SetEnteredZone proves +50,
    // List<int> items +10, size +18, version +1c and 4-byte array elements.
    U64 list{}, items{}, capacity{};
    std::int32_t count{}, version{};
    if (!Ptr(state + 0x50, list) ||
        !Object(list, "List`1", "System.Collections.Generic") ||
        !Get(list + 0x18, count) || count < 0 || count > 4096 ||
        !Get(list + 0x1c, version) || !Get(list + 0x10, items))
      return false;
    if (items == 0 && count == 0) {
      result.clear();
      return true;
    }
    if (!Pointer(items) || !Get(items + 0x18, capacity) || capacity > 8192 ||
        U64(count) > capacity)
      return false;
    std::vector<std::int32_t> ids;
    ids.reserve(count);
    std::array<std::uint8_t, 4096> bytes{};
    for (std::size_t start = 0; start < std::size_t(count);) {
      const auto n = std::min<std::size_t>(1024, std::size_t(count) - start);
      if (!Bytes(items + 0x20 + 4 * U64(start), bytes.data(), n * 4))
        return false;
      for (std::size_t i = 0; i < n; ++i) {
        const auto *b = bytes.data() + i * 4;
        const std::uint32_t id =
            std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 |
            std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
        if (id > 65535 ||
            std::find(ids.begin(), ids.end(), std::int32_t(id)) != ids.end())
          return false;
        ids.push_back(std::int32_t(id));
      }
      start += n;
    }
    result = std::move(ids);
    return true;
  }
  bool DiscoveredLandmarks(U64 explorer, std::vector<std::string> &result) {
    // Native discoveredLandmarks getter and SavableScriptableObject.guid
    // getter.
    constexpr std::array<std::uint8_t, 8> list_pin{0x00, 0x30, 0x40, 0xf9,
                                                   0xc0, 0x03, 0x5f, 0xd6};
    constexpr std::array<std::uint8_t, 8> guid_pin{0x00, 0x0c, 0x40, 0xf9,
                                                   0xc0, 0x03, 0x5f, 0xd6};
    std::array<std::uint8_t, 8> code{};
    if (!Bytes(base + 0x1c28230, code.data(), code.size()) ||
        code != list_pin ||
        !Bytes(base + 0x1e0fff0, code.data(), code.size()) || code != guid_pin)
      return false;
    U64 list{}, items{}, capacity{};
    std::int32_t count{}, version{};
    std::uint8_t debug{};
    if (!Get(explorer + 0xa8, debug) || debug != 0 ||
        !Ptr(explorer + 0x60, list) ||
        !Object(list, "List`1", "System.Collections.Generic") ||
        !Get(list + 0x18, count) || count < 0 || count > 1024 ||
        !Get(list + 0x1c, version) || !Get(list + 0x10, items))
      return false;
    if (count == 0 && items == 0) {
      result.clear();
      return true;
    }
    if (!Pointer(items) || !Get(items + 0x18, capacity) || capacity > 4096 ||
        U64(count) > capacity)
      return false;
    std::vector<std::string> ids;
    for (std::int32_t n = 0; n < count; ++n) {
      U64 asset{}, guid{};
      std::string text;
      if (!Ptr(items + 0x20 + 8 * U64(n), asset) ||
          !Object(asset, "LandmarkData", "") || !NativeAlive(asset) ||
          !Ptr(asset + 0x18, guid) || !Object(guid, "String", "System") ||
          !Utf16(guid, text) || text.size() != 32 ||
          !std::all_of(text.begin(), text.end(),
                       [](char c) {
                         return (c >= '0' && c <= '9') ||
                                (c >= 'a' && c <= 'f');
                       }) ||
          std::find(ids.begin(), ids.end(), text) != ids.end())
        return false;
      ids.push_back(std::move(text));
    }
    result = std::move(ids);
    return true;
  }
  bool Exploration(U64 dict, std::vector<ExplorationRoom> &result) {
    U64 entries{};
    std::int32_t count{}, live{}, found{};
    if (!Dictionary(dict, entries, count, live, 4096))
      return false;
    std::vector<ExplorationRoom> decoded;
    std::size_t total_cells{};
    for (std::int32_t i = 0; i < count; ++i) {
      const U64 at = entries + 0x20 + 0x18 * U64(i);
      std::int32_t hash{}, next{};
      U64 key{}, state{};
      if (!Get(at, hash))
        return false;
      if (hash == -1)
        continue;
      if (hash < 0 || !Get(at + 4, next) || next < -1 || next >= count ||
          !Ptr(at + 8, key) || !Object(key, "String", "System") ||
          !Ptr(at + 0x10, state) ||
          !Object(state, "WorldRoomExplorationState", ""))
        return false;
      ExplorationRoom room;
      U64 cells{}, length{};
      std::uint8_t pending{};
      if (!Utf16(key, room.room_id) || room.room_id.empty() ||
          std::any_of(decoded.begin(), decoded.end(),
                      [&](const auto &prior) {
                        return prior.room_id == room.room_id;
                      }) ||
          !Get(state + 0x10, room.width) || !Get(state + 0x14, room.height) ||
          room.width <= 0 || room.height <= 0 || room.width > 256 ||
          room.height > 256 || !Ptr(state + 0x18, cells) ||
          !Get(cells + 0x18, length) || length > 4096 ||
          U64(room.width) * U64(room.height) != length ||
          !Get(state + 0x20, pending) || pending > 1 ||
          total_cells > 65536 - static_cast<std::size_t>(length))
        return false;
      room.pending_validation = pending != 0;
      if(savepoint_fields_available){
        std::array<std::uint8_t,2> flags{};
        if(OptionalBytes(state+0x40,flags.data(),flags.size())&&flags[0]<=1&&flags[1]<=1){
          room.is_save_room=flags[0];room.used_save_point=flags[1];
        }
      }
      room.cells.resize(static_cast<std::size_t>(length));
      // Copy bounded spans once and journal the same spans. Per-cell host
      // calls would magnify sampling cost and the moving-game race window.
      std::array<std::uint8_t, 4096> raw{};
      for (std::size_t start = 0; start < room.cells.size();) {
        const auto n = std::min<std::size_t>(1024, room.cells.size() - start);
        if (!Bytes(cells + 0x20 + 4 * U64(start), raw.data(), 4 * n))
          return false;
        for (std::size_t j = 0; j < n; ++j) {
          const auto *b = raw.data() + 4 * j;
          const std::uint32_t value =
              std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) |
              (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
          if (value > 3)
            return false;
          room.cells[start + j] = static_cast<std::int32_t>(value);
        }
        start += n;
      }
      total_cells += room.cells.size();
      room.entered_zones_ready = ZoneIds(state, room.entered_zone_ids);
      decoded.push_back(std::move(room));
      ++found;
    }
    if (found != live)
      return false;
    result = std::move(decoded);
    return true;
  }
  bool Components(U64 entity, Snapshot &out, const Snapshot *history) {
    U64 dict{}, entries{};
    std::int32_t count{}, live{};
    if (!Ptr(entity + 0x30, dict) || !Dictionary(dict, entries, count, live))
      return false;
    U64 explorer{}, room{}, position{};
    std::int32_t found{};
    U64 layout_hash = 14695981039346656037ULL;
    const auto mix_layout = [&](U64 value) {
      layout_hash = (layout_hash ^ value) * 1099511628211ULL;
    };
    std::vector<std::int32_t> keys;
    for (std::int32_t i = 0; i < count; ++i) {
      const auto at = entries + 0x20 + 0x18 * U64(i);
      std::int32_t hash{}, next{}, key{};
      U64 value{}, klass{};
      if (!Get(at, hash) || !Get(at + 4, next) || !Get(at + 8, key) ||
          !Get(at + 0x10, value))
        return false;
      mix_layout(U64(hash)); mix_layout(U64(next)); mix_layout(U64(key));
      mix_layout(value);
      if (hash < 0)
        continue; // Removed entry: native Remove writes hash=-1, value=0.
      if (next < -1 || next >= count || !Pointer(value) || !Ptr(value, klass))
        return false;
      mix_layout(klass);
      if (std::find(keys.begin(), keys.end(), key) != keys.end())
        return false;
      keys.push_back(key);
      ++found;
      const auto candidate = [&](const char *name, const char *ns, U64 &slot) {
        if (!Klass(klass, name, ns))
          return true;
        if (slot)
          return false; // Duplicate expected type is ambiguous.
        slot = value;
        return true;
      };
      if (!candidate("ExplorerComponent", "", explorer) ||
          !candidate("CurrentRoomInstance", "", room) ||
          !candidate("InterpolatedTransform", "MotherBase.ActionFramework",
                     position))
        return false;
    }
    if (found != live)
      return false;
    std::int32_t dictionary_version{};
    span_index.erase(belmont::ReadSpanKey{dict + 0x2c, sizeof(dictionary_version)});
    if (!Get(dict + 0x2c, dictionary_version)) return false;
    const Snapshot::MotionContract current{base, dict, entries, explorer, room,
        position, layout_hash, count, live, dictionary_version};
    if (history && (!history->supported || !history->coherent || history->motion_only ||
        !history->components_ready || history->motion != current ||
        history->manager != out.manager || history->entity != entity))
      return false;
    out.motion = current;
    // Class/namespace and uniqueness establish candidates only. No projection
    // or Unity destroyed-object equality is inferred from these diagnostics.
    if (explorer) {
      U64 self{}, exploration{}, e{};
      std::int32_t allocated{}, n{};
      if (NativeAlive(explorer) && Get(explorer + 0x20, self) &&
          self == entity && Ptr(explorer + 0x30, exploration) &&
          Dictionary(exploration, e, allocated, n, 4096)) {
        out.explorer_candidate = true;
        if(!history&&savepoint_fields_available)
          out.last_used_save_room_ready=OptionalString(explorer+0x88,out.last_used_save_room);
        if(!history)out.unlocked_fast_travel_ready=fast_travel_reader.Sample(base,explorer,entity,out.unlocked_fast_travel_levels);
        if (!history)
          out.landmarks_ready =
              DiscoveredLandmarks(explorer, out.discovered_landmarks);
        out.exploration_count = n;
      }
    }
    if (room) {
      U64 instance{}, world{}, id{};
      std::int32_t state{};
      std::string text;
      if (NativeAlive(room) && Get(room + 0x20, state) && state >= 0 &&
          state <= 2) {
        out.room_state = state;
        if (state == 0 && Ptr(room + 0x28, instance) &&
            Object(instance, "RoomInstance", "MotherBase.RoomDesigner") &&
            NativeAlive(instance) && Ptr(instance + 0x20, world) &&
            Object(world, "WorldRoomData", "MotherBase.RoomDesigner") &&
            Ptr(world + 0x48, id) && Object(id, "String", "System") &&
            Utf16(id, text) && !text.empty()) {
          out.room_candidate = true;
          out.room_id = std::move(text);
        }
      }
    }
    if (position && NativeAlive(position)) {
      std::array<float, 3> coords{};
      bool valid = true;
      for (unsigned i = 0; i < 3; ++i) {
        std::uint32_t bits{};
        if (!Get(position + 0x40 + 4 * i, bits)) {
          valid = false;
          break;
        }
        std::memcpy(&coords[i], &bits, 4);
        if (!std::isfinite(coords[i]))
          valid = false;
      }
      if (valid) {
        out.position_candidate = true;
        out.target_position = coords;
      }
    }
    if (history) {
      std::uint8_t debug{};
      if (!out.explorer_candidate || !out.room_candidate ||
          !out.position_candidate || out.room_id != history->room_id ||
          !Get(explorer + 0xa8, debug) || debug != 0) {
        return false;
      }
      out.exploration_ready = history->exploration_ready;
      out.exploration_zones_ready = history->exploration_zones_ready;
      out.exploration = history->exploration;
      out.landmarks_ready = history->landmarks_ready;
      out.discovered_landmarks = history->discovered_landmarks;
      out.unlocked_fast_travel_ready=history->unlocked_fast_travel_ready;
      out.unlocked_fast_travel_levels=history->unlocked_fast_travel_levels;
      out.last_used_save_room_ready=history->last_used_save_room_ready;
      out.last_used_save_room=history->last_used_save_room;
      out.motion_only = true;
      return true;
    }
    if (out.explorer_candidate && out.room_candidate && out.room_state == 0) {
      U64 exploration{};
      std::vector<ExplorationRoom> decoded;
      if (Ptr(explorer + 0x30, exploration) &&
          Exploration(exploration, decoded)) {
        out.exploration_ready = true;
        out.exploration_zones_ready =
            std::all_of(decoded.begin(), decoded.end(),
                        [](const auto &r) { return r.entered_zones_ready; });
        out.exploration = std::move(decoded);
      }
    }
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

  Snapshot Decode(const Identity &id, const Snapshot *history) {
    base = id.base;
    journal.clear();
    span_index.clear();
    fast_travel_reader.Clear();
    optional_journal.clear();savepoint_fields_available=false;
    Snapshot out;
    if (id.title != Title || id.build != Build || !Pointer(id.base) ||
        id.size < 0x6d46230 ||
        id.base > std::numeric_limits<U64>::max() - id.size) {
      out.reason = "unsupported_build";
      return out;
    }
    out.supported = true;
    // Exact original accessor instructions pin each static-root contract in
    // addition to the full build ID. No game calls or metadata initialization.
    constexpr std::array<std::uint8_t, 16> pin0{
        0x88, 0x2,  0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9,
        0x8,  0x5d, 0x40, 0xf9, 0x0,  0x1,  0x40, 0xf9};
    std::array<std::uint8_t, 16> code0{};
    if (!Bytes(id.base + 0x1d4bc74, code0.data(), code0.size()) ||
        code0 != pin0) {
      out.reason = "accessor_mismatch";
      return out;
    }
    constexpr std::array<std::uint8_t, 16> pin1{
        0x88, 0x2,  0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9,
        0x8,  0x5d, 0x40, 0xf9, 0x0,  0x1,  0x40, 0x39};
    std::array<std::uint8_t, 16> code1{};
    if (!Bytes(id.base + 0x40d4b44, code1.data(), code1.size()) ||
        code1 != pin1) {
      out.reason = "accessor_mismatch";
      return out;
    }
    constexpr std::array<std::uint8_t, 12> pin2{
        0x8, 0x5c, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x0, 0x1, 0x40, 0x39};
    std::array<std::uint8_t, 12> code2{};
    if (!Bytes(id.base + 0x1f7e0bc, code2.data(), code2.size()) ||
        code2 != pin2) {
      out.reason = "accessor_mismatch";
      return out;
    }
    constexpr std::array<std::uint8_t, 16> pin3{
        0x88, 0x2,  0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9,
        0x8,  0x5d, 0x40, 0xf9, 0x0,  0x1,  0x40, 0x39};
    std::array<std::uint8_t, 16> code3{};
    if (!Bytes(id.base + 0x4107104, code3.data(), code3.size()) ||
        code3 != pin3) {
      out.reason = "accessor_mismatch";
      return out;
    }
    constexpr std::array<std::uint8_t, 36> pin4{
        0x60, 0x1a, 0x40, 0xf9, 0x0,  0x1,  0x0,  0xb4, 0xe8, 0x58, 0x1,  0xb0,
        0x8,  0x5d, 0x40, 0xf9, 0x2,  0x1,  0x40, 0xf9, 0xf4, 0x4f, 0x42, 0xa9,
        0xf6, 0x57, 0x41, 0xa9, 0xfd, 0x7b, 0xc3, 0xa8, 0xa8, 0x6b, 0xdd, 0x17};
    std::array<std::uint8_t, 36> code4{};
    if (!Bytes(id.base + 0x40e2d90, code4.data(), code4.size()) ||
        code4 != pin4) {
      out.reason = "accessor_mismatch";
      return out;
    }
    constexpr std::array<std::uint8_t, 12> pin5{
        0x9, 0x3, 0x80, 0x52, 0x8, 0x20, 0xa9, 0x9b, 0x0, 0x19, 0x40, 0xf9};
    std::array<std::uint8_t, 12> code5{};
    if (!Bytes(id.base + 0x383dc94, code5.data(), code5.size()) ||
        code5 != pin5) {
      out.reason = "accessor_mismatch";
      return out;
    }
    constexpr std::array<std::uint8_t, 20> pin6{
        0x48, 0x3,  0x0,  0xb9, 0xe8, 0x26, 0x40, 0xb9, 0x69, 0x73,
        0xa9, 0x9b, 0x20, 0x0,  0x80, 0x52, 0x3f, 0x9,  0x0,  0xf9};
    std::array<std::uint8_t, 20> code6{};
    if (!Bytes(id.base + 0x383f714, code6.data(), code6.size()) ||
        code6 != pin6) {
      out.reason = "accessor_mismatch";
      return out;
    }
    constexpr std::array<std::uint8_t, 12> reference_dictionary_pin{
        0x09, 0x03, 0x80, 0x52, 0x08, 0x20, 0xa9, 0x9b, 0x00, 0x19, 0x40, 0xf9};
    std::array<std::uint8_t, 12> reference_dictionary_code{};
    constexpr std::array<std::uint8_t, 8> enum_array_pin{
        0x48, 0xc9, 0x28, 0x8b, 0x02, 0x21, 0x00, 0xb9};
    std::array<std::uint8_t, 8> enum_array_code{};
    if (!Bytes(id.base + 0x3929c7c, reference_dictionary_code.data(),
               reference_dictionary_code.size()) ||
        reference_dictionary_code != reference_dictionary_pin ||
        !Bytes(id.base + 0x1c29074, enum_array_code.data(),
               enum_array_code.size()) ||
        enum_array_code != enum_array_pin) {
      out.reason = "accessor_mismatch";
      return out;
    }
    // Original WorldRoomExplorationState.SetEnteredZone/EnteredZone: +50
    constexpr std::array<std::uint8_t, 8> entered_zones_field_pin{
        0x80, 0x2a, 0x40, 0xf9, 0x20, 0x01, 0x00, 0xb4};
    std::array<std::uint8_t, 8> entered_zones_field_code{};
    if (!Bytes(id.base + 0x1c298a8, entered_zones_field_code.data(),
               entered_zones_field_code.size()) ||
        entered_zones_field_code != entered_zones_field_pin) {
      out.reason = "accessor_mismatch";
      return out;
    }
    // SetEnteredZone: version+1c, items+10, size+18, array length+18.
    constexpr std::array<std::uint8_t, 32> entered_zones_list_pin{
        0x09, 0x1c, 0x40, 0xb9, 0x29, 0x05, 0x00, 0x11, 0x08, 0x01, 0x40,
        0xf9, 0x09, 0x1c, 0x00, 0xb9, 0x09, 0x08, 0x40, 0xf9, 0xa9, 0x02,
        0x00, 0xb4, 0x0a, 0x18, 0x80, 0xb9, 0x2b, 0x19, 0x40, 0xb9};
    std::array<std::uint8_t, 32> entered_zones_list_code{};
    if (!Bytes(id.base + 0x1c296b0, entered_zones_list_code.data(),
               entered_zones_list_code.size()) ||
        entered_zones_list_code != entered_zones_list_pin) {
      out.reason = "accessor_mismatch";
      return out;
    }
    // SetEnteredZone: 4-byte elements at array+20.
    constexpr std::array<std::uint8_t, 8> entered_zones_element_pin{
        0x28, 0x09, 0x0a, 0x8b, 0x13, 0x21, 0x00, 0xb9};
    std::array<std::uint8_t, 8> entered_zones_element_code{};
    if (!Bytes(id.base + 0x1c296e0, entered_zones_element_code.data(),
               entered_zones_element_code.size()) ||
        entered_zones_element_code != entered_zones_element_pin) {
      out.reason = "accessor_mismatch";
      return out;
    }
    // Native pause flags, not a guessed menu/class-name identity. These
    // accessor instructions prove the serialized bytes below.
    for (const auto &[offset, second] :
         std::array<std::pair<U64, std::uint8_t>, 4>{{{0x1d4ef20, 0x60},
                                                      {0x1d4ef30, 0x64},
                                                      {0x1d4ef40, 0x68},
                                                      {0x1d4efd0, 0x8c}}}) {
      const std::array<std::uint8_t, 8> expected{0x00, second, 0x40, 0x39,
                                                 0xc0, 0x03,   0x5f, 0xd6};
      std::array<std::uint8_t, 8> code{};
      if (!Bytes(id.base + offset, code.data(), code.size()) ||
          code != expected) {
        out.reason = "accessor_mismatch";
        return out;
      }
    }
    out.application_loading = Flag(id.base, 0x6d3f780, "ApplicationFlowManager",
                                   "MotherBase.ApplicationFlowManager");
    out.room_loading =
        Flag(id.base, 0x6d46228, "RoomManagerSystem", "RoomManager");
    out.account_initialized =
        Flag(id.base, 0x6d44b20, "NXAccount", "MotherBase.GameServices");
    U64 fields{}, manager_class{}, object_class{};
    if (Static(id.base, 0x6d42540, "GameStateManager", "", fields,
               &manager_class) &&
        Ptr(fields, out.manager) && Ptr(out.manager, object_class) &&
        object_class == manager_class) {
      out.manager_ready = true;
      U64 initial{}, loading{}, stack{}, array{}, tuple{}, length{};
      std::int32_t count{};
      if (Get(out.manager + 0x20, initial) &&
          Get(out.manager + 0x68, loading) && Ptr(out.manager + 0xa0, stack) &&
          Ptr(stack + 0x10, array) && Get(stack + 0x18, count) &&
          Get(array + 0x18, length) && count > 0 && count <= 128 &&
          length <= 128 && U64(count) <= length &&
          Ptr(array + 0x20 + 8 * U64(count - 1), tuple) &&
          Ptr(tuple + 0x18, out.active)) {
        // Non-null pointer identity only, not Unity Object equality/liveness.
        out.initial_identity =
            Pointer(initial) ? int(out.active == initial) : -1;
        out.loading_identity =
            Pointer(loading) ? int(out.active == loading) : -1;
      }
      if (Object(out.active, "EditorParameteredGameState", "") &&
          NativeAlive(out.active)) {
        std::array<std::uint8_t, 14> flags{};
        if (Bytes(out.active + 0x18, flags.data(), flags.size()) &&
            std::all_of(flags.begin(), flags.end(),
                        [](auto b) { return b <= 1; }))
          out.paused =
              flags[0] == 1 && flags[1] == 1 && flags[2] == 0 && flags[11] == 1;
      }
      if (Get(out.manager + 0xb0, out.entity) &&
          Object(out.entity, "Entity", "MotherBase.EntityFramework")) {
        std::uint8_t destroyed{}, enabled{};
        out.entity_ready = Get(out.entity + 0x20, destroyed) &&
                           Get(out.entity + 0x2c8, enabled) && destroyed == 0 &&
                           enabled == 1 && NativeAlive(out.entity);
      }
    }
    if (out.entity_ready && (out.initial_identity == 1 || out.paused == 1) &&
        out.loading_identity == 0 && out.application_loading == 0 &&
        out.room_loading == 0)
      {
        if(!history)savepoint_fields_available=OptionalPins();
        out.components_ready = Components(out.entity,out,history);
      }
    if (!Coherent()) {
      Snapshot rejected;
      rejected.supported = true;
      rejected.reason = "incoherent";
      return rejected;
    }
    if(!OptionalCoherent()){
      out.last_used_save_room_ready=false;out.last_used_save_room.clear();
      for(auto& room:out.exploration){room.is_save_room=-1;room.used_save_point=-1;}
    }
    if(!fast_travel_reader.Coherent())out.unlocked_fast_travel_ready=false;
    if(!out.unlocked_fast_travel_ready)out.unlocked_fast_travel_levels.clear();
    if(!out.last_used_save_room_ready)out.last_used_save_room.clear();
    out.coherent = true;
    if (history && !out.components_ready) {
      out.explorer_candidate = out.room_candidate = out.position_candidate = false;
      out.target_position = {};
      out.room_id.clear();
      out.motion = {};
      out.reason = "motion_refresh_required";
      return out;
    }
    out.reason = out.application_loading == 1 || out.room_loading == 1 ||
                         out.loading_identity == 1
                     ? "loading"
                 : out.initial_identity == 0 && out.paused != 1
                     ? "non_gameplay_identity"
                 : !out.manager_ready ? "manager_unavailable"
                 : !out.entity_ready  ? "entity_unavailable"
                 : out.application_loading < 0 || out.room_loading < 0
                     ? "loading_unavailable"
                     : "map_contract_unverified";
    return out;
  }
public:
  explicit Decoder(Read &r) : read(r),fast_travel_reader(r) {}
  Snapshot Sample(const Identity &id) { return Decode(id, nullptr); }
  // Reuses only exploration and landmark values from a recent *full* snapshot.
  // Every root, state flag, component membership/type/native liveness, room and
  // target position is re-read and journal-validated. Never chain motion results
  // or keep them across room/entity/loading changes. Refresh full data at least
  // every 100 ms; a refresh-required result carries no history or position.
  Snapshot SampleMotion(const Identity &id, const Snapshot &full) {
    return Decode(id, &full);
  }
};
} // namespace belmont
