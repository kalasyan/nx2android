// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_tiles.h"
#include "belmont_map_refs.h"
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace belmont_map_tiles {
namespace {
using V = ce_unity::Value;
constexpr std::string_view Root = "Data/StreamingAssets/aa/Switch/";
constexpr std::string_view MenuBundle =
    "ui-gamemenus_assets_all_866519f7d6a7057a8ba1c459a5b3f694.bundle";
bool N(const V *v, std::int64_t &n) {
  if (!v)
    return false;
  if (v->kind == V::Kind::Int) {
    n = v->i;
    return true;
  }
  if (v->kind == V::Kind::UInt && v->u <= std::uint64_t(INT64_MAX)) {
    n = static_cast<std::int64_t>(v->u);
    return true;
  }
  return false;
}
bool F(const V *v, float &f) {
  if (!v || v->kind != V::Kind::Float || !std::isfinite(v->f))
    return false;
  f = static_cast<float>(v->f);
  return std::isfinite(f);
}
bool P(const V *v, Point &p) {
  return v && F(v->Get("x"), p.x) && F(v->Get("y"), p.y) && F(v->Get("z"), p.z);
}
bool Ref(const V *v, std::int64_t &id) {
  std::int64_t file{};
  return v && N(v->Get("m_FileID"), file) && file == 0 &&
         N(v->Get("m_PathID"), id);
}
std::optional<V>
Object(const std::shared_ptr<const ce_unity::SerializedFile> &sf,
       std::int64_t id, int type) {
  const auto *o = sf ? sf->Find(id) : nullptr;
  if (!o || o->class_id != type || o->byte_size > 4 * 1024 * 1024)
    return {};
  return sf->ReadObject(*o);
}
std::shared_ptr<const ce_unity::SerializedFile>
File(ce_unity::Reader &r, std::string_view bundle, std::string_view cab) {
  auto b = r.OpenBundle(bundle);
  if (!b || b->engine_version != "2022.3.62f2")
    return {};
  const auto *node = b->FindNode(cab);
  if (!node)
    return {};
  auto sf = !b->Nodes().empty() && b->Nodes().front().path == cab
                ? r.OpenSerialized(bundle)
                : std::shared_ptr<const ce_unity::SerializedFile>(
                      ce_unity::SerializedFile::Open(b->NodeSource(*node)));
  return sf && sf->Version() == 22 && sf->Platform() == 38 && sf->HasTypeTrees()
             ? sf
             : nullptr;
}
// Only installed native dependency identities; no inferred array names or IDs.
constexpr std::array<std::string_view, 10> ArtBundles{
    {"art-01_streetspoor_assets_all_e33a704a7ea92e47f2c375f443aacbf1.bundle",
     "art-02_innocentscemetery_assets_all_4079643cdddc4c9a086ca60fb2c05fa0."
     "bundle",
     "art-03_catacombs_assets_all_c055e263f91c9094b04a632b5581848d.bundle",
     "art-05_notredame_assets_all_32ef2b2c715639c5d7e41d803bcb27d1.bundle",
     "art-06_mainhall_assets_all_b8a50ac1737b72382cb0ac62e70bd487.bundle",
     "art-08_library_assets_all_a76b87cc6d1ea3d322f22efc30e0245d.bundle",
     "art-09_clocktower_assets_all_d6bb5abd4c1941cd19cf0e43295e974a.bundle",
     "art-10_hallofmirrors_assets_all_2ee69f349e25b9ac35c3a3220e8b9130.bundle",
     "art-11_masterquarter_assets_all_52b72c61e319c95a97603f4c975125ff.bundle",
     ""}};
constexpr std::array<std::string_view, 10> ArtCabs{
    {"CAB-e585dad378d0230776da9c16b58103cc",
     "CAB-a2964dab7352d20d560ebb88fa823c3a",
     "CAB-daae0586bd0d2d5ae09d1d9142076414",
     "CAB-7b44652be8988a7189e252082732a3f3",
     "CAB-d2e8ad82bfa2022648eb4efc5f314cc9",
     "CAB-442691fc6ae3450fb7e07f6a45586a2a",
     "CAB-cedc5d44adf5513b3e2b7afb06f701b7",
     "CAB-8825ee8ad822982561eaec2356473356",
     "CAB-7c1f4a56ec8ae9d92724aaeb1de8a20d", ""}};
// Per-call memo of ExternalCabs() results keyed by (owner bundle, owner cab).
// Optional: existing callers that pass nullptr keep the prior per-call behavior
// exactly (same code path, same result). LoadLayout() below passes one scoped
// to its own call so that resolving each of up to ~64 tiles' materials against
// the identical map bundle/cab doesn't reparse that bundle's external-CAB
// table from scratch every time.
using CabsCache = std::map<std::pair<std::string, std::string>,
                           std::optional<std::vector<std::string>>>;
std::optional<ResolvedRef>
Resolve(ce_unity::Reader &reader,
        const belmont_map_sources::SourceRecipe &recipe,
        const ResolvedRef &owner, const V *ptr,
        CabsCache *cabs_cache = nullptr) {
  std::int64_t file{}, id{};
  if (!ptr || !N(ptr->Get("m_FileID"), file) || file < 0 || file > 4096 ||
      !N(ptr->Get("m_PathID"), id) || !id)
    return {};
  if (!file)
    return ResolvedRef{owner.bundle, owner.cab, id};
  std::optional<std::vector<std::string>> uncached_cabs;
  const std::optional<std::vector<std::string>> *cabs;
  if (cabs_cache) {
    auto [it, inserted] =
        cabs_cache->try_emplace({owner.bundle, owner.cab});
    if (inserted) {
      auto bundle = reader.OpenBundle(owner.bundle);
      const auto *node = bundle ? bundle->FindNode(owner.cab) : nullptr;
      it->second = node
                       ? belmont_map_refs::ExternalCabs(bundle->NodeSource(*node))
                       : std::nullopt;
    }
    cabs = &it->second;
  } else {
    auto bundle = reader.OpenBundle(owner.bundle);
    const auto *node = bundle ? bundle->FindNode(owner.cab) : nullptr;
    uncached_cabs = node
                        ? belmont_map_refs::ExternalCabs(bundle->NodeSource(*node))
                        : std::nullopt;
    cabs = &uncached_cabs;
  }
  if (!*cabs || std::uint64_t(file) > (*cabs)->size())
    return {};
  const auto &cab = (**cabs)[file - 1];
  if (cab.size() != 36 || !cab.starts_with("CAB-"))
    return {};
  const auto ix = static_cast<std::size_t>(recipe.id);
  if (ix >= ArtBundles.size())
    return {};
  // Native member/CAB identities resolve layout refs without optional art IO.
  if (cab == owner.cab)
    return ResolvedRef{owner.bundle, cab, id};
  if (cab == recipe.map_cab)
    return ResolvedRef{std::string(recipe.map_bundle), cab, id};
  if (cab == "CAB-27e7a4fd03161a1208808cab8fc89755")
    return ResolvedRef{std::string(MenuBundle), cab, id};
  if (cab == ArtCabs[ix] && !ArtBundles[ix].empty())
    return ResolvedRef{std::string(ArtBundles[ix]), cab, id};
  for (auto candidate : {std::string_view(owner.bundle), recipe.map_bundle,
                         ArtBundles[ix], MenuBundle}) {
    if (candidate.empty())
      continue;
    auto dep = reader.OpenBundle(candidate);
    if (dep && dep->engine_version == "2022.3.62f2" && dep->FindNode(cab))
      return ResolvedRef{std::string(candidate), cab, id};
  }
  return {};
}
std::optional<V> ResolvedObject(ce_unity::Reader &r, const ResolvedRef &ref,
                                int cls) {
  return Object(File(r, ref.bundle, ref.cab), ref.id, cls);
}
std::optional<ResolvedRef>
MaterialTexture(ce_unity::Reader &reader,
                const belmont_map_sources::SourceRecipe &recipe,
                const ResolvedRef &material, std::string_view key) {
  auto m = ResolvedObject(reader, material, 21);
  const auto *saved = m ? m->Get("m_SavedProperties") : nullptr;
  const auto *env = saved ? saved->Get("m_TexEnvs") : nullptr;
  if (!env || env->kind != V::Kind::Array || env->items.size() > 128)
    return {};
  std::optional<ResolvedRef> result;
  bool found = false;
  for (const auto &entry : env->items) {
    const auto *name = entry.Get("first"), *value = entry.Get("second");
    if (!name || name->kind != V::Kind::String || !value)
      return {};
    if (name->s == key) {
      if (found)
        return {};
      found = true;
      result = Resolve(reader, recipe, material, value->Get("m_Texture"));
    }
  }
  return result; // Native null background (Ending) stays unavailable.
}
std::uint32_t U32(const unsigned char *p) {
  return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
         std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
bool FloatAt(const std::string &bytes, std::size_t at, float &f) {
  if (at > bytes.size() || 4 > bytes.size() - at)
    return false;
  auto bits = U32(reinterpret_cast<const unsigned char *>(bytes.data() + at));
  std::memcpy(&f, &bits, 4);
  return std::isfinite(f);
}
bool Mesh(const V &mesh, Tile &tile) {
  std::int64_t compression{}, format{}, vertices{}, stream_size{};
  const auto *stream = mesh.Get("m_StreamData");
  const auto *vd = mesh.Get("m_VertexData");
  const auto *channels = vd ? vd->Get("m_Channels") : nullptr;
  const auto *data = vd ? vd->Get("m_DataSize") : nullptr;
  const auto *indices = mesh.Get("m_IndexBuffer");
  if (!N(mesh.Get("m_MeshCompression"), compression) || compression != 0 ||
      !N(mesh.Get("m_IndexFormat"), format) || format != 0 || !stream ||
      !N(stream->Get("size"), stream_size) || stream_size != 0 || !vd ||
      !N(vd->Get("m_VertexCount"), vertices) || vertices <= 0 ||
      vertices > 65536 || !channels || channels->kind != V::Kind::Array ||
      channels->items.size() < 5 || channels->items.size() > 32 || !data ||
      data->kind != V::Kind::Bytes || !indices ||
      indices->kind != V::Kind::Bytes)
    return false;
  std::size_t stride{};
  std::array<std::size_t, 5> offsets{};
  for (std::size_t i = 0; i < channels->items.size(); ++i) {
    const auto &ch = channels->items[i];
    std::int64_t dim{}, fmt{}, s{}, off{};
    if (!N(ch.Get("dimension"), dim) || dim < 0 || dim > 4)
      return false;
    if (!dim)
      continue;
    if ((i != 0 && i != 1 && i != 4) || dim != 3 || !N(ch.Get("format"), fmt) ||
        fmt != 0 || !N(ch.Get("stream"), s) || s != 0 ||
        !N(ch.Get("offset"), off) || off < 0 || off > 256)
      return false;
    offsets[i] = static_cast<std::size_t>(off);
    stride = std::max(stride, offsets[i] + 12);
  }
  // The supported native layout is float3 position, normal, UV0.xyz.
  for (auto i : {0u, 1u, 4u}) {
    std::int64_t dim{};
    if (!N(channels->items[i].Get("dimension"), dim) || dim != 3)
      return false;
  }
  if (stride == 0 || stride > 512 ||
      std::uint64_t(vertices) * stride != data->s.size() ||
      data->s.size() > 4 * 1024 * 1024)
    return false;
  tile.vertices.resize(static_cast<std::size_t>(vertices));
  tile.uv0.resize(tile.vertices.size());
  for (std::size_t i = 0; i < tile.vertices.size(); ++i) {
    auto &p = tile.vertices[i];
    auto &uv = tile.uv0[i];
    const auto at = i * stride;
    if (!FloatAt(data->s, at + offsets[0], p.x) ||
        !FloatAt(data->s, at + offsets[0] + 4, p.y) ||
        !FloatAt(data->s, at + offsets[0] + 8, p.z) ||
        !FloatAt(data->s, at + offsets[4], uv.x) ||
        !FloatAt(data->s, at + offsets[4] + 4, uv.y) ||
        !FloatAt(data->s, at + offsets[4] + 8, uv.z) || uv.z < 0 ||
        uv.z > 127 || std::floor(uv.z) != uv.z)
      return false;
    if (i == 0)
      tile.layer = static_cast<std::uint32_t>(uv.z);
    else if (uv.z != static_cast<float>(tile.layer))
      return false;
  }
  const auto *subs = mesh.Get("m_SubMeshes");
  if (!subs || subs->kind != V::Kind::Array || subs->items.size() != 1)
    return false;
  std::int64_t first{}, count{}, topology{}, base{};
  const auto &sub = subs->items[0];
  if (!N(sub.Get("firstByte"), first) || first != 0 ||
      !N(sub.Get("indexCount"), count) || count <= 0 || count > 196608 ||
      count % 3 || !N(sub.Get("topology"), topology) || topology != 0 ||
      !N(sub.Get("baseVertex"), base) || base != 0 ||
      std::uint64_t(count) * 2 != indices->s.size())
    return false;
  for (std::int64_t i = 0; i < count; ++i) {
    auto *p =
        reinterpret_cast<const unsigned char *>(indices->s.data() + 2 * i);
    auto idx = std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8;
    if (idx >= tile.vertices.size())
      return false;
    tile.triangles.push_back(idx);
  }
  return true;
}
bool Origin(const std::shared_ptr<const ce_unity::SerializedFile> &sf,
            std::int64_t go, Tile &tile) {
  auto object = Object(sf, go, 1);
  const auto *cs = object ? object->Get("m_Component") : nullptr;
  if (!cs || cs->kind != V::Kind::Array || cs->items.size() > 64)
    return false;
  std::int64_t transform{};
  for (const auto &c : cs->items) {
    std::int64_t id{};
    if (!Ref(c.Get("component"), id))
      return false;
    const auto *info = sf->Find(id);
    if (!info)
      return false;
    if (info->class_id == 4) {
      if (transform)
        return false;
      transform = id;
    }
  }
  if (!transform)
    return false;
  std::set<std::int64_t> seen;
  tile.origin = {};
  while (transform) {
    if (seen.size() >= 64 || !seen.insert(transform).second)
      return false;
    auto t = Object(sf, transform, 4);
    Point p, scale, q;
    float qw{};
    if (!t || !P(t->Get("m_LocalPosition"), p) ||
        !P(t->Get("m_LocalScale"), scale) || !P(t->Get("m_LocalRotation"), q) ||
        !F(t->Get("m_LocalRotation")->Get("w"), qw) || scale.x != 1 ||
        scale.y != 1 || scale.z != 1 || q.x != 0 || q.y != 0 || q.z != 0 ||
        qw != 1)
      return false;
    tile.origin.x += p.x;
    tile.origin.y += p.y;
    tile.origin.z += p.z;
    if (!std::isfinite(tile.origin.x) || !std::isfinite(tile.origin.y) ||
        !std::isfinite(tile.origin.z))
      return false;
    if (!Ref(t->Get("m_Father"), transform))
      return false;
  }
  return tile.origin.x == tile.bounds.x && tile.origin.y == tile.bounds.y;
}
} // namespace
Reader::Reader(ce_unity::RangeReader romfs, ce_unity::AstcDecoder decode)
    : unity(std::move(romfs), decode, std::string(Root), 3),
      astc(std::move(decode)) {
  unity.SetUseBuiltinPins(false);
}
Reader::Reader(const EdenDsmodHostApi *host, ce_unity::AstcDecoder decode)
    : Reader(ce_unity::MakeHostRangeReader(host), std::move(decode)) {}
std::optional<ResolvedRef>
Reader::ResolveMaterial(const belmont_map_sources::SourceRecipe &recipe,
                        Source source) {
  std::lock_guard lock(mutex);
  if (!belmont_map_sources::Canonical(recipe))
    return {};
  try {
    auto tiles = LoadLayout(recipe);
    if (!tiles || tiles->empty())
      return {};
    if (source == Source::Geometry || source == Source::Background) {
      const auto &m = tiles->front().material;
      for (const auto &t : *tiles)
        if (t.material != m)
          return {};
      return m;
    }
    if (source != Source::SecretGeometry)
      return {};
    auto sf = File(unity, recipe.map_bundle, recipe.map_cab);
    auto context = Object(sf, recipe.map_context_id, 114);
    std::int64_t settings{};
    if (!context || !Ref(context->Get("settings"), settings))
      return {};
    auto value = Object(sf, settings, 114);
    return value ? Resolve(unity, recipe,
                           ResolvedRef{std::string(recipe.map_bundle),
                                       std::string(recipe.map_cab), settings},
                           value->Get("concealedSecretZoneMapMaterial"))
                 : std::nullopt;
  } catch (...) {
    return {};
  }
}
std::optional<V> Reader::ReadMaterial(const ResolvedRef &material) {
  std::lock_guard lock(mutex);
  try {
    return ResolvedObject(unity, material, 21);
  } catch (...) {
    return {};
  }
}
std::optional<ResolvedRef>
Reader::ResolveMaterialTexture(const belmont_map_sources::SourceRecipe &recipe,
                               const ResolvedRef &material,
                               std::string_view property) {
  std::lock_guard lock(mutex);
  if (!belmont_map_sources::Canonical(recipe) || property.empty() ||
      property.size() > 128)
    return {};
  try {
    return MaterialTexture(unity, recipe, material, property);
  } catch (...) {
    return {};
  }
}
void Reader::ClearCache() {
  std::lock_guard lock(mutex);
  layout.reset();
  layout_source.reset();
  unity.ClearCache();
}
std::optional<std::vector<Tile>> Reader::LoadStreetsPoorLayout() {
  return LoadLayout(belmont_map_sources::StreetsPoor());
}
std::optional<std::vector<Tile>>
Reader::LoadLayout(const belmont_map_sources::SourceRecipe &recipe) {
  std::lock_guard lock(mutex);
  try {
    if (!belmont_map_sources::Canonical(recipe))
      return {};
    if (layout && layout_source && *layout_source == recipe)
      return layout;
    layout.reset();
    layout_source.reset();
    auto sf = File(unity, recipe.map_bundle, recipe.map_cab);
    auto context = Object(sf, recipe.map_context_id, 114);
    std::int64_t renderer_id{}, script_id{};
    if (!context ||
        !N(context->Get("m_Script") ? context->Get("m_Script")->Get("m_PathID")
                                    : nullptr,
           script_id) ||
        script_id != 6647830585995460756LL ||
        !Ref(context->Get("levelMapRenderer"), renderer_id))
      return {};
    auto renderer = Object(sf, renderer_id, 114);
    const auto *refs = renderer ? renderer->Get("tileRenderers") : nullptr;
    if (!refs || refs->kind != V::Kind::Array || refs->items.empty() ||
        refs->items.size() > 128)
      return {};
    std::vector<Tile> decoded;
    std::set<std::uint32_t> layers;
    // Every tile's material PPtr is resolved against the same owner
    // (recipe.map_bundle/map_cab); memoize that owner's external-CAB table for
    // the duration of this one LoadLayout() call instead of reparsing it once
    // per tile (up to 128 refs->items).
    CabsCache cabs_cache;
    for (const auto &ref : refs->items) {
      Tile tile;
      std::int64_t filter{}, go{}, filter_go{}, mesh_renderer{};
      if (!Ref(&ref, tile.renderer_id) || !tile.renderer_id)
        return {};
      auto tr = Object(sf, tile.renderer_id, 114);
      const auto *b = tr ? tr->Get("bounds") : nullptr;
      if (!tr || !Ref(tr->Get("tileMeshFilter"), filter) ||
          !Ref(tr->Get("m_GameObject"), go) ||
          !Ref(tr->Get("tileRenderer"), mesh_renderer) || !b ||
          !F(b->Get("x"), tile.bounds.x) || !F(b->Get("y"), tile.bounds.y) ||
          !F(b->Get("width"), tile.bounds.width) ||
          !F(b->Get("height"), tile.bounds.height) || tile.bounds.width <= 0 ||
          tile.bounds.height <= 0)
        return {};
      auto mr = Object(sf, mesh_renderer, 23);
      const auto *materials = mr ? mr->Get("m_Materials") : nullptr;
      if (!materials || materials->kind != V::Kind::Array ||
          materials->items.size() != 1)
        return {};
      auto material =
          Resolve(unity, recipe,
                  ResolvedRef{std::string(recipe.map_bundle),
                              std::string(recipe.map_cab), mesh_renderer},
                  &materials->items[0], &cabs_cache);
      // A missing dependency does not hide authored layout metadata. Keep
      // CAB/PPtr identity from the native table even if its member is not
      // installed/cached.
      if (!material)
        return {};
      tile.material = *material;
      auto mf = Object(sf, filter, 33);
      if (!mf || !Ref(mf->Get("m_GameObject"), filter_go) ||
          !Ref(mf->Get("m_Mesh"), tile.mesh_id))
        return {};
      auto mesh = Object(sf, tile.mesh_id, 43);
      if (!mesh || !Mesh(*mesh, tile) || !Origin(sf, filter_go, tile) ||
          !layers.insert(tile.layer).second)
        return {};
      decoded.push_back(std::move(tile));
    }
    layout = decoded;
    layout_source = *belmont_map_sources::Canonical(recipe);
    return decoded;
  } catch (...) {
    return {};
  }
}
std::optional<Layer> Reader::LoadStreetsPoorLayer(Source source,
                                                  std::uint32_t index) {
  return LoadLayer(belmont_map_sources::StreetsPoor(), source, index);
}
std::optional<Layer>
Reader::LoadLayer(const belmont_map_sources::SourceRecipe &source_recipe,
                  Source source, std::uint32_t index) {
  std::lock_guard lock(mutex);
  try {
    auto material = ResolveMaterial(source_recipe, source);
    if (!material)
      return {};
    std::string_view property;
    switch (source) {
    case Source::Geometry:
      property = "_MainTexArray";
      break;
    case Source::SecretGeometry:
      property = "_MainTexArray";
      break;
    case Source::Background:
      property = "_BackgroundTexArray";
      break;
    default:
      return {};
    }
    auto recipe = MaterialTexture(unity, source_recipe, *material, property);
    if (!recipe)
      return {};
    auto sf = File(unity, recipe->bundle, recipe->cab);
    auto t = Object(sf, recipe->id, 187);
    const auto *name = t ? t->Get("m_Name") : nullptr;
    std::int64_t w{}, h{}, d{}, mips{}, fmt{}, size{}, off{}, stream_size{};
    if (!t || !name || name->kind != V::Kind::String ||
        !N(t->Get("m_Width"), w) || !N(t->Get("m_Height"), h) || w <= 0 ||
        h <= 0 || w > 2048 || h > 2048 || !N(t->Get("m_Depth"), d) || d <= 0 ||
        d > 128 || index >= d || !N(t->Get("m_MipCount"), mips) || mips != 1 ||
        !N(t->Get("m_Format"), fmt) || !N(t->Get("m_DataSize"), size) ||
        size <= 0 || std::uint64_t(size) > ce_unity::MaxAlloc)
      return {};
    std::uint64_t layer_bytes{};
    int texture_format{};
    if (fmt == 4 || fmt == 8) {
      layer_bytes = std::uint64_t(w) * h * 4;
      texture_format = 4;
    } else if (fmt == 100 || fmt == 101) {
      layer_bytes = std::uint64_t((w + 3) / 4) * ((h + 3) / 4) * 16;
      texture_format = 12; // Native RGBA_DXT5_UNorm, linear RGB.
    } else if (fmt == 139) {
      if (!astc)
        return {};
      layer_bytes = std::uint64_t((w + 11) / 12) * ((h + 11) / 12) * 16;
      texture_format = 59;
    } else
      return {};
    if (layer_bytes * d != std::uint64_t(size) ||
        layer_bytes > ce_unity::MaxAlloc)
      return {};
    const auto *stream = t->Get("m_StreamData");
    const auto *path = stream ? stream->Get("path") : nullptr;
    if (!stream || !path || path->kind != V::Kind::String ||
        !N(stream->Get("offset"), off) || off < 0 ||
        !N(stream->Get("size"), stream_size) || stream_size != size)
      return {};
    auto bundle = unity.OpenBundle(recipe->bundle);
    const auto *node = bundle ? bundle->FindNode(path->s) : nullptr;
    if (!node || std::uint64_t(off) > node->size ||
        std::uint64_t(size) > node->size - std::uint64_t(off))
      return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(layer_bytes));
    if (!bundle->Read(node->offset + std::uint64_t(off) + layer_bytes * index,
                      bytes.data(), bytes.size()))
      return {};
    ce_unity::TextureInfo info;
    info.width = static_cast<int>(w);
    info.height = static_cast<int>(h);
    info.format = texture_format;
    auto image = ce_unity::DecodeTextureData(bytes, info, astc, 0);
    if (!image)
      return {};
    return Layer{std::move(*image), static_cast<std::int32_t>(fmt),
                 static_cast<std::uint32_t>(d), index,
                 fmt == 4 || fmt == 100 || fmt == 139};
  } catch (...) {
    return {};
  }
}
} // namespace belmont_map_tiles
