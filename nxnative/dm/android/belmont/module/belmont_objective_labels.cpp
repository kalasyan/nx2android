// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_objective_labels.h"
#include <set>
namespace belmont_objectives {
namespace {
using V = ce_unity::Value;
constexpr std::string_view Root = "Data/StreamingAssets/aa/Switch/";
constexpr std::string_view Shared = "localization-assets-shared_assets_all_"
                                    "c0102878b4f0e869eaa8db1ab681b72a.bundle";
constexpr std::string_view English =
    "localization-string-tables-english(en)_assets_all_"
    "aa06f0f9074c49a1bdd9e6b274bdec25.bundle";
constexpr std::int64_t SharedId = -9122226071526957937LL,
                       EnglishId = -8054488034962146823LL;
constexpr std::string_view Guid = "f345cc095021a294987c6ffdf046726b";
bool Is(const V *v, std::string_view s) {
  return v && v->kind == V::Kind::String && v->s == s;
}
std::optional<std::int64_t> Int(const V *v) {
  if (v && v->kind == V::Kind::Int)
    return v->i;
  if (v && v->kind == V::Kind::UInt && v->u <= std::uint64_t(INT64_MAX))
    return v->u;
  return {};
}
const std::vector<V> *Array(const V *v) {
  return v && v->kind == V::Kind::Array && v->items.size() <= 256 ? &v->items
                                                                  : nullptr;
}
std::optional<V> Object(ce_unity::Reader &reader, std::string_view bundle,
                        std::string_view cab, std::int64_t id) {
  auto b = reader.OpenBundle(bundle);
  auto sf = reader.OpenSerialized(bundle);
  if (!b || b->engine_version != "2022.3.62f2" || b->Nodes().empty() ||
      b->Nodes().front().path != cab || !sf || !sf->HasTypeTrees() ||
      sf->Platform() != 38 || sf->Version() != 22)
    return {};
  auto obj = sf->Find(id);
  if (!obj || obj->class_id != 114 || obj->byte_size > 1024 * 1024)
    return {};
  return sf->ReadObject(*obj);
}
std::string GuidString(const std::array<std::uint8_t, 16> &bytes) {
  constexpr unsigned order[] = {3, 2, 1,  0,  5,  4,  7,  6,
                                8, 9, 10, 11, 12, 13, 14, 15};
  constexpr char Hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(32);
  for (auto i : order) {
    out += Hex[bytes[i] >> 4];
    out += Hex[bytes[i] & 15];
  }
  return out;
}
bool Plain(std::string_view s) {
  if (s.empty() || s.size() > 1024)
    return false;
  for (std::size_t i = 0; i < s.size();) {
    const auto lead = static_cast<unsigned char>(s[i++]);
    std::uint32_t cp{};
    unsigned n{};
    if (lead < 0x80)
      cp = lead;
    else if (lead >= 0xc2 && lead <= 0xdf) {
      cp = lead & 31;
      n = 1;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      cp = lead & 15;
      n = 2;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      cp = lead & 7;
      n = 3;
    } else
      return false;
    if (n > s.size() - i)
      return false;
    for (unsigned j = 0; j < n; ++j) {
      auto c = static_cast<unsigned char>(s[i++]);
      if ((c & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (c & 63);
    }
    if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) ||
        (n == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 || cp == 127 || cp == '<' ||
        cp == '>')
      return false;
  }
  return true;
}
} // namespace
EnglishReader::EnglishReader(ce_unity::RangeReader source)
    : unity(std::move(source), {}, std::string(Root)) {}
EnglishReader::EnglishReader(const EdenDsmodHostApi *host)
    : EnglishReader(ce_unity::MakeHostRangeReader(host)) {}
void EnglishReader::ClearCache() {
  std::lock_guard lock(mutex);
  labels.clear();
  keys.clear();
  loaded = false;
  unity.ClearCache();
}
std::optional<std::string> EnglishReader::Load(const Snapshot &objective) {
  std::lock_guard lock(mutex);
  if (!objective.supported || !objective.coherent || !objective.ready ||
      objective.objective_guid.empty())
    return {};
  const auto &ref = objective.description;
  if ((ref.table_type == 1 && GuidString(ref.table_guid) != Guid) ||
      (ref.table_type == 2 && ref.table_name != "UI_MainObjectives") ||
      (ref.table_type != 1 && ref.table_type != 2))
    return {};
  if (!loaded) {
    auto shared =
        Object(unity, Shared, "CAB-187dd10d55d33bb2e6150ae0122d9552", SharedId);
    auto english = Object(unity, English,
                          "CAB-e34d1e205cce7543c57e1e8468e132b0", EnglishId);
    if (!shared || !english ||
        !Is(shared->Get("m_TableCollectionName"), "UI_MainObjectives") ||
        !Is(shared->Get("m_TableCollectionNameGuidString"), Guid) ||
        !Is(english->Get("m_Name"), "UI_MainObjectives_en") ||
        !Is(english->Path("m_LocaleId.m_Code"), "en") ||
        Int(english->Path("m_SharedData.m_PathID")) != SharedId ||
        Int(english->Path("m_SharedData.m_FileID")) != 2) {
      unity.ClearCache();
      return {};
    }
    auto entries = Array(shared->Get("m_Entries")),
         rows = Array(english->Get("m_TableData"));
    if (!entries || !rows) {
      unity.ClearCache();
      return {};
    }
    std::map<std::string, std::int64_t, std::less<>> decoded_keys;
    std::set<std::int64_t> ids;
    for (const auto &r : *entries) {
      auto id = Int(r.Get("m_Id"));
      auto key = r.Get("m_Key");
      if (!id || *id <= 0 || !ids.insert(*id).second || !key ||
          key->kind != V::Kind::String || key->s.empty() ||
          key->s.size() > 256 || !decoded_keys.emplace(key->s, *id).second) {
        unity.ClearCache();
        return {};
      }
    }
    std::map<std::int64_t, std::string> decoded;
    for (const auto &r : *rows) {
      auto id = Int(r.Get("m_Id"));
      auto text = r.Get("m_Localized");
      if (!id || !ids.contains(*id) || !text || text->kind != V::Kind::String ||
          !Plain(text->s) || !decoded.emplace(*id, text->s).second) {
        unity.ClearCache();
        return {};
      }
    }
    if (decoded.size() != ids.size()) {
      unity.ClearCache();
      return {};
    }
    labels = std::move(decoded);
    keys = std::move(decoded_keys);
    loaded = true;
    unity.ClearCache();
  }
  std::int64_t id{};
  if (ref.entry_type == 2)
    id = ref.entry_id;
  else if (ref.entry_type == 1) {
    auto k = keys.find(ref.entry_key);
    if (k == keys.end())
      return {};
    id = k->second;
  } else
    return {};
  auto value = labels.find(id);
  return value == labels.end() ? std::optional<std::string>{} : value->second;
}
} // namespace belmont_objectives
