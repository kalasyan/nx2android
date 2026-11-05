// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_fog.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace belmont_map_fog {
namespace {
using V = ce_unity::Value;
constexpr std::string_view Root = "Data/StreamingAssets/aa/Switch/";
struct Cursor {
  std::span<const std::uint8_t> raw;
  std::size_t pos{};
  bool ok{true};
  bool Skip(std::size_t n) {
    if (!ok || pos > raw.size() || n > raw.size() - pos) {
      ok = false;
      return false;
    }
    pos += n;
    return true;
  }
  bool Align() { return Skip((4 - pos % 4) % 4); }
  std::uint32_t U32() {
    if (!Skip(4))
      return 0;
    const auto *p = raw.data() + pos - 4;
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
           std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
  }
  std::int32_t I32() { return static_cast<std::int32_t>(U32()); }
  std::int64_t I64() {
    auto a = U32();
    auto b = U32();
    return static_cast<std::int64_t>(std::uint64_t(a) | std::uint64_t(b) << 32);
  }
  float Float() {
    auto u = U32();
    float f{};
    std::memcpy(&f, &u, 4);
    if (!std::isfinite(f) || std::abs(f) > 1e7f)
      ok = false;
    return f;
  }
  Point XY() {
    float x = Float();
    float y = Float();
    return {x, y};
  }
  Rect Bounds() {
    float x = Float(), y = Float(), w = Float(), h = Float();
    if (w < 0 || h < 0)
      ok = false;
    return {x, y, w, h};
  }
  bool Flag() {
    if (!Skip(1))
      return false;
    auto v = raw[pos - 1];
    if (v > 1)
      ok = false;
    Align();
    return v != 0;
  }
  std::string Text(std::size_t max = 256) {
    auto n = I32();
    if (n < 0 || std::size_t(n) > max || !Skip(std::size_t(n))) {
      ok = false;
      return {};
    }
    std::string s(reinterpret_cast<const char *>(raw.data() + pos - n),
                  std::size_t(n));
    if (s.find('\0') != std::string::npos)
      ok = false;
    Align();
    return s;
  }
  std::int64_t LocalRef() {
    auto file = I32();
    auto id = I64();
    if (file != 0 || id == 0)
      ok = false;
    return id;
  }
  std::int32_t Count(std::int32_t max) {
    auto n = I32();
    if (n < 0 || n > max)
      ok = false;
    return ok ? n : 0;
  }
};
// Pinned serialized holder schema, including only the two shipped managed
// reference classes. Registry payload types are validated; unsupported data is
// unavailable, never interpreted using guessed byte sizes.
std::optional<std::vector<Room>>
DecodeHolder(std::span<const std::uint8_t> raw,
             std::int64_t expected_game_object = 947) {
  if (raw.size() > 4 * 1024 * 1024)
    return {};
  Cursor c{raw};
  if (c.LocalRef() != expected_game_object || !c.Flag())
    return {};
  if (c.I32() != 1 || c.I64() != 4040120242753893301LL || !c.Text().empty())
    return {};
  auto count = c.Count(128);
  if (!c.ok || !count)
    return {};
  std::vector<Room> rooms;
  std::map<std::int64_t, Shape> shapes;
  std::map<std::pair<std::size_t, std::size_t>, std::vector<std::int64_t>> refs;
  std::set<std::string> ids;
  std::size_t total_zones{}, total_refs{};
  for (int i = 0; i < count; ++i) {
    Room r;
    r.id = c.Text();
    if (r.id.empty() || !ids.insert(r.id).second)
      return {};
    auto zones = c.Count(4096);
    total_zones += zones;
    if (!c.ok || total_zones > 4096)
      return {};
    std::set<std::int32_t> zone_ids;
    for (int k = 0; k < zones; ++k) {
      if (c.Text() != r.id)
        return {};
      Zone z;
      z.id = c.I32();
      z.navigation_zone_id = c.LocalRef();
      if (z.id < 0 || !zone_ids.insert(z.id).second)
        return {};
      auto n = c.Count(256);
      total_refs += n;
      if (!c.ok || !n || total_refs > 8192)
        return {};
      auto &v = refs[{rooms.size(), r.zones.size()}];
      for (int s = 0; s < n; ++s) {
        auto rid = c.I64();
        if (rid <= 0)
          return {};
        v.push_back(rid);
      }
      // Source states are not live state; refuse a changed serialized recipe.
      if (c.Flag() || c.Flag() || !c.ok)
        return {};
      r.zones.push_back(std::move(z));
    }
    rooms.push_back(std::move(r));
  }
  if (c.I32() != 2)
    return {};
  auto n = c.Count(8192);
  if (!c.ok || !n)
    return {};
  for (int i = 0; i < n; ++i) {
    Shape s;
    s.rid = c.I64();
    auto klass = c.Text();
    auto ns = c.Text();
    auto assembly = c.Text();
    if (!c.ok || s.rid <= 0 || !ns.empty() || assembly != "Assembly-CSharp")
      return {};
    s.bounds = c.Bounds();
    if (klass == "ExplorationShapeBox") {
      s.kind = ShapeKind::Box;
      s.position = c.XY();
      s.size = c.XY();
      if (s.size.x <= 0 || s.size.y <= 0)
        return {};
    } else if (klass == "ExplorationShapeQuad") {
      s.kind = ShapeKind::Quad;
      if (c.I32() != 4)
        return {};
      for (auto &p : s.vertices)
        p = c.XY();
    } else
      return {};
    if (!c.ok || !shapes.emplace(s.rid, s).second)
      return {};
  }
  if (!c.ok || c.pos != raw.size())
    return {};
  std::set<std::int64_t> used;
  for (const auto &[key, v] : refs)
    for (auto rid : v) {
      auto it = shapes.find(rid);
      if (it == shapes.end())
        return {};
      rooms[key.first].zones[key.second].shapes.push_back(it->second);
      used.insert(rid);
    }
  if (used.size() != shapes.size())
    return {};
  return rooms;
}
bool Int(const V *v, std::int64_t &out) {
  if (!v)
    return false;
  if (v->kind == V::Kind::Int) {
    out = v->i;
    return true;
  }
  if (v->kind == V::Kind::UInt && v->u <= std::uint64_t(INT64_MAX)) {
    out = static_cast<std::int64_t>(v->u);
    return true;
  }
  return false;
}
bool F(const V *v, float &out) {
  if (!v || v->kind != V::Kind::Float || !std::isfinite(v->f) ||
      std::abs(v->f) > 1e7)
    return false;
  out = static_cast<float>(v->f);
  return true;
}
bool P(const V *v, Point &out) {
  return v && F(v->Get("x"), out.x) && F(v->Get("y"), out.y);
}
bool R(const V *v, Rect &r) {
  return v && F(v->Get("x"), r.x) && F(v->Get("y"), r.y) &&
         F(v->Get("width"), r.width) && F(v->Get("height"), r.height) &&
         r.width > 0 && r.height > 0;
}
bool CR(const V *v, CoordRect &r) {
  if (!v)
    return false;
  std::int64_t a[4]{};
  const char *names[] = {"x", "y", "width", "height"};
  for (int i = 0; i < 4; ++i)
    if (!Int(v->Get(names[i]), a[i]) || a[i] < -65536 || a[i] > 65536)
      return false;
  r = {static_cast<std::int32_t>(a[0]), static_cast<std::int32_t>(a[1]),
       static_cast<std::int32_t>(a[2]), static_cast<std::int32_t>(a[3])};
  return r.width > 0 && r.height > 0 &&
         std::int64_t(r.width) * r.height <= 65536;
}
bool Flag(const V *v, bool &b) {
  std::int64_t n{};
  if (!Int(v, n) || n < 0 || n > 1)
    return false;
  b = n != 0;
  return true;
}
bool Ref(const V *v, std::int64_t &id, std::int32_t file = 0) {
  std::int64_t f{};
  return v && Int(v->Get("m_FileID"), f) && f == file &&
         Int(v->Get("m_PathID"), id) && id != 0;
}
const std::vector<V> *Array(const V *v, std::size_t max) {
  return v && v->kind == V::Kind::Array && v->items.size() <= max ? &v->items
                                                                  : nullptr;
}
std::optional<V> Object(const std::shared_ptr<ce_unity::SerializedFile> &sf,
                        std::int64_t id) {
  auto info = sf->Find(id);
  if (!info || info->class_id != 114 || info->byte_size > 4 * 1024 * 1024)
    return {};
  return sf->ReadObject(*info);
}
bool Districts(const V &model, Data &data, std::int32_t district_file_id = 13) {
  if (!CR(model.Get("globalBoundsCoord"), data.district_bounds_coord) ||
      !R(model.Get("globalBounds"), data.district_bounds) ||
      !P(model.Get("chunkSize"), data.chunk_size) || data.chunk_size.x <= 0 ||
      data.chunk_size.y <= 0)
    return false;
  const auto *districts = Array(model.Get("districtsStates"), 128);
  if (!districts || districts->empty())
    return false;
  std::set<std::int64_t> ids;
  for (const auto &v : *districts) {
    District d;
    d.source_file_id = district_file_id;
    if (!Ref(v.Get("district"), d.source_path_id, district_file_id) ||
        !ids.insert(d.source_path_id).second ||
        !CR(v.Get("districtBoundsCoord"), d.bounds_coord) ||
        !R(v.Get("districtBounds"), d.bounds) ||
        !P(v.Get("chunkSize"), d.chunk_size) ||
        !Ref(v.Get("navigationZone"), d.navigation_zone_id))
      return false;
    if (d.chunk_size.x != data.chunk_size.x ||
        d.chunk_size.y != data.chunk_size.y)
      return false;
    const auto &g = data.district_bounds_coord;
    const auto &b = d.bounds_coord;
    if (b.x < g.x || b.y < g.y ||
        std::int64_t(b.x) + b.width > std::int64_t(g.x) + g.width ||
        std::int64_t(b.y) + b.height > std::int64_t(g.y) + g.height)
      return false;
    const auto *additional = Array(v.Get("additionalNavigationZones"), 128);
    if (!additional)
      return false;
    for (const auto &a : *additional) {
      std::int64_t id{};
      if (!Ref(&a, id))
        return false;
      d.additional_navigation_zone_ids.push_back(id);
    }
    const auto *chunks = Array(v.Get("chunksStates"), 4096);
    if (!chunks || chunks->size() != std::size_t(b.width) * b.height)
      return false;
    for (const auto &vch : *chunks) {
      ChunkState ch;
      if (!Flag(vch.Get("active"), ch.active) ||
          !Flag(vch.Get("completed"), ch.completed) ||
          !Flag(vch.Get("selected"), ch.selected))
        return false;
      d.initial_chunks.push_back(ch);
    }
    data.districts.push_back(std::move(d));
  }
  return true;
}
} // namespace
Reader::Reader(ce_unity::RangeReader romfs)
    : unity(std::move(romfs), {}, std::string(Root)) {}
