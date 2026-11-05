// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_map_render.h"
#include "belmont_map_sources.h"
namespace belmont_world_map {
using ExploredRoom = belmont_map_render::ExploredRoom;
struct Request {
  std::string current_room_id;
  // Only coherent entered-zone lists may be supplied; current room is required.
  std::vector<ExploredRoom> explored_rooms;
  std::vector<std::int64_t> culled_navigation_zone_ids;
  float player_x{}, player_y{};
};
struct Frame {
  ce_unity::Image image;
  belmont_map_fog::Rect world_bounds;
  // Union of the revealed zones' shape bounds (world units); empty when none.
  belmont_map_fog::Rect revealed_bounds;
  std::uint32_t revealed_zone_count{}, updated_tiles{};
  std::uint64_t generation{};
  std::int64_t navigation_zone_id{};
  bool is_z_map{};
  belmont_map_sources::Id source_id{belmont_map_sources::Id::StreetsPoor};
  unsigned pixels_per_world{};
  bool decorative_approximation{true};
  // Sorted native (room, zone) identities and changed atlas rectangles. These
  // authorize a reveal transition without mistaking a removal or navigation
  // switch for newly explored terrain.
  std::vector<std::pair<std::size_t, std::size_t>> revealed_zones;
  std::vector<std::array<unsigned, 4>> updated_regions;
  // Full source-backed membership of this navigation group, including rooms
  // still behind fog. Identity metadata only; never exposes their artwork.
  std::vector<std::string> navigation_rooms;
  bool reveal_intermediate{};
};
bool IsNewReveal(const Frame &previous, const Frame &target);
// Worker-only immutable intermediate. Only changed native tiles are blended;
// unchanged pixels are copied verbatim. The caller owns timing/key generations
// and retains its previous published image until the next key is loaded.
// Invalid/incompatible transitions fail closed. Endpoints retain their pointers.
std::shared_ptr<const Frame> BlendReveal(std::shared_ptr<const Frame> previous,
                                       std::shared_ptr<const Frame> target,
                                       float progress);
struct Statistics {
  std::uint64_t generations{}, unchanged_requests{}, baked_tiles{},
      composed_tiles{};
  std::size_t source_color_bytes{}, current_frame_bytes{};
  double last_render_ms{}, cold_source_ms{};
};
// Source-backed biome level maps. Missing materials/art, FastTravel and
// unknown sources fail closed. Secret overlays unavailable. No guest reads/writes.
class Renderer {
public:
  explicit Renderer(ce_unity::RangeReader romfs,
                    ce_unity::AstcDecoder astc = {});
  explicit Renderer(const EdenDsmodHostApi *host,
                    ce_unity::AstcDecoder astc = {});
  // Metadata only; no texture decode. Source extent chooses integer scale1..3
  // respecting4096-side/16MiB limits. Default wrapper selects StreetsPoor.
  std::optional<belmont_map_fog::Rect> Bounds();
  std::optional<belmont_map_fog::Rect>
  Bounds(const belmont_map_sources::SourceRecipe &source);
  std::shared_ptr<const Frame> Render(const Request &request);
  std::shared_ptr<const Frame>
  Render(const belmont_map_sources::SourceRecipe &source, const Request &request);
  Statistics GetStatistics() const;
  void ClearCache();

private:
  struct State;
  std::shared_ptr<State> state;
};
} // namespace belmont_world_map
