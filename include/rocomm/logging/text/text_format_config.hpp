#ifndef ROCOMM_LOGGING_TEXT_TEXT_FORMAT_CONFIG_HPP_
#define ROCOMM_LOGGING_TEXT_TEXT_FORMAT_CONFIG_HPP_

#include <cstdint>

namespace rocomm::logging {

enum class LogTimeZone : std::uint8_t {
    Local,
    Utc,
};

struct TextFormatConfig {
    LogTimeZone timezone = LogTimeZone::Local;
    bool include_timestamp = true;
    bool include_level = true;
    bool include_category = true;
    bool include_thread_id = true;
    bool include_source_location = true;
    bool include_function = false;
};

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_TEXT_TEXT_FORMAT_CONFIG_HPP_
