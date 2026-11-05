// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
namespace belmont_fast_travel {
// Optional sub-reader. Caller must already establish exact Belmont title/build,
// active gameplay/pause and current Entity/Explorer identity. Its independent
// journal cannot invalidate core map or savepoint metadata on a local failure.
template <class Read> class OptionalReader {
  using U64 = std::uint64_t;
  Read &read;
  struct Span {
    U64 at;
    std::vector<std::uint8_t> bytes;
  };
  std::vector<Span> journal;
  static bool Pointer(U64 p) {
    return p >= 0x10000 && p < 0x8000000000ULL && (p & 7) == 0;
  }
  bool Bytes(U64 at, void *out, std::size_t n) {
    if (!n || n > 4096 || at < 0x10000 ||
        at > std::numeric_limits<U64>::max() - n || journal.size() >= 4096 ||
        !read(at, out, n))
      return false;
    const auto *b = static_cast<const std::uint8_t *>(out);
    journal.push_back({at, {b, b + n}});
    return true;
  }
  template <class T> bool Get(U64 at, T &v) {
    std::array<std::uint8_t, sizeof(T)> b{};
    if (!Bytes(at, b.data(), b.size()))
      return false;
    U64 bits{};
    for (unsigned i = 0; i < sizeof(T); ++i)
      bits |= U64(b[i]) << (8 * i);
    std::memcpy(&v, &bits, sizeof(T));
    return true;
  }
  bool Ptr(U64 at, U64 &p) { return Get(at, p) && Pointer(p); }
  bool Text(U64 p, const char *s) {
    const auto n = std::strlen(s) + 1;
    std::array<char, 128> b{};
    return n <= b.size() && Bytes(p, b.data(), n) &&
           !std::memcmp(b.data(), s, n);
  }
  bool Object(U64 p, const char *name, const char *space) {
    U64 k{}, n{}, ns{};
    return Pointer(p) && Ptr(p, k) && Get(k + 0x10, n) && Get(k + 0x18, ns) &&
           Text(n, name) && Text(ns, space);
  }
  bool Alive(U64 p) {
    U64 n{};
    std::uint8_t b{};
    return Ptr(p + 0x10, n) && Bytes(n, &b, 1);
  }
  bool Guid(U64 object, std::string &s) {
    std::int32_t n{};
    std::array<std::uint8_t, 64> b{};
    if (!Object(object, "String", "System") || !Get(object + 0x10, n) ||
        n != 32 || !Bytes(object + 0x14, b.data(), b.size()))
      return false;
    s.clear();
    for (unsigned i = 0; i < 32; ++i) {
      const char c = char(b[i * 2]);
      if (b[i * 2 + 1] || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        return false;
      s += c;
    }
    return true;
  }
  bool Pin(U64 base, U64 rva, std::initializer_list<std::uint8_t> expected) {
    std::vector<std::uint8_t> b(expected.size());
    return Bytes(base + rva, b.data(), b.size()) &&
           std::equal(b.begin(), b.end(), expected.begin());
  }

public:
  explicit OptionalReader(Read &r) : read(r) {}
  void Clear() { journal.clear(); }
  bool Sample(U64 base, U64 explorer, U64 entity,
              std::vector<std::string> &out) {
    Clear();
    out.clear();
    // get_unlockedFastTravelLevelsID, HasUnlockedFastTravelLevel's native debug
    // bypass and +50 list selection, SavableScriptableObject.guid getter.
    if (!Pin(base, 0x1c281f0,
             {0x00, 0x28, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}) ||
        !Pin(base, 0x1c29fc8,
             {0x88, 0xa2, 0x42, 0x39, 0xc8, 0x00, 0x00, 0x34}) ||
        !Pin(base, 0x1c29fe4,
             {0x80, 0x2a, 0x40, 0xf9, 0x60, 0xff, 0xff, 0xb4}) ||
        !Pin(base, 0x1e0fff0, {0x00, 0x0c, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6}))
      return false;
    U64 owner{}, list{}, items{}, capacity{};
    std::uint8_t debug{};
    std::int32_t count{}, version{};
    if (!Object(explorer, "ExplorerComponent", "") || !Alive(explorer) ||
        !Get(explorer + 0x20, owner) || owner != entity ||
        !Get(explorer + 0xa8, debug) || debug != 0 ||
        !Ptr(explorer + 0x50, list) ||
        !Object(list, "List`1", "System.Collections.Generic") ||
        !Get(list + 0x18, count) || count < 0 || count > 128 ||
        !Get(list + 0x1c, version) || !Get(list + 0x10, items))
      return false;
    if (!count && !items)
      return true;
    if (!Pointer(items) || !Get(items + 0x18, capacity) || capacity > 1024 ||
        U64(count) > capacity)
      return false;
    std::vector<std::string> values;
    for (int i = 0; i < count; ++i) {
      U64 asset{}, guid{};
      std::string text;
      if (!Ptr(items + 0x20 + 8 * U64(i), asset) ||
          !Object(asset, "LevelID", "") || !Alive(asset) ||
          !Ptr(asset + 0x18, guid) || !Guid(guid, text) ||
          std::find(values.begin(), values.end(), text) != values.end())
        return false;
      values.push_back(std::move(text));
    }
    out = std::move(values);
    return true;
  }
  bool Coherent() {
    std::array<std::uint8_t, 4096> b{};
    for (const auto &s : journal)
      if (!read(s.at, b.data(), s.bytes.size()) ||
          std::memcmp(b.data(), s.bytes.data(), s.bytes.size()))
        return false;
    return true;
  }
};
} // namespace belmont_fast_travel
