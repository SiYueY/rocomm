#include "../support/check.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <rocomm/logging/logging_context.hpp>

namespace {

class MemoryBackend final : public rocomm::logging::LoggingBackend {
public:
    std::string_view name() const noexcept override { return "memory"; }

    rocomm::Status initialize() noexcept override {
        initialized_ = true;
        return {};
    }

    void write(const rocomm::logging::LogRecord& record) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.emplace_back(record.message());
        ++stats_.write_calls;
    }

    rocomm::Status flush() noexcept override { return {}; }

    rocomm::Status shutdown() noexcept override {
        shutdown_ = true;
        return {};
    }

    rocomm::logging::LoggingBackendStats get_stats() const noexcept override { return stats_; }

private:
    bool initialized_ = false;
    bool shutdown_ = false;
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
    rocomm::logging::LoggingBackendStats stats_;
};

rocomm::logging::LoggingBackendFactory make_backend_factory() {
    return [] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<MemoryBackend>());
    };
}

}  // namespace

int main() {
    rocomm::logging::LoggingConfig config;
    config.set_mode(rocomm::logging::LoggingMode::Asynchronous)
        .set_queue_capacity(2)
        .set_backend_factory(make_backend_factory());

    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.test");
    CHECK(logger_result.ok());
    auto logger = std::move(logger_result).value();

    logger.log(rocomm::logging::LogLevel::Info, "first");
    logger.log_lazy(rocomm::logging::LogLevel::Info, [] { return std::string("second"); });
    CHECK(context.flush(std::chrono::seconds(1)).ok());

    const auto stats = context.get_stats();
    CHECK(stats.accepted == 2);
    CHECK(stats.dispatched == 2);
    CHECK(context.shutdown(std::chrono::seconds(1)).ok());
    CHECK(!logger.enabled(rocomm::logging::LogLevel::Info));
    return 0;
}
