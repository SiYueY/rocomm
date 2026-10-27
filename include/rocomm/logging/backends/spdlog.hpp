#ifndef ROCOMM_LOGGING_BACKENDS_SPDLOG_HPP_
#define ROCOMM_LOGGING_BACKENDS_SPDLOG_HPP_

#include <memory>

#include <rocomm/result.hpp>
#include <rocomm/logging/logging_backend.hpp>
#include <rocomm/logging/text/text_backend_config.hpp>

namespace rocomm::logging::spdlog {

Result<std::unique_ptr<LoggingBackend>> create_backend(TextBackendConfig config);

}  // namespace rocomm::logging::spdlog

#endif  // ROCOMM_LOGGING_BACKENDS_SPDLOG_HPP_
