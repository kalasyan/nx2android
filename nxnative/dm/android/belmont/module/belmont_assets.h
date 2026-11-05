// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "chained_echoes_unity.h"
#include <mutex>

// Asset-free Belmont's Curse 1.1.0 reader. Keys are
// native:<Addressables bundle basename>#<signed Texture2D or Sprite path ID>.
// The caller owns any returned pixels; nothing is persisted by this reader.
namespace belmont_assets {
using Image = ce_unity::Image;
using Font = ce_unity::TmpFont;
struct HostFont {
    Font source; ///< Exact source float metrics and SDF remain available for
                 ///< inspection.
    Image atlas; ///< White RGB, coverage alpha, evaluated at the requested cap
                 ///< height.
    std::uint32_t line_height{}, first_codepoint{};
    std::vector<EdenDsmodFontGlyph> glyphs; ///< Dense codepoint run; integer ABI metrics.
};
class Reader {
  public:
    explicit Reader(ce_unity::RangeReader romfs, ce_unity::AstcDecoder astc = {});
    explicit Reader(const EdenDsmodHostApi* host, ce_unity::AstcDecoder astc = {});
    std::optional<Image> LoadImage(std::string_view key);
    // Exact source TMP metrics and atlas (SDF values, not raster coverage).
    // Only LatinFont and NotoSans_Latin are currently supported.
    std::optional<Font> LoadFont(std::string_view name);
    // Regular face, orthographic 2D coverage; cap height range 24..96.
    // Keep this result together: its atlas and integer metrics are a matching pair.
    // small_caps: lowercase letters (a-z, Latin-1 lowercase with an uppercase pair) draw as
    // their capital at 80% size, the native menu small-caps style; capitals are unchanged.
    std::optional<HostFont> LoadHostFont(std::string_view name, std::uint32_t cap_height = 35,
                                         bool small_caps = false);
    void ClearCache();

  private:
    std::optional<Image> Texture(std::string_view bundle, std::int64_t id);
    std::optional<Image> AtlasSprite(std::string_view bundle, const ce_unity::Value& sprite);
    std::recursive_mutex mutex;
    ce_unity::Reader unity;
    ce_unity::AstcDecoder astc;
};
} // namespace belmont_assets
