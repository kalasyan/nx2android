// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_assets.h"
#include <algorithm>
#include <limits>
#include <set>
#include <span>

namespace belmont_map_assets {
namespace {
constexpr std::string_view AssetRoot = "Data/StreamingAssets/aa/Switch/";
constexpr std::string_view SettingsBundle =
    "rd-shared_assets_all_11b587818476b7ee2b83da6325d71410.bundle";
constexpr std::string_view SettingsCab = "CAB-abb18a88407c7bc3cef268c68a6a2d59";
constexpr std::int64_t SettingsId = 7263961372460223634LL;
constexpr std::size_t MaxRooms = 128, MaxChunks = 4096,
                      MaxRoomBytes = 4 * 1024 * 1024;
using V = ce_unity::Value;

bool Integer(const V *v, std::int32_t &out) {
  if (!v)
    return false;
  std::int64_t i;
  if (v->kind == V::Kind::Int)
    i = v->i;
  else if (v->kind == V::Kind::UInt && v->u <= std::uint64_t(INT32_MAX))
    i = std::int64_t(v->u);
  else
    return false;
  if (i < INT32_MIN || i > INT32_MAX)
    return false;
  out = static_cast<std::int32_t>(i);
  return true;
}
bool Int64(const V *v, std::int64_t &out) {
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
bool Text(const V *v, std::string &s, std::size_t max = 1024) {
  if (!v || v->kind != V::Kind::String || v->s.empty() || v->s.size() > max ||
      v->s.find('\0') != std::string::npos)
    return false;
  s = v->s;
  return true;
}
bool Pair(const V *v, Coord &c) {
  return v && Integer(v->Get("x"), c.x) && Integer(v->Get("y"), c.y);
}
bool Ptr(const V *v, std::int32_t file, std::int64_t &id) {
  std::int32_t f{};
  return v && Integer(v->Get("m_FileID"), f) && f == file &&
         Int64(v->Get("m_PathID"), id) && id != 0;
}
bool Add(std::int32_t a, std::int32_t b, std::int32_t &out) {
  auto n = std::int64_t(a) + b;
  if (n < INT32_MIN || n > INT32_MAX)
    return false;
  out = static_cast<std::int32_t>(n);
  return true;
}
bool Mul(std::int32_t a, std::int32_t b, std::int32_t &out) {
  auto n = std::int64_t(a) * b;
  if (n < INT32_MIN || n > INT32_MAX)
    return false;
  out = static_cast<std::int32_t>(n);
  return true;
}
// This is a bounded serialized-byte prefix decoder, not managed object offsets.
// The pinned RoomData embedded typetree orders fields as:
// MonoBehaviour header; configuration PPtr; layers[] rid; chunks[]; edition
// int2; width; height; chunkSize int2; then ManagedReferencesRegistry. We stop
// before that registry because the shared reader does not decode it.
struct Prefix {
  std::span<const std::uint8_t> bytes;
  std::size_t pos{};
  bool ok{true};
  bool Skip(std::size_t n) {
    if (!ok || pos > bytes.size() || n > bytes.size() - pos) {
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
    const auto *p = bytes.data() + pos - 4;
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
           std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
  }
  std::int32_t I32() { return static_cast<std::int32_t>(U32()); }
  bool Flag(bool &out) {
    if (!Skip(1))
      return false;
    auto b = bytes[pos - 1];
    if (b > 1) {
      ok = false;
      return false;
    }
    out = b != 0;
    return Align();
  }
  bool String(std::string *out, std::size_t max) {
    auto n = I32();
    if (!ok || n < 0 || std::size_t(n) > max || !Skip(std::size_t(n))) {
      ok = false;
      return false;
    }
    if (out)
      out->assign(
          reinterpret_cast<const char *>(bytes.data() + pos - std::size_t(n)),
          std::size_t(n));
    return Align();
  }
};
bool RoomPrefix(std::span<const std::uint8_t> raw, Room &room) {
  if (raw.size() > MaxRoomBytes)
    return false;
  Prefix c{raw};
  bool enabled{};
  // 12-byte GameObject PPtr, enabled+alignment, 12-byte MonoScript PPtr.
  if (!c.Skip(12) || !c.Flag(enabled) || !enabled || !c.Skip(12) ||
      !c.String(&room.name, 256) || room.name.empty() ||
      room.name.find('\0') != std::string::npos || !c.Skip(12))
    return false;
  auto layers = c.I32();
  if (!c.ok || layers < 0 || layers > 256 || !c.Skip(std::size_t(layers) * 8))
    return false;
  auto count = c.I32();
  if (!c.ok || count <= 0 || std::size_t(count) > MaxChunks)
    return false;
  room.chunks.resize(static_cast<std::size_t>(count));
  for (auto &chunk : room.chunks) {
    if (!c.Flag(chunk.enabled))
      return false;
    chunk.doors = c.I32();
    chunk.forced_walls = c.I32();
    chunk.removed_wall_parts = c.I32();
    if (!c.String(nullptr, 4096))
      return false;
    auto collisions = c.I32();
    // Actual bool vector bytes are not geometry needed by this first slice.
    if (!c.ok || collisions < 0 || collisions > 65536 ||
        !c.Skip(static_cast<std::size_t>(collisions)) || !c.Align())
      return false;
  }
  room.edition_offset = {c.I32(), c.I32()};
  room.width = c.I32();
  room.height = c.I32();
  room.chunk_size = {c.I32(), c.I32()};
  return c.ok && room.width > 0 && room.height > 0 && room.width <= 256 &&
         room.height <= 256 &&
         std::int64_t(room.width) * room.height == count &&
         room.chunk_size.x > 0 && room.chunk_size.y > 0;
}
bool BundleIsExpected(ce_unity::Reader &reader, std::string_view bundle,
                      std::string_view cab) {
  auto b = reader.OpenBundle(bundle);
  auto sf = reader.OpenSerialized(bundle);
  return b && sf && b->engine_version == "2022.3.62f2" && sf->Version() == 22 &&
         sf->Platform() == 38 && sf->HasTypeTrees() && !b->Nodes().empty() &&
         b->Nodes().front().path == cab;
}
} // namespace
Reader::Reader(ce_unity::RangeReader romfs)
    : unity(std::move(romfs), {}, std::string(AssetRoot), 2) {}
Reader::Reader(const EdenDsmodHostApi *host)
    : Reader(ce_unity::MakeHostRangeReader(host)) {}
void Reader::ClearCache() {
  std::lock_guard lock(mutex);
  cached.reset();
  cached_source.reset();
  unity.ClearCache();
}
std::optional<World> Reader::LoadStreetsPoor() {
  return LoadWorld(belmont_map_sources::StreetsPoor());
}
std::optional<World> Reader::LoadForRoom(std::string_view room_id) {
  const auto *source = belmont_map_sources::FindSourceForRoom(room_id);
  if (!source)
    return {};
  auto world = LoadWorld(*source);
  if (!world || std::none_of(world->rooms.begin(), world->rooms.end(),
                             [&](const Room &r) { return r.id == room_id; }))
    return {};
  return world;
}
std::optional<World>
Reader::LoadWorld(const belmont_map_sources::SourceRecipe &recipe) {
  const auto *known = belmont_map_sources::Canonical(recipe);
  if (!known)
    return {};
  const auto &source_recipe = *known;
  std::lock_guard lock(mutex);
  try {
    if (cached && cached_source == source_recipe.id)
      return cached;
    cached.reset();
    cached_source.reset();
    if (!BundleIsExpected(unity, source_recipe.world_bundle,
                          source_recipe.world_cab) ||
        !BundleIsExpected(unity, SettingsBundle, SettingsCab))
      return std::nullopt;
    auto source = unity.ReadObject(source_recipe.world_bundle,
                                   source_recipe.world_root_id);
    auto settings = unity.ReadObject(SettingsBundle, SettingsId);
    World world;
    std::int64_t settings_id{};
    if (!source || !settings || !Text(source->Get("m_Name"), world.name) ||
        world.name != source_recipe.world_name ||
        !Ptr(source->Get("worldSettings"), source_recipe.settings_file_id,
             settings_id) ||
        settings_id != SettingsId ||
        !Integer(settings->Get("chunkWidth"), world.chunk_size.x) ||
        !Integer(settings->Get("chunkHeight"), world.chunk_size.y) ||
        world.chunk_size.x <= 0 || world.chunk_size.x > 1024 ||
        world.chunk_size.y <= 0 || world.chunk_size.y > 1024)
      return std::nullopt;
    const auto *rooms = source->Get("_rooms");
    if (!rooms || rooms->kind != V::Kind::Array || rooms->items.empty() ||
        rooms->items.size() > MaxRooms)
      return std::nullopt;
    auto sf = unity.OpenSerialized(source_recipe.world_bundle);
    if (!sf)
      return std::nullopt;
    std::set<std::int64_t> pointers;
    std::set<std::string> ids;
    for (const auto &wr : rooms->items) {
      Room room;
      if (!Text(wr.Get("worldName"), room.world_name) ||
          room.world_name != world.name ||
          !Ptr(wr.Get("roomData"), 0, room.source_path_id) ||
          !pointers.insert(room.source_path_id).second ||
          !Pair(wr.Get("positionCoord"), room.position) ||
          !Integer(wr.Get("runtimeRoomNumber"), room.runtime_number) ||
          room.runtime_number < 0 ||
          !Text(wr.Get("sceneAddressableEntryAddress"), room.scene_address))
        return std::nullopt;
      auto object = sf->Find(room.source_path_id);
      if (!object || object->class_id != 114 ||
          object->byte_size > MaxRoomBytes)
        return std::nullopt;
      auto raw = sf->ReadRaw(*object);
      if (!raw || !RoomPrefix(*raw, room) ||
          room.chunk_size != world.chunk_size)
        return std::nullopt;
      auto instance = room.name + "_uid" + std::to_string(room.runtime_number);
      room.id = room.world_name + "_" + instance;
      auto slash = room.scene_address.find_last_of('/');
      if (slash == std::string::npos ||
          room.scene_address.substr(slash + 1) != instance + ".unity" ||
          !ids.insert(room.id).second)
        return std::nullopt;
      if (!Add(room.position.x, room.edition_offset.x, room.world_origin.x) ||
          !Add(room.position.y, room.edition_offset.y, room.world_origin.y) ||
          !Mul(room.world_origin.x, world.chunk_size.x,
               room.simulation_origin.x) ||
          !Mul(room.world_origin.y, world.chunk_size.y,
               room.simulation_origin.y))
        return std::nullopt;
      for (std::size_t i = 0; i < room.chunks.size(); ++i) {
        auto &chunk = room.chunks[i];
        chunk.local = {static_cast<std::int32_t>(i % room.width),
                       static_cast<std::int32_t>(i / room.width)};
        if (!Add(room.world_origin.x, chunk.local.x, chunk.world.x) ||
            !Add(room.world_origin.y, chunk.local.y, chunk.world.y))
          return std::nullopt;
      }
      world.rooms.push_back(std::move(room));
    }
    cached = world;
    cached_source = source_recipe.id;
    return world;
  } catch (...) {
    return std::nullopt;
  }
}
} // namespace belmont_map_assets
