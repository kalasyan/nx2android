// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_landmark_render.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace belmont_landmark_render {
namespace {
using Image = ce_unity::Image;
using Sprite = belmont_map_landmarks::Sprite;
using Color = std::array<float, 4>;
struct Rect {
  float left{}, bottom{}, right{}, top{};
};
bool Positive(float x) { return std::isfinite(x) && x > 0; }
bool ColorValid(const Color &c) {
  return std::all_of(c.begin(), c.end(), [](float v) {
    return std::isfinite(v) && v >= 0 && v <= 1;
  });
}
bool ImageValid(const Image &i) {
  return i.width && i.height && i.width <= 4096 && i.height <= 4096 &&
         i.rgba.size() == std::size_t(i.width) * i.height * 4;
}
std::array<float, 4> Sample(const Image &, float, float, const Color &);
std::optional<Image> Crop(const Image &i, const Sprite &s) {
  const auto &r = s.texture_rect;
  if (!ImageValid(i) || !Positive(s.width) || !Positive(s.height) ||
      s.width > 4096 || s.height > 4096 || !Positive(r.width) ||
      !Positive(r.height) || !std::isfinite(r.x) || !std::isfinite(r.y) ||
      r.x < 0 || r.y < 0 || r.x + r.width > i.width + .01f ||
      r.y + r.height > i.height + .01f ||
      !std::isfinite(s.texture_rect_offset_x) ||
      !std::isfinite(s.texture_rect_offset_y) || s.texture_rect_offset_x < 0 ||
      s.texture_rect_offset_y < 0 ||
      s.texture_rect_offset_x + r.width > s.width + .01f ||
      s.texture_rect_offset_y + r.height > s.height + .01f ||
      (s.settings_raw & 1) || ((s.settings_raw >> 2) & 15))
    return {};
  const auto w = std::lround(s.width), h = std::lround(s.height);
  if (w <= 0 || h <= 0 || std::abs(w - s.width) > .01f ||
      std::abs(h - s.height) > .01f)
    return {};
  Image out{unsigned(w), unsigned(h), {}};
  out.rgba.resize(std::size_t(w) * h * 4);
  const Color white{1, 1, 1, 1};
  for (long y = 0; y < h; ++y)
    for (long x = 0; x < w; ++x) {
      const float local_x = x + .5f - s.texture_rect_offset_x,
                  local_y = h - y - .5f - s.texture_rect_offset_y;
      if (local_x < 0 || local_x >= r.width || local_y < 0 ||
          local_y >= r.height)
        continue;
      const auto color =
          Sample(i, r.x + local_x - .5f, i.height - r.y - local_y - .5f, white);
      auto *dst = out.rgba.data() + (y * w + x) * 4;
      for (unsigned c = 0; c < 4; ++c)
        dst[c] =
            std::uint8_t(std::lround(std::clamp(color[c], 0.f, 1.f) * 255));
    }
  return out;
}
void Blend(std::uint8_t *dst, const std::array<float, 4> &src) {
  const float sa = std::clamp(src[3], 0.f, 1.f), da = dst[3] / 255.f,
              a = sa + da * (1 - sa);
  for (unsigned c = 0; c < 3; ++c)
    dst[c] = std::uint8_t(std::lround(
        255 *
        (a > 0 ? (src[c] * sa + dst[c] / 255.f * da * (1 - sa)) / a : 0)));
  dst[3] = std::uint8_t(std::lround(255 * a));
}
std::array<float, 4> Sample(const Image &im, float x, float y,
                            const Color &tint) {
  x = std::clamp(x, 0.f, float(im.width - 1));
  y = std::clamp(y, 0.f, float(im.height - 1));
  const auto x0 = unsigned(x), y0 = unsigned(y),
             x1 = std::min(x0 + 1, im.width - 1),
             y1 = std::min(y0 + 1, im.height - 1);
  const float fx = x - x0, fy = y - y0;
  std::array<float, 4> out{};
  // Interpolate premultiplied color to avoid dark transparent texture borders.
  float alpha{};
  std::array<float, 3> premul{};
  for (unsigned j = 0; j < 2; ++j)
    for (unsigned k = 0; k < 2; ++k) {
      const auto p = (std::size_t(j ? y1 : y0) * im.width + (k ? x1 : x0)) * 4;
      const float w = (j ? fy : 1 - fy) * (k ? fx : 1 - fx),
                  a = im.rgba[p + 3] / 255.f;
      alpha += w * a;
      for (unsigned c = 0; c < 3; ++c)
        premul[c] += w * a * im.rgba[p + c] / 255.f;
    }
  for (unsigned c = 0; c < 3; ++c)
    out[c] = (alpha > 0 ? premul[c] / alpha : 0) * tint[c];
  out[3] = alpha * tint[3];
  return out;
}
float Slice(float p, float extent, float src, float first, float last,
            float target_first, float target_last) {
  if (p < target_first && target_first > 0)
    return p / target_first * first;
  if (p > extent - target_last && target_last > 0)
    return src - last + (p - extent + target_last) / target_last * last;
  const float n = extent - target_first - target_last;
  return first + (n > 0 ? (p - target_first) / n : 0) * (src - first - last);
}
void Draw(Image &out, const Image &in, const Rect &rect, const Rect &canvas,
          float ppu, const Color &tint,
          const std::array<float, 4> *border = nullptr,
          float border_scale = 0) {
  const float left = (rect.left - canvas.left) * ppu,
              top = (canvas.top - rect.top) * ppu,
              w = (rect.right - rect.left) * ppu,
              h = (rect.top - rect.bottom) * ppu;
  for (int y = std::max(0, int(std::floor(top)));
       y < std::min(int(out.height), int(std::ceil(top + h))); ++y)
    for (int x = std::max(0, int(std::floor(left)));
         x < std::min(int(out.width), int(std::ceil(left + w))); ++x) {
      const float dx = x + .5f - left, dy = y + .5f - top;
      if (dx < 0 || dx >= w || dy < 0 || dy >= h)
        continue;
      float sx = dx / w * in.width - .5f, sy = dy / h * in.height - .5f;
      if (border) {
        sx = Slice(dx, w, float(in.width), (*border)[0], (*border)[2],
                   (*border)[0] * border_scale, (*border)[2] * border_scale) -
             .5f;
        sy = Slice(dy, h, float(in.height), (*border)[3], (*border)[1],
                   (*border)[3] * border_scale, (*border)[1] * border_scale) -
             .5f;
      }
      Blend(out.rgba.data() + (std::size_t(y) * out.width + x) * 4,
            Sample(in, sx, sy, tint));
    }
}
std::optional<std::pair<Image, Rect>> Canvas(Rect union_rect, float ppu,
                                             bool allocate = true) {
  const float span = std::max(union_rect.right - union_rect.left,
                              union_rect.top - union_rect.bottom);
  if (!Positive(span) || !Positive(ppu) || span * ppu > 2048)
    return {};
  const unsigned side = unsigned(std::ceil(span * ppu));
  if (!side || std::size_t(side) * side * 4 > 16 * 1024 * 1024)
    return {};
  const float cx = (union_rect.left + union_rect.right) * .5f,
              cy = (union_rect.bottom + union_rect.top) * .5f,
              half = side / ppu * .5f;
  Image i{side, side, {}};
  if (allocate)
    i.rgba.resize(std::size_t(side) * side * 4);
  return std::pair{std::move(i),
                   Rect{cx - half, cy - half, cx + half, cy + half}};
}
std::optional<std::vector<std::uint32_t>> Utf8(std::string_view s) {
  std::vector<std::uint32_t> o;
  if (s.empty() || s.size() > 1024)
    return {};
  for (std::size_t i = 0; i < s.size();) {
    const unsigned b = std::uint8_t(s[i++]);
    std::uint32_t cp{};
    unsigned n{};
    if (b < 128)
      cp = b;
    else if (b >= 0xc2 && b <= 0xdf) {
      cp = b & 31;
      n = 1;
    } else if (b >= 0xe0 && b <= 0xef) {
      cp = b & 15;
      n = 2;
    } else if (b >= 0xf0 && b <= 0xf4) {
      cp = b & 7;
      n = 3;
    } else
      return {};
    if (i + n > s.size())
      return {};
    for (unsigned j = 0; j < n; ++j) {
      const unsigned c = std::uint8_t(s[i++]);
      if ((c & 0xc0) != 0x80)
        return {};
      cp = (cp << 6) | (c & 63);
    }
    if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) ||
        (n == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp < 32)
      return {};
    o.push_back(cp);
    if (o.size() > 256)
      return {};
  }
  return o;
}
} // namespace
Renderer::Renderer(ce_unity::RangeReader r, ce_unity::AstcDecoder a)
    : assets(std::move(r), std::move(a)) {}
