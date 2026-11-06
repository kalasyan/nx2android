// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

// Runtime 21: device and emulator values a companion can show on the second screen.
//   @device.battery_present  1 when the device has a battery, else 0
//   @device.battery_pct      0-100, -1 when unknown or there is no battery
//   @device.charging         1 while charging, else 0
//   @perf.fps                game FPS (float; the int copy is rounded)
//   @perf.speed_pct          emulation speed in percent (float; the int copy is rounded)
//   @perf.frametime_ms       wall time per system frame excluding waits, in ms (float; int rounded)
//   @perf.frametime_x10      frametime_ms * 10, rounded (value widgets draw integers only)
// The battery is read at most once a second on Android (where the frontend pushes it into
// Common::GetPowerStatus) and every 10 s elsewhere (a file read on Linux). The perf values are
// computed from deltas of PerfStats::GetTotals every 500 ms, so they change at most twice a second
// and a widget bound to one is redrawn only then; nothing is published for them before the first
// full interval. A gap of more than 2 s between samples (emulation paused, second screen hidden)
// or counters that went backwards (a new PerfStats) start a fresh interval and keep the last
// values. Published only for a package that names them (Manifest::uses_host_keys,
// JsonReferencesHostKeys); a native module reads them with get_i64 / get_f64 whether or not the
// manifest names them. The sources are injectable so tests are deterministic.

#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

#include "common/common_types.h"
#include "core/mods/mod_types.h"

namespace Core::Mods {

/// One battery reading. present = false always comes with pct -1 and charging false.
struct PowerReading {
    bool present{false};
    s32 pct{-1};
    bool charging{false};
};

/// Running totals at one instant (PerfStats::GetTotals plus the emulated clock).
struct PerfSample {
    u64 game_frames{0};
    u64 system_frames{0};
    s64 frametime_ns{0}; ///< accumulated system-frame time excluding waits
    s64 emulated_us{0};  ///< CoreTiming global time
};

/// Where the values come from. Every member may be empty: the defaults are the steady clock, the
/// device battery (ReadDevicePower) and no perf values.
struct HostValueSource {
    std::function<s64()> now_ns; ///< monotonic wall clock
    std::function<PowerReading()> power;
    std::function<std::optional<PerfSample>()> perf; ///< nullopt while there are no stats
};

/// The published key names (runtime 21).
namespace HostKeys {
inline constexpr const char* BatteryPresent = "@device.battery_present";
inline constexpr const char* BatteryPct = "@device.battery_pct";
inline constexpr const char* Charging = "@device.charging";
inline constexpr const char* Fps = "@perf.fps";
inline constexpr const char* SpeedPct = "@perf.speed_pct";
inline constexpr const char* FrametimeMs = "@perf.frametime_ms";
inline constexpr const char* FrametimeX10 = "@perf.frametime_x10";
} // namespace HostKeys

/// "57,1" -> 57 %, charging; "57" -> 57 %, not charging; "none" or a negative percentage -> no
/// battery. nullopt for anything else. The EDEN_DSMOD_FAKE_BATTERY dev override.
[[nodiscard]] std::optional<PowerReading> ParseFakeBattery(std::string_view text);

/// Common::GetPowerStatus mapped to a PowerReading, unless EDEN_DSMOD_FAKE_BATTERY (dev tools
/// builds) overrides it.
[[nodiscard]] PowerReading ReadDevicePower();

/// Samples and holds the runtime 21 values. Tick thread only.
class HostValues {
public:
    static constexpr s64 PerfIntervalNs = 500'000'000;
    static constexpr s64 PerfGapNs = 2'000'000'000;
#ifdef __ANDROID__
    static constexpr s64 PowerIntervalNs = 1'000'000'000;
#else
    static constexpr s64 PowerIntervalNs = 10'000'000'000;
#endif

    void SetSource(HostValueSource source_);
    /// Reads the battery when due and the perf totals every PerfIntervalNs. Cheap otherwise.
    void Update();
    /// Writes the keys that have a value into the snapshot.
    void Publish(StateSnapshot& snapshot) const;
    /// What get_i64 / get_f64 answer for a key; nullopt when it has no value (yet).
    [[nodiscard]] std::optional<s64> GetI64(std::string_view name) const;
    [[nodiscard]] std::optional<f64> GetF64(std::string_view name) const;

    /// Whether Update ever ran.
    [[nodiscard]] bool Sampled() const {
        return last_update_ns.has_value();
    }
    [[nodiscard]] bool HasPerf() const {
        return have_perf;
    }
    /// How many times the battery was read (tests).
    [[nodiscard]] u64 PowerReads() const {
        return power_reads;
    }

private:
    struct Baseline {
        s64 wall_ns;
        PerfSample sample;
    };
    void Rebaseline(s64 now);

    HostValueSource source;
    std::optional<s64> last_update_ns;
    std::optional<s64> last_power_ns;
    std::optional<Baseline> baseline;
    PowerReading power{};
    bool have_power{false};
    bool have_perf{false};
    bool have_frametime{false};
    f64 fps{0.0};
    f64 speed_pct{0.0};
    f64 frametime_ms{0.0};
    u64 power_reads{0};
};

/// Whether `name` is a runtime 21 key ("@device." or "@perf."): published from the host, never
/// from interaction, so not "volatile" for the post-tap derived pass.
[[nodiscard]] bool IsHostValueKey(std::string_view name);

/// Whether a manifest / data file can read the runtime 21 keys: a string or an object key anywhere
/// in it containing "@device." or "@perf." (binds, derived sources, expressions, gates).
[[nodiscard]] bool JsonReferencesHostKeys(const nlohmann::json& json);

} // namespace Core::Mods
