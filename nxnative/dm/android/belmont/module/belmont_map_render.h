// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_assets.h"
#include "belmont_map_fog.h"
#include "belmont_map_tiles.h"
namespace belmont_map_render {
struct ExploredRoom {
  std::string room_id;
  std::vector<std::int32_t> entered_zone_ids;
};
// Standard authored distance primitives used for source-authored visibility.
// (The legacy asset-free single-room Renderer that used to live here was
// superseded by belmont_world_map::Renderer and had no remaining caller;
// removed. These two free functions are still used by belmont_world_map.cpp.)
float ShapeDistance(const belmont_map_fog::Shape &shape, float x, float y);
float SmoothUnion(float a, float b, float smoothness);
} // namespace belmont_map_render
