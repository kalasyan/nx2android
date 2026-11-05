// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_map_sources.h"
#include "chained_echoes_unity.h"
#include <array>
#include <mutex>

namespace belmont_map_fog {
struct Point {
  float x{}, y{};
};
struct Rect {
  float x{}, y{}, width{}, height{};
};
struct CoordRect {
  std::int32_t x{}, y{}, width{}, height{};
};
enum class ShapeKind { Box, Quad };
struct Shape {
  std::int64_t rid{};
  ShapeKind kind{};
  Rect bounds;
  Point position, size; // Box world center/full size, not half extents.
  std::array<Point, 4> vertices{}; // Quad authored world coordinates.
};
struct Zone {
  std::int32_t id{};
  std::int64_t navigation_zone_id{};
  std::vector<Shape> shapes;
};
struct Room {
  std::string id;
  std::vector<Zone> zones;
};
struct ChunkState {
  bool active{}, completed{}, selected{};
};
struct District {
  std::int32_t source_file_id{};
  std::int64_t source_path_id{}, navigation_zone_id{};
  CoordRect bounds_coord;
  Rect bounds;
  Point chunk_size;
  std::vector<std::int64_t> additional_navigation_zone_ids;
  std::vector<ChunkState>
      initial_chunks; // Source defaults, NOT live completion.
};
struct TileSettings {
  std::int64_t renderer_id{}, sdg_id{};
  float smoothness{}, box_roundness{}, quad_roundness{};
};
struct Data {
  std::vector<Room> rooms;
  std::vector<District> districts;
  std::vector<TileSettings> tiles;
  CoordRect district_bounds_coord;
  Rect district_bounds;
  Point chunk_size;
  float tile_bounds_inflate{}, empty_neighbor_thickness{};
  bool generate_empty_neighbors{};
  std::string world_name;
  std::int64_t main_navigation_zone_id{};
  std::vector<std::int64_t> navigation_zone_ids;
};
// Source substrate only. Visibility requires coherent live enteredZonesIds and
// native navigation-zone culling. No rectangle/cell fallback and no rendering.
class Reader {
public:
  explicit Reader(ce_unity::RangeReader romfs);
  explicit Reader(const EdenDsmodHostApi *host);
  std::optional<Data> LoadStreetsPoor();
  std::optional<Data> Load(const belmont_map_sources::SourceRecipe &source);
  std::optional<Data> LoadForRoom(std::string_view room_id);
  void ClearCache();

private:
  std::recursive_mutex mutex;
  ce_unity::Reader unity;
  std::optional<Data> cache;
  std::optional<belmont_map_sources::Id> cached_source;
};
} // namespace belmont_map_fog
