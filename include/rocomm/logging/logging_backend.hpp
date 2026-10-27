#ifndef ROCOMM_LOGGING_LOGGING_BACKEND_HPP_
#define ROCOMM_LOGGING_LOGGING_BACKEND_HPP_

#include <functional>
#include <memory>
#include <string_view>

#include <rocomm/result.hpp>
#include <rocomm/logging/log_record.hpp>
#include <rocomm/logging/logging_stats.hpp>

namespace rocomm::logging {

class LoggingBackend {
public:
    virtual ~LoggingBackend() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual Status initialize() noexcept = 0;
    virtual void write(const LogRecord& record) noexcept = 0;
    virtual Status flush() noexcept = 0;
    virtual Status shutdown() noexcept = 0;
    virtual LoggingBackendStats get_stats() const noexcept { return {}; }
};

using LoggingBackendFactory = std::function<Result<std::unique_ptr<LoggingBackend>>()>;

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_LOGGING_BACKEND_HPP_