Renderer::Renderer(const EdenDsmodHostApi *h, ce_unity::AstcDecoder a)
    : Renderer(ce_unity::MakeHostRangeReader(h), std::move(a)) {}
void Renderer::ClearCache() {
  std::lock_guard lock{mutex};
  font.reset();
  assets.ClearCache();
}
std::optional<WorldFrame>
Renderer::RenderArt(const belmont_map_landmarks::Landmark &l) {
  std::lock_guard lock{mutex};
  try {
    if (!l.unconditional_policy || l.guid.empty() || l.sprites.empty() ||
        l.sprites.size() > 32)
      return {};
    struct Layer {
      Image image;
      Rect rect;
      Color color;
    };
    std::vector<Layer> layers;
    // (This loop used to also accumulate a local `bounds` union of each
    // sprite's rect, but that value was never read: placement below comes
    // from ArtPlacement(l), which computes its own bounds independently.
    // Removed the dead accumulation.)
    for (const auto &s : l.sprites) {
      if (!Positive(s.width) || !Positive(s.height) ||
          !Positive(s.pixels_per_unit) || !Positive(s.scale_x) ||
          !Positive(s.scale_y) || !std::isfinite(s.x) || !std::isfinite(s.y) ||
          !std::isfinite(s.pivot_x) || !std::isfinite(s.pivot_y) ||
          s.pivot_x < 0 || s.pivot_x > 1 || s.pivot_y < 0 || s.pivot_y > 1 ||
          !ColorValid(s.color))
        return {};
      auto raw = assets.LoadImage("native:" + s.bundle + "#" +
                                  std::to_string(s.texture_id));
      auto im = raw ? Crop(*raw, s) : std::nullopt;
      if (!im)
        return {};
      const float w = s.width / s.pixels_per_unit * s.scale_x,
                  h = s.height / s.pixels_per_unit * s.scale_y;
      Rect r{s.x - w * s.pivot_x, s.y - h * s.pivot_y,
             s.x + w * (1 - s.pivot_x), s.y + h * (1 - s.pivot_y)};
      layers.push_back({std::move(*im), r, s.color});
    }
    constexpr float NativePPU = 16;
    const auto placement = ArtPlacement(l);
    if (!placement)
      return {};
    const float half = placement->size * .5f;
    auto canvas =
        Canvas({placement->center_x - half, placement->center_y - half,
                placement->center_x + half, placement->center_y + half},
               NativePPU);
    if (!canvas)
      return {};
    for (const auto &layer : layers)
      Draw(canvas->first, layer.image, layer.rect, canvas->second, NativePPU,
           layer.color);
    const auto &b = canvas->second;
    return WorldFrame{std::move(canvas->first), (b.left + b.right) * .5f,
                      (b.bottom + b.top) * .5f, b.right - b.left};
  } catch (...) {
    return {};
  }
}
std::optional<NameFrame>
Renderer::Name(const belmont_map_landmarks::Landmark &l,
               std::string_view english, float canvas_height,
               bool metadata_only) {
  try {
    const auto &s = l.name_style;
    auto cps = Utf8(english);
    // Native menu prefabs use BaseFont as a locale placeholder. This
    // explicitly English slice pairs it with installed LatinFont; arbitrary
    // font assets must not silently inherit its metrics.
    const bool known_font =
        (s.font_asset_id == 443565887117910916LL &&
         s.font_asset_cab == "CAB-ad2f7c82fdd63252ff47e7196eceb3e3") ||
        (s.font_asset_id == -993644073735041967LL &&
         s.font_asset_cab == "CAB-f5f64efb642dd76c1aaaec4176c00c7e");
    if (!known_font || !l.unconditional_policy || l.guid.empty() || !cps ||
        !Positive(canvas_height) || canvas_height > 4096 || !s.ribbon ||
        s.font_style != 32 || s.horizontal_alignment != 2 ||
        s.vertical_alignment != 512 || !Positive(s.font_size) ||
        !Positive(s.screen_percentage) || s.screen_percentage > 1 ||
        !Positive(s.scale_x) || !Positive(s.scale_y) ||
        !Positive(s.responsive_scale_x) || !Positive(s.responsive_scale_y) ||
        !Positive(s.ribbon_pixels_per_unit_multiplier) ||
        !ColorValid(s.color) || !ColorValid(s.ribbon_color) ||
        !std::isfinite(l.name_x) || !std::isfinite(l.name_y))
      return {};
    if (!font)
      font = assets.LoadHostFont("LatinFont", 51, true);
    if (!font || !Positive(font->source.point_size) ||
        !Positive(font->source.cap_line) || font->line_height != 51 ||
        !ImageValid(font->atlas))
      return {};
    float unit_x = canvas_height * s.screen_percentage * s.scale_x /
                   s.responsive_scale_x,
          unit_y = canvas_height * s.screen_percentage * s.scale_y /
                   s.responsive_scale_y;
    float cap =
        s.font_size * font->source.cap_line / font->source.point_size * unit_y;
    const bool enlarged = cap < 22;
    if (enlarged) {
      const float factor = 22 / cap;
      unit_x *= factor;
      unit_y *= factor;
      cap = 22;
    }
    const float fx = s.font_size * font->source.cap_line /
                     font->source.point_size * unit_x / 51,
                fy = cap / 51;
    if (!Positive(fx) || !Positive(fy) || fx > 64 || fy > 64)
      return {};
    float pen = 0, min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    bool ink = false;
    struct Glyph {
      EdenDsmodFontGlyph glyph;
      float pen;
    };
    std::vector<Glyph> glyphs;
    for (auto cp : *cps) {
      if (cp < font->first_codepoint ||
          cp - font->first_codepoint >= font->glyphs.size())
        return {};
      const auto &g = font->glyphs[cp - font->first_codepoint];
      if (!g.advance && cp != 32)
        return {};
      if (unsigned(g.x) + g.w > font->atlas.width ||
          unsigned(g.y) + g.h > font->atlas.height)
        return {};
      glyphs.push_back({g, pen});
      if (g.w && g.h) {
        const float x = pen + g.bearing_x * fx, y = g.bearing_y * fy;
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x + g.w * fx);
        min_y = std::min(min_y, y - g.h * fy);
        max_y = std::max(max_y, y);
        ink = true;
      }
      pen += g.advance * fx;
    }
    if (!ink || !Positive(pen))
      return {};
    max_x = std::max(max_x, pen);
    for (auto m : s.margin)
      if (!std::isfinite(m) || m < 0 || m > 100)
        return {};
    const float w = max_x - min_x + s.margin[0] * unit_x + s.margin[2] * unit_x,
                h = max_y - min_y + s.margin[1] * unit_y + s.margin[3] * unit_y;
    Rect rect{-w * .5f, -h * .5f, w * .5f, h * .5f};
    auto canvas = Canvas(rect, 1, !metadata_only);
    if (!canvas)
      return {};
    const auto &r = *s.ribbon;
    if (!Positive(r.pixels_per_unit) || r.bundle.empty() || !r.texture_id)
      return {};
    for (auto b : r.border)
      if (!std::isfinite(b) || b < 0)
        return {};
    if (r.border[0] + r.border[2] > r.width ||
        r.border[1] + r.border[3] > r.height)
      return {};
    const float border_scale =
        unit_y / (r.pixels_per_unit * s.ribbon_pixels_per_unit_multiplier);
    if ((r.border[0] + r.border[2]) * border_scale > w ||
        (r.border[1] + r.border[3]) * border_scale > h)
      return {};
    if (metadata_only) {
      const float side = canvas->first.width;
      return NameFrame{std::move(canvas->first), l.name_x, l.name_y, side,
                       enlarged};
    }
    auto raw = assets.LoadImage("native:" + r.bundle + "#" +
                                std::to_string(r.texture_id));
    auto ribbon = raw ? Crop(*raw, r) : std::nullopt;
    if (!ribbon)
      return {};
    Draw(canvas->first, *ribbon, rect, canvas->second, 1, s.ribbon_color,
         &r.border, border_scale);
    const float origin_x = rect.left + s.margin[0] * unit_x - min_x,
                baseline = rect.bottom + s.margin[3] * unit_y - min_y;
    for (const auto &g : glyphs) {
      if (!g.glyph.w || !g.glyph.h)
        continue;
      Image tile{g.glyph.w, g.glyph.h, {}};
      tile.rgba.resize(std::size_t(tile.width) * tile.height * 4);
      for (unsigned y = 0; y < tile.height; ++y)
        std::memcpy(
            tile.rgba.data() + std::size_t(y) * tile.width * 4,
            font->atlas.rgba.data() +
                (std::size_t(g.glyph.y + y) * font->atlas.width + g.glyph.x) *
                    4,
            std::size_t(tile.width) * 4);
      const float x = origin_x + g.pen + g.glyph.bearing_x * fx,
                  y = baseline + g.glyph.bearing_y * fy;
      Draw(canvas->first, tile, {x, y - g.glyph.h * fy, x + g.glyph.w * fx, y},
           canvas->second, 1, s.color);
    }
    const float side = canvas->first.width;
    return NameFrame{std::move(canvas->first), l.name_x, l.name_y, side,
                     enlarged};
  } catch (...) {
    return {};
  }
}
std::optional<Image> Renderer::RenderText(std::string_view text,
    std::uint32_t width, std::uint32_t height, float cap) {
  std::lock_guard lock{mutex};
  try {
    const auto cps = Utf8(text);
    if (!cps || width < 4 || height < 4 || width > 2048 || height > 512 ||
        !Positive(cap) || cap > 128 || cap > height ||
        std::size_t(width) * height * 4 > 16u * 1024u * 1024u)
      return {};
    if (!font) font = assets.LoadHostFont("LatinFont", 51, true);
    if (!font || font->line_height != 51 || !ImageValid(font->atlas)) return {};
    const float scale = cap / 51.f;
    const auto glyph = [&](std::uint32_t cp) -> const EdenDsmodFontGlyph* {
      if (cp < font->first_codepoint || cp - font->first_codepoint >= font->glyphs.size()) return nullptr;
      const auto &g = font->glyphs[cp - font->first_codepoint];
      if ((!g.advance && cp != 32) || unsigned(g.x) + g.w > font->atlas.width ||
          unsigned(g.y) + g.h > font->atlas.height) return nullptr;
      return &g;
    };
    std::vector<std::vector<std::uint32_t>> words;
    for (auto cp : *cps) {
      if (!glyph(cp)) return {};
      if (cp == 32) { if (!words.empty() && !words.back().empty()) words.emplace_back(); }
      else { if (words.empty()) words.emplace_back(); words.back().push_back(cp); }
    }
    if (!words.empty() && words.back().empty()) words.pop_back();
    if (words.empty()) return {};
    struct Metrics {float left{}, right{}, bottom{}, top{}, advance{};};
    const auto measure = [&](const std::vector<std::uint32_t> &line) {
      Metrics m;
      for (auto cp : line) {
        const auto &g = *glyph(cp);
        if (g.w && g.h) {
          m.left = std::min(m.left, m.advance + g.bearing_x * scale);
          m.right = std::max(m.right, m.advance + (g.bearing_x + g.w) * scale);
          m.bottom = std::min(m.bottom, (g.bearing_y - float(g.h)) * scale);
          m.top = std::max(m.top, g.bearing_y * scale);
        }
        m.advance += g.advance * scale;
      }
      m.right = std::max(m.right, m.advance);
      return m;
    };
    std::vector<std::vector<std::uint32_t>> lines(1);
    for (const auto &word : words) {
      auto candidate = lines.back();
      if (!candidate.empty()) candidate.push_back(32);
      candidate.insert(candidate.end(), word.begin(), word.end());
      const auto m = measure(candidate);
      if (m.right - m.left <= width - 2.f) lines.back() = std::move(candidate);
      else {
        if (lines.back().empty() || lines.size() >= 2) return {};
        const auto w = measure(word);
        if (w.right - w.left > width - 2.f) return {};
        lines.push_back(word);
      }
    }
    std::vector<Metrics> metrics;
    float block_height = 0;
    constexpr float Gap = 4;
    for (const auto &line : lines) {
      const auto m = measure(line);
      if (!Positive(m.top - m.bottom)) return {};
      metrics.push_back(m); block_height += m.top - m.bottom;
    }
    block_height += Gap * float(lines.size() - 1);
    if (block_height > height - 2.f) return {};
    Image out{width, height, std::vector<std::uint8_t>(std::size_t(width) * height * 4)};
    const Rect canvas{0, 0, float(width), float(height)};
    // Existing owner-accepted native menu gold (the previous objective label).
    const Color gold{201.f / 255.f, 180.f / 255.f, 106.f / 255.f, 1};
    float top = (height - block_height) * .5f;
    for (std::size_t n = 0; n < lines.size(); ++n) {
      const auto &m = metrics[n];
      const float origin = width - 1.f - m.right,
                  baseline = height - top - m.top;
      float pen = 0;
      for (auto cp : lines[n]) {
        const auto &g = *glyph(cp);
        if (g.w && g.h) {
          Image tile{g.w, g.h, std::vector<std::uint8_t>(std::size_t(g.w) * g.h * 4)};
          for (unsigned y = 0; y < g.h; ++y)
            std::memcpy(tile.rgba.data() + std::size_t(y) * g.w * 4,
                font->atlas.rgba.data() + (std::size_t(g.y + y) * font->atlas.width + g.x) * 4,
                std::size_t(g.w) * 4);
          const float x = origin + pen + g.bearing_x * scale,
                      y = baseline + g.bearing_y * scale;
          Draw(out, tile, {x, y - g.h * scale, x + g.w * scale, y}, canvas, 1, gold);
        }
        pen += g.advance * scale;
      }
      top += m.top - m.bottom + Gap;
    }
    return out;
  } catch (...) { return {}; }
}
std::optional<NameFrame>
Renderer::RenderName(const belmont_map_landmarks::Landmark &l,
                     std::string_view english, float height) {
  std::lock_guard lock{mutex};
  return Name(l, english, height, false);
}
std::optional<Placement>
Renderer::NamePlacement(const belmont_map_landmarks::Landmark &l,
                        std::string_view english, float height) {
  std::lock_guard lock{mutex};
  auto frame = Name(l, english, height, true);
  if (!frame)
    return {};
  return Placement{frame->world_center_x, frame->world_center_y,
                   frame->size_px};
}
std::optional<Placement>
ArtPlacement(const belmont_map_landmarks::Landmark &l) {
  if (!l.unconditional_policy || l.guid.empty() || l.sprites.empty() ||
      l.sprites.size() > 32)
    return {};
  Rect b{};
  bool first = true;
  for (const auto &s : l.sprites) {
    if (!Positive(s.width) || !Positive(s.height) ||
        !Positive(s.pixels_per_unit) || !Positive(s.scale_x) ||
        !Positive(s.scale_y) || !std::isfinite(s.x) || !std::isfinite(s.y) ||
        !std::isfinite(s.pivot_x) || !std::isfinite(s.pivot_y) ||
        s.pivot_x < 0 || s.pivot_x > 1 || s.pivot_y < 0 || s.pivot_y > 1 ||
        !ColorValid(s.color))
      return {};
    const float w = s.width / s.pixels_per_unit * s.scale_x,
                h = s.height / s.pixels_per_unit * s.scale_y;
    Rect r{s.x - w * s.pivot_x, s.y - h * s.pivot_y, s.x + w * (1 - s.pivot_x),
           s.y + h * (1 - s.pivot_y)};
    if (first) {
      b = r;
      first = false;
    } else {
      b.left = std::min(b.left, r.left);
      b.bottom = std::min(b.bottom, r.bottom);
      b.right = std::max(b.right, r.right);
      b.top = std::max(b.top, r.top);
    }
  }
  auto canvas = Canvas(b, 16, false);
  if (!canvas)
    return {};
  return Placement{(b.left + b.right) * .5f, (b.top + b.bottom) * .5f,
                   canvas->first.width / 16.f};
}
} // namespace belmont_landmark_render
