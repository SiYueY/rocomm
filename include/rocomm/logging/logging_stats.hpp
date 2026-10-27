#ifndef ROCOMM_LOGGING_LOGGING_STATS_HPP_
#define ROCOMM_LOGGING_LOGGING_STATS_HPP_

#include <cstdint>

namespace rocomm::logging {

struct LoggingBackendStats {
    std::uint64_t write_calls = 0;
    std::uint64_t write_errors = 0;
    std::uint64_t flush_errors = 0;
    std::uint64_t shutdown_errors = 0;
};

struct LoggingStats {
    std::uint64_t accepted = 0;
    std::uint64_t dispatched = 0;
    std::uint64_t dropped_newest = 0;
    std::uint64_t dropped_oldest = 0;
    std::uint64_t rejected_after_stop = 0;
    std::uint64_t frontend_failures = 0;
    std::uint64_t recursive_fallbacks = 0;
    std::uint64_t queue_high_watermark = 0;
    LoggingBackendStats backend;
};

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_LOGGING_STATS_HPP_
