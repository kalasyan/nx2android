// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_equipment_assets.h"
#include "belmont_map_refs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
namespace belmont_equipment_assets {
namespace {
using V = ce_unity::Value;
constexpr std::string_view Library =
    "libraries_assets_itemslibrary_11e0e5a270144563893aee8d3dd4cb82.bundle";
constexpr std::string_view LibraryCab = "CAB-70a58917a9e5aa76e400d4483da76dbd";
constexpr std::string_view Shared = "localization-assets-shared_assets_all_"
                                    "c0102878b4f0e869eaa8db1ab681b72a.bundle";
constexpr std::string_view English =
    "localization-string-tables-english(en)_assets_all_"
    "aa06f0f9074c49a1bdd9e6b274bdec25.bundle";
bool Int(const V *v, std::int64_t &n) {
  if (!v)
    return false;
  if (v->kind == V::Kind::Int) {
    n = v->i;
    return true;
  }
  if (v->kind == V::Kind::UInt && v->u <= INT64_MAX) {
    n = v->u;
    return true;
  }
  return false;
}
std::optional<std::string> Text(const V *v, std::size_t max = 512) {
  if (!v || v->kind != V::Kind::String || v->s.size() > max ||
      v->s.find('\0') != std::string::npos)
    return {};
  return v->s;
}
const std::vector<V> *Array(const V *v, std::size_t max = 4096) {
  return v && v->kind == V::Kind::Array && v->items.size() <= max ? &v->items
                                                                  : nullptr;
}
bool Ref(const V *v, std::int64_t &id) {
  std::int64_t f{};
  return v && Int(v->Get("m_FileID"), f) && f == 0 &&
         Int(v->Get("m_PathID"), id) && id;
}
bool Guid(std::string_view s) {
  return s.size() == 32 &&
         s.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
bool ValidID(std::string_view s) {
  return !s.empty() && s.size() <= 128 &&
         s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVW"
                             "XYZ0123456789_-") == std::string_view::npos;
}
std::uint32_t Hash(std::string_view s) {
  std::uint32_t a = 5381, b = 5381;
  for (std::size_t i = 0; i < s.size(); ++i) {
    auto &v = i % 2 ? b : a;
    v = v * 33 ^ static_cast<unsigned char>(s[i]);
  }
  return a + b * 1566083941u;
}
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

struct Catalog {
  std::vector<std::string> ids;
  std::vector<std::array<std::int32_t, 7>> entries;
  std::vector<std::vector<std::uint32_t>> buckets;
  std::map<std::string, std::vector<std::uint32_t>, std::less<>> guids;
  bool Load(std::string_view source) {
    auto fields = CatalogFields(source);
    if (!fields)
      return false;
    auto keys = Base64(fields->at("m_KeyDataString")),
         bs = Base64(fields->at("m_BucketDataString")),
         es = Base64(fields->at("m_EntryDataString"));
    if (!keys || !bs || !es)
      return false;
    auto bn = U32(*bs, 0), en = U32(*es, 0), kn = U32(*keys, 0);
    if (!bn || !en || !kn || *bn != *kn || *bn > 200000 || *en > 200000 ||
        es->size() != 4 + std::size_t(*en) * 28)
      return false;
    Json json{fields->at("m_InternalIds")};
    if (!json.Char('['))
      return false;
    for (;;) {
      auto s = json.String();
      if (!s || s->size() > 4096 || s->find('\\') != std::string_view::npos ||
          ids.size() >= 200000)
        return false;
      ids.emplace_back(*s);
      json.Ws();
      if (json.p < json.s.size() && json.s[json.p] == ']') {
        ++json.p;
        break;
      }
      if (!json.Char(','))
        return false;
    }
    json.Ws();
    if (json.p != json.s.size())
      return false;
    for (std::uint32_t i = 0; i < *en; ++i) {
      std::array<std::int32_t, 7> row{};
      for (unsigned j = 0; j < 7; ++j) {
        auto n = U32(*es, 4 + 28 * std::size_t(i) + 4 * j);
        if (!n)
          return false;
        row[j] = static_cast<std::int32_t>(*n);
      }
      if (row[0] < 0 || std::uint64_t(row[0]) >= ids.size())
        return false;
      entries.push_back(row);
    }
    std::size_t p = 4;
    for (std::uint32_t i = 0; i < *bn; ++i) {
      auto ko = U32(*bs, p), n = U32(*bs, p + 4);
      p += 8;
      if (!ko || !n || *n > 4096 || *ko >= keys->size() || p > bs->size() ||
          std::uint64_t(*n) * 4 > bs->size() - p)
        return false;
      std::vector<std::uint32_t> rows;
      for (std::uint32_t j = 0; j < *n; ++j) {
        auto v = U32(*bs, p);
        p += 4;
        if (!v || *v >= entries.size())
          return false;
        rows.push_back(*v);
      }
      buckets.push_back(rows);
      if ((*keys)[*ko] == 0) {
        auto len = U32(*keys, std::size_t(*ko) + 1);
        if (!len || std::size_t(*ko) + 5 > keys->size() ||
            *len > keys->size() - (std::size_t(*ko) + 5))
          return false;
        if (*len == 32) {
          std::string g(reinterpret_cast<const char *>(keys->data() + *ko + 5),
                        32);
          if (Guid(g) && !guids.emplace(g, rows).second)
            return false;
        }
      }
    }
    return p == bs->size();
  }
  struct Address {
    std::string asset;
    std::vector<std::string> bundles;
  };
  std::optional<Address> Find(std::string_view guid) {
    auto it = guids.find(guid);
    if (it == guids.end() || it->second.empty() || it->second.size() > 16)
      return {};
    std::optional<Address> out;
    for (auto index : it->second) {
      auto &r = entries[index];
      auto &path = ids[r[0]];
      if (!path.starts_with("Assets/") ||
          path.find("..") != std::string::npos || r[2] < 0 ||
          std::uint64_t(r[2]) >= buckets.size())
        continue;
      Address a;
      a.asset = path;
      for (auto dep : buckets[r[2]]) {
        auto &dr = entries[dep];
        if (dr[6] != 0)
          continue;
        auto full = std::string_view(ids[dr[0]]);
        constexpr std::string_view prefix =
            "{UnityEngine.AddressableAssets.Addressables.RuntimePath}/Switch/";
        if (!full.starts_with(prefix))
          return {};
        auto name = full.substr(prefix.size());
        if (name.empty() || name.size() > 240 || !name.ends_with(".bundle") ||
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ"
                                   "RSTUVWXYZ0123456789_-.") !=
                std::string_view::npos ||
            name.find("..") != std::string_view::npos)
          return {};
        a.bundles.emplace_back(name);
      }
      if (out && out->asset != a.asset)
        return {};
      out = std::move(a);
    }
    return out;
  }
};
struct Cursor {
  std::span<const std::uint8_t> b;
  std::size_t p{};
  bool ok{true};
  bool Skip(std::size_t n) {
    if (p > b.size() || n > b.size() - p) {
      ok = false;
      return false;
    }
    p += n;
    return true;
  }
  std::uint32_t U() {
    if (!Skip(4))
      return 0;
    auto i = p - 4;
    return std::uint32_t(b[i]) | std::uint32_t(b[i + 1]) << 8 |
           std::uint32_t(b[i + 2]) << 16 | std::uint32_t(b[i + 3]) << 24;
  }
  std::int64_t I() {
    auto a = U(), c = U();
    return std::int64_t(std::uint64_t(a) | std::uint64_t(c) << 32);
  }
  std::string S(std::size_t max = 512) {
    auto n = U();
    if (n > max || !Skip(n)) {
      ok = false;
      return {};
    }
    std::string s(reinterpret_cast<const char *>(b.data() + p - n), n);
    Skip((4 - p % 4) % 4);
    if (s.find('\0') != std::string::npos)
      ok = false;
    return s;
  }
  std::uint32_t N() {
    auto n = U();
    if (n > 4096) {
      ok = false;
      return 0;
    }
    return n;
  }
  struct Asset {
    std::string guid, name, type;
  };
  Asset A() { return {S(32), S(256), S(512)}; }
  struct Loc {
    std::string table;
    std::int64_t key{};
  };
  Loc L(bool allow_variables = false) {
    Loc v;
    v.table = S(64);
    v.key = I();
    S(256);
    auto fallback = U(), wait = U(), vars = U();
    if (fallback > 2 || wait > 1 || vars > 128 || (!allow_variables && vars))
      ok = false;
    for (std::uint32_t i = 0; i < vars && ok; ++i) {
      S(256);
      I();
    }
    return v;
  }
};
struct NativeItem {
  std::string id;
  std::int32_t type{};
  Cursor::Loc name;
  Cursor::Asset icon;
  std::int32_t illustration_file{};
  std::int64_t illustration_id{};
  std::int32_t character_file{};
  std::int64_t character_id{};
};
std::optional<NativeItem> Decode(std::span<const std::uint8_t> raw) {
  if (raw.size() > 2 * 1024 * 1024)
    return {};
  Cursor c{raw};
  if (c.U() != 0 || c.I() != 0 || c.U() != 1)
    return {};
  auto scriptFile = c.U();
  auto scriptID = c.I();
  if (scriptFile != 1 || !scriptID)
    return {};
  c.S(256);
  for (unsigned i = 0; i < 2; ++i) {
    auto n = c.N();
    for (std::uint32_t k = 0; k < n && c.ok; ++k)
      c.A();
  }
  auto common = c.N();
  c.Skip(std::uint64_t(common) * 12);
  for (unsigned i = 0; i < 2; ++i) {
    auto n = c.N();
    c.Skip(std::uint64_t(n) * 4);
  }
  NativeItem out;
  out.id = c.S(128);
  out.type = c.U();
  out.name = c.L();
  c.L(true);
  c.L(true);
  auto direct = c.A(), atlas = c.A();
  out.icon = Guid(direct.guid) ? direct : atlas;
  // Bounded ItemData/ActiveItemData prefix. Managed conditions/stat providers
  // are never evaluated; only their serialized references are skipped.
  auto notions = c.N();
  c.Skip(std::uint64_t(notions) * 12);
  auto stats = c.N();
  c.Skip(std::uint64_t(stats) * 12);
  auto abstractFlag = c.U();
  auto flags = c.U();
  if (flags != 0 || abstractFlag != 0)
    c.ok = false;
  if (out.type == 1 || out.type == 2) {
    auto attacks = c.N();
    c.Skip(std::uint64_t(attacks) * 48);
    auto attachments = c.N();
    c.Skip(std::uint64_t(attachments) * 44);
    if (out.type == 1) {
      c.Skip(12);
      c.A();
      c.Skip(12);
      c.L(true);
      c.L(true);
      c.Skip(4 + 12 + 12);
      out.illustration_file = static_cast<std::int32_t>(c.U());
      out.illustration_id = c.I();
    } else {
      out.character_file = static_cast<std::int32_t>(c.U());
      out.character_id = c.I();
    }
  }

  if (!c.ok || !ValidID(out.id) || !Guid(out.icon.guid) ||
      !out.name.table.starts_with("GUID:") ||
      !Guid(std::string_view(out.name.table).substr(5)) || out.name.key <= 0)
    return {};
  return out;
}
std::shared_ptr<const ce_unity::SerializedFile> File(ce_unity::Reader &r,
                                                     std::string_view bundle) {
  auto b = r.OpenBundle(bundle);
  auto f = r.OpenSerialized(bundle);
  return b && f && b->engine_version == "2022.3.62f2" && f->Platform() == 38 &&
                 f->Version() == 22 && f->HasTypeTrees()
             ? f
             : nullptr;
}
std::optional<std::int64_t>
Container(const std::shared_ptr<const ce_unity::SerializedFile> &f,
          std::string_view path, int expected_class = 114) {
  std::optional<std::int64_t> out;
  for (const auto &o : f->Objects())
    if (o.class_id == 142) {
      auto v = f->ReadObject(o);
      auto rows = v ? Array(v->Get("m_Container"), 65536) : nullptr;
      if (!rows)
        return {};
      for (const auto &row : *rows) {
        if (row.kind != V::Kind::Array || row.items.size() != 2)
          return {};
        auto name = Text(&row.items[0], 4096);
        if (name && *name == path) {
          std::int64_t id{};
          if (!Ref(row.items[1].Get("asset"), id))
            return {};
          auto target = f->Find(id);
          if (!target || target->class_id != expected_class)
            continue;
          if (out)
            return {};
          out = id;
        }
      }
    }
  return out;
}
} // namespace
struct Reader::Impl {
  std::mutex mutex;
  ce_unity::RangeReader source;
  ce_unity::Reader unity;
  belmont_assets::Reader textures;
  std::optional<Catalog> catalog;
  std::optional<V> library;
  std::map<std::string, std::string> names;
  std::map<std::string, Item> cache;
  Impl(ce_unity::RangeReader r, ce_unity::AstcDecoder a)
      : source(r), unity(r, a), textures(r, a) {
    unity.SetUseBuiltinPins(false);
  }
  bool Init() {
    if (catalog && library)
      return true;
    auto f = File(unity, Library);
    auto b = unity.OpenBundle(Library);
    auto o = f ? f->Find(6106735095123141531LL) : nullptr;
    if (!b || !b->FindNode(LibraryCab) || !o || o->class_id != 114)
      return false;
    auto l = f->ReadObject(*o);
    if (!l || Text(l->Get("m_Name")) != "ItemsLibrary")
      return false;
    auto n = source.size ? source.size("Data/StreamingAssets/aa/catalog.json")
                         : std::nullopt;
    if (!n || *n > 40 * 1024 * 1024)
      return false;
    std::string text(*n, '\0');
    if (!source.read("Data/StreamingAssets/aa/catalog.json", 0,
                     reinterpret_cast<std::uint8_t *>(text.data()),
                     text.size()))
      return false;
    Catalog c;
    if (!c.Load(text))
      return false;
    library = std::move(l);
    catalog = std::move(c);
    return true;
  }
  std::optional<std::string> GuidFor(std::string_view id) {
    auto keys = Array(library->Path("_idsToElementRefs.m_keys"), 4096),
         values = Array(library->Path("_idsToElementRefs.m_values"), 4096);
    if (!keys || !values || keys->size() != values->size())
      return {};
    auto hash = static_cast<std::int32_t>(Hash(id));
    std::optional<std::string> out;
    for (std::size_t i = 0; i < keys->size(); ++i) {
      std::int64_t k{};
      if (!Int(&(*keys)[i], k))
        return {};
      if (k == hash) {
        auto g = Text((*values)[i].Get("m_AssetGUID"), 32);
        if (out || !g || !Guid(*g))
          return {};
        out = *g;
      }
    }
    return out;
  }
  std::optional<std::string> Name(const Cursor::Loc &ref) {
    auto ck = ref.table + ":" + std::to_string(ref.key);
    if (auto it = names.find(ck); it != names.end())
      return it->second;
    auto shared = File(unity, Shared);
    if (!shared)
      return {};
    std::optional<std::string> collection;
    for (const auto &o : shared->Objects())
      if (o.class_id == 114) {
        auto v = shared->ReadObject(o);
        auto guid = v ? Text(v->Get("m_TableCollectionNameGuidString"), 32)
                      : std::nullopt;
        if (guid && ref.table == "GUID:" + *guid) {
          if (collection)
            return {};
          collection = Text(v->Get("m_TableCollectionName"), 256);
        }
      }
    if (!collection)
      return {};
    auto table = unity.ReadObjectByName(English, *collection + "_en", 114);
    auto rows = table ? Array(table->Get("m_TableData"), 16384) : nullptr;
    if (!rows || Text(table->Path("m_LocaleId.m_Code")) != "en")
      return {};
    std::optional<std::string> out;
    for (const auto &r : *rows) {
      std::int64_t k{};
      if (Int(r.Get("m_Id"), k) && k == ref.key) {
        auto s = Text(r.Get("m_Localized"), 256);
        if (out || !s || s->empty() ||
            s->find_first_of("<>\r\n") != std::string::npos)
          return {};
        out = *s;
      }
    }
    if (out)
      names.emplace(ck, *out);
    return out;
  }
  struct Resolved {
    std::string bundle;
    std::shared_ptr<const ce_unity::SerializedFile> file;
    std::int64_t id{};
  };
  std::optional<Resolved> Resolve(std::string_view owner, const V *ptr,
                                  const std::vector<std::string> &deps) {
    std::int64_t f{}, id{};
    if (!ptr || !Int(ptr->Get("m_FileID"), f) ||
        !Int(ptr->Get("m_PathID"), id) || !id || f < 0)
      return {};
    auto b = unity.OpenBundle(owner);
    if (!b || b->Nodes().empty())
      return {};
    if (!f) {
      auto sf = File(unity, owner);
      return sf ? std::optional<Resolved>({std::string(owner), sf, id})
                : std::nullopt;
    }
    auto cabs =
        belmont_map_refs::ExternalCabs(b->NodeSource(b->Nodes().front()));
    if (!cabs || std::uint64_t(f) > cabs->size())
      return {};
    auto cab = (*cabs)[f - 1];
    for (std::size_t i = 0; i < std::min<std::size_t>(32, deps.size()); ++i) {
      auto candidate = unity.OpenBundle(deps[i]);
      if (candidate && candidate->FindNode(cab)) {
        auto sf = File(unity, deps[i]);
        if (sf && candidate->Nodes().front().path == cab)
          return Resolved{deps[i], sf, id};
      }
    }
    return {};
  }
  bool SameKey(const V *a, const V *b) {
    if (!a || !b)
      return false;
    std::int64_t as{}, bs{};
    if (!Int(a->Get("second"), as) || !Int(b->Get("second"), bs) || as != bs)
      return false;
    auto ag = a->Get("first"), bg = b->Get("first");
    if (!ag || !bg || ag->kind != V::Kind::Object ||
        bg->kind != V::Kind::Object || ag->fields.size() != 4 ||
        bg->fields.size() != 4)
      return false;
    for (unsigned i = 0; i < 4; ++i) {
      std::int64_t av{}, bv{};
      if (!Int(&ag->fields[i].second, av) || !Int(&bg->fields[i].second, bv) ||
          av != bv)
        return false;
    }
    return true;
  }
  std::optional<ce_unity::Image> Pixels(std::string_view bundle,
                                        const V &sprite,
                                        const std::vector<std::string> &deps,
                                        std::int64_t sprite_id = 0) {
    const V *rd = sprite.Get("m_RD");
    std::optional<V> atlas;
    auto ar = Resolve(bundle, sprite.Get("m_SpriteAtlas"), deps);
    std::string owner(bundle);
    if (ar) {
      auto o = ar->file->Find(ar->id);
      atlas = o && o->class_id == 687078895 ? ar->file->ReadObject(*o)
                                            : std::nullopt;
      auto map = atlas ? Array(atlas->Get("m_RenderDataMap"), 4096) : nullptr;
      if (!map)
        return {};
      bool found = false;
      for (const auto &pair : *map)
        if (SameKey(sprite.Get("m_RenderDataKey"), pair.Get("first"))) {
          if (found)
            return {};
          found = true;
          rd = pair.Get("second");
        }
      if (!found)
        return {};
      owner = ar->bundle;
    }
    std::int64_t settings{};
    if (!rd || !Int(rd->Get("settingsRaw"), settings) || ((settings >> 2) & 15))
      return {};
    auto tex = Resolve(owner, rd->Get("texture"), deps);
    if (!tex)
      return {};
    auto object = tex->file->Find(tex->id);
    if (!object || object->class_id != 28)
      return {};
    if (!ar && ((settings >> 1) & 1) == 0) {
      // Reuse the SDK's source mesh mask for tight sprites. A rectangular
      // crop can otherwise include nontransparent pixels outside the native
      // SpriteRenderer mesh. This path is limited to a local texture and the
      // SDK's verified multi-GOB decoder; one-GOB tight layouts fail closed.
      if (!sprite_id || tex->bundle != bundle)
        return {};
      auto tv = tex->file->ReadObject(*object);
      auto blob = tv ? tv->Get("m_PlatformBlob") : nullptr;
      std::vector<std::uint8_t> bytes;
      if (blob && blob->kind == V::Kind::Bytes)
        bytes.assign(blob->s.begin(), blob->s.end());
      else if (auto values = Array(blob, 1024))
        for (const auto &v : *values) {
          std::int64_t n{};
          if (!Int(&v, n) || n < 0 || n > 255)
            return {};
          bytes.push_back(n);
        }
      if (bytes.size() < 12)
        return {};
      auto shift = std::uint32_t(bytes[8]) | std::uint32_t(bytes[9]) << 8 |
                   std::uint32_t(bytes[10]) << 16 |
                   std::uint32_t(bytes[11]) << 24;
      if (shift == 0 || shift > 5)
        return {};
      auto name = Text(sprite.Get("m_Name"));
      if (!name)
        return {};
      return unity.LoadSpritePinned(bundle, *name, {0, sprite_id});
    }

    auto image = textures.LoadImage("native:" + tex->bundle + "#" +
                                    std::to_string(tex->id));
    if (!image)
      return {};
    auto num = [&](const char *k) -> std::optional<double> {
      auto v = rd->Path(k);
      return v && v->kind == V::Kind::Float && std::isfinite(v->f)
                 ? std::optional<double>(v->f)
                 : std::nullopt;
    };
    auto x = num("textureRect.x"), y = num("textureRect.y"),
         w = num("textureRect.width"), h = num("textureRect.height");
    if (!x || !y || !w || !h)
      return {};
    auto ix = std::llround(*x), iy = std::llround(*y), iw = std::llround(*w),
         ih = std::llround(*h);
    if (ix < 0 || iy < 0 || iw <= 0 || ih <= 0 || iw > 2048 || ih > 2048 ||
        ix + iw > image->width || iy + ih > image->height)
      return {};
    ce_unity::Image crop;
    crop.width = iw;
    crop.height = ih;
    crop.rgba.resize(std::size_t(iw) * ih * 4);
    for (std::int64_t row = 0; row < ih; ++row) {
      auto sy = std::int64_t(image->height) - iy - ih + row;
      std::memcpy(crop.rgba.data() + row * iw * 4,
                  image->rgba.data() + (sy * image->width + ix) * 4,
                  std::size_t(iw) * 4);
    }
    return crop;
  }
  V PtrValue(std::int32_t f, std::int64_t id) {
    V out;
    out.kind = V::Kind::Object;
    V fv, iv;
    fv.kind = iv.kind = V::Kind::Int;
    fv.i = f;
    iv.i = id;
    out.fields.emplace_back("m_FileID", std::move(fv));
    out.fields.emplace_back("m_PathID", std::move(iv));
    return out;
  }
  bool PortraitMaterialSupported() {
    constexpr std::string_view bundle =
        "ui-commonsalltime_assets_all_cbf3732ca9bb007ffe39c0428d68b4e2.bundle";
    constexpr std::string_view cab = "CAB-8ad76c76a25b1633521dad0cadb8b424";
    auto b = unity.OpenBundle(bundle);
    auto f = File(unity, bundle);
    auto object = f ? f->Find(2710570340017399780LL) : nullptr;
    if (!b || !b->FindNode(cab) || !object || object->class_id != 21)
      return false;
    auto material = f->ReadObject(*object);
    if (!material ||
        Text(material->Get("m_Name")) != "RedWine_UI_Image_Goldleaf_Portraits")
      return false;
    std::int64_t shaderFile{}, shaderID{};
    auto ptr = material->Get("m_Shader");
    if (!ptr || !Int(ptr->Get("m_FileID"), shaderFile) ||
        !Int(ptr->Get("m_PathID"), shaderID) || shaderFile != 3 ||
        shaderID != -6499671801937755723LL)
      return false;
    auto cabs =
        belmont_map_refs::ExternalCabs(b->NodeSource(*b->FindNode(cab)));
    if (!cabs || cabs->size() < 3 ||
        (*cabs)[2] != "CAB-9f67343a1df2c3ec046076fb94b2165a")
      return false;
    auto keywords = Array(material->Get("m_ValidKeywords"), 32);
    if (!keywords)
      return false;
    bool byAlpha = false, byEdge = false;
    for (const auto &k : *keywords) {
      auto key = Text(&k, 128);
      if (!key)
        return false;
      byAlpha |= *key == "_GOLDLEAFMODE_GOLDLEAF_BY_ALPHA";
      byEdge |= *key == "_GOLDLEAFMODE_GOLDLEAF_BY_EDGE";
    }
    if (!byAlpha || !byEdge)
      return false;
    auto floats = Array(material->Path("m_SavedProperties.m_Floats"), 128);
    auto colors = Array(material->Path("m_SavedProperties.m_Colors"), 128);
    if (!floats || !colors)
      return false;
    std::map<std::string, double> values;
    for (const auto &pair : *floats) {
      auto name = Text(pair.Get("first"), 128);
      auto v = pair.Get("second");
      if (!name || !v || v->kind != V::Kind::Float || !std::isfinite(v->f) ||
          !values.emplace(*name, v->f).second)
        return false;
    }
    if (!values.contains("_FilterByImageAlphaRange") ||
        values.at("_FilterByImageAlphaRange") != 1 ||
        !values.contains("_FilterByImageAlphaContrast") ||
        values.at("_FilterByImageAlphaContrast") != 1 ||
        !values.contains("_Saturation") || values.at("_Saturation") != 1 ||
        !values.contains("_UseVideoSettingsBrightness") ||
        values.at("_UseVideoSettingsBrightness") != 0)
      return false;
    bool white = false;
    for (const auto &pair : *colors) {
      auto name = Text(pair.Get("first"), 128);
      if (name && *name == "_Color") {
        if (white)
          return false;
        auto v = pair.Get("second");
        for (auto channel : {"r", "g", "b", "a"}) {
          auto c = v ? v->Get(channel) : nullptr;
          if (!c || c->kind != V::Kind::Float || c->f != 1)
            return false;
        }
        white = true;
      }
    }
    return white;
  }
  void Arcana(std::string_view owner, const NativeItem &item,
              const std::vector<std::string> &deps, Item &out) {
    if (item.type != 2 || !item.character_id || item.character_file < 0)
      return;
    auto cp = PtrValue(item.character_file, item.character_id);
    auto character = Resolve(owner, &cp, deps);
    if (!character)
      return;
    constexpr std::string_view dictionary =
        "libraries_assets_characterdictionary_uidisplaydata_"
        "02b3f62922c8a61d745faf6361d111c8.bundle";
    auto df = File(unity, dictionary);
    auto dob = df ? df->Find(-1957202046911590528LL) : nullptr;
    auto dv = dob && dob->class_id == 114 ? df->ReadObject(*dob) : std::nullopt;
    auto elements = dv ? Array(dv->Get("elements"), 256) : nullptr;
    if (!elements ||
        Text(dv->Get("m_Name")) != "CharacterDictionary_UIDisplayData")
      return;
    std::optional<std::string> guid;
    std::vector<std::string> dictionaryDeps{std::string(dictionary),
                                            character->bundle};
    for (const auto &e : *elements) {
      auto ref = Resolve(dictionary, e.Get("id"), dictionaryDeps);
      if (ref && ref->id == character->id && ref->bundle == character->bundle) {
        auto g = Text(e.Path("item.m_AssetGUID"), 32);
        if (guid || !g || !Guid(*g))
          return;
        guid = *g;
      }
    }
    auto address = guid ? catalog->Find(*guid) : std::nullopt;
    if (!address)
      return;
    for (std::size_t i = 0;
         i < std::min<std::size_t>(32, address->bundles.size()); ++i) {
      auto &bundle = address->bundles[i];
      auto f = File(unity, bundle);
      auto id = f ? Container(f, address->asset) : std::nullopt;
      auto o = id ? f->Find(*id) : nullptr;
      auto raw = o && o->class_id == 114 ? f->ReadRaw(*o) : std::nullopt;
      if (!raw || raw->size() > 65536)
        continue;
      Cursor c{*raw};
      if (c.U() != 0 || c.I() != 0 || c.U() != 1 || c.U() != 1)
        return;
      c.I();
      c.S(256);
      c.I();
      c.L(true);
      c.Skip(4 * 12 + 12 + 12);
      auto spriteFile = static_cast<std::int32_t>(c.U());
      auto spriteID = c.I();
      auto gold = c.U();
      if (gold > 1)
        return;
      c.Skip(12);
      c.S(256);
      c.Skip(3 * 12 + 2 * 16);
      auto isArcana = c.U();
      auto tarotName = c.L();
      if (!c.ok || isArcana != 1 || spriteFile < 0 || !spriteID)
        return;
      auto sp = PtrValue(spriteFile, spriteID);
      auto art = Resolve(bundle, &sp, address->bundles);
      auto so = art ? art->file->Find(art->id) : nullptr;
      auto sprite =
          so && so->class_id == 213 ? art->file->ReadObject(*so) : std::nullopt;
      if (!sprite)
        return;
      auto image = Pixels(art->bundle, *sprite, address->bundles, art->id);
      auto name = Name(tarotName);
      if (!image || !name)
        return;
      if (gold) {
        if (!PortraitMaterialSupported())
          return;
        // This portrait material stores a gold/edge mask in texture alpha.
        // Its native filter range/contrast are both one: at source 8-bit
        // texel centres the opacity is zero for A=0 and full for every A>0.
        // Preserve source RGB. Animated gold specular highlights are omitted.
        for (std::size_t a = 3; a < image->rgba.size(); a += 4)
          image->rgba[a] = image->rgba[a] ? 255 : 0;
      }
      out.arcana = std::move(*image);
      out.arcana_name = *name;
      return;
    }
  }
  std::optional<ce_unity::Image> Icon(const Cursor::Asset &ref,
                                      std::string &foundBundle,
                                      std::string &foundName) {
    auto address = catalog->Find(ref.guid);
    if (!address)
      return {};
    auto path = address->asset;
    for (std::size_t i = 0;
         i < std::min<std::size_t>(32, address->bundles.size()); ++i) {
      const auto &bundle = address->bundles[i];
      auto f = File(unity, bundle);
      if (!f)
        continue;
      std::optional<V> sprite;
      std::int64_t sid{};
      if (!ref.name.empty()) {
        for (const auto &o : f->Objects())
          if (o.class_id == 213) {
            auto name = f->PeekName(o);
            if (name && *name == ref.name) {
              if (sprite)
                return {};
              sprite = f->ReadObject(o);
              sid = o.path_id;
            }
          }
      } else {
        auto id = Container(f, path, 213);
        if (!id)
          id = Container(f, path, 28);
        auto o = id ? f->Find(*id) : nullptr;
        if (o && o->class_id == 213) {
          sprite = f->ReadObject(*o);
          sid = *id;
        } else if (o && o->class_id == 28) {
          for (const auto &so : f->Objects())
            if (so.class_id == 213) {
              auto v = f->ReadObject(so);
              std::int64_t t{};
              if (v && Ref(v->Path("m_RD.texture"), t) && t == *id) {
                if (sprite)
                  return {};
                sprite = std::move(v);
                sid = so.path_id;
              }
            }
        }
      }
      if (!sprite)
        continue;
      auto sn = Text(sprite->Get("m_Name"));
      if (!sn)
        return {};
      auto image = Pixels(bundle, *sprite, address->bundles, sid);
      if (!image)
        return {};
      foundBundle = bundle;
      foundName = *sn;
      (void)sid;
      return image;
    }
    return {};
  }
};
Reader::Reader(ce_unity::RangeReader source, ce_unity::AstcDecoder astc)
    : impl(std::make_unique<Impl>(std::move(source), std::move(astc))) {}
