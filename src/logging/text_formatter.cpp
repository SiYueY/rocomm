#include "text_formatter.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <sstream>

namespace rocomm::logging {

namespace {

const char* level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Critical:
            return "CRITICAL";
        case LogLevel::Off:
            return "OFF";
    }
    return "INVALID";
}

bool make_calendar_time(std::time_t value, LogTimeZone timezone, std::tm& calendar_time) noexcept {
    if (timezone == LogTimeZone::Utc) {
        return gmtime_r(&value, &calendar_time) != nullptr;
    }
    return localtime_r(&value, &calendar_time) != nullptr;
}

}  // namespace

bool TextFormatter::format(const LogRecord& record, std::string& output) noexcept {
    output.clear();
    try {
        std::ostringstream stream;
        bool needs_separator = false;
        if (config_.include_timestamp) {
            const auto timestamp = record.timestamp();
            const auto seconds = std::chrono::system_clock::to_time_t(timestamp);
            std::tm calendar_time{};
            if (!make_calendar_time(seconds, config_.timezone, calendar_time)) {
                return false;
            }
            char date_buffer[40] = {};
            if (std::strftime(
                    date_buffer, sizeof(date_buffer), "%Y-%m-%dT%H:%M:%S", &calendar_time) == 0U) {
                return false;
            }
            const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                                    timestamp.time_since_epoch()) %
                                std::chrono::seconds(1);
            stream << date_buffer << '.' << micros.count();
            if (config_.timezone == LogTimeZone::Utc) {
                stream << 'Z';
            } else {
                char zone_buffer[8] = {};
                if (std::strftime(zone_buffer, sizeof(zone_buffer), "%z", &calendar_time) == 0U) {
                    return false;
                }
                stream << zone_buffer;
            }
            needs_separator = true;
        }
        if (config_.include_level) {
            if (needs_separator) {
                stream << ' ';
            }
            stream << '[' << level_name(record.level()) << ']';
            needs_separator = true;
        }
        if (config_.include_category) {
            if (needs_separator) {
                stream << ' ';
            }
            stream << '[' << record.category() << ']';
            needs_separator = true;
        }
        if (config_.include_thread_id) {
            if (needs_separator) {
                stream << ' ';
            }
            stream << "[tid=" << std::hash<std::thread::id>{}(record.thread_id()) << ']';
            needs_separator = true;
        }
        if (needs_separator) {
            stream << ' ';
        }
        stream << record.message();
        if (config_.include_source_location) {
            const SourceLocation source = record.source();
            stream << " [" << (source.file() == nullptr ? "?" : source.file()) << ':'
                   << source.line() << ']';
            if (config_.include_function) {
                stream << " [function=" << (source.function() == nullptr ? "?" : source.function())
                       << ']';
            }
        } else if (config_.include_function) {
            const SourceLocation source = record.source();
            stream << " [function=" << (source.function() == nullptr ? "?" : source.function())
                   << ']';
        }
        output = stream.str();
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace rocomm::logging
