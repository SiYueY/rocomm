#include <cassert>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string_view>

#include <rocomm/runtime.hpp>
#include <rocomm/logging/macros.hpp>

namespace {

class NullBackend final : public rocomm::logging::LoggingBackend {
public:
    std::string_view name() const noexcept override { return "null"; }
    rocomm::Status initialize() noexcept override { return {}; }
    void write(const rocomm::logging::LogRecord&) noexcept override {}
    rocomm::Status flush() noexcept override { return {}; }
    rocomm::Status shutdown() noexcept override { return {}; }
};

struct Gate {
    std::mutex mutex;
    std::condition_variable entered_cv;
    std::condition_variable release_cv;
    bool entered = false;
    bool released = false;
};

class BlockingBackend final : public rocomm::logging::LoggingBackend {
public:
    explicit BlockingBackend(std::shared_ptr<Gate> gate) noexcept : gate_(std::move(gate)) {}

    std::string_view name() const noexcept override { return "runtime-blocking"; }
    rocomm::Status initialize() noexcept override { return {}; }
    void write(const rocomm::logging::LogRecord&) noexcept override {
        std::unique_lock<std::mutex> lock(gate_->mutex);
        gate_->entered = true;
        gate_->entered_cv.notify_all();
        gate_->release_cv.wait(lock, [this] { return gate_->released; });
    }
    rocomm::Status flush() noexcept override { return {}; }
    rocomm::Status shutdown() noexcept override { return {}; }

private:
    std::shared_ptr<Gate> gate_;
};

void verify_stop_can_rewait_after_timeout() {
    const auto gate = std::make_shared<Gate>();
    rocomm::RuntimeConfig config;
    rocomm::logging::LoggingConfig logging_config;
    logging_config.set_backend_factory([gate] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<BlockingBackend>(gate));
    });
    config.set_logging(std::move(logging_config))
        .set_shutdown_timeout(std::chrono::milliseconds(10));
    auto runtime_result = rocomm::Runtime::create(std::move(config));
    assert(runtime_result.ok());
    auto runtime = std::move(runtime_result).value();
    auto logger_result = runtime.get_logger("rocomm.runtime.timeout");
    assert(logger_result.ok());
    auto logger = std::move(logger_result).value();
    logger.log(rocomm::logging::LogLevel::Info, "blocked");
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        gate->entered_cv.wait(lock, [&] { return gate->entered; });
    }
    assert(runtime.stop().code() == rocomm::StatusCode::Timeout);
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->released = true;
    }
    gate->release_cv.notify_all();
    assert(runtime.stop().ok());
}

}  // namespace

int main() {
    verify_stop_can_rewait_after_timeout();
    rocomm::RuntimeConfig invalid_config;
    invalid_config.set_shutdown_timeout(std::chrono::milliseconds(-1));
    assert(!rocomm::Runtime::create(std::move(invalid_config)).ok());

    rocomm::RuntimeConfig config;
    rocomm::logging::LoggingConfig logging_config;
    logging_config.set_backend_factory([] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<NullBackend>());
    });
    config.set_logging(std::move(logging_config)).set_shutdown_timeout(std::chrono::seconds(1));

    auto runtime_result = rocomm::Runtime::create(std::move(config));
    assert(runtime_result.ok());
    auto runtime = std::move(runtime_result).value();
    auto logger_result = runtime.get_logger("rocomm.runtime");
    assert(logger_result.ok());
    auto logger = std::move(logger_result).value();
    int message_evaluations = 0;
    ROCOMM_LOG_INFO(logger, (++message_evaluations, "macro expression"));
    assert(runtime.stop().ok());
    assert(!runtime.get_logger("rocomm.after_stop").ok());

#ifdef ROCOMM_HAS_SPDLOG_BACKEND
    rocomm::RuntimeConfig default_config;
    default_config.set_shutdown_timeout(std::chrono::seconds(1));
    auto default_runtime_result = rocomm::Runtime::create(std::move(default_config));
    assert(default_runtime_result.ok());
    auto default_runtime = std::move(default_runtime_result).value();
    assert(default_runtime.stop().ok());
#else
    rocomm::RuntimeConfig default_config;
    auto default_runtime_result = rocomm::Runtime::create(std::move(default_config));
    assert(!default_runtime_result.ok());
    assert(default_runtime_result.status().code() == rocomm::StatusCode::UnsupportedConfiguration);
#endif
    return 0;
}