Reader::Reader(const EdenDsmodHostApi *host, ce_unity::AstcDecoder astc)
    : Reader(ce_unity::MakeHostRangeReader(host), std::move(astc)) {}
Reader::~Reader() = default;
void Reader::ClearCache() {
  std::lock_guard lock(impl->mutex);
  impl->cache.clear();
  impl->names.clear();
  impl->catalog.reset();
  impl->library.reset();
  impl->unity.ClearCache();
  impl->textures.ClearCache();
}
std::optional<Item> Reader::ResolveEnglish(Type type, std::string_view id) {
  if (!ValidID(id) || (type != Type::Weapon && type != Type::Spell &&
                       type != Type::Charm && type != Type::Skin))
    return {};
  std::lock_guard lock(impl->mutex);
  try {
    auto key = std::to_string(int(type)) + ":" + std::string(id);
    if (auto it = impl->cache.find(key); it != impl->cache.end())
      return it->second;
    if (!impl->Init())
      return {};
    auto guid = impl->GuidFor(id);
    auto address = guid ? impl->catalog->Find(*guid) : std::nullopt;
    if (!address)
      return {};
    auto path = address->asset;
    std::optional<Item> result;
    for (std::size_t i = 0;
         i < std::min<std::size_t>(32, address->bundles.size()); ++i) {
      auto &bundle = address->bundles[i];
      auto f = File(impl->unity, bundle);
      if (!f)
        continue;
      auto sourceID = Container(f, path);
      auto object = sourceID ? f->Find(*sourceID) : nullptr;
      if (!object || object->class_id != 114)
        continue;
      auto raw = f->ReadRaw(*object);
      auto native = raw ? Decode(*raw) : std::nullopt;
      if (!native || native->id != id || native->type != int(type))
        return {};
      auto name = impl->Name(native->name);
      if (!name)
        return {};
      Item out;
      out.id = id;
      out.type = type;
      out.name = *name;
      out.source_asset = address->asset;
      out.source_bundle = bundle;
      auto icon = impl->Icon(native->icon, out.icon_bundle, out.icon_sprite);
      if (!icon)
        return {};
      out.image = std::move(*icon);
      impl->Arcana(bundle, *native, address->bundles, out);
      if (type == Type::Weapon && native->illustration_id &&
          native->illustration_file >= 0) {
        V ptr;
        ptr.kind = V::Kind::Object;
        V fileValue, idValue;
        fileValue.kind = V::Kind::Int;
        fileValue.i = native->illustration_file;
        idValue.kind = V::Kind::Int;
        idValue.i = native->illustration_id;
        ptr.fields.emplace_back("m_FileID", std::move(fileValue));
        ptr.fields.emplace_back("m_PathID", std::move(idValue));
        auto art = impl->Resolve(bundle, &ptr, address->bundles);
        auto object = art ? art->file->Find(art->id) : nullptr;
        auto sprite = object && object->class_id == 213
                          ? art->file->ReadObject(*object)
                          : std::nullopt;
        if (sprite)
          out.illustration =
              impl->Pixels(art->bundle, *sprite, address->bundles, art->id);
      }

      result = std::move(out);
      break;
    }
    if (result) {
      if (impl->cache.size() >= 16)
        impl->cache.clear();
      impl->cache.emplace(key, *result);
      impl->unity.ClearCache();
      impl->textures.ClearCache();
    }
    return result;
  } catch (...) {
    return {};
  }
}
} // namespace belmont_equipment_assets

