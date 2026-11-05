// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_landmarks.h"
#include "belmont_map_refs.h"
#include "belmont_pins.h"
#include <algorithm>
#include <cmath>
#include <set>
namespace belmont_map_landmarks {
namespace {
using V = ce_unity::Value;
constexpr std::string_view Savable = "savablescriptableobject_assets_all_"
                                     "5b62e0c70cf11954564852274ab4ce5a.bundle";
constexpr std::string_view SavableCab = "CAB-7268582d7b52b05a701bb84d36e366c4";
constexpr std::string_view Menu =
    "ui-gamemenus_assets_all_866519f7d6a7057a8ba1c459a5b3f694.bundle";
constexpr std::string_view Common =
    "ui-commonsingame_assets_all_acb23e9b0608a368aad72d2fe488a679.bundle";
constexpr std::string_view CommonCab = "CAB-5d3eb16e2bd4e768158db17f3956cfa9";
constexpr std::string_view MenuCab = "CAB-27e7a4fd03161a1208808cab8fc89755";
constexpr std::string_view LevelIds="leveldata-savablescriptableobject_assets_all_ff1d7d702377adb724f6d92b5520502b.bundle";
constexpr std::string_view LevelIdsCab="CAB-5bdba696448e19fcf5755cc457f22fcf";
constexpr std::string_view LevelData="leveldata_assets_all_940f672e456a1dd57099d3d5c994fbc7.bundle";
constexpr std::string_view LevelDataCab="CAB-18af95c67205941bc13db59d8c142572";
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
bool Num(const V *v, float &f) {
  if (!v || v->kind != V::Kind::Float || !std::isfinite(v->f) ||
      std::abs(v->f) > 1e7)
    return false;
  f = v->f;
  return true;
}
bool Ref(const V *v, std::int64_t &id, std::int64_t &file) {
  return v && Int(v->Get("m_FileID"), file) && Int(v->Get("m_PathID"), id);
}
std::int64_t Local(const V *v) {
  std::int64_t id{}, f{};
  return Ref(v, id, f) && f == 0 ? id : 0;
}
const std::vector<V> *Array(const V *v, std::size_t max = 256) {
  return v && v->kind == V::Kind::Array && v->items.size() <= max ? &v->items
                                                                  : nullptr;
}
std::optional<std::string> Text(const V *v, std::size_t max = 256) {
  if (!v || v->kind != V::Kind::String || v->s.size() > max ||
      v->s.find('\0') != std::string::npos)
    return {};
  return v->s;
}
std::shared_ptr<const ce_unity::SerializedFile>
File(ce_unity::Reader &r, std::string_view bundle, std::string_view cab) {
  auto b = r.OpenBundle(bundle);
  auto n = b ? b->FindNode(cab) : nullptr;
  if (!n || b->engine_version != "2022.3.62f2")
    return {};
  auto f = ce_unity::SerializedFile::Open(b->NodeSource(*n));
  return f && f->Version() == 22 && f->Platform() == 38 && f->HasTypeTrees()
             ? f
             : nullptr;
}
std::optional<V> Obj(const std::shared_ptr<const ce_unity::SerializedFile> &f,
                     std::int64_t id, int cls) {
  auto o = f ? f->Find(id) : nullptr;
  return o && o->class_id == cls && o->byte_size < 4 * 1024 * 1024
             ? f->ReadObject(*o)
             : std::nullopt;
}
bool Metadata(const V &s, Sprite &art) {
  auto sn = Text(s.Get("m_Name"));
  std::int64_t tf{};
  if (!sn || !Num(s.Path("m_Rect.width"), art.width) ||
      !Num(s.Path("m_Rect.height"), art.height) ||
      !Num(s.Path("m_Rect.x"), art.rect.x) ||
      !Num(s.Path("m_Rect.y"), art.rect.y) ||
      !Num(s.Path("m_Pivot.x"), art.pivot_x) ||
      !Num(s.Path("m_Pivot.y"), art.pivot_y) ||
      !Num(s.Get("m_PixelsToUnits"), art.pixels_per_unit) ||
      art.pixels_per_unit <= 0 ||
      !Ref(s.Path("m_RD.texture"), art.texture_id, tf) || tf != 0 ||
      !art.texture_id)
    return false;
  art.name = *sn;
  art.rect.width = art.width;
  art.rect.height = art.height;
  auto tr = s.Path("m_RD.textureRect");
  if (!tr || !Num(tr->Get("x"), art.texture_rect.x) ||
      !Num(tr->Get("y"), art.texture_rect.y) ||
      !Num(tr->Get("width"), art.texture_rect.width) ||
      !Num(tr->Get("height"), art.texture_rect.height))
    return false;
  std::int64_t settings{};
  if (!Num(s.Path("m_RD.textureRectOffset.x"), art.texture_rect_offset_x) ||
      !Num(s.Path("m_RD.textureRectOffset.y"), art.texture_rect_offset_y) ||
      !Int(s.Path("m_RD.settingsRaw"), settings) || settings < 0 ||
      std::uint64_t(settings) > UINT32_MAX)
    return false;
  art.settings_raw = static_cast<std::uint32_t>(settings);
  const char *c[] = {"x", "y", "z", "w"};
  auto border = s.Get("m_Border");
  for (unsigned k = 0; k < 4; ++k)
    if (!border || !Num(border->Get(c[k]), art.border[k]))
      return false;
  return true;
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
          !s->starts_with("Assets/Data/Maps/MapMarkers/") ||
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

// Managed-reference payloads are not interpreted. This bounded prefix schema
// reads identity/localization and rejects every condition-dependent policy.
struct NativeData {
  std::string guid, table;
  std::int64_t key{};
  bool unconditional{};
};
std::optional<NativeData>
LandmarkData(const std::shared_ptr<const ce_unity::SerializedFile> &sf,
             std::int64_t id) {
  auto o = sf->Find(id);
  if (!o || o->class_id != 114 || o->byte_size > 65536)
    return {};
  auto raw = sf->ReadRaw(*o);
  if (!raw)
    return {};
  struct Cursor {
    std::span<const std::uint8_t> b;
    std::size_t p{};
    bool ok{true};
    std::uint32_t U() {
      if (p > b.size() || b.size() - p < 4) {
        ok = false;
        return 0;
      }
      auto v = std::uint32_t(b[p]) | std::uint32_t(b[p + 1]) << 8 |
               std::uint32_t(b[p + 2]) << 16 | std::uint32_t(b[p + 3]) << 24;
      p += 4;
      return v;
    }
    std::int64_t I() {
      auto lo = U();
      auto hi = U();
      return std::int64_t(std::uint64_t(lo) | std::uint64_t(hi) << 32);
    }
    std::string S(std::size_t max) {
      auto n = U();
      if (n > max || p > b.size() || n > b.size() - p) {
        ok = false;
        return {};
      }
      std::string s(reinterpret_cast<const char *>(b.data() + p), n);
      p = (p + n + 3) & ~std::size_t(3);
      if (p > b.size() || s.find('\0') != std::string::npos)
        ok = false;
      return s;
    }
    std::uint32_t Count(unsigned stride) {
      auto n = U();
      if (n > 128 || p > b.size() || std::uint64_t(n) * stride > b.size() - p) {
        ok = false;
        return 0;
      }
      p += n * stride;
      return n;
    }
  } c{*raw};
  if (c.U() != 0 || c.I() != 0 || c.U() != 1 || c.U() != 1 ||
      c.I() != 7762847280348797211LL || !c.S(256).starts_with("Landmark_"))
    return {};
  NativeData d;
  d.guid = c.S(32);
  d.table = c.S(64);
  d.key = c.I();
  c.S(256);
  auto fallback = c.U();
  auto wait = c.U();
  auto vars = c.U();
  if (fallback > 2 || wait > 1 || vars != 0)
    return {};
  auto reveal = c.I();
  auto steps = c.Count(8), sub = c.Count(8), names = c.Count(4),
       subnames = c.Count(4);
  if (!c.ok)
    return {};
  d.unconditional = reveal == -2 && !steps && !sub && !names && !subnames;
  return d;
}
struct Transform {
  float x{}, y{}, sx{1}, sy{1};
};
struct Scene {
  std::shared_ptr<const ce_unity::SerializedFile> file;
  std::map<std::int64_t, V> values;
  std::map<std::int64_t, std::int64_t> transforms;
  const V *Get(std::int64_t id) {
    auto it = values.find(id);
    if (it != values.end())
      return &it->second;
    auto o = file->Find(id);
    auto v = o ? file->ReadObject(*o) : std::nullopt;
    if (!v)
      return nullptr;
    return &values.emplace(id, std::move(*v)).first->second;
  }
  std::optional<Transform> World(std::int64_t id,
                                 std::set<std::int64_t> seen = {}) {
    if (seen.size() > 64 || !seen.insert(id).second)
      return {};
    auto t = Get(id);
    if (!t)
      return {};
    Transform a;
    float z{}, qx{}, qy{}, qz{}, qw{};
    if (!Num(t->Path("m_LocalPosition.x"), a.x) ||
        !Num(t->Path("m_LocalPosition.y"), a.y) ||
        !Num(t->Path("m_LocalScale.x"), a.sx) ||
        !Num(t->Path("m_LocalScale.y"), a.sy) ||
        !Num(t->Path("m_LocalScale.z"), z) ||
        !Num(t->Path("m_LocalRotation.x"), qx) ||
        !Num(t->Path("m_LocalRotation.y"), qy) ||
        !Num(t->Path("m_LocalRotation.z"), qz) ||
        !Num(t->Path("m_LocalRotation.w"), qw) || qx != 0 || qy != 0 ||
        qz != 0 || std::abs(qw) != 1 || a.sx <= 0 || a.sy <= 0)
      return {};
    auto parent = Local(t->Get("m_Father"));
    if (parent) {
      auto b = World(parent, std::move(seen));
      if (!b)
        return {};
      a.x = b->x + a.x * b->sx;
      a.y = b->y + a.y * b->sy;
      a.sx *= b->sx;
      a.sy *= b->sy;
    }
    return a;
  }
};
bool ExternalIs(const std::vector<std::string> &cabs, const V *v,
                std::string_view cab, std::int64_t &id) {
  std::int64_t f{};
  return Ref(v, id, f) && id && f > 0 && std::uint64_t(f) <= cabs.size() &&
         cabs[f - 1] == cab;
}
bool Flag(const V *v, bool &out) {
  if (!v) return false;
  if (v->kind == V::Kind::Bool) { out=v->i!=0; return true; }
  std::int64_t n{};if(!Int(v,n)||(n!=0&&n!=1))return false;
  out=n!=0;return true;
}
const std::vector<V>* Calls(const V *v, std::string_view event) {
  auto e=v?v->Get(event):nullptr;
  return e?Array(e->Path("m_PersistentCalls.m_Calls"),4):nullptr;
}
std::int64_t EventTarget(const V *v,std::string_view event,std::string_view method) {
  auto calls=Calls(v,event);if(!calls||calls->size()!=1)return 0;
  const auto& c=calls->front();auto name=Text(c.Get("m_MethodName"));std::int64_t mode{},state{};
  return name&&*name==method&&Int(c.Get("m_Mode"),mode)&&mode==0&&
    Int(c.Get("m_CallState"),state)&&state==1?Local(c.Get("m_Target")):0;
}
// Follow the serialized BooleanUIView event graph, rather than using prefab
// active flags or assuming the last-used variant shares the normal sprite.
std::optional<std::pair<std::int64_t,std::int64_t>> VariantRoots(const V *v,
    std::string_view true_event="onSetTrue",std::string_view false_event="onSetFalse",int call_state=1) {
  auto yes=Calls(v,true_event),no=Calls(v,false_event);
  if(!yes||!no||yes->size()!=2||no->size()!=2)return {};
  std::map<std::int64_t,bool> a,b;
  for(unsigned side=0;side<2;++side)for(const auto& c:side?*no:*yes){
    auto method=Text(c.Get("m_MethodName"));std::int64_t mode{},state{};bool value{};
    const auto id=Local(c.Get("m_Target"));
    if(!id||!method||*method!="SetActive"||!Int(c.Get("m_Mode"),mode)||mode!=6||
      !Int(c.Get("m_CallState"),state)||state!=call_state||!Flag(c.Path("m_Arguments.m_BoolArgument"),value)||
      !(side?b:a).emplace(id,value).second)return {};
  }
  std::int64_t normal{},last{};
  for(const auto& [id,value]:a){auto it=b.find(id);if(it==b.end()||it->second==value)return {}; (value?last:normal)=id;}
  return normal&&last?std::optional{std::pair{normal,last}}:std::nullopt;
}
std::optional<SavePoint> ParseSavePoint(Scene& scene,const V& v,
    const belmont_map_sources::SourceRecipe& source,const std::vector<std::string>& cabs,
    const std::shared_ptr<const ce_unity::SerializedFile>& menu,std::int64_t item_id) {
  SavePoint result;auto& base=result.normal_art;
  auto room=Text(v.Get("itemWorldRoomId"));const auto go=Local(v.Get("m_GameObject"));
  auto g=scene.Get(go);auto name=g?Text(g->Get("m_Name")):std::nullopt;
  std::int64_t script{},file{},zone{},secret{};
  if(!room||!room->starts_with(source.world_name)||!name||*name!="LevelMap_SavePoint_Item(Clone)"||
    Local(v.Get("context"))!=source.map_context_id||
    !Ref(v.Get("m_Script"),script,file)||script!=3538717887839853159LL||file!=1||
    !Int(v.Get("itemExplorationZoneId"),zone)||zone<0||zone>65535||
    !Int(v.Get("itemLinkedSecretZoneId"),secret)||secret!=-1||
    !Flag(v.Get("visibleInUnrevealedMap"),result.visible_in_unrevealed_map))return {};
  base.source_id=item_id;base.source_name=*name;base.room_id=*room;base.exploration_zone_id=std::int32_t(zone);
  base.navigation_zone_id=Local(v.Get("navigationZone"));base.unconditional_policy=true;
  auto tr=scene.transforms.find(go);auto pos=tr!=scene.transforms.end()?scene.World(tr->second):std::nullopt;
  if(!pos||!base.navigation_zone_id)return {};
  base.x=pos->x;base.y=pos->y;
  auto display=scene.Get(Local(v.Get("display")));
  const auto used=EventTarget(display,"onSetUpUsed","SetActive");
  auto variants=VariantRoots(scene.Get(EventTarget(display,"onSetUpIsLastUsed","SetBool")));
  if(!used||!variants)return {};
  auto components=g?Array(g->Get("m_Component"),32):nullptr;bool responsive=false;
  std::optional<Transform> sizable;
  if(components)for(const auto& c:*components){auto m=scene.Get(Local(c.Get("component")));
    if(m&&Local(m->Get("levelMapItem"))==item_id&&m->Get("screenPercentage")){
      if(responsive||!Num(m->Get("screenPercentage"),result.screen_percentage)||
        !Num(m->Get("screenPercentageAtZoomMax"),result.screen_percentage_at_zoom_max)||
        result.screen_percentage<=0||result.screen_percentage>.25f||
        result.screen_percentage_at_zoom_max<=0||result.screen_percentage_at_zoom_max>.25f)return {};
      responsive=true;
      sizable=scene.World(Local(m->Get("sizableObject")));
    }}
  if(!responsive||!sizable||sizable->x!=base.x||sizable->y!=base.y)return {};
  result.last_used_art=base;
  for(const auto& object:scene.file->Objects())if(object.class_id==212){
    auto sr=scene.Get(object.path_id);auto sg=sr?Local(sr->Get("m_GameObject")):0;
    auto st=scene.transforms.find(sg);if(st==scene.transforms.end())continue;
    bool under=false,in_used=false,normal=false,last=false;std::set<std::int64_t> seen;auto at=st->second;
    while(at&&seen.size()<64&&seen.insert(at).second){auto t=scene.Get(at);if(!t)break;auto ag=Local(t->Get("m_GameObject"));
      under|=ag==go;in_used|=ag==used;normal|=ag==variants->first;last|=ag==variants->second;at=Local(t->Get("m_Father"));}
    if(!under||!in_used||normal==last)continue;
    std::int64_t sid{};if(!ExternalIs(cabs,sr->Get("m_Sprite"),MenuCab,sid))return {};
    auto sprite=Obj(menu,sid,213);auto p=scene.World(st->second);Sprite art;
    if(!sprite||!p||!Metadata(*sprite,art))return {};
    art.id=sid;art.bundle=Menu;art.cab=MenuCab;art.x=p->x;art.y=p->y;art.scale_x=p->sx;art.scale_y=p->sy;
    const char* channels[]={"r","g","b","a"};for(unsigned n=0;n<4;++n)
      if(!Num(sr->Path(std::string("m_Color.")+channels[n]),art.color[n]))return {};
    (last?result.last_used_art:result.normal_art).sprites.push_back(std::move(art));
  }
  if(result.normal_art.sprites.size()!=1||result.last_used_art.sprites.size()!=1)return {};
  const auto& normal=result.normal_art.sprites[0];
  const auto& alternate=result.last_used_art.sprites[0];
  const float w=normal.width/normal.pixels_per_unit*normal.scale_x/sizable->sx;
  const float h=normal.height/normal.pixels_per_unit*normal.scale_y/sizable->sy;
  if(w<=0||w>4||std::abs(w-h)>1e-6f||normal.x!=alternate.x||normal.y!=alternate.y||
     normal.width!=alternate.width||normal.height!=alternate.height||
     normal.pixels_per_unit!=alternate.pixels_per_unit||normal.scale_x!=alternate.scale_x||
     normal.scale_y!=alternate.scale_y||normal.pivot_x!=alternate.pivot_x||normal.pivot_y!=alternate.pivot_y)return {};
  const float cx=(normal.x-sizable->x)/sizable->sx+(.5f-normal.pivot_x)*w;
  const float cy=(normal.y-sizable->y)/sizable->sy+(.5f-normal.pivot_y)*h;
  result.icon_size_factor=w;
  result.icon_anchor_x=.5f-cx/w;result.icon_anchor_y=.5f+cy/h;
  if(!std::isfinite(result.icon_anchor_x)||!std::isfinite(result.icon_anchor_y)||
    std::abs(result.icon_anchor_x)>4||std::abs(result.icon_anchor_y)>4)return {};
  return result;
}
std::optional<Elevator> ParseElevator(Scene& scene,const V& v,
    const belmont_map_sources::SourceRecipe& source,const std::vector<std::string>& cabs,
    const std::shared_ptr<const ce_unity::SerializedFile>& menu,std::int64_t item_id) {
  Elevator result;auto& art=result.art;
  auto room=Text(v.Get("itemWorldRoomId"));const auto go=Local(v.Get("m_GameObject"));
  auto g=scene.Get(go);auto name=g?Text(g->Get("m_Name")):std::nullopt;
  std::int64_t script{},file{},zone{},secret{};bool before_reveal{};
  if(!room||!room->starts_with(source.world_name)||!name||*name!="LevelMap_Elevator_Item(Clone)"||
    Local(v.Get("context"))!=source.map_context_id||
    !Ref(v.Get("m_Script"),script,file)||script!=-6054241008690704164LL||file!=1||
    !Int(v.Get("itemExplorationZoneId"),zone)||zone<0||zone>65535||
    !Int(v.Get("itemLinkedSecretZoneId"),secret)||secret < -1||secret>65535||
    !Flag(v.Get("visibleInUnrevealedMap"),before_reveal)||before_reveal)return {};
  art.source_id=item_id;art.source_name=*name;art.room_id=*room;
  art.exploration_zone_id=std::int32_t(zone);art.navigation_zone_id=Local(v.Get("navigationZone"));
  art.unconditional_policy=true;result.secret_zone_id=int(secret);
  auto tr=scene.transforms.find(go);auto pos=tr!=scene.transforms.end()?scene.World(tr->second):std::nullopt;
  if(!pos||!art.navigation_zone_id)return {};
  art.x=pos->x;art.y=pos->y;
  auto display=scene.Get(Local(v.Get("display")));
  auto roots=VariantRoots(display,"onHover","onUnHover",2);
  const auto visibility=display?Local(display->Get("visibilityParent")):0;
  if(!roots||!visibility)return {};
  std::optional<Transform> sizable;bool responsive=false;
  auto components=Array(g->Get("m_Component"),32);
  if(components)for(const auto& c:*components){auto m=scene.Get(Local(c.Get("component")));
    if(m&&Local(m->Get("levelMapItem"))==item_id&&m->Get("screenPercentage")){
      if(responsive||!Num(m->Get("screenPercentage"),result.screen_percentage)||
        !Num(m->Get("screenPercentageAtZoomMax"),result.screen_percentage_at_zoom_max)||
        result.screen_percentage<=0||result.screen_percentage>.25f||
        result.screen_percentage_at_zoom_max<=0||result.screen_percentage_at_zoom_max>.25f)return {};
      responsive=true;sizable=scene.World(Local(m->Get("sizableObject")));
    }}
  if(!responsive||!sizable||sizable->x!=art.x||sizable->y!=art.y)return {};
  bool normal_active{},hover_active{};
  const auto normal_go=scene.Get(roots->first),hover_go=scene.Get(roots->second);
  if(!normal_go||!hover_go||!Flag(normal_go->Get("m_IsActive"),normal_active)||
     !Flag(hover_go->Get("m_IsActive"),hover_active)||!normal_active||hover_active)return {};
  for(const auto& object:scene.file->Objects())if(object.class_id==212){
    auto sr=scene.Get(object.path_id);const auto sg=sr?Local(sr->Get("m_GameObject")):0;
    auto st=scene.transforms.find(sg);if(st==scene.transforms.end())continue;
    bool under=false,in_visible=false,normal=false,hover=false;std::set<std::int64_t> seen;auto at=st->second;
    while(at&&seen.size()<64&&seen.insert(at).second){auto t=scene.Get(at);if(!t)break;
      auto ag=Local(t->Get("m_GameObject"));under|=ag==go;in_visible|=ag==visibility;
      normal|=ag==roots->first;hover|=ag==roots->second;at=Local(t->Get("m_Father"));}
    if(!under||!in_visible||!normal||hover)continue;
    bool enabled{},flip_x{},flip_y{};std::int64_t draw{};
    if(!Flag(sr->Get("m_Enabled"),enabled)||!enabled||!Flag(sr->Get("m_FlipX"),flip_x)||flip_x||
      !Flag(sr->Get("m_FlipY"),flip_y)||flip_y||!Int(sr->Get("m_DrawMode"),draw)||draw!=0)return {};
    std::int64_t sid{};if(!ExternalIs(cabs,sr->Get("m_Sprite"),MenuCab,sid))return {};
    auto sprite=Obj(menu,sid,213);auto p=scene.World(st->second);Sprite glyph;
    if(!sprite||!p||!Metadata(*sprite,glyph))return {};
    glyph.id=sid;glyph.bundle=Menu;glyph.cab=MenuCab;glyph.x=p->x;glyph.y=p->y;
    glyph.scale_x=p->sx;glyph.scale_y=p->sy;
    const char* channels[]={"r","g","b","a"};for(unsigned n=0;n<4;++n)
      if(!Num(sr->Path(std::string("m_Color.")+channels[n]),glyph.color[n])||glyph.color[n]!=1)return {};
    art.sprites.push_back(std::move(glyph));
  }
  if(art.sprites.size()!=1)return {};
  const auto& glyph=art.sprites[0];
  const float w=glyph.width/glyph.pixels_per_unit*glyph.scale_x/sizable->sx;
  const float h=glyph.height/glyph.pixels_per_unit*glyph.scale_y/sizable->sy;
  if(w<=0||w>4||std::abs(w-h)>1e-6f)return {};
  const float cx=(glyph.x-sizable->x)/sizable->sx+(.5f-glyph.pivot_x)*w;
  const float cy=(glyph.y-sizable->y)/sizable->sy+(.5f-glyph.pivot_y)*h;
  result.icon_size_factor=w;result.icon_anchor_x=.5f-cx/w;result.icon_anchor_y=.5f+cy/h;
  if(!std::isfinite(result.icon_anchor_x)||!std::isfinite(result.icon_anchor_y)||
    std::abs(result.icon_anchor_x)>4||std::abs(result.icon_anchor_y)>4)return {};
  return result;
}
std::optional<FastTravelPoint> ParseFastTravel(
    Scene &scene, const V &v, const belmont_map_sources::SourceRecipe &source,
    const std::vector<std::string> &cabs,
    const std::shared_ptr<const ce_unity::SerializedFile> &common,
    const std::shared_ptr<const ce_unity::SerializedFile> &level_ids,
    std::int64_t item_id) {
  // Unlocked native discovery/entered paths bypass secret-door discovery too;
  // this does not permit any locked secret-linked point to be shown.
  FastTravelPoint result;
  auto &art = result.unlocked_art;
  auto room = Text(v.Get("itemWorldRoomId"));
  const auto go = Local(v.Get("m_GameObject"));
  auto g = scene.Get(go);
  auto name = g ? Text(g->Get("m_Name")) : std::nullopt;
  std::int64_t script{}, file{}, zone{}, secret{}, condition{}, level_id{},
      level_script{}, level_file{}, door{};
  bool fast{};
  if (!common || !level_ids || !room || !room->starts_with(source.world_name) ||
      !name || *name != "LevelMap_ZDoor_FastTravel_Item(Clone)" ||
      Local(v.Get("context")) != source.map_context_id ||
      !Ref(v.Get("m_Script"), script, file) ||
      script != -6779324738952612746LL || file != 1 ||
      !Flag(v.Get("isFastTravelDoor"), fast) || !fast ||
      !Int(v.Get("zDoorID"), door) || door < 0 || door > 65535 ||
      !Int(v.Get("itemExplorationZoneId"), zone) || zone < 0 || zone > 65535 ||
      !Int(v.Get("itemLinkedSecretZoneId"), secret) || secret < -1 ||
      secret > 65535 ||
      !Int(v.Path("additionalDiscoveredCondition.rid"), condition) ||
      condition != -2 ||
      !Flag(v.Get("visibleInUnrevealedMap"), result.visible_in_unrevealed_map))
    return {};
  auto context = scene.Get(source.map_context_id);
  if (!context ||
      !ExternalIs(cabs, context->Get("level"), LevelIdsCab, level_id))
    return {};
  auto level = Obj(level_ids, level_id, 114);
  auto guid = level ? Text(level->Get("_guid")) : std::nullopt;
  if (!level || !guid || guid->size() != 32 ||
      !std::all_of(guid->begin(), guid->end(),
                   [](char c) {
                     return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                   }) ||
      !Ref(level->Get("m_Script"), level_script, level_file) ||
      level_script != 1341962149057043309LL || level_file != 1)
    return {};
  result.level_guid = *guid;
  art.source_id = item_id;
  art.source_name = *name;
  art.room_id = *room;
  art.exploration_zone_id = int(zone);
  art.navigation_zone_id = Local(v.Get("navigationZone"));
  art.unconditional_policy = true;
  auto tr = scene.transforms.find(go);
  auto position =
      tr != scene.transforms.end() ? scene.World(tr->second) : std::nullopt;
  if (!position || !art.navigation_zone_id)
    return {};
  art.x = position->x;
  art.y = position->y;
  auto display = scene.Get(Local(v.Get("display")));
  auto view = display ? scene.Get(Local(display->Get("fastTravelUnlockedView")))
                      : nullptr;
  auto roots = VariantRoots(view, "onSetTrue", "onSetFalse", 2);
  const auto visible = display ? Local(display->Get("visibilityParent")) : 0;
  const auto fast_parent =
      display ? Local(display->Get("fastTravelDoorParent")) : 0;
  const auto group = view ? Local(view->Get("m_GameObject")) : 0;
  if (!roots || !visible || !fast_parent || !group)
    return {};
  // Unlocked native fast travel implies entered. This shared group appears in
  // both native entered states, so no completion/progression guess is needed.
  for (const auto *field :
       {"enteredUncompletedObjects", "enteredCompletedObjects"}) {
    const auto parents = Array(display->Get(field), 16);
    if (!parents ||
        std::count_if(parents->begin(), parents->end(),
                      [&](const V &p) { return Local(&p) == group; }) != 1)
      return {};
  }
  auto responsive = scene.Get(Local(v.Get("responsivenessComponent")));
  std::optional<Transform> sizable;
  float multiplier{};
  bool max_diff{};
  if (!responsive || Local(responsive->Get("levelMapItem")) != item_id ||
      !Num(responsive->Get("screenPercentage"), result.screen_percentage) ||
      !Flag(responsive->Get("differentScreenPercentageAtZoomMax"), max_diff) ||
      !Num(responsive->Get("screenPercentageAtZoomMax"),
           result.screen_percentage_at_zoom_max) ||
      !Num(v.Get("isZDoorScaleMultiplier"), multiplier) || multiplier <= 0 ||
      multiplier > 4 || result.screen_percentage <= 0 ||
      result.screen_percentage > .25f ||
      result.screen_percentage_at_zoom_max <= 0 ||
      result.screen_percentage_at_zoom_max > .25f)
    return {};
  if (!max_diff)
    result.screen_percentage_at_zoom_max = result.screen_percentage;
  sizable = scene.World(Local(responsive->Get("sizableObject")));
  if (!sizable || sizable->x != art.x || sizable->y != art.y)
    return {};
  for (const auto &object : scene.file->Objects())
    if (object.class_id == 212) {
      auto sr = scene.Get(object.path_id);
      const auto sg = sr ? Local(sr->Get("m_GameObject")) : 0;
      auto st = scene.transforms.find(sg);
      if (st == scene.transforms.end())
        continue;
      bool under = false, in_visible = false, in_fast = false, in_group = false,
           unlocked = false, locked = false;
      std::set<std::int64_t> seen;
      auto at = st->second;
      while (at && seen.size() < 64 && seen.insert(at).second) {
        auto t = scene.Get(at);
        if (!t)
          break;
        auto ag = Local(t->Get("m_GameObject"));
        under |= ag == go;
        in_visible |= ag == visible;
        in_fast |= ag == fast_parent;
        in_group |= ag == group;
        unlocked |= ag == roots->second;
        locked |= ag == roots->first;
        at = Local(t->Get("m_Father"));
      }
      if (!under || !in_visible || !in_fast || !in_group || !unlocked || locked)
        continue;
      bool enabled{}, flip_x{}, flip_y{};
      std::int64_t draw{}, sid{};
      if (!Flag(sr->Get("m_Enabled"), enabled) || !enabled ||
          !Flag(sr->Get("m_FlipX"), flip_x) || flip_x ||
          !Flag(sr->Get("m_FlipY"), flip_y) || flip_y ||
          !Int(sr->Get("m_DrawMode"), draw) || draw != 0 ||
          !ExternalIs(cabs, sr->Get("m_Sprite"), CommonCab, sid))
        return {};
      auto sprite = Obj(common, sid, 213);
      auto p = scene.World(st->second);
      Sprite glyph;
      if (!sprite || !p || !Metadata(*sprite, glyph))
        return {};
      glyph.id = sid;
      glyph.bundle = Common;
      glyph.cab = CommonCab;
      glyph.x = p->x;
      glyph.y = p->y;
      glyph.scale_x = p->sx;
      glyph.scale_y = p->sy;
      const char *channels[] = {"r", "g", "b", "a"};
      for (unsigned n = 0; n < 4; ++n)
        if (!Num(sr->Path(std::string("m_Color.") + channels[n]),
                 glyph.color[n]) ||
            glyph.color[n] != 1)
          return {};
      art.sprites.push_back(std::move(glyph));
    }
  if (art.sprites.size() != 1)
    return {};
  const auto &glyph = art.sprites[0];
  const float w =
      glyph.width / glyph.pixels_per_unit * glyph.scale_x / sizable->sx;
  const float h =
      glyph.height / glyph.pixels_per_unit * glyph.scale_y / sizable->sy;
  if (w <= 0 || w > 4 || std::abs(w - h) > 1e-6f)
    return {};
  const float cx =
      (glyph.x - sizable->x) / sizable->sx + (.5f - glyph.pivot_x) * w;
  const float cy =
      (glyph.y - sizable->y) / sizable->sy + (.5f - glyph.pivot_y) * h;
  result.icon_size_factor = w * multiplier;
  result.icon_anchor_x = .5f - cx / w;
  result.icon_anchor_y = .5f + cy / h;
  if (!std::isfinite(result.icon_anchor_x) ||
      !std::isfinite(result.icon_anchor_y) ||
      std::abs(result.icon_anchor_x) > 4 || std::abs(result.icon_anchor_y) > 4)
    return {};
  return result;
}
constexpr std::string_view Managers =
    "afm_scenes_scenes_managers_650601800ddfc88b482f7f4a9ca851cf.bundle";
constexpr std::string_view ManagersCab = "CAB-0d1f3b1580eb36793eb3df18a126b3cb";
constexpr std::string_view ManagersShared =
    "CAB-0d1f3b1580eb36793eb3df18a126b3cb.sharedAssets";
constexpr std::string_view MapCommon =
    "map-common_assets_all_8379384e501f34587b2eb9eb294b0b1b.bundle";
constexpr std::string_view MapCommonCab =
    "CAB-eabd3d803cf517a027fa917c9b3473ca";
std::optional<std::vector<std::string>>
Cabs(ce_unity::Reader &r, std::string_view member, std::string_view cab) {
  auto b = r.OpenBundle(member);
  auto n = b ? b->FindNode(cab) : nullptr;
  return n ? belmont_map_refs::ExternalCabs(b->NodeSource(*n)) : std::nullopt;
}
bool Script(const V &v, std::int64_t wanted) {
  std::int64_t id{}, file{};
  return Ref(v.Get("m_Script"), id, file) && file == 1 && id == wanted;
}
bool PinContext(
    Scene &scene, const belmont_map_sources::SourceRecipe &source,
    const std::vector<std::string> &cabs,
    const std::shared_ptr<const ce_unity::SerializedFile> &level_ids,
    Data &out) {
  auto context = scene.Get(source.map_context_id);
  std::int64_t level_id{};
  if (!context || !Script(*context, 6647830585995460756LL) ||
      !ExternalIs(cabs, context->Get("level"), LevelIdsCab, level_id))
    return false;
  auto level = Obj(level_ids, level_id, 114);
  auto guid = level ? Text(level->Get("_guid")) : std::nullopt;
  if (!level || !Script(*level, 1341962149057043309LL) || !guid || !Guid(*guid))
    return false;
  auto settings = scene.Get(Local(context->Get("settings")));
  auto manager = settings
                     ? scene.Get(Local(settings->Get("navigationZonesManager")))
                     : nullptr;
  auto zones = manager ? Array(manager->Get("navigationZones"), 64) : nullptr;
  if (!manager || !Script(*manager, -1189974016863535272LL) || !zones ||
      zones->empty())
    return false;
  std::set<std::int64_t> seen;
  std::vector<std::int64_t> ordered;
  for (const auto &ref : *zones) {
    auto id = Local(&ref);
    auto zone = scene.Get(id);
    if (!id || !seen.insert(id).second || !zone ||
        !Script(*zone, -2269660471775966453LL))
      return false;
    ordered.push_back(id);
  }
  if (!seen.contains(Local(manager->Get("mainNavigationZone"))))
    return false;
  out.level_guid = *guid;
  out.navigation_zone_ids = std::move(ordered);
  return true;
}
std::optional<std::vector<PinType>>
PinTypes(ce_unity::Reader &reader, const ce_unity::RangeReader &romfs) {
  auto scene = File(reader, Managers, ManagersCab),
       shared = File(reader, Managers, ManagersShared);
  auto ids = File(reader, Savable, SavableCab),
       common = File(reader, MapCommon, MapCommonCab),
       menu = File(reader, Menu, MenuCab);
  auto cabs = Cabs(reader, Managers, ManagersCab),
       shared_cabs = Cabs(reader, Managers, ManagersShared),
       common_cabs = Cabs(reader, MapCommon, MapCommonCab);
  if (!scene || !shared || !ids || !common || !menu || !cabs || !shared_cabs ||
      !common_cabs)
    return {};
  auto ui = Obj(scene, 344, 114);
  std::int64_t dictionary_id{};
  if (!ui || !Script(*ui, -1298472910816597764LL) ||
      !ExternalIs(*cabs, ui->Get("mapMarkerTypeDisplayDataDictionary"),
                  ManagersShared, dictionary_id))
    return {};
  auto dictionary = Obj(shared, dictionary_id, 114);
  auto elements =
      dictionary ? Array(dictionary->Get("elements"), belmont_pins::KindCount)
                 : nullptr;
  if (!dictionary || !Script(*dictionary, -6398427658833412776LL) ||
      !elements || elements->size() != belmont_pins::KindCount)
    return {};
  struct Pending {
    PinType type;
    std::string address_guid;
  };
  std::vector<Pending> pending;
  std::set<std::string> guids, type_guids;
  std::set<std::int64_t> type_ids;
  for (const auto &entry : *elements) {
    std::int64_t id{};
    if (!ExternalIs(*shared_cabs, entry.Get("id"), SavableCab, id) ||
        !type_ids.insert(id).second)
      return {};
    auto type = Obj(ids, id, 114);
    auto guid = type ? Text(type->Get("_guid"), 32) : std::nullopt;
    auto address = Text(entry.Path("item.m_AssetGUID"), 32);
    if (!type || !Script(*type, 1587585272943161382LL) || !guid ||
        !Guid(*guid) || !type_guids.insert(*guid).second || !address ||
        !Guid(*address) || !guids.insert(*address).second ||
        Text(entry.Path("item.m_SubObjectName")).value_or("bad") != "" ||
        Text(entry.Path("item.m_SubObjectType")).value_or("bad") != "")
      return {};
    pending.push_back({PinType{*guid, 0, 0, id, 0}, *address});
  }
  constexpr std::string_view path = "Data/StreamingAssets/aa/catalog.json";
  auto size = romfs ? romfs.size(path) : std::nullopt;
  if (!size || !*size || *size > 40 * 1024 * 1024)
    return {};
  std::string catalog(std::size_t(*size), '\0');
  for (std::size_t at = 0; at < catalog.size();) {
    auto count = std::min<std::size_t>(1024 * 1024, catalog.size() - at);
    if (!romfs.read(path, at,
                    reinterpret_cast<std::uint8_t *>(catalog.data() + at),
                    count))
      return {};
    at += count;
  }
  auto addresses = Addresses(catalog, guids);
  if (!addresses)
    return {};
  std::map<std::string, std::int64_t, std::less<>> objects;
  unsigned containers = 0;
  for (const auto &o : common->Objects())
    if (o.class_id == 142) {
      if (++containers != 1)
        return {};
      auto container = Obj(common, o.path_id, 142);
      auto rows =
          container ? Array(container->Get("m_Container"), 256) : nullptr;
      if (!rows)
        return {};
      for (const auto &row : *rows) {
        if (row.kind != V::Kind::Array || row.items.size() != 2)
          return {};
        auto key = Text(&row.items[0], 4096);
        auto id = Local(row.items[1].Get("asset"));
        if (!key || !id || !objects.emplace(*key, id).second)
          return {};
      }
    }
  if (containers != 1)
    return {};
  std::vector<PinType> result;
  std::set<std::size_t> kinds;
  for (auto &p : pending) {
    auto key = addresses->at(p.address_guid);
    auto it = objects.find(key);
    if (it == objects.end())
      return {};
    auto display = Obj(common, it->second, 114);
    std::int64_t sprite{};
    if (!display || !Script(*display, 8890737712029371569LL) ||
        !ExternalIs(*common_cabs, display->Get("markerIcon"), MenuCab, sprite))
      return {};
    auto kind = std::find(belmont_pins::Sprites.begin(),
                          belmont_pins::Sprites.end(), sprite);
    if (kind == belmont_pins::Sprites.end() || !Obj(menu, sprite, 213))
      return {};
    p.type.kind = std::size_t(kind - belmont_pins::Sprites.begin());
    p.type.sprite_id = sprite;
    p.type.display_id = it->second;
    if (!kinds.insert(p.type.kind).second)
      return {};
    result.push_back(std::move(p.type));
  }
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.kind < b.kind; });
  return result;
}
} // namespace
bool Visible(const FastTravelPoint& point,int revealed,int unlocked,std::int64_t navigation){
  return unlocked==1&&(revealed==1||point.visible_in_unrevealed_map)&&navigation>0&&point.unlocked_art.navigation_zone_id==navigation;
}
bool Visible(const SavePoint& point,int revealed,int used,std::int64_t navigation){
  return used==1&&(revealed==1||point.visible_in_unrevealed_map)&&
    navigation>0&&point.normal_art.navigation_zone_id==navigation;
}
bool Visible(const Elevator& point,int revealed,int entered,std::int64_t navigation){
  return revealed==1&&entered==1&&point.secret_zone_id==-1&&navigation>0&&point.art.navigation_zone_id==navigation;
}
bool Visible(const Landmark &l, std::span<const std::string> known,
             std::span<const std::int64_t> culled) {
  return l.unconditional_policy && !l.guid.empty() &&
         std::find(known.begin(), known.end(), l.guid) != known.end() &&
         std::find(culled.begin(), culled.end(), l.navigation_zone_id) ==
             culled.end();
}
Reader::Reader(ce_unity::RangeReader r) : romfs(r), unity(std::move(r)) {}
Reader::Reader(const EdenDsmodHostApi *h)
    : Reader(ce_unity::MakeHostRangeReader(h)) {}
