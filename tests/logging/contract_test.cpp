#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include <rocomm/logging/logging_context.hpp>

namespace {

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

    std::string_view name() const noexcept override { return "blocking"; }
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

class RecursiveBackend final : public rocomm::logging::LoggingBackend {
public:
    explicit RecursiveBackend(std::shared_ptr<rocomm::logging::Logger*> logger_slot) noexcept
    : logger_slot_(std::move(logger_slot)) {}

    std::string_view name() const noexcept override { return "recursive"; }
    rocomm::Status initialize() noexcept override { return {}; }

    void write(const rocomm::logging::LogRecord&) noexcept override {
        ++writes_;
        if (*logger_slot_ != nullptr) {
            (*logger_slot_)->log(rocomm::logging::LogLevel::Trace, "recursive backend log");
        }
    }

    rocomm::Status flush() noexcept override { return {}; }
    rocomm::Status shutdown() noexcept override { return {}; }

private:
    std::shared_ptr<rocomm::logging::Logger*> logger_slot_;
    unsigned int writes_ = 0;
};

class NoopBackend final : public rocomm::logging::LoggingBackend {
public:
    std::string_view name() const noexcept override { return "noop"; }
    rocomm::Status initialize() noexcept override { return {}; }
    void write(const rocomm::logging::LogRecord&) noexcept override {}
    rocomm::Status flush() noexcept override { return {}; }
    rocomm::Status shutdown() noexcept override { return {}; }
};

void release_gate(const std::shared_ptr<Gate>& gate) {
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->released = true;
    }
    gate->release_cv.notify_all();
}

rocomm::logging::LoggingBackendFactory blocking_factory(const std::shared_ptr<Gate>& gate) {
    return [gate] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<BlockingBackend>(gate));
    };
}

void verify_disabled_lazy_path() {
    rocomm::logging::LoggingConfig config;
    config.set_enabled(false);
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    assert(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.disabled");
    assert(logger_result.ok());
    auto logger = std::move(logger_result).value();
    int builder_calls = 0;
    logger.log_lazy(rocomm::logging::LogLevel::Info, [&] {
        ++builder_calls;
        return "not evaluated";
    });
    assert(builder_calls == 0);
    assert(context.flush(std::chrono::milliseconds(-1)).ok());
    assert(context.shutdown(std::chrono::milliseconds(-1)).ok());
    assert(context.shutdown(std::chrono::milliseconds(1)).ok());
}

void verify_overflow_and_shutdown_rejection() {
    const auto gate = std::make_shared<Gate>();
    rocomm::logging::LoggingConfig config;
    config.set_queue_capacity(1)
        .set_overflow_policy(rocomm::logging::LogOverflowPolicy::DropNewest)
        .set_backend_factory(blocking_factory(gate));
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    assert(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.overflow");
    assert(logger_result.ok());
    auto logger = std::move(logger_result).value();

    logger.log(rocomm::logging::LogLevel::Info, "in flight");
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        gate->entered_cv.wait(lock, [&] { return gate->entered; });
    }
    logger.log(rocomm::logging::LogLevel::Info, "queued");
    logger.log(rocomm::logging::LogLevel::Info, "dropped");
    assert(context.get_stats().dropped_newest == 1);
    assert(context.shutdown(std::chrono::milliseconds(0)).code() == rocomm::StatusCode::Timeout);
    logger.log(rocomm::logging::LogLevel::Info, "after stop");
    assert(context.get_stats().rejected_after_stop == 1);

    release_gate(gate);
    assert(context.shutdown(std::chrono::seconds(1)).ok());
}

void verify_block_shutdown_cancellation() {
    const auto gate = std::make_shared<Gate>();
    rocomm::logging::LoggingConfig config;
    config.set_queue_capacity(1)
        .set_overflow_policy(rocomm::logging::LogOverflowPolicy::Block)
        .set_backend_factory(blocking_factory(gate));
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    assert(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.block");
    assert(logger_result.ok());
    const auto logger = std::move(logger_result).value();

    logger.log(rocomm::logging::LogLevel::Info, "in flight");
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        gate->entered_cv.wait(lock, [&] { return gate->entered; });
    }
    logger.log(rocomm::logging::LogLevel::Info, "queued");

    std::atomic<bool> producer_started{false};
    std::atomic<bool> producer_finished{false};
    std::thread producer([&] {
        producer_started.store(true, std::memory_order_release);
        logger.log(rocomm::logging::LogLevel::Info, "cancelled");
        producer_finished.store(true, std::memory_order_release);
    });
    while (!producer_started.load(std::memory_order_acquire)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!producer_finished.load(std::memory_order_acquire));
    assert(context.shutdown(std::chrono::milliseconds(0)).code() == rocomm::StatusCode::Timeout);
    producer.join();
    assert(producer_finished.load(std::memory_order_acquire));
    release_gate(gate);
    assert(context.shutdown(std::chrono::seconds(1)).ok());
    const auto stats = context.get_stats();
    assert(stats.accepted == 2);
    assert(stats.dispatched == 2);
    assert(stats.dropped_newest == 0);
    assert(stats.dropped_oldest == 0);
}

void verify_category_inheritance_and_context_isolation() {
    rocomm::logging::LoggingConfig first_config;
    first_config.set_mode(rocomm::logging::LoggingMode::Synchronous)
        .set_default_level(rocomm::logging::LogLevel::Warn)
        .set_category_level("rocomm.motion", rocomm::logging::LogLevel::Info)
        .set_backend_factory([] {
            return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
                std::make_unique<NoopBackend>());
        });
    auto first_result = rocomm::logging::LoggingContext::create(std::move(first_config));
    assert(first_result.ok());
    auto first = std::move(first_result).value();
    auto first_logger_result = first.get_logger("rocomm.motion.plan");
    assert(first_logger_result.ok());
    const auto first_logger = std::move(first_logger_result).value();
    assert(first_logger.enabled(rocomm::logging::LogLevel::Info));
    assert(!first_logger.enabled(rocomm::logging::LogLevel::Debug));

    rocomm::logging::LoggingConfig second_config;
    second_config.set_mode(rocomm::logging::LoggingMode::Synchronous)
        .set_default_level(rocomm::logging::LogLevel::Critical)
        .set_backend_factory([] {
            return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
                std::make_unique<NoopBackend>());
        });
    auto second_result = rocomm::logging::LoggingContext::create(std::move(second_config));
    assert(second_result.ok());
    auto second = std::move(second_result).value();
    auto second_logger_result = second.get_logger("rocomm.motion.plan");
    assert(second_logger_result.ok());
    const auto second_logger = std::move(second_logger_result).value();
    assert(!second_logger.enabled(rocomm::logging::LogLevel::Error));

    assert(first.set_category_level("rocomm", rocomm::logging::LogLevel::Trace).ok());
    assert(first_logger.enabled(rocomm::logging::LogLevel::Info));
    assert(first.clear_category_level("rocomm.motion").ok());
    assert(first_logger.enabled(rocomm::logging::LogLevel::Trace));
    assert(!second_logger.enabled(rocomm::logging::LogLevel::Error));
    assert(first.shutdown(std::chrono::seconds(1)).ok());
    assert(second.shutdown(std::chrono::seconds(1)).ok());
}

