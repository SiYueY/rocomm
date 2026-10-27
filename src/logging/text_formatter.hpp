#ifndef ROCOMM_SRC_LOGGING_TEXT_FORMATTER_HPP_
#define ROCOMM_SRC_LOGGING_TEXT_FORMATTER_HPP_

#include <string>

#include <rocomm/logging/log_record.hpp>
#include <rocomm/logging/text/text_format_config.hpp>

namespace rocomm::logging {

class TextFormatter {
public:
    explicit TextFormatter(TextFormatConfig config) noexcept : config_(config) {}

    bool format(const LogRecord& record, std::string& output) noexcept;

private:
    TextFormatConfig config_;
};

}  // namespace rocomm::logging

#endif  // ROCOMM_SRC_LOGGING_TEXT_FORMATTER_HPP_
