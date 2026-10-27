#ifndef ROCOMM_LOGGING_LOG_RECORD_HPP_
#define ROCOMM_LOGGING_LOG_RECORD_HPP_

#include <chrono>
#include <string>
#include <string_view>
#include <thread>

#include <rocomm/logging/log_level.hpp>
#include <rocomm/logging/source_location.hpp>

namespace rocomm::logging {

class LogRecord {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    LogRecord(const LogRecord&) = delete;
    LogRecord& operator=(const LogRecord&) = delete;
    LogRecord(LogRecord&&) noexcept = default;
    LogRecord& operator=(LogRecord&&) noexcept = default;

    TimePoint timestamp() const noexcept { return timestamp_; }
    LogLevel level() const noexcept { return level_; }
    std::string_view category() const noexcept { return category_; }
    std::string_view message() const noexcept { return message_; }
    SourceLocation source() const noexcept { return source_; }
    std::thread::id thread_id() const noexcept { return thread_id_; }

private:
    friend class LoggingContext;
    friend class Logger;

    LogRecord(
        TimePoint timestamp, LogLevel level, std::string category, std::string message,
        SourceLocation source, std::thread::id thread_id) noexcept
    : timestamp_(timestamp),
      level_(level),
      category_(std::move(category)),
      message_(std::move(message)),
      source_(source),
      thread_id_(thread_id) {}

    TimePoint timestamp_;
    LogLevel level_ = LogLevel::Info;
    std::string category_;
    std::string message_;
    SourceLocation source_;
    std::thread::id thread_id_;
};

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_LOG_RECORD_HPP_