void Reader::ClearCache() {
  std::lock_guard lock(mutex);
  cache.reset();
  save_cache.reset();
  pin_type_cache.reset();
  unity.ClearCache();
}
std::optional<Data> Reader::LoadForRoom(std::string_view room) {
  return Load(room,false);
}
std::optional<Data> Reader::LoadSavePointsForRoom(std::string_view room) {
  return LoadMapItemsForRoom(room);
}
std::optional<Data> Reader::LoadMapItemsForRoom(std::string_view room) {
  return Load(room,true);
}
std::optional<Data> Reader::Load(std::string_view room,bool save_points_only) {
  std::lock_guard lock(mutex);
  try {
    auto source = belmont_map_sources::FindSourceForRoom(room);
    if (!source)
      return {};
    auto& selected_cache=save_points_only?save_cache:cache;
    if (selected_cache && selected_cache->world_name == source->world_name) {
      if (selected_cache->pin_types.empty()) {
        if (!pin_type_cache) pin_type_cache = PinTypes(unity, romfs);
        if (pin_type_cache) selected_cache->pin_types = *pin_type_cache;
      }
      return selected_cache;
    }
    auto sf = File(unity, source->map_bundle, source->map_cab);
    auto data = save_points_only ? nullptr : File(unity, Savable, SavableCab);
    auto menu = File(unity, Menu, MenuCab);
    auto common = File(unity, Common, CommonCab);
    auto level_ids=File(unity,LevelIds,LevelIdsCab);
    auto b = unity.OpenBundle(source->map_bundle);
    auto node = b ? b->FindNode(source->map_cab) : nullptr;
    auto cabs = node ? belmont_map_refs::ExternalCabs(b->NodeSource(*node))
                     : std::nullopt;
    if (!sf || !menu || !cabs || (!save_points_only&&(!data||!common)))
      return {};
    Scene scene{sf, {}, {}};
    Data out;
    out.world_name = source->world_name;
    PinContext(scene, *source, *cabs, level_ids, out);
    if (!pin_type_cache) pin_type_cache = PinTypes(unity, romfs);
    if (pin_type_cache) out.pin_types = *pin_type_cache;
    // ResetContext writes true to revealedMap; InitializeContext overwrites
    // it only when LevelUIDisplayData.mapRevealCondition is non-null. Do not
    // approximate conditional Hall of Mirrors revelation from explored fog.
    if(auto levels=File(unity,LevelData,LevelDataCab)){
      for(const auto& object:levels->Objects())if(object.class_id==114){
        auto value=Obj(levels,object.path_id,114);
        auto name=value?Text(value->Get("m_Name")):std::nullopt;
        if(name&&*name==out.world_name+"_UIDisplay"){
          std::int64_t rid{};
          if(Int(value->Path("mapRevealCondition.rid"),rid)&&rid==-2)out.map_revealed_policy=1;
          break;
        }
      }
    }
    for (const auto &o : sf->Objects())
      if (o.class_id == 4 || o.class_id == 224) {
        auto t = scene.Get(o.path_id);
        auto go = t ? Local(t->Get("m_GameObject")) : 0;
        if (go && !scene.transforms.emplace(go, o.path_id).second)
          return {};
      }
    for (const auto &o : sf->Objects()) {
      if (o.class_id != 114)
        continue;
      auto v = scene.Get(o.path_id);
      if(v){
        std::int64_t sid{},file{};
        if(Ref(v->Get("m_Script"),sid,file)&&sid==3538717887839853159LL&&file==1)
          if(auto save=ParseSavePoint(scene,*v,*source,*cabs,menu,o.path_id))out.save_points.push_back(std::move(*save));
        if(sid==-6054241008690704164LL&&file==1)
          if(auto lift=ParseElevator(scene,*v,*source,*cabs,menu,o.path_id))out.elevators.push_back(std::move(*lift));
        if(sid==-6779324738952612746LL&&file==1)
          if(auto point=ParseFastTravel(scene,*v,*source,*cabs,common,level_ids,o.path_id))out.fast_travel_points.push_back(std::move(*point));
      }
      if(save_points_only)continue;
      if (!v || !v->Get("landmarkData"))
        continue;
      Landmark l;
      l.source_id = o.path_id;
      auto roomname = Text(v->Get("itemWorldRoomId"));
      std::int64_t dataid{}, zone{};
      auto go = Local(v->Get("m_GameObject"));
      auto g = scene.Get(go);
      auto name = g ? Text(g->Get("m_Name")) : std::nullopt;
      if (!roomname || !roomname->starts_with(source->world_name) || !name ||
          !name->starts_with("LevelMap_Landmark_") ||
          Local(v->Get("context")) != source->map_context_id ||
          !ExternalIs(*cabs, v->Get("landmarkData"), SavableCab, dataid) ||
          !Int(v->Get("itemExplorationZoneId"), zone) || zone < 0 ||
          zone > 65535)
        return {};
      auto d = LandmarkData(data, dataid);
      if (!d || d->guid.size() != 32 ||
          !std::all_of(d->guid.begin(), d->guid.end(),
                       [](char c) {
                         return (c >= '0' && c <= '9') ||
                                (c >= 'a' && c <= 'f');
                       }) ||
          !d->table.starts_with("GUID:") || d->table.size() != 37 || d->key < 0)
        return {};
      l.guid = d->guid;
      l.room_id = *roomname;
      l.source_name = *name;
      l.name_table = d->table;
      l.name_key_id = d->key;
      l.exploration_zone_id = zone;
      l.navigation_zone_id = Local(v->Get("navigationZone"));
      l.unconditional_policy = d->unconditional;
      auto ti = scene.transforms.find(go);
      auto pos =
          ti != scene.transforms.end() ? scene.World(ti->second) : std::nullopt;
      if (!pos)
        return {};
      l.x = pos->x;
      l.y = pos->y;
      l.name_x = l.x;
      l.name_y = l.y;
      // Find only native step zero under the linked display hierarchy. Other
      // states need condition readers and are deliberately not guessed from
      // active flags.
      std::set<std::int64_t> zero_roots, excluded_roots;
      auto display = scene.Get(Local(v->Get("display")));
      auto displays = display ? Array(display->Get("displays")) : nullptr;
      if (displays)
        for (const auto &dr : *displays) {
          auto dv = scene.Get(Local(&dr));
          if (!dv)
            continue;
          if (auto steps = Array(dv->Get("stepBasedObjects"))) {
            for (std::size_t i = 0; i < steps->size(); ++i)
              (i ? excluded_roots : zero_roots).insert(Local(&(*steps)[i]));
          }
          if (dv->Get("landmarkNameText")) {
            auto ng = Local(dv->Get("m_GameObject"));
            auto nt = scene.transforms.find(ng);
            auto np = nt != scene.transforms.end() ? scene.World(nt->second)
                                                   : std::nullopt;
            if (np) {
              l.name_x = np->x;
              l.name_y = np->y;
            }
            auto localized = scene.Get(Local(dv->Get("landmarkNameText")));
            auto text = localized
                            ? scene.Get(Local(localized->Get("textDisplay")))
                            : nullptr;
            if (text) {
              Num(text->Get("m_fontSize"), l.name_style.font_size);
              Int(text->Get("m_fontStyle"), l.name_style.font_style);
              std::int64_t ff{};
              if (Ref(text->Get("m_fontAsset"), l.name_style.font_asset_id,
                      ff) &&
                  ff > 0 && std::uint64_t(ff) <= cabs->size())
                l.name_style.font_asset_cab = (*cabs)[ff - 1];
              Int(text->Get("m_HorizontalAlignment"),
                  l.name_style.horizontal_alignment);
              Int(text->Get("m_VerticalAlignment"),
                  l.name_style.vertical_alignment);
              const char *c[] = {"r", "g", "b", "a"};
              const char *m[] = {"x", "y", "z", "w"};
              for (unsigned k = 0; k < 4; ++k) {
                auto color = text->Get("m_fontColor");
                auto margin = text->Get("m_margin");
                if (color)
                  Num(color->Get(c[k]), l.name_style.color[k]);
                if (margin)
                  Num(margin->Get(m[k]), l.name_style.margin[k]);
              }
              auto tg = Local(text->Get("m_GameObject"));
              auto tt = scene.transforms.find(tg);
              auto tp = tt != scene.transforms.end() ? scene.World(tt->second)
                                                     : std::nullopt;
              if (tp) {
                l.name_style.scale_x = tp->sx;
                l.name_style.scale_y = tp->sy;
              }
            }
            // Native UI Image and responsive sizing components under this
            // display wrapper. Layout fitter sizes ribbon from real text.
            for (const auto &candidate : sf->Objects())
              if (candidate.class_id == 114) {
                auto cv = scene.Get(candidate.path_id);
                if (!cv)
                  continue;
                auto cg = Local(cv->Get("m_GameObject"));
                auto ct = scene.transforms.find(cg);
                if (ct == scene.transforms.end())
                  continue;
                auto at = ct->second;
                std::set<std::int64_t> ancestors;
                bool child = false;
                while (at && ancestors.size() < 64 &&
                       ancestors.insert(at).second) {
                  auto tr = scene.Get(at);
                  if (!tr)
                    break;
                  child |= Local(tr->Get("m_GameObject")) == ng;
                  at = Local(tr->Get("m_Father"));
                }
                if (!child)
                  continue;
                if (cv->Get("screenPercentage")) {
                  Num(cv->Get("screenPercentage"),
                      l.name_style.screen_percentage);
                  auto rs = scene.World(Local(cv->Get("sizableObject")));
                  if (rs) {
                    l.name_style.responsive_scale_x = rs->sx;
                    l.name_style.responsive_scale_y = rs->sy;
                  }
                }
                if (cv->Get("m_PixelsPerUnitMultiplier")) {
                  std::int64_t file{};
                  Ref(cv->Get("m_Sprite"), l.name_style.ribbon_sprite_id, file);
                  std::int64_t ribbon_id{};
                  if (ExternalIs(*cabs, cv->Get("m_Sprite"), CommonCab,
                                 ribbon_id)) {
                    auto native = Obj(common, ribbon_id, 213);
                    Sprite ribbon;
                    ribbon.id = ribbon_id;
                    ribbon.bundle = Common;
                    ribbon.cab = CommonCab;
                    if (native && Metadata(*native, ribbon)) {
                      auto rp = scene.World(ct->second);
                      if (rp) {
                        ribbon.x = rp->x;
                        ribbon.y = rp->y;
                        ribbon.scale_x = rp->sx;
                        ribbon.scale_y = rp->sy;
                      }
                      l.name_style.ribbon = std::move(ribbon);
                    }
                  }
                  Num(cv->Get("m_PixelsPerUnitMultiplier"),
                      l.name_style.ribbon_pixels_per_unit_multiplier);
                  const char *channels[] = {"r", "g", "b", "a"};
                  auto color = cv->Get("m_Color");
                  for (unsigned k = 0; k < 4; ++k)
                    if (color)
                      Num(color->Get(channels[k]),
                          l.name_style.ribbon_color[k]);
                }
              }
          }
        }
      for (const auto &so : sf->Objects())
        if (so.class_id == 212) {
          auto sr = scene.Get(so.path_id);
          auto sg = sr ? Local(sr->Get("m_GameObject")) : 0;
          auto st = scene.transforms.find(sg);
          if (st == scene.transforms.end())
            continue;
          std::set<std::int64_t> seen;
          auto at = st->second;
          bool under = false, zero = false, excluded = false;
          while (at && seen.size() < 64 && seen.insert(at).second) {
            auto t = scene.Get(at);
            if (!t)
              break;
            auto ag = Local(t->Get("m_GameObject"));
            under |= ag == go;
            zero |= zero_roots.contains(ag);
            excluded |= excluded_roots.contains(ag);
            at = Local(t->Get("m_Father"));
          }
          if (!under || !zero || excluded)
            continue;
          std::int64_t sid{};
          if (!ExternalIs(*cabs, sr->Get("m_Sprite"), MenuCab, sid))
            continue;
          auto s = Obj(menu, sid, 213);
          auto p = scene.World(st->second);
          Sprite art;
          art.id = sid;
          art.bundle = Menu;
          art.cab = MenuCab;
          if (!s || !p || !Metadata(*s, art))
            return {};
          art.x = p->x;
          art.y = p->y;
          art.scale_x = p->sx;
          art.scale_y = p->sy;
          const char *channels[] = {"r", "g", "b", "a"};
          for (unsigned c = 0; c < 4; ++c) {
            auto col = sr->Get("m_Color");
            if (!col || !Num(col->Get(channels[c]), art.color[c]))
              return {};
          }
          l.sprites.push_back(std::move(art));
        }
      out.landmarks.push_back(std::move(l));
      if (out.landmarks.size() > 128)
        return {};
    }
    selected_cache = out;
    unity.ClearCache();
    return out;
  } catch (...) {
    return {};
  }
}
} // namespace belmont_map_landmarks
