#ifndef ROCOMM_LOGGING_LOGGING_CONTEXT_HPP_
#define ROCOMM_LOGGING_LOGGING_CONTEXT_HPP_

#include <memory>
#include <string_view>

#include <rocomm/result.hpp>
#include <rocomm/time.hpp>
#include <rocomm/logging/logger.hpp>
#include <rocomm/logging/logging_config.hpp>
#include <rocomm/logging/logging_stats.hpp>

namespace rocomm::logging {

class LoggingContext {
public:
    LoggingContext(const LoggingContext&) = delete;
    LoggingContext& operator=(const LoggingContext&) = delete;

    LoggingContext(LoggingContext&& other) noexcept;
    LoggingContext& operator=(LoggingContext&& other) noexcept;

    static Result<LoggingContext> create(LoggingConfig config);

    Result<Logger> get_logger(std::string_view category);
    Status set_default_level(LogLevel level) noexcept;
    Status set_category_level(std::string_view category, LogLevel level) noexcept;
    Status clear_category_level(std::string_view category) noexcept;
    Status flush(Duration timeout) noexcept;
    Status shutdown(Duration timeout) noexcept;
    LoggingStats get_stats() const noexcept;

    ~LoggingContext() noexcept;

private:
    friend class Logger;

    explicit LoggingContext(std::shared_ptr<Logger::State> state) noexcept
    : state_(std::move(state)) {}

    std::shared_ptr<Logger::State> state_;
};

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_LOGGING_CONTEXT_HPP_
