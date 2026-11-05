// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
namespace belmont_pins {
inline constexpr std::size_t Capacity = 64, KindCount = 9;
// Native MapMarker_*_DisplayData.markerIcon PPtrs (map-common external CAB2).
// Asset identities only: no game text, pixels or progression table is shipped.
inline constexpr std::array<std::int64_t, KindCount> Sprites{
    1912900965121370566LL, 7944782504571480518LL,  -1464126068056282682LL,
    6194193013819419078LL, -8184545778678978106LL, 5697089712629524934LL,
    932508020663953862LL,  5463948360878696902LL,  -8868760338130556474LL};
inline std::string NativeImage(std::size_t kind) {
  if (kind >= KindCount)
    return {};
  return "module:native:ui-gamemenus_assets_all_"
         "866519f7d6a7057a8ba1c459a5b3f694.bundle#" +
         std::to_string(Sprites[kind]);
}
inline std::string Image(std::size_t kind) {
  return kind < KindCount ? "module:belmont:pin-icon:" + std::to_string(kind)
                          : "";
}
struct Pin {
  std::string area;
  double x{}, y{};
  std::size_t kind{};
  std::int32_t native_id{-1}, native_navigation{-1};
};
enum class Operation { Add, Change, Remove };
struct Request {
  Operation operation;
  Pin pin;
};
struct Bounds {
  double x0{}, y0{}, x1{}, y1{};
  bool Valid() const {
    return std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) &&
           std::isfinite(y1) && x1 > x0 && y1 > y0;
  }
  bool Contains(double x, double y) const {
    return Valid() && std::isfinite(x) && std::isfinite(y) && x >= x0 &&
           x <= x1 && y >= y0 && y <= y1;
  }
  bool Same(const Bounds &other) const {
    return x0 == other.x0 && y0 == other.y0 && x1 == other.x1 && y1 == other.y1;
  }
};
class Store {
  std::array<std::optional<Pin>, Capacity> pins{};
  std::optional<std::size_t> armed_kind;
  std::optional<Pin> editing;
  std::optional<Request> request;
  std::string area;
  Bounds bounds;
  bool interactive{}, choosing{}, waiting{}, executing{};
  std::int64_t old_sequence{-1};
  static bool SameIdentity(const Pin &a, const Pin &b) {
    return a.area == b.area && a.native_id == b.native_id &&
           a.native_navigation == b.native_navigation;
  }
  const Pin *Find(const Pin &target) const {
    for (const auto &p : pins)
      if (p && SameIdentity(*p, target))
        return &*p;
    return nullptr;
  }
  bool HasCapacity() const {
    for (const auto &p : pins)
      if (!p)
        return true;
    return false;
  }
  bool Enqueue(Operation operation, Pin pin) {
    if (Busy())
      return false;
    Cancel();
    request = Request{operation, std::move(pin)};
    return true;
  }

public:
  // The caller supplies only source-joined pins for the current native level /
  // navigation. This is the sole path that changes visible saved pin records.
  // Reject malformed snapshots atomically rather than truncate or retain stale
  // pins. Stable native identity preserves an open edit when native lists
  // reorder.
  bool SetPins(std::span<const Pin> native) {
    std::array<std::optional<Pin>, Capacity> next{};
    std::set<std::tuple<std::string, std::int32_t, std::int32_t>> identities;
    bool valid = native.size() <= Capacity;
    if (valid)
      for (std::size_t i = 0; i < native.size(); ++i) {
        const auto &p = native[i];
        if (p.area.empty() || p.kind >= KindCount || p.native_id < 0 ||
            p.native_navigation < 0 || p.native_navigation > 255 ||
            !std::isfinite(p.x) || !std::isfinite(p.y) ||
            !identities.emplace(p.area, p.native_navigation, p.native_id)
                 .second) {
          valid = false;
          break;
        }
        next[i] = p;
      }
    pins = valid ? std::move(next) : decltype(next){};
    if (!valid) {
      Cancel();
      return false;
    }
    if (editing) {
      if (const auto *p = Find(*editing))
        editing = *p;
      else
        Cancel();
    }
    if (request && request->operation != Operation::Add && !Find(request->pin))
      request.reset();
    return true;
  }
  void Context(std::string current, Bounds b, bool enabled) {
    const bool valid = enabled && !current.empty() && b.Valid();
    if (!valid || area != current || !bounds.Same(b))
      Cancel();
    area = std::move(current);
    bounds = b;
    interactive = valid;
  }
  bool Open() {
    if (!interactive || Busy())
      return false;
    Cancel();
    choosing = true;
    return true;
  }
  bool Begin(std::int64_t sequence) {
    if (!interactive || Busy() || !armed_kind || choosing || waiting ||
        sequence < -1)
      return false;
    waiting = true;
    old_sequence = sequence;
    return true;
  }
  bool Capture(std::int64_t sequence, double x, double y) {
    if (!interactive || Busy() || !armed_kind || !waiting ||
        sequence <= old_sequence)
      return false;
    waiting = false;
    if (!bounds.Contains(x, y) || !HasCapacity())
      return false;
    return Enqueue(Operation::Add, Pin{area, x, y, *armed_kind});
  }
  bool Select(std::int64_t index) {
    if (!interactive || Busy() || index < 0 ||
        index >= std::int64_t(Capacity) || !pins[std::size_t(index)] ||
        pins[std::size_t(index)]->area != area)
      return false;
    Cancel();
    editing = *pins[std::size_t(index)];
    choosing = true;
    return true;
  }
  bool Choose(std::int64_t kind) {
    if (!interactive || Busy() || !choosing || kind < 0 ||
        kind >= std::int64_t(KindCount))
      return false;
    if (editing) {
      const auto *target = Find(*editing);
      if (!target || target->area != area)
        return false;
      Pin changed = *target;
      changed.kind = std::size_t(kind);
      return Enqueue(Operation::Change, std::move(changed));
    }
    if (!HasCapacity())
      return false;
    Cancel();
    armed_kind = std::size_t(kind);
    return true;
  }
  bool Remove() {
    if (!interactive || Busy() || !choosing || !editing)
      return false;
    const auto *target = Find(*editing);
    if (!target || target->area != area)
      return false;
    return Enqueue(Operation::Remove, *target);
  }
  bool Busy() const { return request.has_value() || executing; }
  std::optional<Request> TakeRequest() {
    if (executing || !request)
      return {};
    auto out = std::move(request);
    request.reset();
    executing = true;
    return out;
  }
  // Completion or failure releases input; the next SetPins supplies the actual
  // native result. Neither request dispatch nor completion changes visible
  // pins.
  void FinishRequest() {
    request.reset();
    executing = false;
  }
  void Cancel() {
    armed_kind.reset();
    editing.reset();
    request.reset();
    choosing = waiting = false;
    old_sequence = -1;
    // A dispatched native action cannot be undone locally; the controller must
    // cancel/acknowledge and call FinishRequest before another action may
    // start.
  }
  void Clear() {
    Cancel();
    FinishRequest();
    for (auto &p : pins)
      p.reset();
  }
  bool Waiting() const { return waiting; }
  bool Picker() const { return choosing; }
  bool Editing() const { return choosing && editing.has_value(); }
  bool Armed() const { return armed_kind.has_value(); }
  const auto &All() const { return pins; }
};
} // namespace belmont_pins
