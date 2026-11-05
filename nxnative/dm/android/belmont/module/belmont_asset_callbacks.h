// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_assets.h"
#include <charconv>
#include <map>
#include <atomic>

namespace belmont_assets {
// Game-only adapter. The caller MUST verify title/build before passing true to
// supported_build. False never constructs a RomFS reader or invokes a sink.
// The copied host table's userdata must remain alive until this object is destroyed.
class Callbacks {
  public:
    explicit Callbacks(const EdenDsmodHostApi& host, bool supported_build)
        : host_(host),
          supported_(supported_build && host.abi_version == EDEN_DSMOD_MODULE_ABI_VERSION &&
                     host.struct_size == sizeof(EdenDsmodHostApi) &&
                     host.abi_hash == EDEN_DSMOD_MODULE_ABI_HASH && host.read_romfs) {}

    void Configure(const EdenDsmodHostExtensions* ext) {
        std::lock_guard lock{mutex_};
        astc_ = {};
        if (ext && ext->version == EDEN_DSMOD_EXT_VERSION &&
            ext->struct_size == sizeof(EdenDsmodHostExtensions) &&
            ext->abi_hash == EDEN_DSMOD_EXT_HASH && ext->decode_astc)
            astc_ = ce_unity::MakeHostAstcDecoder(ext->userdata, ext->decode_astc);
        reader_.reset();
        fonts_.clear();
        font_ready_ = false;
    }

    // Worker-facing preparation. No guest-memory access, host publication or
    // renderer calls; retry after an unavailable RomFS read is safe.
    bool PrepareFont() {
        try {
            std::lock_guard lock{mutex_};
            return supported_ && bool(GetFont(Spec{"LatinFont", 51})) && font_ready_;
        } catch (...) {
            return false;
        }
    }

    bool FontReady() const {
        return font_ready_.load(std::memory_order_acquire);
    }

    EdenDsmodBool LoadImage(const char* key, void* receiver, EdenDsmodImageSink sink) {
        try {
            std::lock_guard lock{mutex_};
            if (!supported_ || !key || !sink)
                return EDEN_DSMOD_FALSE;
            std::size_t n = 0;
            while (n <= 4096 && key[n])
                ++n;
            if (n > 4096)
                return EDEN_DSMOD_FALSE;
            std::string_view value{key, n};
            if (value == "module:belmont:rose-marker") {
                // Native MCPosition Sprite -> Map_PlayerIcon Texture2D. This
                // shipped texture is the complete 128px glyph; no substitute.
                constexpr std::string_view marker =
                    "native:ui-gamemenus_assets_all_866519f7d6a7057a8ba1c459a5b3f694.bundle#6019023001892253940";
                auto image = GetReader().LoadImage(marker);
                if (!image || image->width != 128 || image->height != 128 ||
                    image->rgba.size() != 128u * 128u * 4u)
                    return EDEN_DSMOD_FALSE;
                sink(receiver, image->width, image->height, image->rgba.data(), image->rgba.size());
                return EDEN_DSMOD_TRUE;
            }
            if (value.starts_with("module:native:")) {
                auto image = GetReader().LoadImage(value.substr(7));
                if (!image || image->rgba.empty())
                    return EDEN_DSMOD_FALSE;
                sink(receiver, image->width, image->height, image->rgba.data(), image->rgba.size());
                return EDEN_DSMOD_TRUE;
            }
            constexpr std::string_view prefix = "module:belmont-font:";
            if (!value.starts_with(prefix))
                return EDEN_DSMOD_FALSE;
            value.remove_prefix(prefix.size());
            const auto split = value.find(':');
            if (split == std::string_view::npos)
                return EDEN_DSMOD_FALSE;
            const auto spec = Parse(value.substr(0, split), value.substr(split + 1));
            if (!spec)
                return EDEN_DSMOD_FALSE;
            const auto font = GetFont(*spec);
            if (!font)
                return EDEN_DSMOD_FALSE;
            sink(receiver, font->atlas.width, font->atlas.height, font->atlas.rgba.data(),
                 font->atlas.rgba.size());
            return EDEN_DSMOD_TRUE;
        } catch (...) {
            return EDEN_DSMOD_FALSE;
        }
    }

    EdenDsmodBool DecodeFont(const std::uint8_t* bytes, std::size_t size, void* receiver,
                             EdenDsmodFontSink sink) {
        try {
            std::lock_guard lock{mutex_};
            if (!supported_ || !bytes || !sink || size > 64)
                return EDEN_DSMOD_FALSE;
            std::string_view value{reinterpret_cast<const char*>(bytes), size};
            constexpr std::string_view prefix = "BELMONT_FONT_V1 ";
            if (!value.starts_with(prefix) || !value.ends_with('\n'))
                return EDEN_DSMOD_FALSE;
            value.remove_prefix(prefix.size());
            value.remove_suffix(1);
            const auto split = value.find(' ');
            if (split == std::string_view::npos)
                return EDEN_DSMOD_FALSE;
            const auto spec = Parse(value.substr(0, split), value.substr(split + 1));
            if (!spec)
                return EDEN_DSMOD_FALSE;
            const auto font = GetFont(*spec);
            if (!font || font->glyphs.empty())
                return EDEN_DSMOD_FALSE;
            sink(receiver, font->line_height, font->first_codepoint, font->glyphs.data(),
                 static_cast<std::uint32_t>(font->glyphs.size()));
            return EDEN_DSMOD_TRUE;
        } catch (...) {
            return EDEN_DSMOD_FALSE;
        }
    }

  private:
    struct Spec {
        std::string_view name;
        std::uint32_t cap;
        bool small_caps{};
    };
    static std::optional<Spec> Parse(std::string_view name, std::string_view cap) {
        const bool small_caps = cap.ends_with("sc");
        if (small_caps)
            cap.remove_suffix(2);
        if ((name != "LatinFont" && name != "NotoSans_Latin") || cap.size() != 2)
            return {};
        std::uint32_t number{};
        const auto [end, error] = std::from_chars(cap.data(), cap.data() + cap.size(), number);
        if (error != std::errc{} || end != cap.data() + cap.size() || number < 24 || number > 96)
            return {};
        return Spec{name, number, small_caps};
    }
    Reader& GetReader() {
        if (!reader_)
            reader_ = std::make_unique<Reader>(&host_, astc_);
        return *reader_;
    }
    std::shared_ptr<const HostFont> GetFont(const Spec& spec) {
        const auto key = std::string{spec.name} + ':' + std::to_string(spec.cap) +
                         (spec.small_caps ? "sc" : "");
        if (const auto it = fonts_.find(key); it != fonts_.end())
            return it->second;
        auto font = GetReader().LoadHostFont(spec.name, spec.cap, spec.small_caps);
        if (!font || font->atlas.width == 0 || font->atlas.height == 0 ||
            std::uint64_t(font->atlas.width) * font->atlas.height * 4 != font->atlas.rgba.size() ||
            font->glyphs.empty() || font->line_height == 0)
            return {};
        auto result = std::make_shared<HostFont>(std::move(*font));
        if (fonts_.size() == 2)
            fonts_.erase(fonts_.begin());
        fonts_.emplace(key, result);
        if (spec.name == "LatinFont" && spec.cap == 51)
            font_ready_ = true;
        return result;
    }
    EdenDsmodHostApi host_{};
    bool supported_{};
    mutable std::mutex mutex_;
    std::atomic<bool> font_ready_{};
    ce_unity::AstcDecoder astc_;
    std::unique_ptr<Reader> reader_;
    std::map<std::string, std::shared_ptr<const HostFont>> fonts_;
};
} // namespace belmont_assets
