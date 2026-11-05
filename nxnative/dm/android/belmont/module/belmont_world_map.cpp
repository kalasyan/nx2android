// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_world_map.h"
#include "belmont_map_assets.h"
#include "belmont_map_decoration.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
namespace belmont_world_map {
namespace {
struct Color {
  float r{}, g{}, b{}, a{};
};
float Clamp(float v) { return std::clamp(v, 0.f, 1.f); }
float Smooth(float v) {
  v = Clamp(v);
  return v * v * (3.f - 2.f * v);
}
float Linear(float v) {
  return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
}
float Gamma(float v) {
  v = Clamp(v);
  return v <= .0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - .055f;
}
Color Scale(Color v, float a) { return {v.r * a, v.g * a, v.b * a, v.a}; }
Color Mix(Color a, Color b, float t) {
  t = Clamp(t);
  return {std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t),
          std::lerp(a.b, b.b, t), std::lerp(a.a, b.a, t)};
}
float Dot(Color a, Color b) {
  return a.r * b.r + a.g * b.g + a.b * b.b + a.a * b.a;
}
float TexelLinear(std::uint8_t v) {
  static const auto table = [] {
    std::array<float, 256> a{};
    for (unsigned i = 0; i < 256; ++i)
      a[i] = Linear(float(i) / 255.f);
    return a;
  }();
  return table[v];
}
Color Sample(const ce_unity::Image &im, float u, float v, bool repeat,
             bool srgb) {
  const float px = u * float(im.width) - .5f,
              py = (1.f - v) * float(im.height) - .5f;
  const auto x0 = static_cast<int>(std::floor(px)),
             y0 = static_cast<int>(std::floor(py));
  auto index = [repeat](int i, int n) {
    if (repeat) {
      i %= n;
      if (i < 0)
        i += n;
      return i;
    }
    return std::clamp(i, 0, n - 1);
  };
  auto texel = [&](int x, int y) {
    const auto o = (std::size_t(index(y, int(im.height))) * im.width +
                    std::size_t(index(x, int(im.width)))) *
                   4;
    Color c{im.rgba[o] / 255.f, im.rgba[o + 1] / 255.f, im.rgba[o + 2] / 255.f,
            im.rgba[o + 3] / 255.f};
    if (srgb) {
      c.r = TexelLinear(im.rgba[o]);
      c.g = TexelLinear(im.rgba[o + 1]);
      c.b = TexelLinear(im.rgba[o + 2]);
    }
    return c;
  };
  return Mix(Mix(texel(x0, y0), texel(x0 + 1, y0), px - float(x0)),
             Mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), px - float(x0)),
             py - float(y0));
}
bool ImageValid(const ce_unity::Image &i) {
  return i.width && i.height && i.width <= 2048 && i.height <= 2048 &&
         i.rgba.size() == std::size_t(i.width) * i.height * 4;
}

