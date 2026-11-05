// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_reader.h"

// Equipment is read from the current live entity, never a save or the complete
// inventory. Empty slots are explicit; failed reads never return last-good IDs.
namespace belmont_equipment {
using belmont::U64;
struct Item {
  bool equipped{};
  std::string id;
};
struct Snapshot {
  bool supported{}, coherent{}, ready{};
  U64 entity{};
  Item weapon, spell, skin;
  std::vector<Item> charms;
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
  bool Pins(U64 b) {
    // Original native accessors prove owner, the four equipped fields, ID,
    // type enum, and InventoryComponent.inventory. No methods are called.
    return Pin(b, 0x1f52c90,
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
  bool Components(U64 e, U64 &inventory) {
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
      }
    }
    U64 owner{};
    return found == count - free && component && Alive(component) &&
           Ptr(component + 0x28, inventory) && Object(inventory, "Inventory") &&
           Get(inventory + 0x50, owner) && owner == e;
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
    return true;
  }
  bool Decode(U64 inventory, Snapshot &out) {
    U64 weapon{}, spell{}, skin{}, charms{}, length{};
    if (!Get(inventory + 0x28, weapon) || !Get(inventory + 0x30, skin) ||
        !Get(inventory + 0x38, spell) || !Ptr(inventory + 0x40, charms) ||
        !Get(charms + 0x18, length) || length > 32 ||
        !Equipped(weapon, 1, out.weapon) || !Equipped(spell, 2, out.spell) ||
        !Equipped(skin, 7, out.skin))
      return false;
    out.charms.resize(std::size_t(length));
    for (std::size_t i = 0; i < out.charms.size(); ++i) {
      U64 p{};
      if (!Get(charms + 0x20 + 8 * i, p) || !Equipped(p, 3, out.charms[i]))
        return false;
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
    U64 inventory{};
    if (!Components(source.entity, inventory) || !Decode(inventory, out)) {
      Snapshot rejected;
      rejected.supported = true;
      rejected.reason = "equipment_unavailable";
      return rejected;
    }
    if (!Coherent()) {
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
} // namespace belmont_equipment