namespace belmont_equipment_assets {
std::optional<ce_unity::Image> Reader::ResolveUpgrade(bool mana, unsigned pieces) {
  if (pieces > 4) return {};
  std::lock_guard lock(impl->mutex);
  try {
    constexpr std::string_view bundle = "atlas-t_ui_common_collectibles_atlas_assets_all_bedae6c7148baaa4ccbe315140f6aa8f.bundle";
    auto file = File(impl->unity, bundle);
    auto opened = impl->unity.OpenBundle(bundle);
    if (!file || !opened || !opened->FindNode("CAB-2c35241d365106bdb8f637a8330bad87")) return {};
    constexpr std::string_view owner = "ui-collectibles_assets_all_9b30a9bc23f36c43875163ebd92b0ec1.bundle";
    auto atlasFile=File(impl->unity,owner);
    auto atlasBundle=impl->unity.OpenBundle(owner);
    auto atlasObject=atlasFile ? atlasFile->Find(1279585402129214424LL) : nullptr;
    auto atlas=atlasObject && atlasObject->class_id==687078895 ? atlasFile->ReadObject(*atlasObject) : std::nullopt;
    auto renderMap=atlas ? Array(atlas->Get("m_RenderDataMap"),4096) : nullptr;
    if(!atlasBundle || !atlasBundle->FindNode("CAB-3498a4904ed5700f5b4444fd37e1a63e") || !renderMap) return {};
    const auto round=[](double v){const auto f=std::floor(v),d=v-f;return std::int64_t(d<.5?f:d>.5?f+1:std::fmod(f,2)==0?f:f+1);};
    const std::int64_t health[] = {-8420212106308902653LL,7111227746032146772LL,-5064570947253341100LL,7584853110640693420LL};
    const std::int64_t magic[] = {-8687635812860969103LL,8378970302279540607LL,6552034755979668438LL,2002054895149306880LL};
    std::vector<std::int64_t> layers{-6450544958166635071LL,-3603595132252081608LL};
    for (unsigned i=0;i<pieces;++i) layers.push_back((mana ? magic : health)[i]);
    std::optional<ce_unity::Image> out;
    for (auto id: layers) {
      auto object=file->Find(id);
      auto sprite=object && object->class_id==213 ? file->ReadObject(*object) : std::nullopt;
      if(!sprite) return {};
      const V* render=nullptr;
      for(const auto& pair:*renderMap)
        if(impl->SameKey(sprite->Get("m_RenderDataKey"),pair.Get("first"))){if(render)return {};render=pair.Get("second");}
      std::int64_t settings{},fileId{},textureId{},alphaId{};
      if(!render || !Int(render->Get("settingsRaw"),settings) || (settings&3)!=3 || ((settings>>2)&15) ||
         !Int(render->Path("texture.m_FileID"),fileId) || fileId ||
         !Int(render->Path("texture.m_PathID"),textureId) ||
         !Int(render->Path("alphaTexture.m_PathID"),alphaId) || alphaId) return {};
      const auto num=[&](std::string_view path)->std::optional<double>{auto v=render->Path(path);return v&&v->kind==V::Kind::Float&&std::isfinite(v->f)&&std::abs(v->f)<16384?std::optional<double>(v->f):std::nullopt;};
      auto x=num("textureRect.x"),y=num("textureRect.y"),w=num("textureRect.width"),h=num("textureRect.height"),ox=num("textureRectOffset.x"),oy=num("textureRectOffset.y");
      if(!x||!y||!w||!h||!ox||!oy) return {};
      auto texture=impl->textures.LoadImage("native:"+std::string(owner)+"#"+std::to_string(textureId));
      if(!texture) return {};
      const auto x0=round(*x),y0=round(*y),x1=round(*x+*w),y1=round(*y+*h),dx=round(*ox),dy=round(*oy);
      const auto width=x1-x0,height=y1-y0;
      if(x0<0||y0<0||width<=0||height<=0||x1>texture->width||y1>texture->height||dx<0||dy<0||dx+width>128||dy+height>128)return {};
      ce_unity::Image layer{128,128,{}};layer.rgba.resize(128*128*4);
      for(std::int64_t row=0;row<height;++row)
        std::memcpy(layer.rgba.data()+((128-dy-height+row)*128+dx)*4,
          texture->rgba.data()+((texture->height-y1+row)*texture->width+x0)*4,std::size_t(width)*4);
      auto image=std::optional<ce_unity::Image>(std::move(layer));
      if (!out) { out=std::move(image); continue; }
      for (std::size_t i=0;i<out->rgba.size();i+=4) {
        const unsigned sa=image->rgba[i+3], da=out->rgba[i+3];
        const unsigned alpha=sa*255+da*(255-sa);
        for(unsigned c=0;c<3;++c)
          out->rgba[i+c]=alpha ? (unsigned(image->rgba[i+c])*sa*255+unsigned(out->rgba[i+c])*da*(255-sa)+alpha/2)/alpha : 0;
        out->rgba[i+3]=(alpha+127)/255;
      }
    }
    return out;
  } catch (...) { return {}; }
}
} // namespace belmont_equipment_assets