Reader::Reader(const EdenDsmodHostApi *host)
    : Reader(ce_unity::MakeHostRangeReader(host)) {}
void Reader::ClearCache() {
  std::lock_guard lock(mutex);
  cache.reset();
  cached_source.reset();
  unity.ClearCache();
}
std::optional<Data> Reader::LoadStreetsPoor() {
  return Load(belmont_map_sources::StreetsPoor());
}
std::optional<Data> Reader::LoadForRoom(std::string_view room_id) {
  const auto *recipe = belmont_map_sources::FindSourceForRoom(room_id);
  if (!recipe)
    return {};
  auto data = Load(*recipe);
  if (!data || std::none_of(data->rooms.begin(), data->rooms.end(),
                            [&](const Room &r) { return r.id == room_id; }))
    return {};
  return data;
}
std::optional<Data>
Reader::Load(const belmont_map_sources::SourceRecipe &recipe) {
  const auto *known = belmont_map_sources::Canonical(recipe);
  if (!known)
    return {};
  const auto &source = *known;
  std::lock_guard lock(mutex);
  if (cache && cached_source == source.id)
    return cache;
  cache.reset();
  cached_source.reset();
  try {
    auto bundle = unity.OpenBundle(source.map_bundle);
    if (!bundle || bundle->engine_version != "2022.3.62f2")
      return {};
    auto node = bundle->FindNode(source.map_cab);
    if (!node)
      return {};
    auto sf = ce_unity::SerializedFile::Open(bundle->NodeSource(*node));
    if (!sf || sf->Version() != 22 || sf->Platform() != 38 ||
        !sf->HasTypeTrees())
      return {};
    auto context = Object(sf, source.map_context_id);
    std::int64_t script{}, fog_id{}, district_id{}, renderer_id{}, main_id{},
        settings_id{}, holder_id{}, nav_manager_id{};
    if (!context || !Ref(context->Get("m_Script"), script, 1) ||
        script != 6647830585995460756LL ||
        !Ref(context->Get("fowModel"), fog_id) ||
        !Ref(context->Get("districtsModel"), district_id) ||
        !Ref(context->Get("rendererFogOfWar"), renderer_id) ||
        !Ref(context->Get("levelMapRenderer"), main_id) ||
        !Ref(context->Get("settings"), settings_id))
      return {};
    auto model = Object(sf, district_id), fog = Object(sf, fog_id),
         renderer = Object(sf, renderer_id), main = Object(sf, main_id),
         settings = Object(sf, settings_id);
    if (!model || !fog || !renderer || !main || !settings ||
        !Ref(fog->Get("worldRoomStatesHolderInstance"), holder_id) ||
        !Ref(settings->Get("navigationZonesManager"), nav_manager_id))
      return {};
    auto holder = sf->Find(holder_id);
    if (!holder || holder->class_id != 114 ||
        holder->byte_size > 4 * 1024 * 1024)
      return {};
    auto raw = sf->ReadRaw(*holder);
    if (!raw)
      return {};
    Cursor header{*raw};
    auto go = header.LocalRef();
    auto go_info = sf->Find(go);
    if (!header.ok || !go_info || go_info->class_id != 1)
      return {};
    auto rooms = DecodeHolder(*raw, go);
    if (!rooms)
      return {};
    for (const auto &room : *rooms)
      if (!room.id.starts_with(std::string(source.world_name) + "_"))
        return {};
    Data data;
    data.rooms = std::move(*rooms);
    data.world_name = source.world_name;
    if (!Districts(*model, data, source.district_file_id) ||
        !F(renderer->Get("tileBoundsInflateValue"), data.tile_bounds_inflate) ||
        data.tile_bounds_inflate < 0 ||
        !F(fog->Get("fogOfWarForEmptyChunksNeighboursThickness"),
           data.empty_neighbor_thickness) ||
        data.empty_neighbor_thickness < 0 ||
        !Flag(fog->Get("generateFogOfWarForEmptyChunksNeighbours"),
              data.generate_empty_neighbors))
      return {};
    auto nav = Object(sf, nav_manager_id);
    if (!nav ||
        !Ref(nav->Get("mainNavigationZone"), data.main_navigation_zone_id))
      return {};
    auto groups = Array(nav->Get("navigationZones"), 128);
    if (!groups || groups->empty())
      return {};
    std::set<std::int64_t> nav_ids;
    for (const auto &v : *groups) {
      std::int64_t id{};
      if (!Ref(&v, id) || !nav_ids.insert(id).second)
        return {};
      auto info = sf->Find(id);
      if (!info || info->class_id != 114)
        return {};
      data.navigation_zone_ids.push_back(id);
    }
    if (!nav_ids.contains(data.main_navigation_zone_id))
      return {};
    for (const auto &room : data.rooms)
      for (const auto &z : room.zones)
        if (!nav_ids.contains(z.navigation_zone_id))
          return {};
    for (const auto &d : data.districts) {
      if (!nav_ids.contains(d.navigation_zone_id))
        return {};
      for (auto id : d.additional_navigation_zone_ids)
        if (!nav_ids.contains(id))
          return {};
    }
    auto tiles = Array(main->Get("tileRenderers"), 128);
    if (!tiles || tiles->empty())
      return {};
    std::set<std::int64_t> tile_ids;
    for (const auto &v : *tiles) {
      TileSettings t;
      if (!Ref(&v, t.renderer_id) || !tile_ids.insert(t.renderer_id).second)
        return {};
      auto tile = Object(sf, t.renderer_id);
      if (!tile || !Ref(tile->Get("sdgTexture"), t.sdg_id))
        return {};
      auto sdg = Object(sf, t.sdg_id);
      if (!sdg || !F(sdg->Get("smoothness"), t.smoothness) ||
          !F(sdg->Get("boxRoundness"), t.box_roundness) ||
          !F(sdg->Get("quadRoundness"), t.quad_roundness) ||
          t.smoothness <= 0 || t.box_roundness < 0 || t.quad_roundness < 0)
        return {};
      data.tiles.push_back(t);
    }
    cache = data;
    cached_source = source.id;
    return data;
  } catch (...) {
    return {};
  }
}
} // namespace belmont_map_fog