void verify_factory_failure_boundary() {
    rocomm::logging::LoggingConfig failing_config;
    failing_config.set_backend_factory([] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            rocomm::Status(rocomm::StatusCode::ResourceExhausted));
    });
    auto failed_context = rocomm::logging::LoggingContext::create(std::move(failing_config));
    assert(!failed_context.ok());
    assert(failed_context.status().code() == rocomm::StatusCode::ResourceExhausted);

    rocomm::logging::LoggingConfig null_config;
    null_config.set_backend_factory([] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::unique_ptr<rocomm::logging::LoggingBackend>());
    });
    auto null_context = rocomm::logging::LoggingContext::create(std::move(null_config));
    assert(!null_context.ok());
    assert(null_context.status().code() == rocomm::StatusCode::InternalError);

    rocomm::logging::LoggingConfig oversized_config;
    oversized_config.set_queue_capacity(std::numeric_limits<std::size_t>::max())
        .set_backend_factory([] {
            return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
                std::make_unique<NoopBackend>());
        });
    auto oversized_context = rocomm::logging::LoggingContext::create(std::move(oversized_config));
    assert(!oversized_context.ok());
    assert(oversized_context.status().code() == rocomm::StatusCode::ResourceExhausted);
}

void verify_backend_recursion_fallback() {
    const auto logger_slot = std::make_shared<rocomm::logging::Logger*>(nullptr);
    rocomm::logging::LoggingConfig config;
    config.set_backend_factory([logger_slot] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<RecursiveBackend>(logger_slot));
    });
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    assert(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.recursive");
    assert(logger_result.ok());
    auto logger = std::move(logger_result).value();
    *logger_slot = &logger;
    logger.log(rocomm::logging::LogLevel::Info, "outer record");
    assert(context.flush(std::chrono::seconds(1)).ok());
    assert(context.get_stats().recursive_fallbacks == 1);
    assert(context.shutdown(std::chrono::seconds(1)).ok());
    *logger_slot = nullptr;
}

void verify_admission_shutdown_race() {
    rocomm::logging::LoggingConfig config;
    config.set_queue_capacity(64).set_backend_factory([] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<NoopBackend>());
    });
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    assert(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.race");
    assert(logger_result.ok());
    const auto logger = std::move(logger_result).value();

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::vector<std::thread> producers;
    for (int index = 0; index < 4; ++index) {
        producers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
            }
            while (!stop.load(std::memory_order_acquire)) {
                logger.log(rocomm::logging::LogLevel::Info, "race record");
            }
        });
    }
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(context.shutdown(std::chrono::seconds(1)).ok());
    stop.store(true, std::memory_order_release);
    for (std::thread& producer : producers) {
        producer.join();
    }
    const auto stats = context.get_stats();
    assert(stats.accepted == stats.dispatched);
}

}  // namespace

int main() {
    verify_disabled_lazy_path();
    verify_overflow_and_shutdown_rejection();
    verify_block_shutdown_cancellation();
    verify_factory_failure_boundary();
    verify_backend_recursion_fallback();
    verify_admission_shutdown_race();
    verify_category_inheritance_and_context_isolation();
    return 0;
}
