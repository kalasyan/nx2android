// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_objectives.h"
#include "chained_echoes_unity.h"
#include <mutex>
namespace belmont_objectives {
// Worker-only resolver for the installed native main-objective collection.
// English is explicit; automatic active-locale selection is not claimed.
class EnglishReader {
public:
  explicit EnglishReader(ce_unity::RangeReader source);
  explicit EnglishReader(const EdenDsmodHostApi *host);
  std::optional<std::string> Load(const Snapshot &objective);
  void ClearCache();

private:
  std::mutex mutex;
  ce_unity::Reader unity;
  std::map<std::int64_t, std::string> labels;
  std::map<std::string, std::int64_t, std::less<>> keys;
  bool loaded{};
};
} // namespace belmont_objectives
