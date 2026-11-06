// nx2android: Core::Timing for the dual-screen runtime: one looping event, run by the companion's own tick thread (duo_host.cpp)
#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "common/common_types.h"
namespace Core::Timing {
using TimedCallback = std::function<std::optional<std::chrono::nanoseconds>(s64, std::chrono::nanoseconds)>;
struct EventType { std::string name; TimedCallback callback; };
inline std::shared_ptr<EventType> CreateEvent(std::string name, TimedCallback callback) {
    return std::make_shared<EventType>(EventType{std::move(name), std::move(callback)});
}
class CoreTiming {
public:
    std::chrono::nanoseconds GetGlobalTimeNs() const;
    std::chrono::microseconds GetGlobalTimeUs() const { return std::chrono::duration_cast<std::chrono::microseconds>(GetGlobalTimeNs()); }
    void ScheduleLoopingEvent(std::chrono::nanoseconds start, std::chrono::nanoseconds every, const std::shared_ptr<EventType>& e, bool = false);
    void UnscheduleEvent(const std::shared_ptr<EventType>& e, bool = false);
    u64 GetClockTicks() const { return static_cast<u64>(GetGlobalTimeNs().count()) * 192 / 10000; }
};
} // namespace Core::Timing
