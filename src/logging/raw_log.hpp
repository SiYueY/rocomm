#ifndef ROCOMM_SRC_LOGGING_RAW_LOG_HPP_
#define ROCOMM_SRC_LOGGING_RAW_LOG_HPP_

#include <string_view>

namespace rocomm::logging {

void raw_log(std::string_view message) noexcept;

}  // namespace rocomm::logging

#endif  // ROCOMM_SRC_LOGGING_RAW_LOG_HPP_
