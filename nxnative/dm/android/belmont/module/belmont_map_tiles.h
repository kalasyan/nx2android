// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_map_sources.h"
#include "chained_echoes_unity.h"
#include <array>
#include <mutex>
#include <optional>
#include <vector>

namespace belmont_map_tiles {
enum class Source { Geometry, SecretGeometry, Background };
struct Point {
  float x{}, y{}, z{};
};
struct ResolvedRef {
  std::string bundle, cab;
  std::int64_t id{};
  bool operator==(const ResolvedRef &) const = default;
};
struct Tile {
  ResolvedRef material;
  std::int64_t renderer_id{}, mesh_id{};
  std::uint32_t layer{};
  ce_unity::Rectf bounds;
  Point origin;
  std::vector<Point> vertices, uv0;
  std::vector<std::uint32_t> triangles;
};
struct Layer {
  // Native packed mask/distance/skyline bytes, not final shader-colored art.
  // Image origin is top-left; native mesh UV origin is bottom-left.
  ce_unity::Image image;
  std::int32_t graphics_format{};
  std::uint32_t depth{}, index{};
  bool srgb{};
};
class Reader {
public:
  explicit Reader(ce_unity::RangeReader romfs, ce_unity::AstcDecoder astc = {});
  explicit Reader(const EdenDsmodHostApi *host,
                  ce_unity::AstcDecoder astc = {});
  // Exact authored render tiles/UVs. These quads are NOT room contours.
  std::optional<std::vector<Tile>> LoadStreetsPoorLayout();
  std::optional<std::vector<Tile>>
  LoadLayout(const belmont_map_sources::SourceRecipe &recipe);
  std::optional<Layer>
  LoadLayer(const belmont_map_sources::SourceRecipe &recipe, Source source,
            std::uint32_t layer);
  // Reads/decompresses one native array slice; never loads all slices into RAM.
  // Background ASTC requires a decoder callback; missing decoder is
  // unavailable.
  std::optional<Layer> LoadStreetsPoorLayer(Source source, std::uint32_t layer);
  std::optional<ResolvedRef>
  ResolveMaterial(const belmont_map_sources::SourceRecipe &recipe,
                  Source source);
  std::optional<ce_unity::Value> ReadMaterial(const ResolvedRef &material);
  std::optional<ResolvedRef>
  ResolveMaterialTexture(const belmont_map_sources::SourceRecipe &recipe,
                         const ResolvedRef &material,
                         std::string_view property);
  void ClearCache();

private:
  std::recursive_mutex mutex;
  ce_unity::Reader unity;
  ce_unity::AstcDecoder astc;
  std::optional<std::vector<Tile>> layout;
  std::optional<belmont_map_sources::SourceRecipe> layout_source;
};
} // namespace belmont_map_tiles
