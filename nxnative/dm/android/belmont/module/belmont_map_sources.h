// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace belmont_map_sources {
enum class Id {
  StreetsPoor,
  Cemetery,
  Catacombs,
  NotreDame,
  MainHall,
  Library,
  ClockTower,
  HallOfMirrors,
  MasterQuarter,
  Ending
};
// Pinned native asset identities only. No room positions, mask or label data.
struct SourceRecipe {
  Id id;
  std::string_view world_name, world_bundle, world_cab;
  std::int64_t world_root_id;
  std::string_view map_bundle, map_cab;
  std::int64_t map_context_id;
  std::int32_t settings_file_id, district_file_id;
  bool operator==(const SourceRecipe &) const = default;
};
inline constexpr std::array<SourceRecipe, 10> Recipes{{
    {Id::StreetsPoor, "World_Biome_01_Streets_Poor",
     "data-world_biome_01_streets_poor_assets_all_"
     "23b02b049ba03fe6ba07c15c0344bb8f.bundle",
     "CAB-78b393b0882399b884efe6d400c3a5dd", -629725061878729923LL,
     "maps_scenes_01_streets_poor_map_1_d8baa1dae0a4b34f7e696a7a9212e535."
     "bundle",
     "CAB-88d9fd29421b7f4c3de59deff8f49986", 5809, 3, 13},
    {Id::Cemetery, "World_Biome_02_InnocentsCemetery",
     "data-world_biome_02_innocentscemetery_assets_all_"
     "fcba54839881790227496919b552d0cd.bundle",
     "CAB-1383fca7cba7b5ceff84004c0344b3bf", -2623367051430626653LL,
     "maps_scenes_02_innocentscemetery_map_1_f2e15edf86f4eea7221f0c454c807c8b."
     "bundle",
     "CAB-7aec8b1fc47b8c97933cb32206722464", 2202, 4, 12},
    {Id::Catacombs, "World_Biome_03_Catacombs",
     "data-world_biome_03_catacombs_assets_all_"
     "2ba40ce82d86fa44171542115000c5df.bundle",
     "CAB-0dfc2dfdb695a27f3f4a861a52945c68", 7196794280323950214LL,
     "maps_scenes_03_catacombs_map_1_1f83de3ad066abba844f00912fba8449.bundle",
     "CAB-f088c9f748ffe9cfb7be20102aa703d9", 5035, 2, 13},
    {Id::NotreDame, "World_Biome_05_NotreDame",
     "data-world_biome_05_notredame_assets_all_"
     "e299b6a2add0b52db3c5cd4da139b416.bundle",
     "CAB-1c40b31198513cd57a96b7abd3ac961c", 8859751853795525464LL,
     "maps_scenes_05_notredame_map_1_26f32a40ec5b6d6ac411f1b3985e888b.bundle",
     "CAB-0915937ae09e3f876d3b5e289cc8dbff", 4020, 2, 16},
    {Id::MainHall, "World_Biome_06_MainHall",
     "data-world_biome_06_mainhall_assets_all_ab8d489b546cd4880035d15480269905."
     "bundle",
     "CAB-3d7aa8ee6befda840c535875a17e7860", -3290765567559523130LL,
     "maps_scenes_06_mainhall_map_1_7f54316e1cd9f5888f2bf10fc6978f5c.bundle",
     "CAB-f9bacb0066b7a6c102335a66346d1717", 6554, 5, 15},
    {Id::Library, "World_Biome_08_Library",
     "data-world_biome_08_library_assets_all_88d6a474162457ce1cafbc4c60c3f25a."
     "bundle",
     "CAB-5a83f3e3846265c7d0d8659a75281650", 5638767547521059354LL,
     "maps_scenes_08_library_map_1_70a8d7cd2968bb78ee887718a7218274.bundle",
     "CAB-22566ad50acdbda3c21cf0d4eca7504f", 2076, 2, 14},
    {Id::ClockTower, "World_Biome_09_ClockTower",
     "data-world_biome_09_clocktower_assets_all_"
     "ef10956e8d0a4d38bf34bb4c87bafe78.bundle",
     "CAB-8f8bb5fc657cb8009a5c5331098ae71f", 4097717192587628094LL,
     "maps_scenes_09_clocktower_map_1_2595940dda28ec47f1485f144dca168c.bundle",
     "CAB-2e5efb1b003bd2d04a236a09d1238397", 3013, 2, 19},
    {Id::HallOfMirrors, "World_Biome_10_HallOfMirrors",
     "data-world_biome_10_hallofmirrors_assets_all_"
     "799695d4b97ad4de039c2fa4abcd0696.bundle",
     "CAB-a4b371182cfbd28b215dd1f22eda1bfe", -6030555822247981468LL,
     "maps_scenes_10_hallofmirrors_map_1_87e5922a2567cac81ef3f12e18a9d2c1."
     "bundle",
     "CAB-278bd53881bd43981c770876ca5f0301", 2767, 2, 15},
    {Id::MasterQuarter, "World_Biome_11_MasterQuarter",
     "data-world_biome_11_masterquarter_assets_all_"
     "dc5705e02c1166b8b182a02f2d869663.bundle",
     "CAB-f30e3baa0036c2e3ad1aa0c62b61326e", -8351072320171853883LL,
     "maps_scenes_11_masterquarter_map_1_8d214ab1358b12c885e6d2dcd4a97a5e."
     "bundle",
     "CAB-4853af1ec48815a2d800263043ae41cb", 1000, 2, 16},
    {Id::Ending, "World_Biome_12_MQBadEnding",
     "data-world_biome_12_mqbadending_assets_all_"
     "fadf140969c1464afd57eee9d998d80f.bundle",
     "CAB-c445c266dc7c7d386903b9dacb12c45f", 4198425240398769349LL,
     "maps_scenes_12_mqbadending_map_1_61d3334591f067ba587cc2fe9e198ef6.bundle",
     "CAB-dbe21730dd2ac06e07b54ec543fcb0a8", 481, 2, 9},
}};
inline constexpr const SourceRecipe &StreetsPoor() { return Recipes.front(); }
inline const SourceRecipe *Canonical(const SourceRecipe &recipe) {
  for (const auto &known : Recipes)
    if (recipe == known)
      return &known;
  return nullptr;
}
// Candidate selection only. Call LoadForRoom to verify exact source membership.
// Unknown/gym identities are rejected without reading ROMFS.
inline const SourceRecipe *FindSourceForRoom(std::string_view room_id) {
  if (room_id.empty() || room_id.size() > 256 ||
      room_id.find('\0') != std::string_view::npos)
    return nullptr;
  for (const auto &source : Recipes)
    if (room_id.size() > source.world_name.size() + 1 &&
        room_id.starts_with(source.world_name) &&
        room_id[source.world_name.size()] == '_')
      return &source;
  return nullptr;
}
} // namespace belmont_map_sources
