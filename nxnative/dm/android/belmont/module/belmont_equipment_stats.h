// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_equipment.h"
#include <map>
#include <optional>
namespace belmont_equipment_stats {
struct Value {
  bool ready{};
  std::int32_t value{};
  std::string reason{"unavailable"};
};
struct Snapshot {
  bool supported{}, coherent{}, ready{};
  belmont::U64 entity{};
  Value atk, atk_bonus, atk_display, defense, defense_bonus, defense_display,
      max_hp, max_mp, level, current_hp, current_mp;
  Value health_upgrades, health_fragments, health_fragments_needed,
      mana_upgrades, mana_fragments, mana_fragments_needed;
  std::string reason{"unavailable"};
};
// Native six-display row: completed elements show four shards, only the
// next element shows the unmerged fragment count. Missing inputs hide the row.
inline Value UpgradePieces(const Value &completed, const Value &fragments,
                           const Value &needed, unsigned index) {
  if (!completed.ready || !fragments.ready || !needed.ready)
    return {false, 0, "upgrade_inputs_unavailable"};
  if (index >= 6 || completed.value < 0 || completed.value > 6 ||
      needed.value != 4 || fragments.value < 0 || fragments.value >= 4 ||
      (completed.value == 6 && fragments.value != 0))
    return {false, 0, "upgrade_inputs_out_of_range"};
  return {true, int(index) < completed.value ? 4 :
      int(index) == completed.value ? fragments.value : 0, "ready"};
}
inline float RoundEven(float value) {
  const double lo = std::floor(double(value)), fraction = double(value) - lo;
  return float(fraction < .5                     ? lo
               : fraction > .5                   ? lo + 1
               : std::fmod(std::abs(lo), 2) == 0 ? lo
                                                 : lo + 1);
}
// Exact native operation ordering and float32 arithmetic; no expression
// reassociation, guest calls, save reads, or last-good values.
template <class Read> class Decoder {
  using U64 = belmont::U64;
  Read &read;
  struct Span {
    U64 at;
    std::vector<std::uint8_t> bytes;
  };
  std::vector<Span> journal;
  // Reuse only exact spans inside this decode. Final coherence validation
  // still reads every unique span from the guest; nothing survives Sample.
  belmont::ReadSpanIndex span_index;
  std::size_t total_bytes{};
  bool malformed{};
  U64 main_base{};
  std::string failure;
  struct Stat {
    std::string name;
    U64 type{}, object{};
    float base{};
    std::vector<U64> modifiers;
  };
  std::vector<Stat> stats;
  std::vector<U64> evaluating;
  std::map<U64, std::optional<float>> memo;
  bool Bytes(U64 at, void *out, std::size_t n) {
    if (!n || n > 4096 || at < 0x10000 ||
        at > std::numeric_limits<U64>::max() - n || journal.size() >= 32768 ||
        total_bytes > 1024 * 1024 - n) {
      malformed = true;
      return false;
    }
    if (auto found=span_index.find({at,n});found!=span_index.end()) {
      const auto& saved=journal[found->second].bytes;
      std::memcpy(out,saved.data(),n);
      return true;
    }
    if (!read(at,out,n)) { malformed=true; return false; }
    const auto *b = static_cast<const std::uint8_t *>(out);
    span_index.emplace(belmont::ReadSpanKey{at,n},journal.size());
    journal.push_back({at, {b, b + n}});
    total_bytes += n;
    return true;
  }
  template <class T> bool Get(U64 at, T &v) {
    static_assert(std::is_integral_v<T>);
    std::array<std::uint8_t, sizeof(T)> b{};
    if (!Bytes(at, b.data(), b.size()))
      return false;
    U64 n{};
    for (unsigned i = 0; i < sizeof(T); ++i)
      n |= U64(b[i]) << (8 * i);
    std::memcpy(&v, &n, sizeof(T));
    return true;
  }
  bool Ptr(U64 at, U64 &p) {
    if (!Get(at, p) || !belmont::Pointer(p)) {
      malformed = true;
      return false;
    }
    return true;
  }
  bool Float(U64 at, float &v) {
    std::uint32_t b{};
    if (!Get(at, b))
      return false;
    std::memcpy(&v, &b, 4);
    if (!std::isfinite(v) || std::abs(v) > 1e9f) {
      malformed = true;
      return false;
    }
    return true;
  }
  bool Cstring(U64 at, std::string &s) {
    U64 p{};
    if (!Get(at, p) || p < 0x10000 || p >= 0x8000000000ULL) {
      malformed = true;
      return false;
    }
    s.clear();
    for (unsigned i = 0; i < 128; ++i) {
      std::uint8_t b{};
      if (!Bytes(p + i, &b, 1))
        return false;
      if (!b)
        return true;
      if (b < 32 || b > 126) {
        malformed = true;
        return false;
      }
      s += char(b);
    }
    malformed = true;
    return false;
  }
  bool Class(U64 object, std::string &name, std::string &ns) {
    U64 k{};
    return belmont::Pointer(object) && Ptr(object, k) &&
           Cstring(k + 0x10, name) && Cstring(k + 0x18, ns);
  }
  bool Is(U64 object, const char *name, const char *ns = "") {
    std::string n, s;
    return Class(object, n, s) && n == name && s == ns;
  }
  bool Alive(U64 object) {
    U64 p{};
    std::uint8_t b{};
    return Ptr(object + 0x10, p) && Bytes(p, &b, 1);
  }
  bool Pin(U64 rva, std::initializer_list<std::uint8_t> expected) {
    std::vector<std::uint8_t> b(expected.size());
    return Bytes(main_base + rva, b.data(), b.size()) &&
           std::equal(b.begin(), b.end(), expected.begin());
  }
  bool Pins() {
    return Pin(0x1e2f1b0, {0x1f, 0x14, 0x00, 0x71, 0x48, 0x02, 0x00, 0x54}) &&
           Pin(0x52c9144, {0x00, 0x03, 0x05, 0x07, 0x01, 0x09}) &&
           Pin(0x1e2f1f8, {0x56, 0x5b, 0xf4, 0x17}) &&
           Pin(0x1b45f50, {0x10, 0x89, 0x02, 0x90, 0x11, 0x36, 0x40, 0xf9, 0x10, 0xa2, 0x01, 0x91, 0x20, 0x02, 0x1f, 0xd6}) &&
           Pin(0x4deae1c, {0x81, 0xf6, 0x47, 0xf9, 0xe1, 0x00, 0x00, 0xb4}) &&
           Pin(0x1279e54,
               {0x00, 0x09, 0x40, 0xf9, 0x20, 0x01, 0x00, 0xb4, 0x00, 0x41,
                0x20, 0x1e, 0xe1, 0x03, 0x1f, 0xaa, 0x1d, 0xed, 0xd7, 0x97}) &&
           Pin(0x8752ec,
               {0x08, 0x24, 0x40, 0xf9, 0xf4, 0x03, 0x00, 0xaa, 0x1f, 0x05,
                0x00, 0x71, 0x81, 0x00, 0x00, 0x54, 0x88, 0x1e, 0x40, 0xf9}) &&
           Pin(0x87536c,
               {0xa0, 0x12, 0x00, 0x54, 0x8c, 0x03, 0x80, 0x52, 0x0a, 0x29,
                0x0c, 0x9b, 0x4a, 0x41, 0x5e, 0xb8, 0x7f, 0x01, 0x2a, 0x6a}) &&
           Pin(0x87539c, {0x88, 0x5a, 0x40, 0xb9, 0x68, 0x06, 0x00, 0x34,
                          0x1f, 0x05, 0x00, 0x71, 0x60, 0x0c, 0x00, 0x54,
                          0x1f, 0x09, 0x00, 0x71, 0x81, 0x07, 0x00, 0x54}) &&
           Pin(0x8753dc, {0x89, 0x5e, 0x40, 0xb9, 0x69, 0x04, 0x00, 0x34,
                          0x3f, 0x05, 0x00, 0x71, 0xe0, 0x08, 0x00, 0x54,
                          0x3f, 0x09, 0x00, 0x71, 0x81, 0x05, 0x00, 0x54}) &&
           Pin(0x875744, {0x0b, 0x05, 0x00, 0x51, 0xf8, 0xff, 0xff, 0x17}) &&
           Pin(0x1e307ec, {0xa0, 0x16, 0x40, 0xf9, 0x20, 0x02, 0x00, 0xb4,
                           0xa0, 0x1f, 0x40, 0xbd, 0xe1, 0x03, 0x1f, 0xaa,
                           0x81, 0xe9, 0xbe, 0x94, 0x60, 0x02, 0x00, 0xbd}) &&
           Pin(0x1effc44, {0x62, 0x52, 0x40, 0xb9, 0xe3, 0x03, 0x1f, 0xaa,
                           0x61, 0x3a, 0x40, 0xf9, 0x30, 0xd0, 0xfc, 0x97,
                           0x88, 0x02, 0x00, 0x0b, 0xa8, 0x1f, 0x00, 0xb9}) &&
           Pin(0x591b48, {0x08, 0x00, 0x40, 0xf9, 0x00, 0x81, 0x00, 0x91, 0xf9,
                          0xa4, 0xff, 0x17}) &&
           Pin(0x57af88, {0xa8, 0x0f, 0x40, 0xf9, 0xe2, 0x03, 0x00, 0xaa, 0xa1,
                          0x63, 0x00, 0x91, 0x08, 0x08, 0x00, 0xf9}) &&
           Pin(0x1e295d4, {0x80, 0x16, 0x40, 0xf9, 0x20, 0x01, 0x00, 0xb4}) &&
           Pin(0x1e33bdc, {0x89, 0x1a, 0x40, 0xbd}) &&
           Pin(0x1e33c30, {0xf5, 0x17, 0x40, 0xf9, 0xf5, 0x03, 0x00, 0xb4, 0xa8,
                           0x02, 0x40, 0xf9, 0x96, 0x0a, 0x40, 0xf9}) &&
           Pin(0x1e33b20, {0x00, 0x18, 0x40, 0xbd, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(0x439b424, {0x28, 0x08, 0x40, 0xf9, 0x08, 0x00, 0x00, 0xf9, 0xc0,
                           0x03, 0x5f, 0xd6}) &&
           Pin(0x1e2eed0, {0x00, 0x14, 0x40, 0xbd, 0x20, 0x00, 0x80, 0x52, 0x40,
                           0x00, 0x00, 0xbd, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(0x1e41610, {0x00, 0x10, 0x40, 0xbd, 0xc0, 0x03, 0x5f, 0xd6}) &&
           Pin(0x1e2f18c, {0x60, 0x12, 0x40, 0xb9, 0x08, 0x00, 0x00, 0x94}) &&
           Pin(0x1e33d28, {0x82, 0xff, 0xff, 0x97, 0x08, 0x40, 0x20, 0x1e}) &&
           Pin(0x1e29600, {0x22, 0x00, 0x80, 0x52, 0xc3, 0x29, 0x00, 0x14});
  }
  bool Dictionary(U64 dict, std::vector<std::pair<U64, U64>> &out,
                  bool integer_key = false) {
    U64 entries{}, capacity{};
    std::int32_t count{}, free{}, version{};
    if (!Ptr(dict + 0x18, entries) || !Get(dict + 0x20, count) ||
        !Get(dict + 0x28, free) || !Get(dict + 0x2c, version) || count < 0 ||
        count > 256 || free < 0 || free > count ||
        !Get(entries + 0x18, capacity) || capacity > 4096 ||
        U64(count) > capacity) {
      malformed = true;
      return false;
    }
    out.clear();
    std::vector<U64> keys;
    for (int i = 0; i < count; ++i) {
      const U64 at = entries + 0x20 + 24 * U64(i);
      std::int32_t hash{}, next{};
      U64 key{}, value{};
      if (!Get(at, hash))
        return false;
      if (hash == -1)
        continue;
      if (hash < 0 || !Get(at + 4, next) || next < -1 || next >= count ||
          !Get(at + 8, key) || !Ptr(at + 16, value)) {
        malformed = true;
        return false;
      }
      if (integer_key)
        key &= 0xffffffffU;
      if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
        malformed = true;
        return false;
      }
      keys.push_back(key);
      out.emplace_back(key, value);
    }
    if (int(out.size()) != count - free) {
      malformed = true;
      return false;
    }
    return true;
  }
  bool List(U64 list, std::vector<U64> &out) {
    U64 items{}, capacity{};
    std::int32_t count{}, version{};
    if (!Is(list, "List`1", "System.Collections.Generic") ||
        !Get(list + 0x10, items) || !Get(list + 0x18, count) ||
        !Get(list + 0x1c, version) || count < 0 || count > 128) {
      malformed = true;
      return false;
    }
    out.clear();
    if (!items && count == 0)
      return true;
    if (!belmont::Pointer(items) || !Get(items + 0x18, capacity) ||
        capacity > 4096 || U64(count) > capacity) {
      malformed = true;
      return false;
    }
    for (int i = 0; i < count; ++i) {
      U64 p{};
      if (!Ptr(items + 0x20 + 8 * U64(i), p))
        return false;
      out.push_back(p);
    }
    return true;
  }
  bool Load(U64 entity) {
    U64 dictionary{};
    std::vector<std::pair<U64, U64>> entries;
    if (!Ptr(entity + 0x30, dictionary) ||
        !Dictionary(dictionary, entries, true))
      return false;
    U64 component{};
    for (auto [key, object] : entries) {
      (void)key;
      std::string name, ns;
      if (!Class(object, name, ns))
        return false;
      if (name == "StatsStateComponent" && ns.empty()) {
        if (component) {
          malformed = true;
          return false;
        }
        component = object;
      }
    }
    if (!component || !Alive(component) || !Ptr(component + 0x28, dictionary) ||
        !Dictionary(dictionary, entries))
      return false;
    stats.clear();
    for (auto [key, object] : entries) {
      U64 type{}, stored_type{}, modifiers{};
      if (!Is(key, "RuntimeType", "System") || !Ptr(key + 0x10, type) ||
          !Is(object, "StatObject") || !Get(object + 0x10, stored_type) ||
          stored_type != key || !Ptr(object + 0x20, modifiers)) {
        malformed = true;
        return false;
      }
      Stat stat;
      stat.type = type;
      stat.object = object;
      // GetType's native runtime passes Il2CppClass.byval_arg (+20) to the
      // RuntimeType constructor; this canonical handle differs from metadata
      // *_var copies. Validate its declaring stat class and inheritance.
      const U64 klass = type - 0x20;
      U64 self{}, parent{};
      std::string ns, parent_name, parent_ns;
      if (!belmont::Pointer(klass) || !Get(klass + 0x78, self) ||
          self != klass || !Cstring(klass + 0x10, stat.name) ||
          !Cstring(klass + 0x18, ns) || !ns.empty() ||
          !Ptr(klass + 0x58, parent) || !Cstring(parent + 0x10, parent_name) ||
          !Cstring(parent + 0x18, parent_ns) || parent_name != "AFloatStat" ||
          !parent_ns.empty()) {
        malformed = true;
        return false;
      }
      if (!Float(object + 0x18, stat.base) || !List(modifiers, stat.modifiers))
        return false;
      if (std::any_of(stats.begin(), stats.end(),
                      [&](const Stat &s) { return s.type == type; })) {
        malformed = true;
        return false;
      }
      stats.push_back(std::move(stat));
    }
    return true;
  }
  Stat *Find(std::string_view name) {
    auto it = std::find_if(stats.begin(), stats.end(),
                           [&](const auto &s) { return s.name == name; });
    return it == stats.end() ? nullptr : &*it;
  }
  std::optional<float> Reference(U64 stat_class, std::int32_t kind,
                                 unsigned depth) {
    std::string name, ns;
    if (!Class(stat_class, name, ns) || !ns.empty())
      return {};
    auto stat = Find(name);
    if (!stat) {
      failure = "dependency_unavailable";
      return {};
    }
    if (kind == 1)
      return stat->base;
    if (kind != 0) {
      malformed = true;
      return {};
    }
    return Evaluate(*stat, depth + 1);
  }
  std::optional<float> Provider(U64 p, unsigned depth) {
    if (depth > 16) {
      failure = "dependency_depth";
      return {};
    }
    std::string name, ns;
    if (!Class(p, name, ns) || !ns.empty())
      return {};
    if (name == "FloatProviderBrute") {
      float v{};
      if (Float(p + 0x10, v))
        return v;
      return {};
    }
    if (name == "FloatProviderStat") {
      U64 target{};
      std::int32_t kind{};
      if (!Ptr(p + 0x10, target) || !Get(p + 0x18, kind))
        return {};
      return Reference(target, kind, depth + 1);
    }
    failure = "unsupported_provider:" + name;
    return {};
  }
  std::optional<float> Operation(float current, std::int32_t operation,
                                 float v) {
    float out{};
    switch (operation) {
    case 0:
      out = current + v;
      break;
    case 1:
      out = current - v;
      break;
    case 2:
      out = current * v;
      break;
    case 3:
      if (v == 0) {
        failure = "division_by_zero";
        return {};
      }
      out = current / v;
      break;
    case 4:
      out = v;
      break;
    case 5:
      // Native StatOperationType.Modulo dispatches to imported fmodf.
      if (v == 0) {
        failure = "modulo_by_zero";
        return {};
      }
      out = std::fmod(current, v);
      break;
    default:
      failure = "unsupported_operation";
      return {};
    }
    if (!std::isfinite(out) || std::abs(out) > 1e9f) {
      failure = "nonfinite_result";
      return {};
    }
    return out;
  }
  std::optional<float> Curve(U64 curve, float input) {
    U64 callback{}, native{}, keys{}, count{}, capacity{};
    std::int32_t before{}, after{};
    if (!Get(main_base + 0x6f7efe8, callback))
      return {};
    if (callback != main_base + 0x1279e20) {
      failure = "curve_evaluator_unavailable";
      return {};
    }
    if (!Is(curve, "AnimationCurve", "UnityEngine") ||
        !Ptr(curve + 0x10, native) || !Ptr(native + 0x38, keys) ||
        !Get(native + 0x48, count) || !Get(native + 0x50, capacity) ||
        !Get(native + 0x58, before) || !Get(native + 0x5c, after))
      return {};
    if (!count || count > 128 || capacity < count || capacity > 1024) {
      malformed = true;
      return {};
    }
    if (before != 2 || after != 2) {
      failure = "unsupported_curve_wrap";
      return {};
    }
    std::vector<std::uint8_t> bytes(std::size_t(count) * 28);
    if (!Bytes(keys, bytes.data(), bytes.size()))
      return {};
    struct Key {
      float time, value, in, out;
    };
    std::vector<Key> decoded;
    for (std::size_t i = 0; i < count; ++i) {
      const auto *at = bytes.data() + 28 * i;
      Key k{};
      std::int32_t weighted{};
      std::memcpy(&k.time, at, 4);
      std::memcpy(&k.value, at + 4, 4);
      std::memcpy(&k.in, at + 8, 4);
      std::memcpy(&k.out, at + 12, 4);
      std::memcpy(&weighted, at + 16, 4);
      if (weighted) {
        failure = "unsupported_weighted_curve";
        return {};
      }
      if (!std::isfinite(k.time) || !std::isfinite(k.value) ||
          std::abs(k.time) > 1e9f || std::abs(k.value) > 1e9f ||
          (!decoded.empty() && k.time <= decoded.back().time)) {
        malformed = true;
        return {};
      }
      decoded.push_back(k);
    }
    // Deliberately support only native unweighted linear segments. Reject all
    // other shapes even when the requested time happens to be an endpoint.
    for (std::size_t i = 1; i < decoded.size(); ++i) {
      const auto &a = decoded[i - 1], &b = decoded[i];
      if (!std::isfinite(a.out) || !std::isfinite(b.in)) {
        failure = "unsupported_curve_shape";
        return {};
      }
      const float dt = b.time - a.time, dy = b.value - a.value;
      volatile float twice = dy + dy, three = dy + twice;
      volatile float minus_one = three - dy, minus_two = minus_one - dy;
      const float quadratic_numerator = minus_two - dy;
      if (dt < .0001f || a.out != b.in || a.out * dt != dy || b.in * dt != dy ||
          quadratic_numerator != 0) {
        failure = "unsupported_curve_shape";
        return {};
      }
    }
    if (input <= decoded.front().time)
      return decoded.front().value;
    if (input >= decoded.back().time)
      return decoded.back().value;
    for (std::size_t i = 1; i < decoded.size(); ++i)
      if (input < decoded[i].time) {
        const auto &a = decoded[i - 1];
        // Native evaluator separates float multiply and add. Volatile forces
        // the same rounding boundary on ARM64 as well as hosts with FMA
        // support.
        volatile float product = (input - a.time) * a.out;
        const float value = a.value + product;
        if (!std::isfinite(value) || std::abs(value) > 1e9f) {
          malformed = true;
          return {};
        }
        return value;
      }
    failure = "curve_unavailable";
    return {};
  }
  std::optional<float> Modifier(U64 p, float current, unsigned depth) {
    std::string name, ns;
    if (!Class(p, name, ns) || !ns.empty())
      return {};
    if (name == "OperationBruteValueStatModifier") {
      std::int32_t op{};
      float v{};
      if (!Get(p + 0x10, op) || !Float(p + 0x14, v))
        return {};
      return Operation(current, op, v);
    }
    if (name == "ToIntStatModifier") {
      std::int32_t op{};
      if (!Get(p + 0x10, op))
        return {};
      if (op == 0)
        return std::floor(current);
      if (op == 1)
        return RoundEven(current);
      if (op == 2)
        return std::ceil(current);
      malformed = true;
      return {};
    }
    if (name == "UseStatModifier" || name == "UseStatCurveModifier") {
      std::int32_t op{}, kind{};
      U64 target{};
      if (!Get(p + 0x10, op) || !Ptr(p + 0x18, target) || !Get(p + 0x20, kind))
        return {};
      auto v = Reference(target, kind, depth + 1);
      if (!v)
        return {};
      if (name == "UseStatCurveModifier") {
        U64 curve{};
        if (!Ptr(p + 0x28, curve))
          return {};
        v = Curve(curve, *v);
        if (!v)
          return {};
      }
      return Operation(current, op, *v);
    }
    if (name == "UseFloatProviderStatModifier") {
      std::int32_t op{};
      U64 provider{};
      if (!Get(p + 0x10, op) || !Get(p + 0x18, provider))
        return {};
      if (!provider)
        return current;
      auto v = Provider(provider, depth + 1);
      if (!v)
        return {};
      return Operation(current, op, *v);
    }
    if (name == "ClampStatModifier") {
      std::uint8_t min{}, max{};
      if (!Get(p + 0x10, min) || !Get(p + 0x20, max) || min > 1 || max > 1) {
        malformed = true;
        return {};
      }
      if (min) {
        U64 provider{};
        if (!Ptr(p + 0x18, provider))
          return {};
        auto v = Provider(provider, depth + 1);
        if (!v)
          return {};
        current = std::max(current, *v);
      }
      if (max) {
        U64 provider{};
        if (!Ptr(p + 0x28, provider))
          return {};
        auto v = Provider(provider, depth + 1);
        if (!v)
          return {};
        current = std::min(current, *v);
      }
      return current;
    }
    failure = "unsupported_modifier:" + name;
    return {};
  }
  std::optional<float> Evaluate(Stat &s, unsigned depth) {
    if (depth > 16) {
      failure = "dependency_depth";
      return {};
    }
    if (auto it = memo.find(s.object); it != memo.end())
      return it->second;
    if (std::find(evaluating.begin(), evaluating.end(), s.object) !=
        evaluating.end()) {
      failure = "dependency_cycle";
      return {};
    }
    evaluating.push_back(s.object);
    std::optional<float> result = s.base;
    for (auto m : s.modifiers) {
      result = Modifier(m, *result, depth + 1);
      if (!result)
        break;
    }
    evaluating.pop_back();
    memo[s.object] = result;
    return result;
  }
  Value ReadValue(const char *name, bool round = false) {
    failure.clear();
    auto s = Find(name);
    if (!s)
      return {false, 0, "stat_unavailable"};
    auto v = Evaluate(*s, 0);
    if (!v)
      return {false, 0, failure.empty() ? "dependency_unavailable" : failure};
    const float n = round ? RoundEven(*v) : std::floor(*v);
    if (!std::isfinite(n) || std::abs(n) > 1e9f)
      return {false, 0, "out_of_range"};
    return {true, std::int32_t(n), "ready"};
  }
  static Value DisplaySum(const Value &primary, const Value &additional) {
    if (!primary.ready || !additional.ready)
      return {false, 0, "display_input_unavailable"};
    const auto sum = std::int64_t(primary.value) + additional.value;
    if (sum < std::numeric_limits<std::int32_t>::min() ||
        sum > std::numeric_limits<std::int32_t>::max())
      return {false, 0, "out_of_range"};
    return {true, std::int32_t(sum), "ready"};
  }
  bool Coherent() {
    std::array<std::uint8_t,4096> fresh{};
    for (const auto &span : journal) {
      if (span.bytes.size()>fresh.size() ||
          !read(span.at,fresh.data(),span.bytes.size()) ||
          std::memcmp(fresh.data(),span.bytes.data(),span.bytes.size())!=0)
        return false;
    }
    return true;
  }

public:
  explicit Decoder(Read &r) : read(r) {}
  Snapshot Sample(const belmont::Identity &id,
                  const belmont::Snapshot &source) {
    journal.clear();
    span_index.clear();
    total_bytes = 0;
    malformed = false;
    stats.clear();
    evaluating.clear();
    memo.clear();
    failure.clear();
    main_base = id.base;
    Snapshot out;
    if (id.title != belmont::Title || id.build != belmont::Build ||
        !belmont::Pointer(id.base) || id.size < 0x6f7eff0 ||
        id.base > std::numeric_limits<U64>::max() - id.size) {
      out.reason = "unsupported_build";
      return out;
    }
    out.supported = true;
    // Existing equipment gate revalidates the exact current entity/active
    // state, global loading, ownership and instruction pins; all its reads also
    // join this reader's final coherence journal.
    auto guarded = [&](U64 at, void *dst, std::size_t n) {
      return Bytes(at, dst, n);
    };
    belmont_equipment::Decoder gate(guarded);
    auto equipment = gate.Sample(id, source);
    if (!equipment.ready) {
      out.reason = "gameplay_unavailable";
      return out;
    }
    if (!Pins()) {
      out.reason = "accessor_mismatch";
      return out;
    }
    if (!Load(source.entity)) {
      out.reason = "stats_unavailable";
      return out;
    }
    out.atk = ReadValue("PowerAttack");
    out.atk_bonus = ReadValue("PowerAttackBonus");
    out.defense = ReadValue("PowerDefense");
    out.defense_bonus = ReadValue("PowerDefenseBonus");
    out.max_hp = ReadValue("HealthMax");
    out.max_mp = ReadValue("SpellManaMax");
    out.level = ReadValue("Level", true);
    out.current_hp = ReadValue("HealthCurrent");
    out.current_mp = ReadValue("SpellManaCurrent");
    out.health_upgrades = ReadValue("HealthUpgradeCount");
    out.health_fragments = ReadValue("HealthUpgradeFragmentUnmergedCount");
    out.health_fragments_needed = ReadValue("HealthUpgradeFragmentNeededToMergeCount");
    out.mana_upgrades = ReadValue("ManaUpgradeCount");
    out.mana_fragments = ReadValue("ManaUpgradeFragmentUnmergedCount");
    out.mana_fragments_needed = ReadValue("ManaUpgradeFragmentNeededToMergeCount");
    out.atk_display = DisplaySum(out.atk, out.atk_bonus);
    out.defense_display = DisplaySum(out.defense, out.defense_bonus);
    if (malformed) {
      Snapshot rejected;
      rejected.supported = true;
      rejected.reason = "malformed_stats";
      return rejected;
    }
    if (!Coherent()) {
      Snapshot rejected;
      rejected.supported = true;
      rejected.reason = "incoherent";
      return rejected;
    }
    out.coherent = true;
    out.entity = equipment.entity;
    out.ready = out.atk.ready || out.defense.ready || out.max_hp.ready ||
                out.max_mp.ready || out.level.ready || out.current_hp.ready ||
                out.current_mp.ready;
    out.reason = out.ready ? "ready" : "formulas_unavailable";
    return out;
  }
};
} // namespace belmont_equipment_stats
