// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "chained_echoes_unity.h"
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace belmont_map_refs {
// Unity 2022.3 Switch metadata only. Never searches object payload for CAB
// strings. External PPtr fileID n resolves to returned table[n-1]; fileID0 is
// local.
inline std::optional<std::vector<std::string>>
ExternalCabs(ce_unity::ByteSource src) {
  if (!src.read || src.size < 48)
    return {};
  std::uint8_t h[48]{};
  if (!src.read(0, h, 48))
    return {};
  auto be = [](const std::uint8_t *p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
           std::uint32_t(p[2]) << 8 | p[3];
  };
  const auto size = be(h + 20);
  if (be(h + 8) != 22 || h[16] != 0 || size > 8 * 1024 * 1024 ||
      size > src.size - 48)
    return {};
  struct Cursor {
    std::vector<std::uint8_t> b;
    std::size_t p{};
    bool ok{true};
    void skip(std::uint64_t n) {
      if (n > b.size() - p) {
        ok = false;
        return;
      }
      p += n;
    }
    std::uint32_t u32() {
      if (b.size() - p < 4) {
        ok = false;
        return 0;
      }
      auto *q = b.data() + p;
      p += 4;
      return std::uint32_t(q[0]) | std::uint32_t(q[1]) << 8 |
             std::uint32_t(q[2]) << 16 | std::uint32_t(q[3]) << 24;
    }
    void align() { skip((4 - p % 4) % 4); }
    std::string str() {
      if (!ok)
        return {};
      auto at = p;
      while (p < b.size() && b[p] && p - at <= 1024)
        ++p;
      if (p == b.size() || p - at > 1024) {
        ok = false;
        return {};
      }
      std::string s(reinterpret_cast<char *>(b.data() + at), p - at);
      ++p;
      return s;
    }
    bool count(std::uint32_t n, std::uint32_t max) {
      if (n > max)
        ok = false;
      return ok;
    }
  } c;
  c.b.resize(size);
  if (!src.read(48, c.b.data(), size))
    return {};
  if (c.str() != "2022.3.62f2" || c.u32() != 38)
    return {};
  if (c.p >= c.b.size() || c.b[c.p++] != 1)
    return {};
  auto types = c.u32();
  if (!c.count(types, 65536))
    return {};
  for (std::uint32_t i = 0; i < types && c.ok; ++i) {
    auto cls = c.u32();
    c.skip(3);
    if (cls == 114)
      c.skip(16);
    c.skip(16);
    auto nodes = c.u32(), strings = c.u32();
    if (!c.count(nodes, 1000000) || strings > 8 * 1024 * 1024)
      return {};
    c.skip(std::uint64_t(nodes) * 32 + strings);
    auto deps = c.u32();
    if (!c.count(deps, 1000000))
      return {};
    c.skip(std::uint64_t(deps) * 4);
  }
  auto objects = c.u32();
  if (!c.count(objects, 1000000))
    return {};
  for (std::uint32_t i = 0; i < objects && c.ok; ++i) {
    c.align();
    c.skip(24);
  }
  auto scripts = c.u32();
  if (!c.count(scripts, 1000000))
    return {};
  for (std::uint32_t i = 0; i < scripts && c.ok; ++i) {
    c.skip(4);
    c.align();
    c.skip(8);
  }
  auto count = c.u32();
  if (!c.count(count, 4096))
    return {};
  std::vector<std::string> out;
  out.reserve(count);
  for (std::uint32_t i = 0; i < count && c.ok; ++i) {
    c.str();
    c.skip(16);
    auto type = c.u32();
    auto path = c.str();
    if (type > 3)
      return {};
    auto pos = path.find_last_of('/');
    auto base = pos == std::string::npos ? path : path.substr(pos + 1);
    // Built-in resource entries are retained too; caller must demand exact CAB.
    out.push_back(std::move(base));
  }
  if (!c.ok)
    return {};
  return out;
}
} // namespace belmont_map_refs