constexpr std::size_t MaximumImageBytes = 16u * 1024u * 1024u;
// Reveal edge in world units beyond the explored-zone SDF: solid, then a
// smoothstep fade (fitted to native map output, 2026-10-20).
constexpr float kRevealSolid = .2f, kRevealFade = 1.3f;
using Rect = belmont_map_fog::Rect;
using ZoneKey = std::pair<std::size_t, std::size_t>;
bool Overlaps(Rect a, Rect b) {
  return a.x <= b.x + b.width && a.x + a.width >= b.x &&
         a.y <= b.y + b.height && a.y + a.height >= b.y;
}
Rect Inflate(Rect r, float by) {
  return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by};
}
void PixelRgb(std::uint8_t *out, Color c) {
  out[0] = std::uint8_t(std::lround(Gamma(c.r) * 255.f));
  out[1] = std::uint8_t(std::lround(Gamma(c.g) * 255.f));
  out[2] = std::uint8_t(std::lround(Gamma(c.b) * 255.f));
}
} // namespace
struct Renderer::State {
  mutable std::recursive_mutex mutex;
  ce_unity::Reader unity;
  belmont_assets::Reader assets;
  belmont_map_tiles::Reader tiles;
  belmont_map_fog::Reader fog;
  belmont_map_assets::Reader registry;
  const belmont_map_sources::SourceRecipe *source =
      &belmont_map_sources::StreetsPoor();
  std::optional<belmont_map_fog::Data> data;
  std::vector<belmont_map_tiles::Tile> layout;
  std::map<std::string, std::size_t, std::less<>> room_indices;
  std::map<std::string, float, std::less<>> numbers;
  std::map<std::string, Color, std::less<>> vectors;
  std::set<std::int64_t> navigation_ids;
  std::int64_t main_navigation{};
  Rect bounds{};
  unsigned width{}, height{}, mask_width{}, mask_height{}, pixels_per_world{};
  struct TileColor {
    unsigned x{}, y{}, w{}, h{};
    std::vector<std::uint8_t> clean, revealed;
    std::vector<std::uint16_t> ornament_gate;
  };
  std::vector<TileColor> colors;
  ce_unity::Image ornament_noise;
  bool ornament_noise_srgb{};
  Color ornament_nst{}, ornament_perlin_st{}, ornament_ink{};
  belmont_map_decoration::Lines ornament_lines;
  std::set<ZoneKey> visible;
  std::shared_ptr<const Frame> front;
  Statistics statistics;
  bool metadata_ready{}, colors_ready{}, colors_z{};
  std::int64_t published_navigation{};
  State(ce_unity::RangeReader range, ce_unity::AstcDecoder astc)
      : unity(range, astc), assets(range, astc), tiles(range, astc),
        fog(range), registry(range) {}
  void Reset(bool reset_statistics) {
    metadata_ready = colors_ready = false;
    published_navigation = 0;
    data.reset();
    layout.clear();
    colors.clear();
    ornament_noise = {};
    visible.clear();
    front.reset();
    room_indices.clear();
    navigation_ids.clear();
    numbers.clear();
    vectors.clear();
    if (reset_statistics) statistics = {};
    statistics.source_color_bytes = statistics.current_frame_bytes = 0;
    unity.ClearCache();
    assets.ClearCache();
    tiles.ClearCache();
    fog.ClearCache();
    registry.ClearCache();
  }
  bool Select(const belmont_map_sources::SourceRecipe &candidate) {
    const auto *canonical = belmont_map_sources::Canonical(candidate);
    if (!canonical) return false;
    if (source != canonical) {
      Reset(false);
      source = canonical;
    }
    return true;
  }
  bool Metadata() {
    if (metadata_ready)
      return true;
    auto d = fog.Load(*source);
    auto l = tiles.LoadLayout(*source);
    auto world = registry.LoadWorld(*source);
    if (!d || !l || !world || d->world_name != source->world_name ||
        world->name != source->world_name || d->rooms.empty() ||
        d->rooms.size() > 128 || l->empty() || l->size() > 64)
      return false;
    std::set<std::int64_t> navs(d->navigation_zone_ids.begin(),
                                d->navigation_zone_ids.end());
    if (navs.empty() || navs.size() > 64 || navs.contains(0) ||
        navs.size() != d->navigation_zone_ids.size() ||
        !navs.contains(d->main_navigation_zone_id))
      return false;
    std::map<std::string, std::size_t, std::less<>> ri;
    for (std::size_t i = 0; i < d->rooms.size(); ++i) {
      const auto &room = d->rooms[i];
      if (room.id.empty() || room.zones.empty() ||
          std::none_of(world->rooms.begin(), world->rooms.end(),
                       [&](const auto &r) { return r.id == room.id; }) ||
          !ri.emplace(room.id, i).second)
        return false;
      const auto nav = room.zones.front().navigation_zone_id;
      if (!navs.contains(nav))
        return false;
      std::set<int> ids;
      for (const auto &zone : room.zones)
        if (zone.navigation_zone_id != nav || !ids.insert(zone.id).second)
          return false;
    }
    float minx = std::numeric_limits<float>::max(), miny = minx, maxx = -minx,
          maxy = -minx;
    for (const auto &t : *l) {
      const auto &b = t.bounds;
      if (!std::isfinite(b.x) || !std::isfinite(b.y) ||
          !std::isfinite(b.width) || !std::isfinite(b.height) || b.width <= 0 ||
          b.height <= 0)
        return false;
      minx = std::min(minx, b.x);
      miny = std::min(miny, b.y);
      maxx = std::max(maxx, b.x + b.width);
      maxy = std::max(maxy, b.y + b.height);
      auto setting =
          std::find_if(d->tiles.begin(), d->tiles.end(), [&](const auto &v) {
            return v.renderer_id == t.renderer_id;
          });
      if (setting == d->tiles.end() || setting->smoothness <= 0 ||
          setting->smoothness > 16 || setting->box_roundness != 0 ||
          setting->quad_roundness != 0)
        return false;
    }
    unsigned scale = 3;
    unsigned w{}, h{};
    for (; scale; --scale) {
      const double wf = double(maxx - minx) * scale,
                   hf = double(maxy - miny) * scale;
      if (!std::isfinite(wf) || !std::isfinite(hf) || wf < 1 || hf < 1 ||
          wf > 4096 || hf > 4096 || std::abs(wf - std::round(wf)) > .0001 ||
          std::abs(hf - std::round(hf)) > .0001)
        continue;
      w = unsigned(std::lround(wf)); h = unsigned(std::lround(hf));
      if (std::uint64_t(w) * h * 4 <= MaximumImageBytes) break;
    }
    if (!scale) return false;
    std::vector<TileColor> tc;
    std::uint64_t covered = 0;
    for (const auto &t : *l) {
      const auto &b = t.bounds;
      TileColor c;
      const double xf = double(b.x - minx) * scale,
                   yf = double(maxy - b.y - b.height) * scale,
                   tw = double(b.width) * scale,
                   th = double(b.height) * scale;
      for (double v : {xf, yf, tw, th})
        if (v < 0 || v > 4096 || std::abs(v - std::round(v)) > .0001)
          return false;
      c.x = unsigned(std::lround(xf));
      c.y = unsigned(std::lround(yf));
      c.w = unsigned(std::lround(tw));
      c.h = unsigned(std::lround(th));
      if (!c.w || !c.h || c.x + c.w > w || c.y + c.h > h)
        return false;
      for (const auto &prior : tc)
        if (c.x < prior.x + prior.w && c.x + c.w > prior.x &&
            c.y < prior.y + prior.h && c.y + c.h > prior.y)
          return false;
      covered += std::uint64_t(c.w) * c.h;
      tc.push_back(std::move(c));
    }
    if (covered != std::uint64_t(w) * h)
      return false;
    data = std::move(d);
    layout = std::move(*l);
    colors = std::move(tc);
    room_indices = std::move(ri);
    navigation_ids = std::move(navs);
    main_navigation = data->main_navigation_zone_id;
    pixels_per_world = scale;
    bounds = {minx, miny, maxx - minx, maxy - miny};
    width = w;
    height = h;
    // Fixed home-scale geometric mask: one source world unit per sample.
    // Native .1 is screen-relative; applying it to the whole atlas would
    // shrink narrow rooms at a coarse3.3world-unit grid. Camera/zoom parity
    // is intentionally not claimed by this fixed-world companion bitmap.
    mask_width = std::max(1u, unsigned(std::ceil(bounds.width)));
    mask_height = std::max(1u, unsigned(std::ceil(bounds.height)));
    metadata_ready = true;
    return true;
  }
  struct SamplerImage {
    ce_unity::Image image;
    bool srgb{};
  };
  std::optional<SamplerImage>
  LoadSampler(const belmont_map_tiles::ResolvedRef &ref) {
    auto bundle = unity.OpenBundle(ref.bundle);
    const auto *node = bundle ? bundle->FindNode(ref.cab) : nullptr;
    if (!node) return {};
    // The existing 2D decoder takes bundle+ID. Require one serialized CAB so
    // its address is exactly the PPtr-resolved CAB, never another node's ID.
    unsigned serialized_nodes = 0;
    for (const auto &candidate : bundle->Nodes()) {
      if (candidate.path.ends_with(".resS") ||
          candidate.path.ends_with(".resource")) continue;
      if (candidate.path != node->path) return {};
      ++serialized_nodes;
    }
    if (serialized_nodes != 1) return {};
    auto file = ce_unity::SerializedFile::Open(bundle->NodeSource(*node));
    const auto *object = file ? file->Find(ref.id) : nullptr;
    if (!object || object->class_id != 28 || file->Platform() != 38 ||
        file->Version() != 22) return {};
    auto texture = file->ReadObject(*object);
    if (!texture) return {};
    const auto *space = texture->Get("m_ColorSpace"),
               *filter = texture->Path("m_TextureSettings.m_FilterMode"),
               *wrap_u = texture->Path("m_TextureSettings.m_WrapU"),
               *wrap_v = texture->Path("m_TextureSettings.m_WrapV");
    if (!space || (space->AsInt() != 0 && space->AsInt() != 1) ||
        !filter || filter->AsInt() != 1 || !wrap_u || !wrap_v ||
        wrap_u->AsInt() != 0 || wrap_v->AsInt() != 0) return {};
    auto image = assets.LoadImage("native:" + ref.bundle + "#" +
                                  std::to_string(ref.id));
    if (!image || !ImageValid(*image)) return {};
    return SamplerImage{std::move(*image), space->AsInt() == 1};
  }
  bool Colors(bool zmap) {
    if (colors_ready && colors_z == zmap)
      return true;
    colors_ready = false;
    const auto start = std::chrono::steady_clock::now();
    if (!Metadata())
      return false;
    auto material_ref = tiles.ResolveMaterial(
        *source, belmont_map_tiles::Source::Geometry);
    if (!material_ref || std::any_of(layout.begin(), layout.end(),
          [&](const auto &tile) { return tile.material != *material_ref; }))
      return false;
    auto material = tiles.ReadMaterial(*material_ref);
    const auto *shader = material ? material->Path("m_Shader.m_PathID") : nullptr;
    const auto *keywords = material ? material->Get("m_ValidKeywords") : nullptr;
    if (!shader || shader->AsInt() != -1071992361143225679LL || !keywords)
      return false;
    std::set<std::string_view> active_keywords;
    for (const auto &keyword : keywords->items)
      active_keywords.insert(keyword.AsString());
    const std::set<std::string_view> supported_keywords{
        "_BACKGROUNDMODE_SHOWASBACKGROUND", "_SURFACE_TYPE_TRANSPARENT",
        "_USEDISTRICTSSTATETEXTURE", "_USEFOW", "_USETEXTUREARRAY"};
    if (active_keywords != supported_keywords) return false;
    auto background_ref = tiles.ResolveMaterialTexture(
        *source, *material_ref, "_Background");
    auto noise_ref = tiles.ResolveMaterialTexture(
        *source, *material_ref, "_SDGMaskNoiseTex");
    // A null skyline is not a white/default texel. Ending remains unavailable
    // until the native null binding/branch is independently proved.
    auto skyline_ref = tiles.ResolveMaterialTexture(
        *source, *material_ref, "_BackgroundTexArray");
    if (!background_ref || !noise_ref || !skyline_ref) return false;
    auto bg = LoadSampler(*background_ref), n = LoadSampler(*noise_ref);
    if (!bg || !n) return false;
    const auto *fp = material->Path("m_SavedProperties.m_Floats");
    const auto *cp = material->Path("m_SavedProperties.m_Colors");
    if (!fp || !cp || fp->items.size() > 256 || cp->items.size() > 256)
      return false;
    numbers.clear();
    vectors.clear();
    for (const auto &pair : fp->items) {
      if (pair.items.size() != 2)
        return false;
      const float v = float(pair.items[1].AsFloat());
      if (!std::isfinite(v))
        return false;
      numbers.emplace(std::string(pair.items[0].AsString()), v);
    }
    for (const auto &pair : cp->items) {
      if (pair.items.size() != 2)
        return false;
      const auto &v = pair.items[1];
      Color q;
      const char *keys[] = {"r", "g", "b", "a"};
      float vals[4];
      for (int j = 0; j < 4; ++j) {
        const auto *f = v.Get(keys[j]);
        if (!f)
          return false;
        vals[j] = float(f->AsFloat());
        if (!std::isfinite(vals[j]))
          return false;
      }
      q = {vals[0], vals[1], vals[2], vals[3]};
      vectors.emplace(std::string(pair.items[0].AsString()), q);
    }
    for (const auto name :
         {"_SelectedBackColor", "_SelectedEdgeColor", "_LineColor",
          "_BackgroundST", "_SDGMaskNoiseTexST", "_BakcgroundNoiseMask",
          "_GlobalNoiseST", "_BakgroundTexNoiseStrength", "_WaterColor",
          "_WaterNoiseStrength", "_SDGMaskNoiseStrength", "_GradientColorPerlinST"})
      if (!vectors.contains(name))
        return false;
    for (const auto name :
         {"_EdgeDetectMin", "_EdgeDetectMax", "_AlphaSDFRange",
          "_AlphaSDFContrast", "_CloudSDFMaskRange", "_CloudSDFMaskContrast",
          "_WaterSDFMaskRange", "_WaterSDFMaskContrast", "_WaterThickness",
          "_WaterContrast", "_WaterLineDisplaceNumber", "_WaterLineDisplaceStrength",
          "_CollisionSDFMaskRange", "_CollisionSDFMaskContrast", "_SDGGlobalRange",
          "_SDGGlobalContrast", "_MainLineAddRange", "_MainLineThickness", "_MainLineContrast",
          "_SecondLineAddRange", "_SecondLineThickness", "_SecondLineContrast",
          "_MainLineNoiseMaskStrength", "_MainLineNoiseMaskContrast",
          "_SecondLineNoiseMaskStrength", "_SecondLineNoiseMaskContrast",
          "_GradientColorRange", "_GradientColorContrast",
          "_GradientColorPerlinMaskRange", "_GradientColorPerlinMaskContrast"})
      if (!numbers.contains(name))
        return false;

    const auto back = Property("_SelectedBackColor", true),
               edge = Property("_SelectedEdgeColor", true),
               bgst = Property("_BackgroundST"),
               nst = Property("_SDGMaskNoiseTexST"),
               weights = Property("_BakcgroundNoiseMask"),
               global_st = Property("_GlobalNoiseST"),
               background_strength = Property("_BakgroundTexNoiseStrength");
    const auto water_color = Property("_WaterColor", true),
               water_noise_weights = Property("_WaterNoiseStrength");
    const belmont_map_decoration::Water water{
        Number("_WaterSDFMaskRange"), Number("_WaterSDFMaskContrast"),
        Number("_WaterThickness"), Number("_WaterContrast")};
    if (water.collision_contrast >= 1.f || water.contrast >= 1.f) return false;
    const auto displacement = Property("_SDGMaskNoiseStrength");
    ornament_lines = {Number("_SDGGlobalRange"), Number("_MainLineAddRange"),
        Number("_MainLineThickness"), Number("_MainLineContrast"),
        Number("_SecondLineAddRange"), Number("_SecondLineThickness"), Number("_SecondLineContrast"),
        Number("_MainLineNoiseMaskStrength"), Number("_MainLineNoiseMaskContrast"),
        Number("_SecondLineNoiseMaskStrength"), Number("_SecondLineNoiseMaskContrast"),
        {displacement.r, displacement.g, displacement.b, displacement.a},
        Number("_SDGGlobalContrast"), Number("_GradientColorRange"), Number("_GradientColorContrast"),
        Number("_GradientColorPerlinMaskRange"), Number("_GradientColorPerlinMaskContrast")};
    ornament_nst = nst; ornament_perlin_st = Property("_GradientColorPerlinST");
    ornament_ink = Property("_LineColor", true);
    const float collision_range = Number("_CollisionSDFMaskRange"),
                collision_width = 1.f - Number("_CollisionSDFMaskContrast"),
                cloud_range = Number("_CloudSDFMaskRange"),
                cloud_width = 1.f - Number("_CloudSDFMaskContrast");
    const float water_repetitions = Number("_WaterLineDisplaceNumber"),
                water_strength = Number("_WaterLineDisplaceStrength");
    const float emin = Number("_EdgeDetectMin"),
                emax = Number("_EdgeDetectMax");
    if (emax <= emin)
      return false;
    // Hoisted out of the per-pixel loop below (same values every iteration);
    // matches the collision_range/cloud_range/emin/emax hoisting above.
    const float alpha_range = std::max(Number("_AlphaSDFRange"), .001f),
                alpha_width = std::max(.0001f, 1.f - Number("_AlphaSDFContrast"));
    for (std::size_t i = 0; i < layout.size(); ++i) {
      auto geometry = tiles.LoadLayer(
          *source, belmont_map_tiles::Source::Geometry, layout[i].layer);
      std::optional<belmont_map_tiles::Layer> skyline;
      if (!zmap)
        skyline = tiles.LoadLayer(
            *source, belmont_map_tiles::Source::Background, layout[i].layer);
      if (!geometry || !ImageValid(geometry->image) ||
          (!zmap && (!skyline || !ImageValid(skyline->image))))
        return false;
      const auto &tile = layout[i];
      auto &c = colors[i];
      c.clean.resize(std::size_t(c.w) * c.h * 3);
      c.revealed.resize(c.clean.size());
      c.ornament_gate.resize(std::size_t(c.w) * c.h);
      for (unsigned y = 0; y < c.h; ++y)
        for (unsigned x = 0; x < c.w; ++x) {
          const float u = (float(x) + .5f) / float(c.w),
                      v = (float(c.h - y) - .5f) / float(c.h),
                      wx = tile.bounds.x + u * tile.bounds.width,
                      wy = tile.bounds.y + v * tile.bounds.height;
          const auto noise = Sample(n->image, wx * nst.r, wy * nst.g, true, n->srgb);
          const auto global_noise = zmap
              ? Sample(n->image, wx * global_st.r, wy * global_st.g, true, n->srgb)
              : noise;
          // Sampled once and reused below (native_selection used to resample
          // the identical (skyline, u, v) a second time per pixel).
          const float skyline_r =
              zmap ? 0.f : Sample(skyline->image, u, v, false, skyline->srgb).r;
          auto clean = Sample(bg->image, wx * bgst.r, wy * bgst.g, true, bg->srgb);
          clean = Scale(clean, Clamp(1.f - .25f * Dot(global_noise, weights)));
          // Native Z branch replaces 1-skyline.R by 1 before min(noise, factor).
          // Main appearance retains its previously validated still approximation.
          const float selection = zmap
              ? std::min(1.f - .25f * Dot(global_noise, background_strength), 1.f)
              : 0.f;
          clean = Scale(clean, zmap ? 1.f - selection : skyline_r);
          const auto fields =
              Sample(geometry->image, u, v, false, geometry->srgb);
          const float outline = Smooth((fields.b - emin) / (emax - emin));
          const float opacity =
              1.f - Smooth((fields.g - alpha_range) / alpha_width);
          auto ink =
              Scale(Mix(Scale(back, 1.f - fields.r), edge, outline),
                    1.f - .1f * noise.g);
          if (zmap) {
            // Native collision/gradient decoration also depends on the cloud,
            // gradient and moving-noise masks. Omit that decorative layer;
            // applying LineColor from collision alone invents a gold band.
            ink = Scale(ink, selection);
          }
          // Native source alpha is the water distance field. Restore its
          // material color/noise/shoreline contribution at time zero; keep it
          // inside this same source-backed reveal, including side navigation.
          const auto water_noise = zmap ? global_noise : Sample(n->image,
              wx * global_st.r, wy * global_st.g, true, n->srgb);
          const auto water_field = Sample(geometry->image, u,
              v + belmont_map_decoration::WaterOffset(wx,
                  water_repetitions, water_strength),
              false, geometry->srgb);
          const float water_mask = belmont_map_decoration::WaterMask(
              water_field.a, fields.b, Dot(water_noise, water_noise_weights), water);
          // Native noise attenuation may be negative: preserve extrapolation
          // and clamp only the final output, as with the shader's linear RGB.
          ink = {std::lerp(ink.r, water_color.r, water_mask),
                 std::lerp(ink.g, water_color.g, water_mask),
                 std::lerp(ink.b, water_color.b, water_mask), ink.a};
          const float cloud = belmont_map_decoration::Gate(fields.g - cloud_range, cloud_width);
          const float open = 1.f - belmont_map_decoration::Gate(fields.g - collision_range, collision_width);
          const float native_selection = std::min(1.f - .25f * Dot(water_noise, background_strength),
              zmap ? 1.f : 1.f - skyline_r);
          // Quantized source-only gate, not another revealed image. The small
          // retained native noise supplies the four moving patterns at time0.
          c.ornament_gate[std::size_t(y) * c.w + x] = std::uint16_t(std::lround(
              Clamp((1.f - cloud) * open * std::max(open, native_selection) * opacity) * 65535.f));
          const auto offset = (std::size_t(y) * c.w + x) * 3;
          PixelRgb(c.clean.data() + offset, clean);
          PixelRgb(c.revealed.data() + offset, Mix(clean, ink, opacity));
        }
    }
    ornament_noise_srgb = n->srgb;
    ornament_noise = std::move(n->image);
    // Static RGB source tiles now own every needed sample. Drop duplicate
    // Unity metadata/decompression caches before immutable output allocation.
    unity.ClearCache();
    assets.ClearCache();
    tiles.ClearCache();
    fog.ClearCache();
    registry.ClearCache();
    colors_z = zmap;
    colors_ready = true;
    statistics.baked_tiles += colors.size();
    statistics.source_color_bytes = 0;
    for (const auto &c : colors)
      statistics.source_color_bytes += c.clean.size() + c.revealed.size() +
          c.ornament_gate.size() * sizeof(std::uint16_t);
    statistics.source_color_bytes += ornament_noise.rgba.size();
    statistics.cold_source_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
    return true;
  }
  Color Property(std::string_view key, bool is_color = false) const {
    auto p = vectors.find(key);
    Color v = p == vectors.end() ? Color{} : p->second;
    if (is_color) {
      v.r = Linear(v.r);
      v.g = Linear(v.g);
      v.b = Linear(v.b);
    }
    return v;
  }
  float Number(std::string_view key) const {
    auto p = numbers.find(key);
    return p == numbers.end() ? 0.f : p->second;
  }
  const belmont_map_fog::Zone &Zone(ZoneKey key) const {
    return data->rooms[key.first].zones[key.second];
  }
  bool Visible(const Request &req, std::set<ZoneKey> &result,
               std::int64_t &selected_navigation) {
    if (req.current_room_id.empty() || req.explored_rooms.size() > 4096 ||
        req.culled_navigation_zone_ids.size() > 256 ||
        !std::isfinite(req.player_x) || !std::isfinite(req.player_y) ||
        !Metadata())
      return false;
    const auto current = room_indices.find(req.current_room_id);
    if (current == room_indices.end())
      return false;
    const auto navigation =
        data->rooms[current->second].zones.front().navigation_zone_id;
    if (!navigation_ids.contains(navigation) ||
        std::find(req.culled_navigation_zone_ids.begin(),
                  req.culled_navigation_zone_ids.end(),
                  navigation) != req.culled_navigation_zone_ids.end())
      return false;
    if (req.player_x < bounds.x - 8 || req.player_y < bounds.y - 8 ||
        req.player_x > bounds.x + bounds.width + 8 ||
        req.player_y > bounds.y + bounds.height + 8)
      return false;
    selected_navigation = navigation;
    bool current_ready = false;
    std::set<std::string_view> seen;
    std::size_t count = 0;
    for (const auto &record : req.explored_rooms) {
      if (record.room_id.size() > 1024 ||
          record.entered_zone_ids.size() > 4096 ||
          count > 65536 - record.entered_zone_ids.size())
        return false;
      count += record.entered_zone_ids.size();
      if (!seen.insert(record.room_id).second)
        return false;
      const auto room = room_indices.find(record.room_id);
      if (room == room_indices.end())
        continue;
      const auto &source = data->rooms[room->second];
      std::set<int> ids;
      for (int id : record.entered_zone_ids) {
        if (!ids.insert(id).second)
          return false;
        auto zone = std::find_if(source.zones.begin(), source.zones.end(),
                                 [&](const auto &z) { return z.id == id; });
        if (zone == source.zones.end())
          return false;
        if (zone->navigation_zone_id == navigation)
          result.emplace(room->second,
                         std::size_t(zone - source.zones.begin()));
      }
      if (room == current && !record.entered_zone_ids.empty())
        current_ready = true;
    }
    return current_ready && !result.empty();
  }
  bool Compose(std::size_t i, const std::set<ZoneKey> &current, Frame &target) {
    const auto &tile = layout[i];
    const auto &color = colors[i];
    const Rect area{tile.bounds.x, tile.bounds.y, tile.bounds.width,
                    tile.bounds.height};
    const auto query = Inflate(area, data->tile_bounds_inflate);
    std::vector<const belmont_map_fog::Shape *> boxes, quads;
    for (auto key : current)
      for (const auto &shape : Zone(key).shapes)
        if (Overlaps(shape.bounds, query))
          (shape.kind == belmont_map_fog::ShapeKind::Box ? boxes : quads)
              .push_back(&shape);
    if (boxes.size() + quads.size() > 256)
      return false;
    if (boxes.empty() && quads.empty()) {
      for (unsigned y = 0; y < color.h; ++y)
        for (unsigned x = 0; x < color.w; ++x) {
          const auto src = (std::size_t(y) * color.w + x) * 3;
          const auto dst = (std::size_t(color.y + y) * width + color.x + x) * 4;
          std::memcpy(target.image.rgba.data() + dst, color.clean.data() + src,
                      3);
          target.image.rgba[dst + 3] = 255;
        }
      return true;
    }
    const auto settings = std::find_if(
        data->tiles.begin(), data->tiles.end(),
        [&](const auto &t) { return t.renderer_id == tile.renderer_id; });
    if (settings == data->tiles.end())
      return false;
    // Globally aligned R8 grid and one-texel halo keep dirty-tile seams stable.
    const int lo_x = std::max(
                  0, int(std::floor(float(color.x) * float(mask_width) /
                                        float(width) -
                                    .5f)) -
                         1),
              lo_y = std::max(
                  0, int(std::floor(float(color.y) * float(mask_height) /
                                        float(height) -
                                    .5f)) -
                         1);
    const int hi_x =
                  std::min(int(mask_width) - 1,
                           int(std::ceil(float(color.x + color.w) *
                                             float(mask_width) / float(width) -
                                         .5f)) +
                               1),
              hi_y = std::min(
                  int(mask_height) - 1,
                  int(std::ceil(float(color.y + color.h) * float(mask_height) /
                                    float(height) -
                                .5f)) +
                      1);
    const int mw = hi_x - lo_x + 1, mh = hi_y - lo_y + 1;
    std::vector<std::uint8_t> mask(std::size_t(mw) * std::size_t(mh));
    for (int y = lo_y; y <= hi_y; ++y)
      for (int x = lo_x; x <= hi_x; ++x) {
        const float wx = bounds.x +
                         (float(x) + .5f) * bounds.width / float(mask_width),
                    wy = bounds.y + bounds.height -
                         (float(y) + .5f) * bounds.height / float(mask_height);
        float d = std::numeric_limits<float>::max();
        for (const auto *shape : boxes)
          d = belmont_map_render::SmoothUnion(
              d, belmont_map_render::ShapeDistance(*shape, wx, wy),
              settings->smoothness);
        for (const auto *shape : quads)
          d = belmont_map_render::SmoothUnion(
              d, belmont_map_render::ShapeDistance(*shape, wx, wy),
              settings->smoothness);
        mask[std::size_t(y - lo_y) * std::size_t(mw) + std::size_t(x - lo_x)] =
            std::uint8_t(std::lround(Clamp(d * .1f) * 255.f));
      }
    auto texel = [&](int x, int y) {
      x = std::clamp(x, lo_x, hi_x);
      y = std::clamp(y, lo_y, hi_y);
      return float(mask[std::size_t(y - lo_y) * std::size_t(mw) +
                        std::size_t(x - lo_x)]);
    };
    for (unsigned y = 0; y < color.h; ++y)
      for (unsigned x = 0; x < color.w; ++x) {
        const float px = (float(color.x + x) + .5f) * float(mask_width) /
                             float(width) -
                         .5f,
                    py = (float(color.y + y) + .5f) * float(mask_height) /
                             float(height) -
                         .5f;
        const int x0 = int(std::floor(px)), y0 = int(std::floor(py));
        const float tx = px - float(x0), ty = py - float(y0);
        const float f = std::lerp(
            std::lerp(texel(x0, y0), texel(x0 + 1, y0), tx),
            std::lerp(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx), ty);
        const auto src = (std::size_t(y) * color.w + x) * 3,
                   dst = (std::size_t(color.y + y) * width + color.x + x) * 4;
        // R8 holds the clamped world distance / 10. Native reveal stays solid
        // just past the shapes and fades out softly instead of cutting the
        // revealed ink at the (mostly rectangular) zone boundary. Range and
        // width are a still approximation fitted against native output;
        // animated SDG noise displacement is intentionally omitted.
        const float reveal =
            1.f - Smooth((f * (10.f / 255.f) - kRevealSolid) / kRevealFade);
        auto *out = target.image.rgba.data() + dst;
        if (reveal >= 1.f)
          std::memcpy(out, color.revealed.data() + src, 3);
        else if (reveal <= 0.f)
          std::memcpy(out, color.clean.data() + src, 3);
        else
          for (int c = 0; c < 3; ++c)
            out[c] = std::uint8_t(std::lround(
                Gamma(std::lerp(TexelLinear(color.clean[src + c]),
                                TexelLinear(color.revealed[src + c]),
                                reveal)) *
                255.f));
        const auto gate = color.ornament_gate[std::size_t(y) * color.w + x];
        if (gate) {
          const float u = (float(x) + .5f) / float(color.w),
                      v = (float(color.h - y) - .5f) / float(color.h);
          const auto noise = [&](float su, float sv) {
            const auto n = Sample(ornament_noise, su, sv, true, ornament_noise_srgb);
            return belmont_map_decoration::Noise{n.r, n.g, n.b, n.a};
          };
          const auto a = noise(u * ornament_nst.r * 2.f, v * ornament_nst.g * 2.f),
                     b = noise((u * 2.f + .5f) * ornament_nst.r, (v * 2.f + .5f) * ornament_nst.g),
                     c = noise(u * ornament_nst.r, v * ornament_nst.g),
                     p = noise(u * ornament_perlin_st.r, v * ornament_perlin_st.g);
          const float weight = 2.f * (float(gate) / 65535.f) *
              belmont_map_decoration::LineFactor(f * (10.f / 255.f), a, b, c, p, ornament_lines);
          if (weight > 0.f) {
            const float ink[] = {ornament_ink.r, ornament_ink.g, ornament_ink.b};
            for (unsigned channel = 0; channel < 3; ++channel)
              out[channel] = std::uint8_t(std::lround(Gamma(TexelLinear(out[channel]) +
                  ink[channel] * weight) * 255.f));
          }
        }
        out[3] = 255;
      }
    return true;
  }
};
Renderer::Renderer(ce_unity::RangeReader r, ce_unity::AstcDecoder a)
    : state(std::make_shared<State>(std::move(r), std::move(a))) {}
