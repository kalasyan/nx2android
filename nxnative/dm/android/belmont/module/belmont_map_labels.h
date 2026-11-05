// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "chained_echoes_unity.h"
#include <mutex>
namespace belmont_map_labels {
// Explicit initial-fixture language. Does not claim the player's active locale.
// Text is read from installed native district/display/localization records.
class Reader {
public:
  explicit Reader(ce_unity::RangeReader romfs);
  explicit Reader(const EdenDsmodHostApi *host);
  std::optional<std::string> LoadAreaEnglish(std::string_view room_id);
  // The room's biome title ("Paris Streets"); empty when not resolvable.
  std::optional<std::string> LoadBiomeEnglish(std::string_view room_id);
  std::optional<std::string> LoadFirstRoomAreaEnglish();
  std::optional<std::string>
  LoadLandmarkEnglish(std::string_view room_id,
                      std::string_view table_reference, std::int64_t entry_id);
  void ClearCache();

private:
  std::mutex mutex;
  ce_unity::RangeReader romfs;
  ce_unity::Reader unity;
  bool initialized{};
  std::string active_world, biome;
  std::map<std::string, std::string, std::less<>> labels;
};
} // namespace belmont_map_labels
