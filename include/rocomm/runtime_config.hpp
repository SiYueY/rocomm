#ifndef ROCOMM_RUNTIME_CONFIG_HPP_
#define ROCOMM_RUNTIME_CONFIG_HPP_

#include <utility>

#include <rocomm/time.hpp>
#include <rocomm/logging/logging_config.hpp>

namespace rocomm {

class RuntimeConfig {
public:
    RuntimeConfig& set_logging(logging::LoggingConfig config) {
        logging_config_ = std::move(config);
        return *this;
    }

    RuntimeConfig& set_shutdown_timeout(Duration timeout) noexcept {
        shutdown_timeout_ = timeout;
        return *this;
    }

private:
    friend class Runtime;

    logging::LoggingConfig logging_config_;
    Duration shutdown_timeout_ = Duration::zero();
};

}  // namespace rocomm

#endif  // ROCOMM_RUNTIME_CONFIG_HPP_
