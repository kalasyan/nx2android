// nx2android: Eden's logging macros, written to the port's log (duo_host.cpp) instead of Eden's logger.
#pragma once
#include <string>
#include <type_traits>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include "common/swap.h"

template <typename T, typename U>
struct fmt::formatter<SwapStructT<T, U>> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
    template <typename FormatContext>
    auto format(const SwapStructT<T, U>& reg, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", T(reg));
    }
};

// Eden's logger printed enumerations as their numbers
template <typename T>
    requires std::is_enum_v<T>
struct fmt::formatter<T, char> : fmt::formatter<std::underlying_type_t<T>, char> {
    auto format(T v, format_context& ctx) const {
        return fmt::formatter<std::underlying_type_t<T>, char>::format(static_cast<std::underlying_type_t<T>>(v), ctx);
    }
};

namespace Common::Log {
enum class Level : unsigned char { Trace, Debug, Info, Warning, Error, Critical, Count };
void DuoWrite(Level level, const char* cls, const std::string& text);
bool DuoEnabled(Level level);
template <typename... Args>
void DuoLog(Level level, const char* cls, fmt::format_string<Args...> f, Args&&... args) {
    if (DuoEnabled(level)) DuoWrite(level, cls, fmt::format(f, std::forward<Args>(args)...));
}
} // namespace Common::Log

#define LOG_TRACE(log_class, ...) (void(0))
#define LOG_DEBUG(log_class, ...) ::Common::Log::DuoLog(::Common::Log::Level::Debug, #log_class, __VA_ARGS__)
#define LOG_INFO(log_class, ...) ::Common::Log::DuoLog(::Common::Log::Level::Info, #log_class, __VA_ARGS__)
#define LOG_WARNING(log_class, ...) ::Common::Log::DuoLog(::Common::Log::Level::Warning, #log_class, __VA_ARGS__)
#define LOG_ERROR(log_class, ...) ::Common::Log::DuoLog(::Common::Log::Level::Error, #log_class, __VA_ARGS__)
#define LOG_CRITICAL(log_class, ...) ::Common::Log::DuoLog(::Common::Log::Level::Critical, #log_class, __VA_ARGS__)
