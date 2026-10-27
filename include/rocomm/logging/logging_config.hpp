#ifndef ROCOMM_LOGGING_LOGGING_CONFIG_HPP_
#define ROCOMM_LOGGING_LOGGING_CONFIG_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <rocomm/logging/log_level.hpp>
#include <rocomm/logging/logging_backend.hpp>

namespace rocomm::logging {

enum class LoggingMode : std::uint8_t {
    Synchronous,
    Asynchronous,
};

enum class LogOverflowPolicy : std::uint8_t {
    DropNewest,
    DropOldest,
    Block,
};

class LoggingConfig {
public:
    LoggingConfig() = default;
    LoggingConfig(const LoggingConfig&) = default;
    LoggingConfig& operator=(const LoggingConfig&) = default;
    LoggingConfig(LoggingConfig&&) noexcept = default;
    LoggingConfig& operator=(LoggingConfig&&) noexcept = default;

    LoggingConfig& set_enabled(bool enabled) noexcept {
        enabled_ = enabled;
        return *this;
    }

    LoggingConfig& set_default_level(LogLevel level) noexcept {
        default_level_ = level;
        return *this;
    }

    LoggingConfig& set_category_level(std::string category, LogLevel level) {
        for (auto& override : category_levels_) {
            if (override.first == category) {
                override.second = level;
                return *this;
            }
        }
        category_levels_.emplace_back(std::move(category), level);
        return *this;
    }

    LoggingConfig& set_mode(LoggingMode mode) noexcept {
        mode_ = mode;
        return *this;
    }

    LoggingConfig& set_queue_capacity(std::size_t capacity) noexcept {
        queue_capacity_ = capacity;
        return *this;
    }

    LoggingConfig& set_overflow_policy(LogOverflowPolicy policy) noexcept {
        overflow_policy_ = policy;
        return *this;
    }

    LoggingConfig& set_flush_on_level(LogLevel level) noexcept {
        flush_on_level_ = level;
        return *this;
    }

    LoggingConfig& set_backend_factory(LoggingBackendFactory factory) {
        backend_factory_ = std::move(factory);
        return *this;
    }

private:
    friend class LoggingContext;

    bool enabled_ = true;
    LogLevel default_level_ = LogLevel::Info;
    std::vector<std::pair<std::string, LogLevel>> category_levels_;
    LoggingMode mode_ = LoggingMode::Asynchronous;
    std::size_t queue_capacity_ = 8192;
    LogOverflowPolicy overflow_policy_ = LogOverflowPolicy::DropNewest;
    LogLevel flush_on_level_ = LogLevel::Error;
    LoggingBackendFactory backend_factory_;
};

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_LOGGING_CONFIG_HPP_
