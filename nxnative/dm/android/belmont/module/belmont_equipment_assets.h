// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_assets.h"
#include <mutex>
namespace belmont_equipment_assets {
enum class Type : std::int32_t { Weapon = 1, Spell = 2, Charm = 3, Skin = 7 };
struct Item {
  std::string id, name;
  Type type{};
  ce_unity::Image image;
  std::optional<ce_unity::Image> illustration;
  std::optional<ce_unity::Image> arcana;
  std::string arcana_name;
  std::string source_bundle, source_asset, icon_bundle, icon_sprite;
};
// Worker-only. Resolves only a copied currently-equipped ID. No inventory API.
// Missing, ambiguous, wrong-type and unsupported sprite packing fail closed.
class Reader {
public:
  explicit Reader(ce_unity::RangeReader source,
                  ce_unity::AstcDecoder astc = {});
  explicit Reader(const EdenDsmodHostApi *host,
                  ce_unity::AstcDecoder astc = {});
  ~Reader();
  std::optional<Item> ResolveEnglish(Type type, std::string_view id);
  std::optional<ce_unity::Image> ResolveUpgrade(bool mana, unsigned pieces);
  void ClearCache();

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace belmont_equipment_assets
