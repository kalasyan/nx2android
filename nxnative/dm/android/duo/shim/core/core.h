// nx2android: Core::System for the dual-screen runtime. The port has no emulator around the game, so the runtime gets the few services it
// used from Eden's System from duo_host.cpp: the clock and its tick thread, the game's process and memory (this process's), the second
// screen (VideoCore::DSMod::AuxRouting), the companion's virtual pad and player 1's pad.
#pragma once
#include <functional>
#include <utility>
#include "common/common_types.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "hid_core/hid_core.h"
#include "input_common/main.h"

namespace VideoCore::DSMod { class AuxRouting; }

namespace Core {
struct PerfStatsResults {};
struct PerfTotals { u64 game_frames{}; u64 system_frames{}; std::chrono::nanoseconds frametime{}; };
class PerfStats { public: PerfTotals GetTotals() const; };
class GPUShim { public: VideoCore::DSMod::AuxRouting& DSModAux(); };

class System {
public:
    static System& GetInstance();
    Timing::CoreTiming& CoreTiming() { return timing; }
    Kernel::KernelCore& Kernel() { return kernel; }
    GPUShim& GPU() { return gpu; }
    InputCommon::InputSubsystem* GetInputSubsystem() { return &input; }
    HID::HIDCore& HIDCore() { return hid; }
    PerfStats& GetPerfStats() { return perf; }
    bool IsPoweredOn() const { return true; }
    bool IsMulticore() const { return true; }
    /// Eden stops the guest's threads here; the port's run natively, so the work runs as is (writes are a few aligned stores)
    template <typename F> bool RunWithGuestThreadsSuspended(F&& f) { std::forward<F>(f)(); return true; }
    std::pair<u64, u64> GetDualScreenGuestMailbox() const { return {0, 0}; }
    u64 GetDualScreenGuestMailboxEpoch() const { return 0; }
    Memory::Memory& ApplicationMemory() { return kernel.ApplicationProcess()->GetMemory(); }
private:
    Timing::CoreTiming timing;
    Kernel::KernelCore kernel;
    GPUShim gpu;
    InputCommon::InputSubsystem input;
    HID::HIDCore hid;
    PerfStats perf;
};
inline void InvalidateInstructionCacheRange(const Kernel::KProcess*, u64 addr, u64 size) { __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(addr + size)); }
} // namespace Core
