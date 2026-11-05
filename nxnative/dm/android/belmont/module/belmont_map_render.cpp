// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_render.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace belmont_map_render {
namespace {
float Clamp(float v) { return std::clamp(v, 0.f, 1.f); }
} // namespace
float ShapeDistance(const belmont_map_fog::Shape &s, float x, float y) {
  if (s.kind == belmont_map_fog::ShapeKind::Box) {
    const float qx = std::abs(x - s.position.x) - s.size.x * .5f,
                qy = std::abs(y - s.position.y) - s.size.y * .5f;
    return std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) +
           std::min(std::max(qx, qy), 0.f);
  }
  float best = std::numeric_limits<float>::max();
  bool inside = false, degenerate = false;
  for (unsigned i = 0; i < 4; ++i) {
    const auto a = s.vertices[i], b = s.vertices[(i + 1) % 4];
    const float dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
    if (length == 0) {
      degenerate = true;
      continue;
    }
    const float t = Clamp(((x - a.x) * dx + (y - a.y) * dy) / length);
    best = std::min(best, std::hypot(x - a.x - t * dx, y - a.y - t * dy));
    if ((a.y > y) != (b.y > y)) {
      const float cross = (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x;
      if (x < cross)
        inside = !inside;
    }
  }
  // Native authored triangles are four-vertex quads with a repeated edge.
  // The native four-edge sign test produces zero on their interior.
  return inside ? (degenerate ? 0.f : -best) : best;
}
float SmoothUnion(float a, float b, float k) {
  if (k <= 0)
    return std::min(a, b);
  const float h = std::max(k - std::abs(a - b), 0.f);
  return std::min(a, b) - h * h / (4.f * k);
}
} // namespace belmont_map_render
