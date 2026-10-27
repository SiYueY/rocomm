#ifndef ROCOMM_LOGGING_TEXT_TEXT_BACKEND_CONFIG_HPP_
#define ROCOMM_LOGGING_TEXT_TEXT_BACKEND_CONFIG_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <rocomm/logging/log_level.hpp>
#include <rocomm/logging/text/text_format_config.hpp>

namespace rocomm::logging {

enum class ConsoleStream : std::uint8_t {
    Stdout,
    Stderr,
};

enum class ColorMode : std::uint8_t {
    Auto,
    Always,
    Never,
};

struct ConsoleOutputConfig {
    ConsoleStream stream = ConsoleStream::Stderr;
    ColorMode color = ColorMode::Auto;
    LogLevel min_level = LogLevel::Trace;
};

struct FileOutputConfig {
    std::string path;
    bool truncate = false;
    LogLevel min_level = LogLevel::Trace;
};

struct RotatingFileOutputConfig {
    std::string path;
    std::uint64_t max_file_size_bytes = 64U * 1024U * 1024U;
    std::size_t max_archived_files = 4;
    LogLevel min_level = LogLevel::Trace;
};

using TextOutputConfig =
    std::variant<ConsoleOutputConfig, FileOutputConfig, RotatingFileOutputConfig>;

struct TextBackendConfig {
    TextFormatConfig format;
    std::vector<TextOutputConfig> outputs;
};

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_TEXT_TEXT_BACKEND_CONFIG_HPP_
