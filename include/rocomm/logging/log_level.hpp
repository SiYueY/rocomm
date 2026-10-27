#ifndef ROCOMM_LOGGING_LOG_LEVEL_HPP_
#define ROCOMM_LOGGING_LOG_LEVEL_HPP_

#include <cstdint>

namespace rocomm::logging {

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5,
    Off = 6,
};

constexpr bool is_valid_log_level(LogLevel level) noexcept {
    return static_cast<std::uint8_t>(level) <= static_cast<std::uint8_t>(LogLevel::Off);
}

constexpr bool is_record_level(LogLevel level) noexcept {
    return is_valid_log_level(level) && level != LogLevel::Off;
}

constexpr bool is_enabled(LogLevel level, LogLevel threshold) noexcept {
    return is_record_level(level) && is_valid_log_level(threshold) && level >= threshold &&
           threshold != LogLevel::Off;
}

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_LOG_LEVEL_HPP_
