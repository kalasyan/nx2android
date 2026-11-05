// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_map_sources.h"
#include "chained_echoes_unity.h"
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Native map data read only from the user's installed 1.1.0 RomFS. No derived
// geometry, reveal mask or player position is built in or persisted. Callers
// must pin the loaded executable build and apply actual exploration separately.
namespace belmont_map_assets {
struct Coord {
  std::int32_t x{}, y{};
  bool operator==(const Coord &) const = default;
};
struct Chunk {
  // Index in RoomData.chunks = local.y * width + local.x.
  Coord local;
  Coord world;
  bool enabled{};
  std::int32_t doors{}, forced_walls{}, removed_wall_parts{};
};
struct Room {
  std::string id, name, world_name, scene_address;
  std::int64_t source_path_id{};
  std::int32_t runtime_number{}, width{}, height{};
  Coord position, edition_offset, chunk_size, world_origin;
  // Native WorldRoomData.GetWorldPosition uses x/y; z is zero.
  Coord simulation_origin;
  std::vector<Chunk> chunks;
};
struct World {
  std::string name;
  Coord chunk_size;
  std::vector<Room> rooms;
};
class Reader {
public:
  explicit Reader(ce_unity::RangeReader romfs);
  explicit Reader(const EdenDsmodHostApi *host);
  // All-or-nothing. Missing, truncated, unsupported or inconsistent data is
  // unavailable. The output is static geometry, never a revealed map.
  std::optional<World> LoadStreetsPoor();
  std::optional<World>
  LoadWorld(const belmont_map_sources::SourceRecipe &source);
  std::optional<World> LoadForRoom(std::string_view room_id);
  void ClearCache();

private:
  std::recursive_mutex mutex;
  ce_unity::Reader unity;
  std::optional<World> cached;
  std::optional<belmont_map_sources::Id> cached_source;
};
} // namespace belmont_map_assets
