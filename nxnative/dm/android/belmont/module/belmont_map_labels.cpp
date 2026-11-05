// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_labels.h"
#include "belmont_map_sources.h"
#include <algorithm>
#include <set>
namespace belmont_map_labels {
namespace {
using V = ce_unity::Value;
constexpr std::string_view Root = "Data/StreamingAssets/aa/Switch/";
// Native district/display/localization asset identities, never localized text.
struct LabelSource {
  std::string_view world, district_bundle, district_cab, display_bundle,
      display_cab, collection;
  std::int64_t shared_id, english_id;
  std::int32_t shared_file_id;
};
constexpr LabelSource LabelSources[] = {
    {"World_Biome_01_Streets_Poor",
     "district-districtdata_streets-savablescriptableobject_assets_all_"
     "a7360e4b14d252869ffd592c17efe004.bundle",
     "CAB-2bdcac95e489306fe0cbfd0017b71428",
     "district-districtdata_streets_assets_all_"
     "a67fbddd6cfbd9d4b54812a06cb661e6.bundle",
     "CAB-9e91d3982e85d3bcae075d5fbad22610", "World_StreetPoor",
     -7953798063310951156LL, 5783849446827825953LL, 2},
    {"World_Biome_02_InnocentsCemetery",
     "district-districtdata_innocentscemetery-savablescriptableobject_assets_"
     "all_ba3ca8f9cf13bff5255ec8c087c84500.bundle",
     "CAB-917bd23528ec29dd947c967795cb9854",
     "district-districtdata_innocentscemetery_assets_all_"
     "75ae6dfb49977b4f36851aef40d2a1f6.bundle",
     "CAB-76a117d0df50aca0c0fec919f8d41d0d", "World_Cemetery",
     -6021095772137510310LL, -3958977973271791929LL, 2},
    {"World_Biome_03_Catacombs",
     "district-districtdata_catacombs-savablescriptableobject_assets_all_"
     "2c5599631df37e00b8a1c0d6e6109564.bundle",
     "CAB-7141a7465fc601463114a7fea44b74bd",
     "district-districtdata_catacombs_assets_all_"
     "f8b2592e639239a7e849b0b650ef23bf.bundle",
     "CAB-659ab0f8d6a9c7135dd962302651f9a5", "World_Catacombs",
     -2947602139505055068LL, -8512518600659959923LL, 2},
    {"World_Biome_05_NotreDame",
     "district-districtdata_notredame-savablescriptableobject_assets_all_"
     "55764b0987a98f6a32eccd39798ecd2a.bundle",
     "CAB-ce5c47d82ca9ae50d5617211c813c474",
     "district-districtdata_notredame_assets_all_"
     "d95542ff2638c9df7f45627444627b91.bundle",
     "CAB-b894994bbce125c377ee4d15418bf562", "World_NotreDame",
     -365140620871605399LL, 2507759878254267858LL, 2},
    {"World_Biome_06_MainHall",
     "district-districtdata_mainhall-savablescriptableobject_assets_all_"
     "21a44dc94ab1725d1fb79bad78406d32.bundle",
     "CAB-6f333dd3967e396f26fa7ab824ece87d",
     "district-districtdata_mainhall_assets_all_"
     "508e07bff978f5721f37b4330ea27c8c.bundle",
     "CAB-1fd3e09e878a818b2a0d79ac320665d1", "World_MainHall",
     -2300025070738497257LL, 7842833108877558450LL, 2},
    {"World_Biome_08_Library",
     "district-districtdata_library-savablescriptableobject_assets_all_"
     "9941cb47832135d5007815c99dbb233e.bundle",
     "CAB-7b343db52869fe36e337508a019d3b6c",
     "district-districtdata_library_assets_all_"
     "eba90c828280a188e9b40cf8c5750a87.bundle",
     "CAB-cc4f82c2795c7b1b8da10367e78a0e35", "World_Library",
     3749849501523766471LL, 6224182616963611598LL, 2},
    {"World_Biome_09_ClockTower",
     "district-districtdata_clocktower-savablescriptableobject_assets_all_"
     "3eac144db0bb2f9d6648cad1ffe65d0a.bundle",
     "CAB-4ebd6642207e5119620afb63e14c3761",
     "district-districtdata_clocktower_assets_all_"
     "36008c50d01783c19caea477abbf1e42.bundle",
     "CAB-cafa6a5afbceb36261c210f7bb0f38dd", "World_ClockTower",
     685367595850689736LL, 6169347463269811487LL, 2},
    {"World_Biome_10_HallOfMirrors",
     "district-districtdata_hallofmirrors-savablescriptableobject_assets_all_"
     "63d8aaf5423ffd98918b0656f8210244.bundle",
     "CAB-d9b9f2316521bc75305893bf52ff5197",
     "district-districtdata_hallofmirrors_assets_all_"
     "16d663d40c049744135ff8726d3cee67.bundle",
     "CAB-257231e80776f93b39e7cba8badb8cf7", "World_HallOfMirror",
     -8084838702621569373LL, -1134702080912616568LL, 2},
    {"World_Biome_11_MasterQuarter",
     "district-districtdata_masterquarter-savablescriptableobject_assets_all_"
     "bf7724d3a77f668df950f66935bc6663.bundle",
     "CAB-46d4d13e3eed80935cc5812484b3b5d5",
     "district-districtdata_masterquarter_assets_all_"
     "150dec9f967c9fec7148ce6522ed5710.bundle",
     "CAB-a0ea46d9ce90cc9905bc86d9481a1d9f", "World_MasterQuarters",
     -3345979374683269171LL, -4886040002670007985LL, 2},
    {"World_Biome_12_MQBadEnding",
     "district-districtdata_mqbadending-savablescriptableobject_assets_all_"
     "326541aef871fe71f5f46a6fb8c9fb43.bundle",
     "CAB-d371eda4a10b2badf02be961970a2df1",
     "district-districtdata_mqbadending_assets_all_"
     "36a3a6a7c75bfe00b969b2fe7cbcc9ac.bundle",
     "CAB-43109fa515bc98c055073c5f8cc25a1a", "UI_Common",
     -6226457662471024740LL, -1853024506947264134LL, 2},
};
constexpr std::string_view EnglishBundle =
    "localization-string-tables-english(en)_assets_all_"
    "aa06f0f9074c49a1bdd9e6b274bdec25.bundle";
constexpr std::string_view SharedBundle =
    "localization-assets-shared_assets_all_c0102878b4f0e869eaa8db1ab681b72a."
    "bundle";
bool I(const V *v, std::int64_t &out) {
  if (!v)
    return false;
  if (v->kind == V::Kind::Int) {
    out = v->i;
    return true;
  }
  if (v->kind == V::Kind::UInt && v->u <= std::uint64_t(INT64_MAX)) {
    out = static_cast<std::int64_t>(v->u);
    return true;
  }
  return false;
}
std::optional<std::string_view> Text(const V *v, std::size_t max = 512) {
  if (!v || v->kind != V::Kind::String || v->s.size() > max ||
      v->s.find('\0') != std::string::npos)
    return {};
  return v->s;
}
bool Equals(const V *v, std::string_view s) {
  auto t = Text(v);
  return t && *t == s;
}
const std::vector<V> *Array(const V *v, std::size_t max) {
  return v && v->kind == V::Kind::Array && v->items.size() <= max ? &v->items
                                                                  : nullptr;
}
bool Ref(const V *v, std::int64_t id, std::int32_t file = 0) {
  std::int64_t f{}, p{};
  return v && I(v->Get("m_FileID"), f) && f == file &&
         I(v->Get("m_PathID"), p) && p == id;
}
bool PlainUtf8(std::string_view s) {
  if (s.empty())
    return false;
  for (std::size_t i = 0; i < s.size();) {
    auto a = static_cast<unsigned char>(s[i++]);
    std::uint32_t cp{};
    unsigned n{};
    if (a < 128) {
      cp = a;
      n = 0;
    } else if (a >= 0xc2 && a <= 0xdf) {
      cp = a & 31;
      n = 1;
    } else if (a >= 0xe0 && a <= 0xef) {
      cp = a & 15;
      n = 2;
    } else if (a >= 0xf0 && a <= 0xf4) {
      cp = a & 7;
      n = 3;
    } else
      return false;
    if (n > s.size() - i)
      return false;
    for (unsigned j = 0; j < n; ++j) {
      auto b = static_cast<unsigned char>(s[i++]);
      if ((b & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (b & 63);
    }
    if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) ||
        (n == 3 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 || cp == 127 || cp == '<' ||
        cp == '>')
      return false;
  }
  return true;
}
std::optional<std::string> Lookup(const V &display, const V &shared,
                                  const V &english,
                                  const LabelSource &source = LabelSources[0]) {
  auto ref = display.Path(
      "districtDisplayName.m_TableReference.m_TableCollectionName");
  auto guid = Text(shared.Get("m_TableCollectionNameGuidString"), 32);
  auto collection = Text(ref, 37);
  std::int64_t key{};
  if (!guid || guid->size() != 32 || !collection ||
      *collection != std::string("GUID:") + std::string(*guid) ||
      !Equals(shared.Get("m_TableCollectionName"), source.collection) ||
      !Equals(english.Get("m_Name"), std::string(source.collection) + "_en") ||
      !Equals(english.Path("m_LocaleId.m_Code"), "en") ||
      !Ref(english.Get("m_SharedData"), source.shared_id,
           source.shared_file_id) ||
      !I(display.Path("districtDisplayName.m_TableEntryReference.m_KeyId"),
         key) ||
      key <= 0 ||
      !Equals(display.Path("districtDisplayName.m_TableEntryReference.m_Key"),
              ""))
    return {};
  const auto *shared_rows = Array(shared.Get("m_Entries"), 4096);
  const auto *rows = Array(english.Get("m_TableData"), 4096);
  if (!shared_rows || !rows)
    return {};
  bool shared_found = false;
  std::set<std::int64_t> keys;
  for (const auto &r : *shared_rows) {
    std::int64_t k{};
    if (!I(r.Get("m_Id"), k) || k <= 0 || !keys.insert(k).second)
      return {};
    if (k == key)
      shared_found = true;
  }
  if (!shared_found)
    return {};
  keys.clear();
  std::optional<std::string> result;
  for (const auto &r : *rows) {
    std::int64_t k{};
    if (!I(r.Get("m_Id"), k) || k <= 0 || !keys.insert(k).second)
      return {};
    if (k == key) {
      auto t = Text(r.Get("m_Localized"));
      if (!t || !PlainUtf8(*t))
        return {};
      result = std::string(*t);
    }
  }
  return result;
}
// The biome title is the collection's "<Biome>_BiomeName" entry (e.g.
// World_StreetPoor -> StreetPoor_BiomeName), found by key, never by a stored
// ID.
std::optional<std::string> BiomeName(const V &shared, const V &english,
                                     const LabelSource &source) {
  constexpr std::string_view prefix = "World_";
  if (!source.collection.starts_with(prefix) ||
      !Equals(shared.Get("m_TableCollectionName"), source.collection) ||
      !Equals(english.Get("m_Name"), std::string(source.collection) + "_en") ||
      !Equals(english.Path("m_LocaleId.m_Code"), "en"))
    return {};
  const auto wanted =
      std::string(source.collection.substr(prefix.size())) + "_BiomeName";
  const auto *shared_rows = Array(shared.Get("m_Entries"), 4096);
  const auto *rows = Array(english.Get("m_TableData"), 4096);
  if (!shared_rows || !rows)
    return {};
  std::int64_t key{};
  for (const auto &r : *shared_rows) {
    std::int64_t k{};
    if (Equals(r.Get("m_Key"), wanted)) {
      if (key || !I(r.Get("m_Id"), k) || k <= 0)
        return {};
      key = k;
    }
  }
  if (!key)
    return {};
  for (const auto &r : *rows) {
    std::int64_t k{};
    if (I(r.Get("m_Id"), k) && k == key) {
      auto t = Text(r.Get("m_Localized"), 256);
      if (!t || t->empty() || !PlainUtf8(*t))
        return {};
      return std::string(*t);
    }
  }
  return {};
}
// Minimal bounded JSON traversal for the installed Addressables catalog. It
// retains views into one capped file buffer, not a general-purpose DOM.
struct Json {
  std::string_view s;
  std::size_t p{}, nodes{};
  void Ws() {
    while (p < s.size() &&
           (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t'))
      ++p;
  }
  bool Char(char c) {
    Ws();
    if (p == s.size() || s[p] != c)
      return false;
    ++p;
    return true;
  }
  std::optional<std::string_view> String() {
    Ws();
    if (p == s.size() || s[p++] != '"')
      return {};
    auto start = p;
    while (p < s.size()) {
      auto c = static_cast<unsigned char>(s[p++]);
      if (c == '"')
        return s.substr(start, p - start - 1);
      if (c < 32)
        return {};
      if (c == '\\') {
        if (p == s.size())
          return {};
        auto e = s[p++];
        if (e == 'u') {
          for (int k = 0; k < 4; ++k) {
            if (p == s.size() ||
                !((s[p] >= '0' && s[p] <= '9') ||
                  (s[p] >= 'a' && s[p] <= 'f') || (s[p] >= 'A' && s[p] <= 'F')))
              return {};
            ++p;
          }
        } else if (std::string_view("\"\\/bfnrt").find(e) ==
                   std::string_view::npos)
          return {};
      }
    }
    return {};
  }
  bool Value(unsigned depth = 0) {
    if (depth > 16 || ++nodes > 1000000)
      return false;
    Ws();
    if (p == s.size())
      return false;
    if (s[p] == '"')
      return bool(String());
    if (s[p] == '{' || s[p] == '[') {
      bool obj = s[p++] == '{';
      Ws();
      auto close = obj ? '}' : ']';
      if (p < s.size() && s[p] == close) {
        ++p;
        return true;
      }
      for (;;) {
        if (obj && (!String() || !Char(':')))
          return false;
        if (!Value(depth + 1))
          return false;
        Ws();
        if (p == s.size())
          return false;
        if (s[p] == close) {
          ++p;
          return true;
        }
        if (!Char(','))
          return false;
      }
    }
    auto start = p;
    while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ']' &&
           s[p] != ' ' && s[p] != '\n' && s[p] != '\r' && s[p] != '\t')
      ++p;
    auto token = s.substr(start, p - start);
    if (token == "true" || token == "false" || token == "null")
      return true;
    std::size_t n = 0;
    if (n < token.size() && token[n] == '-')
      ++n;
    if (n == token.size())
      return false;
    if (token[n] == '0')
      ++n;
    else {
      if (token[n] < '1' || token[n] > '9')
        return false;
      while (n < token.size() && token[n] >= '0' && token[n] <= '9')
        ++n;
    }
    if (n < token.size() && token[n] == '.') {
      auto q = ++n;
      while (n < token.size() && token[n] >= '0' && token[n] <= '9')
        ++n;
      if (q == n)
        return false;
    }
    if (n < token.size() && (token[n] == 'e' || token[n] == 'E')) {
      ++n;
      if (n < token.size() && (token[n] == '+' || token[n] == '-'))
        ++n;
      auto q = n;
      while (n < token.size() && token[n] >= '0' && token[n] <= '9')
        ++n;
      if (q == n)
        return false;
    }
    return n == token.size();
  }
};
std::optional<std::map<std::string, std::string_view>>
CatalogFields(std::string_view s) {
  Json j{s};
  if (!j.Char('{'))
    return {};
  std::map<std::string, std::string_view> fields;
  std::set<std::string_view> seen;
  for (;;) {
    auto key = j.String();
    if (!key || key->find('\\') != std::string_view::npos ||
        !seen.insert(*key).second || !j.Char(':'))
      return {};
    j.Ws();
    auto at = j.p;
    if (!j.Value())
      return {};
    if (*key == "m_InternalIds" || *key == "m_KeyDataString" ||
        *key == "m_BucketDataString" || *key == "m_EntryDataString")
      fields.emplace(*key, s.substr(at, j.p - at));
    j.Ws();
    if (j.p < s.size() && s[j.p] == '}') {
      ++j.p;
      break;
    }
    if (!j.Char(','))
      return {};
  }
  j.Ws();
  if (j.p != s.size() || fields.size() != 4)
    return {};
  return fields;
}
std::optional<std::vector<std::uint8_t>> Base64(std::string_view raw) {
  Json j{raw};
  auto s = j.String();
  j.Ws();
  if (!s || j.p != raw.size() || s->size() % 4 || s->size() > 24 * 1024 * 1024)
    return {};
  std::vector<std::uint8_t> out;
  out.reserve(s->size() / 4 * 3);
  auto digit = [](char c) -> int {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    if (c == '+')
      return 62;
    if (c == '/')
      return 63;
    return -1;
  };
  for (std::size_t i = 0; i < s->size(); i += 4) {
    auto a = digit((*s)[i]), b = digit((*s)[i + 1]);
    bool pad2 = (*s)[i + 2] == '=', pad3 = (*s)[i + 3] == '=';
    auto c = pad2 ? 0 : digit((*s)[i + 2]), d = pad3 ? 0 : digit((*s)[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad3) ||
        ((pad2 || pad3) && i + 4 != s->size()) || (pad2 && (b & 15)) ||
        (!pad2 && pad3 && (c & 3)))
      return {};
    std::uint32_t n = (a << 18) | (b << 12) | (c << 6) | d;
    out.push_back(n >> 16);
    if (!pad2)
      out.push_back(n >> 8);
    if (!pad3)
      out.push_back(n);
  }
  return out;
}
std::optional<std::uint32_t> U32(std::span<const std::uint8_t> b,
                                 std::size_t p) {
  if (p > b.size() || 4 > b.size() - p)
    return {};
  return std::uint32_t(b[p]) | std::uint32_t(b[p + 1]) << 8 |
         std::uint32_t(b[p + 2]) << 16 | std::uint32_t(b[p + 3]) << 24;
}
bool Guid(std::string_view s) {
  return s.size() == 32 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
std::optional<std::map<std::string, std::string>>
Addresses(std::string_view catalog, const std::set<std::string> &guids) {
  auto fields = CatalogFields(catalog);
  if (!fields)
    return {};
  auto keys = Base64(fields->at("m_KeyDataString")),
       buckets = Base64(fields->at("m_BucketDataString")),
       entries = Base64(fields->at("m_EntryDataString"));
  if (!keys || !buckets || !entries)
    return {};
  auto kn = U32(*keys, 0), bn = U32(*buckets, 0), en = U32(*entries, 0);
  if (!kn || !bn || !en || *kn != *bn || *bn > 200000 || *en > 200000 ||
      entries->size() != 4 + std::size_t(*en) * 28)
    return {};
  std::map<std::string, std::uint32_t> targets;
  std::size_t pos = 4;
  for (std::uint32_t i = 0; i < *bn; ++i) {
    auto ko = U32(*buckets, pos), n = U32(*buckets, pos + 4);
    pos += 8;
    if (!ko || !n || *n > 200000 || pos > buckets->size() ||
        std::size_t(*n) > (buckets->size() - pos) / 4 || *ko >= keys->size())
      return {};
    if ((*keys)[*ko] == 0) {
      auto len = U32(*keys, std::size_t(*ko) + 1);
      if (!len || std::size_t(*ko) + 5 > keys->size() ||
          *len > keys->size() - (std::size_t(*ko) + 5))
        return {};
      if (*len == 32) {
        std::string guid(reinterpret_cast<const char *>(keys->data() + *ko + 5),
                         32);
        if (guids.contains(guid)) {
          if (*n != 1)
            return {};
          auto ei = U32(*buckets, pos);
          if (!ei || *ei >= *en)
            return {};
          auto id = U32(*entries, 4 + std::size_t(*ei) * 28);
          if (!id || !targets.emplace(guid, *id).second)
            return {};
        }
      }
    }
    pos += std::size_t(*n) * 4;
  }
  if (pos != buckets->size() || targets.size() != guids.size())
    return {};
  Json ids{fields->at("m_InternalIds")};
  if (!ids.Char('['))
    return {};
  std::map<std::uint32_t, std::string> wanted;
  for (const auto &[g, id] : targets)
    wanted.emplace(id, "");
  std::uint32_t index = 0;
  ids.Ws();
  if (ids.p == ids.s.size() || ids.s[ids.p] == ']')
    return {};
  for (;;) {
    auto s = ids.String();
    if (!s || s->size() > 4096 || index >= 200000)
      return {};
    if (auto it = wanted.find(index); it != wanted.end()) {
      if (s->find('\\') != std::string_view::npos ||
          !s->starts_with("Assets/Data/DistrictData/") ||
          s->find("..") != std::string::npos || !s->ends_with(".asset"))
        return {};
      it->second = *s;
    }
    ++index;
    ids.Ws();
    if (ids.p < ids.s.size() && ids.s[ids.p] == ']') {
      ++ids.p;
      break;
    }
    if (!ids.Char(','))
      return {};
  }
  ids.Ws();
  if (ids.p != ids.s.size())
    return {};
  std::map<std::string, std::string> result;
  for (const auto &[g, id] : targets) {
    auto it = wanted.find(id);
    if (it == wanted.end() || it->second.empty())
      return {};
    result.emplace(g, it->second);
  }
  return result;
}

std::optional<V> Object(ce_unity::Reader &reader, std::string_view bundle,
                        std::string_view cab, std::int64_t id,
                        std::string_view name) {
  auto b = reader.OpenBundle(bundle);
  auto sf = reader.OpenSerialized(bundle);
  if (!b || !sf || b->engine_version != "2022.3.62f2" || b->Nodes().empty() ||
      b->Nodes().front().path != cab || sf->Version() != 22 ||
      sf->Platform() != 38 || !sf->HasTypeTrees())
    return {};
  auto info = sf->Find(id);
  if (!info || info->class_id != 114 || info->byte_size > 1024 * 1024)
    return {};
  auto v = sf->ReadObject(*info);
  if (!v || !Equals(v->Get("m_Name"), name))
    return {};
  return v;
}
} // namespace
Reader::Reader(ce_unity::RangeReader source)
    : romfs(source), unity(std::move(source), {}, std::string(Root)) {}
Reader::Reader(const EdenDsmodHostApi *host)
    : Reader(ce_unity::MakeHostRangeReader(host)) {}
void Reader::ClearCache() {
  std::lock_guard lock(mutex);
  initialized = false;
  active_world.clear();
  labels.clear();
  unity.ClearCache();
}
std::optional<std::string> Reader::LoadFirstRoomAreaEnglish() {
  return LoadAreaEnglish(
      "World_Biome_01_Streets_Poor_StreetsPoor_FirstRoom_uid73");
}
std::optional<std::string> Reader::LoadBiomeEnglish(std::string_view room_id) {
  if (!LoadAreaEnglish(room_id))
    return {};
  std::lock_guard lock(mutex);
  return biome.empty() ? std::nullopt : std::optional<std::string>(biome);
}
std::optional<std::string> Reader::LoadAreaEnglish(std::string_view room_id) {
  std::lock_guard lock(mutex);
  if (room_id.empty() || room_id.size() > 256 ||
      room_id.find('\0') != std::string_view::npos)
    return {};
  const auto *recipe = belmont_map_sources::FindSourceForRoom(room_id);
  if (!recipe)
    return {};
  const LabelSource *source = nullptr;
  for (const auto &candidate : LabelSources)
    if (candidate.world == recipe->world_name)
      source = &candidate;
  if (!source)
    return {};
  try {
    if (initialized && active_world == source->world) {
      auto it = labels.find(room_id);
      return it == labels.end() ? std::nullopt
                                : std::optional<std::string>(it->second);
    }
    // A single active source bounds retained decoder and label memory.
    initialized = false;
    labels.clear();
    active_world.clear();
    biome.clear();
    unity.ClearCache();
    auto bundle = unity.OpenBundle(source->district_bundle);
    auto sf = unity.OpenSerialized(source->district_bundle);
    if (!bundle || !sf || bundle->engine_version != "2022.3.62f2" ||
        bundle->Nodes().empty() ||
        bundle->Nodes().front().path != source->district_cab ||
        sf->Version() != 22 || sf->Platform() != 38 || !sf->HasTypeTrees() ||
        sf->Objects().size() > 256)
      return {};
    std::map<std::string, std::string, std::less<>> room_guids;
    std::set<std::string> guids;
    unsigned districts = 0;
    for (const auto &info : sf->Objects()) {
      if (info.class_id != 114)
        continue;
      if (info.byte_size > 1024 * 1024 || ++districts > 128)
        return {};
      auto district = sf->ReadObject(info);
      if (!district)
        return {};
      const auto *rooms = Array(district->Get("districtRooms"), 128);
      auto guid = Text(district->Path("displayData.m_AssetGUID"), 32);
      if (!rooms || rooms->empty() || !guid || !Guid(*guid))
        return {};
      guids.emplace(*guid);
      for (const auto &room : *rooms) {
        auto id = Text(room.Get("worldRoomUniqueID"), 256);
        if (!id || !id->starts_with(std::string(source->world) + "_") ||
            !room_guids.emplace(std::string(*id), std::string(*guid)).second ||
            room_guids.size() > 256)
          return {};
      }
    }
    if (room_guids.empty())
      return {};
    // Resolve source AssetGUIDs through the installed Addressables catalog;
    // no derived room->label map, localized ID or display GUID table is
    // shipped.
    constexpr std::string_view path = "Data/StreamingAssets/aa/catalog.json";
    if (!romfs)
      return {};
    auto size = romfs.size(path);
    if (!size || *size == 0 || *size > 40 * 1024 * 1024)
      return {};
    std::string catalog(static_cast<std::size_t>(*size), '\0');
    for (std::size_t at = 0; at < catalog.size();) {
      auto n = std::min<std::size_t>(1024 * 1024, catalog.size() - at);
      if (!romfs.read(path, at,
                      reinterpret_cast<std::uint8_t *>(catalog.data() + at), n))
        return {};
      at += n;
    }
    auto addresses = Addresses(catalog, guids);
    if (!addresses)
      return {};
    catalog.clear();
    catalog.shrink_to_fit();
    auto display_bundle = unity.OpenBundle(source->display_bundle);
    auto display_sf = unity.OpenSerialized(source->display_bundle);
    if (!display_bundle || !display_sf ||
        display_bundle->engine_version != "2022.3.62f2" ||
        display_bundle->Nodes().empty() ||
        display_bundle->Nodes().front().path != source->display_cab ||
        display_sf->Version() != 22 || display_sf->Platform() != 38 ||
        !display_sf->HasTypeTrees() || display_sf->Objects().size() > 256)
      return {};
    std::optional<V> container;
    for (const auto &info : display_sf->Objects())
      if (info.class_id == 142) {
        if (container || info.byte_size > 1024 * 1024)
          return {};
        container = display_sf->ReadObject(info);
        if (!container)
          return {};
      }
    const auto *objects =
        container ? Array(container->Get("m_Container"), 128) : nullptr;
    if (!objects)
      return {};
    std::map<std::string, std::int64_t, std::less<>> display_ids;
    for (const auto &row : *objects) {
      if (row.kind != V::Kind::Array || row.items.size() != 2)
        return {};
      auto address = Text(&row.items[0], 4096);
      std::int64_t id{}, file{};
      if (!address || !I(row.items[1].Path("asset.m_PathID"), id) ||
          !I(row.items[1].Path("asset.m_FileID"), file) || file != 0 ||
          id == 0 || !display_ids.emplace(std::string(*address), id).second)
        return {};
    }
    auto shared = Object(
        unity, SharedBundle, "CAB-187dd10d55d33bb2e6150ae0122d9552",
        source->shared_id, std::string(source->collection) + " Shared Data");
    auto english =
        Object(unity, EnglishBundle, "CAB-e34d1e205cce7543c57e1e8468e132b0",
               source->english_id, std::string(source->collection) + "_en");
    if (!shared || !english)
      return {};
    biome = BiomeName(*shared, *english, *source).value_or(std::string{});
    std::map<std::string, std::string> guid_labels;
    for (const auto &[guid, address] : *addresses) {
      auto entry = display_ids.find(address);
      if (entry == display_ids.end())
        return {};
      auto info = display_sf->Find(entry->second);
      if (!info || info->class_id != 114 || info->byte_size > 1024 * 1024)
        return {};
      auto display = display_sf->ReadObject(*info);
      if (!display)
        return {};
      auto label = Lookup(*display, *shared, *english, *source);
      if (!label)
        return {};
      guid_labels.emplace(guid, std::move(*label));
    }
    std::map<std::string, std::string, std::less<>> result;
    for (const auto &[room, guid] : room_guids) {
      auto label = guid_labels.find(guid);
      if (label == guid_labels.end())
        return {};
      result.emplace(room, label->second);
    }
    labels = std::move(result);
    active_world = source->world;
    initialized = true;
    auto it = labels.find(room_id);
    return it == labels.end() ? std::nullopt
                              : std::optional<std::string>(it->second);
  } catch (...) {
    return {};
  }
}
std::optional<std::string>
Reader::LoadLandmarkEnglish(std::string_view room_id,
                            std::string_view table_reference,
                            std::int64_t entry_id) {
  std::lock_guard lock(mutex);
  const auto *recipe = belmont_map_sources::FindSourceForRoom(room_id);
  if (!recipe || entry_id <= 0)
    return {};
  const LabelSource *source = nullptr;
  for (const auto &candidate : LabelSources)
    if (candidate.world == recipe->world_name)
      source = &candidate;
  if (!source)
    return {};
  try {
    auto shared = Object(
        unity, SharedBundle, "CAB-187dd10d55d33bb2e6150ae0122d9552",
        source->shared_id, std::string(source->collection) + " Shared Data");
    auto english =
        Object(unity, EnglishBundle, "CAB-e34d1e205cce7543c57e1e8468e132b0",
               source->english_id, std::string(source->collection) + "_en");
    if (!shared || !english)
      return {};
    V display;
    display.kind = V::Kind::Object;
    V localized;
    localized.kind = V::Kind::Object;
    V table;
    table.kind = V::Kind::Object;
    V name;
    name.kind = V::Kind::String;
    name.s = table_reference;
    table.fields.emplace_back("m_TableCollectionName", std::move(name));
    V entry;
    entry.kind = V::Kind::Object;
    V id;
    id.kind = V::Kind::Int;
    id.i = entry_id;
    entry.fields.emplace_back("m_KeyId", std::move(id));
    V key;
    key.kind = V::Kind::String;
    entry.fields.emplace_back("m_Key", std::move(key));
    localized.fields.emplace_back("m_TableReference", std::move(table));
    localized.fields.emplace_back("m_TableEntryReference", std::move(entry));
    display.fields.emplace_back("districtDisplayName", std::move(localized));
    return Lookup(display, *shared, *english, *source);
  } catch (...) {
    return {};
  }
}
} // namespace belmont_map_labels