Renderer::Renderer(const EdenDsmodHostApi *h, ce_unity::AstcDecoder a)
    : Renderer(ce_unity::MakeHostRangeReader(h), std::move(a)) {}
std::optional<belmont_map_fog::Rect> Renderer::Bounds() {
  return Bounds(belmont_map_sources::StreetsPoor());
}
std::optional<belmont_map_fog::Rect>
Renderer::Bounds(const belmont_map_sources::SourceRecipe &source) {
  std::lock_guard lock(state->mutex);
  return state->Select(source) && state->Metadata()
      ? std::optional{state->bounds} : std::nullopt;
}
Statistics Renderer::GetStatistics() const {
  std::lock_guard lock(state->mutex);
  return state->statistics;
}
void Renderer::ClearCache() {
  std::lock_guard lock(state->mutex);
  state->Reset(true);
}
std::shared_ptr<const Frame> Renderer::Render(const Request &request) {
  const auto *source = belmont_map_sources::FindSourceForRoom(request.current_room_id);
  return source ? Render(*source, request) : nullptr;
}
std::shared_ptr<const Frame> Renderer::Render(
    const belmont_map_sources::SourceRecipe &source, const Request &request) {
  std::lock_guard lock(state->mutex);
  if (belmont_map_sources::FindSourceForRoom(request.current_room_id) !=
      belmont_map_sources::Canonical(source) || !state->Select(source))
    return {};

  auto &s = *state;
  std::set<ZoneKey> next;
  std::int64_t navigation{};
  if (!s.Visible(request, next, navigation))
    return {};
  const bool navigation_changed = navigation != s.published_navigation;
  if (s.front && !navigation_changed && next == s.visible) {
    ++s.statistics.unchanged_requests;
    return s.front;
  }
  const auto start = std::chrono::steady_clock::now();
  const bool zmap = navigation != s.main_navigation;
  if (!s.Colors(zmap))
    return {};
  std::vector<bool> dirty(s.layout.size(), !s.front || navigation_changed);
  std::vector<ZoneKey> changed;
  std::set_symmetric_difference(s.visible.begin(), s.visible.end(),
                                next.begin(), next.end(),
                                std::back_inserter(changed));
  for (auto key : changed)
    for (const auto &shape : s.Zone(key).shapes)
      for (std::size_t i = 0; i < s.layout.size(); ++i) {
        const auto &b = s.layout[i].bounds;
        if (Overlaps(shape.bounds, Inflate({b.x, b.y, b.width, b.height},
                                           s.data->tile_bounds_inflate)))
          dirty[i] = true;
      }
  auto frame = std::make_shared<Frame>();
  if (s.front)
    frame->image = s.front->image;
  else
    frame->image = {
        s.width, s.height,
        std::vector<std::uint8_t>(std::size_t(s.width) * s.height * 4)};
  frame->world_bounds = s.bounds;
  frame->source_id = s.source->id;
  frame->pixels_per_world = s.pixels_per_world;
  frame->revealed_zone_count = std::uint32_t(next.size());
  frame->revealed_zones.assign(next.begin(), next.end());
  {
    float x0 = std::numeric_limits<float>::max(), y0 = x0, x1 = -x0, y1 = -x0;
    for (auto key : next)
      for (const auto &shape : s.Zone(key).shapes) {
        x0 = std::min(x0, shape.bounds.x);
        y0 = std::min(y0, shape.bounds.y);
        x1 = std::max(x1, shape.bounds.x + shape.bounds.width);
        y1 = std::max(y1, shape.bounds.y + shape.bounds.height);
      }
    if (x1 > x0 && y1 > y0)
      frame->revealed_bounds = {x0, y0, x1 - x0, y1 - y0};
  }
  frame->generation = s.statistics.generations + 1;
  for (std::size_t i = 0; i < dirty.size(); ++i)
    if (dirty[i]) {
      if (!s.Compose(i, next, *frame))
        return {};
      const auto &tile = s.colors[i];
      frame->updated_regions.push_back({tile.x, tile.y, tile.w, tile.h});
      ++frame->updated_tiles;
    }
  s.published_navigation = navigation;
  frame->navigation_zone_id = navigation;
  frame->is_z_map = zmap;
  for (const auto &room : s.data->rooms)
    if (!room.zones.empty() && room.zones.front().navigation_zone_id == navigation)
      frame->navigation_rooms.push_back(room.id);
  s.visible = std::move(next);
  s.front = frame;
  ++s.statistics.generations;
  s.statistics.composed_tiles += frame->updated_tiles;
  s.statistics.current_frame_bytes = frame->image.rgba.size();
  s.statistics.last_render_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
  return frame;
}
bool IsNewReveal(const Frame &a, const Frame &b) {
  if (a.source_id != b.source_id || a.navigation_zone_id != b.navigation_zone_id ||
      a.is_z_map != b.is_z_map || a.pixels_per_world != b.pixels_per_world ||
      a.world_bounds.x != b.world_bounds.x || a.world_bounds.y != b.world_bounds.y ||
      a.world_bounds.width != b.world_bounds.width || a.world_bounds.height != b.world_bounds.height ||
      a.image.width != b.image.width || a.image.height != b.image.height ||
      !a.image.width || !a.image.height ||
      a.image.rgba.size() != std::size_t(a.image.width)*a.image.height*4 ||
      a.image.rgba.size() != b.image.rgba.size() || a.image.rgba.size() > MaximumImageBytes ||
      a.revealed_zones.empty() || b.revealed_zones.size() <= a.revealed_zones.size() ||
      b.updated_regions.empty() ||
      !std::is_sorted(a.revealed_zones.begin(),a.revealed_zones.end()) ||
      !std::is_sorted(b.revealed_zones.begin(),b.revealed_zones.end()) ||
      std::adjacent_find(a.revealed_zones.begin(),a.revealed_zones.end()) != a.revealed_zones.end() ||
      std::adjacent_find(b.revealed_zones.begin(),b.revealed_zones.end()) != b.revealed_zones.end() ||
      !std::includes(b.revealed_zones.begin(),b.revealed_zones.end(),
                     a.revealed_zones.begin(),a.revealed_zones.end())) return false;
  for (const auto &r : b.updated_regions)
    if (!r[2] || !r[3] || r[0] >= b.image.width || r[1] >= b.image.height ||
        r[2] > b.image.width-r[0] || r[3] > b.image.height-r[1]) return false;
  if(a.reveal_intermediate)for(const auto& r:a.updated_regions)
    if(!r[2]||!r[3]||r[0]>=b.image.width||r[1]>=b.image.height||
       r[2]>b.image.width-r[0]||r[3]>b.image.height-r[1])return false;
  return true;
}
std::shared_ptr<const Frame> BlendReveal(std::shared_ptr<const Frame> previous,
                                       std::shared_ptr<const Frame> target,
                                       float progress) {
  if (!previous || !target || !std::isfinite(progress) || !IsNewReveal(*previous,*target)) return {};
  if (progress <= 0.f) return previous;
  if (progress >= 1.f) return target;
  auto output = std::make_shared<Frame>(*target);
  output->reveal_intermediate=true;
  // A second discovery can arrive before the first reveal has finished. Keep
  // its unfinished rectangles in the blend, even if the next new zone affects
  // a different tile. Native final frames never inherit historical dirtiness.
  if(previous->reveal_intermediate){
    output->updated_regions.insert(output->updated_regions.end(),previous->updated_regions.begin(),previous->updated_regions.end());
    std::sort(output->updated_regions.begin(),output->updated_regions.end());
    output->updated_regions.erase(std::unique(output->updated_regions.begin(),output->updated_regions.end()),output->updated_regions.end());
  }
  // Smooth native-color endpoints directly in display space. Integer convex
  // interpolation cannot overshoot to white or expose transparent canvas.
  const unsigned weight=unsigned(std::lround(Smooth(progress)*65536.f));
  for (const auto &r : output->updated_regions)
    for (unsigned y=r[1]; y<r[1]+r[3]; ++y)
      for (unsigned x=r[0]; x<r[0]+r[2]; ++x) {
        const auto offset=(std::size_t(y)*target->image.width+x)*4;
        for (unsigned c=0;c<3;++c) {
          const unsigned a=previous->image.rgba[offset+c], b=target->image.rgba[offset+c];
          output->image.rgba[offset+c]=std::uint8_t((a*(65536-weight)+b*weight+32768)>>16);
        }
        output->image.rgba[offset+3]=255;
      }
  return output;
}
} // namespace belmont_world_map
