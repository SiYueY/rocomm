#include <atomic>
#include "../support/check.hpp"
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

class CountingBackend final : public rocomm::logging::LoggingBackend {
public:
    explicit CountingBackend(std::shared_ptr<std::atomic<int>> flush_count) noexcept
    : flush_count_(std::move(flush_count)) {}

    std::string_view name() const noexcept override { return "counting"; }
    rocomm::Status initialize() noexcept override { return {}; }
    void write(const rocomm::logging::LogRecord&) noexcept override {}
    rocomm::Status flush() noexcept override {
        flush_count_->fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    rocomm::Status shutdown() noexcept override { return {}; }

private:
    std::shared_ptr<std::atomic<int>> flush_count_;
};

class FailingFlushBackend final : public rocomm::logging::LoggingBackend {
public:
    std::string_view name() const noexcept override { return "failing-flush"; }
    rocomm::Status initialize() noexcept override { return {}; }
    void write(const rocomm::logging::LogRecord&) noexcept override {}
    rocomm::Status flush() noexcept override {
        return rocomm::Status(rocomm::StatusCode::InternalError);
    }
    rocomm::Status shutdown() noexcept override { return {}; }
};

void verify_flush_error_propagation() {
    rocomm::logging::LoggingConfig config;
    config.set_backend_factory([] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<FailingFlushBackend>());
    });
    auto result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(result.ok());
    auto context = std::move(result).value();
    CHECK(context.flush(std::chrono::seconds(1)).code() == rocomm::StatusCode::InternalError);
    CHECK(context.shutdown(std::chrono::seconds(1)).code() == rocomm::StatusCode::InternalError);
    CHECK(context.shutdown(std::chrono::seconds(1)).code() == rocomm::StatusCode::InternalError);
}

class BlockingFlushBackend final : public rocomm::logging::LoggingBackend {
public:
    explicit BlockingFlushBackend(std::shared_ptr<Gate> gate) noexcept : gate_(std::move(gate)) {}
    std::string_view name() const noexcept override { return "blocking-flush"; }
    rocomm::Status initialize() noexcept override { return {}; }
    void write(const rocomm::logging::LogRecord&) noexcept override {}
    rocomm::Status flush() noexcept override {
        std::unique_lock<std::mutex> lock(gate_->mutex);
        gate_->entered = true;
        gate_->entered_cv.notify_all();
        gate_->release_cv.wait(lock, [this] { return gate_->released; });
        return {};
    }
    rocomm::Status shutdown() noexcept override { return {}; }

private:
    std::shared_ptr<Gate> gate_;
};

void verify_flush_wait_timeout_and_recovery() {
    const auto gate = std::make_shared<Gate>();
    rocomm::logging::LoggingConfig config;
    config.set_backend_factory([gate] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<BlockingFlushBackend>(gate));
    });
    auto result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(result.ok());
    auto context = std::move(result).value();
    CHECK(context.flush(std::chrono::milliseconds(5)).code() == rocomm::StatusCode::Timeout);
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        CHECK(gate->entered_cv.wait_for(
            lock, std::chrono::seconds(1), [&] { return gate->entered; }));
        gate->released = true;
    }
    gate->release_cv.notify_all();
    CHECK(context.flush(std::chrono::seconds(1)).ok());
    CHECK(context.shutdown(std::chrono::seconds(1)).ok());
}

void verify_invalid_mode_rejected() {
    rocomm::logging::LoggingConfig config;
    config.set_mode(static_cast<rocomm::logging::LoggingMode>(255));
    CHECK(
        rocomm::logging::LoggingContext::create(std::move(config)).status().code() ==
        rocomm::StatusCode::InvalidArgument);
}

void verify_individual_flush_completion() {
    const auto flush_count = std::make_shared<std::atomic<int>>(0);
    rocomm::logging::LoggingConfig config;
    config.set_backend_factory([flush_count] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<CountingBackend>(flush_count));
    });
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(context_result.ok());
    auto context = std::move(context_result).value();

    // An empty prefix still requires an actual backend flush for each call.
    CHECK(context.flush(std::chrono::seconds(1)).ok());
    CHECK(flush_count->load(std::memory_order_relaxed) == 1);
    CHECK(context.flush(std::chrono::seconds(1)).ok());
    CHECK(flush_count->load(std::memory_order_relaxed) == 2);

    std::atomic<bool> go{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if (!context.flush(std::chrono::seconds(2)).ok()) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK(failures.load(std::memory_order_relaxed) == 0);
    CHECK(flush_count->load(std::memory_order_relaxed) == 10);
    CHECK(context.shutdown(std::chrono::seconds(2)).ok());
}

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
    CHECK(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.disabled");
    CHECK(logger_result.ok());
    auto logger = std::move(logger_result).value();
    int builder_calls = 0;
    logger.log_lazy(rocomm::logging::LogLevel::Info, [&] {
        ++builder_calls;
        return "not evaluated";
    });
    CHECK(builder_calls == 0);
    CHECK(context.flush(std::chrono::milliseconds(-1)).ok());
    CHECK(context.shutdown(std::chrono::milliseconds(-1)).ok());
    CHECK(context.shutdown(std::chrono::milliseconds(1)).ok());
}

void verify_overflow_and_shutdown_rejection() {
    const auto gate = std::make_shared<Gate>();
    rocomm::logging::LoggingConfig config;
    config.set_queue_capacity(1)
        .set_overflow_policy(rocomm::logging::LogOverflowPolicy::DropNewest)
        .set_backend_factory(blocking_factory(gate));
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.overflow");
    CHECK(logger_result.ok());
    auto logger = std::move(logger_result).value();

    logger.log(rocomm::logging::LogLevel::Info, "in flight");
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        gate->entered_cv.wait(lock, [&] { return gate->entered; });
    }
    logger.log(rocomm::logging::LogLevel::Info, "queued");
    logger.log(rocomm::logging::LogLevel::Info, "dropped");
    CHECK(context.get_stats().dropped_newest == 1);
    CHECK(context.shutdown(std::chrono::milliseconds(0)).code() == rocomm::StatusCode::Timeout);
    logger.log(rocomm::logging::LogLevel::Info, "after stop");
    CHECK(context.get_stats().rejected_after_stop == 1);

    release_gate(gate);
    CHECK(context.shutdown(std::chrono::seconds(1)).ok());
}

void verify_block_shutdown_cancellation() {
    const auto gate = std::make_shared<Gate>();
    rocomm::logging::LoggingConfig config;
    config.set_queue_capacity(1)
        .set_overflow_policy(rocomm::logging::LogOverflowPolicy::Block)
        .set_backend_factory(blocking_factory(gate));
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.block");
    CHECK(logger_result.ok());
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
    CHECK(!producer_finished.load(std::memory_order_acquire));
    CHECK(context.shutdown(std::chrono::milliseconds(0)).code() == rocomm::StatusCode::Timeout);
    producer.join();
    CHECK(producer_finished.load(std::memory_order_acquire));
    release_gate(gate);
    CHECK(context.shutdown(std::chrono::seconds(1)).ok());
    const auto stats = context.get_stats();
    CHECK(stats.accepted == 2);
    CHECK(stats.dispatched == 2);
    CHECK(stats.dropped_newest == 0);
    CHECK(stats.dropped_oldest == 0);
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
    CHECK(first_result.ok());
    auto first = std::move(first_result).value();
    auto first_logger_result = first.get_logger("rocomm.motion.plan");
    CHECK(first_logger_result.ok());
    const auto first_logger = std::move(first_logger_result).value();
    CHECK(first_logger.enabled(rocomm::logging::LogLevel::Info));
    CHECK(!first_logger.enabled(rocomm::logging::LogLevel::Debug));

    rocomm::logging::LoggingConfig second_config;
    second_config.set_mode(rocomm::logging::LoggingMode::Synchronous)
        .set_default_level(rocomm::logging::LogLevel::Critical)
        .set_backend_factory([] {
            return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
                std::make_unique<NoopBackend>());
        });
    auto second_result = rocomm::logging::LoggingContext::create(std::move(second_config));
    CHECK(second_result.ok());
    auto second = std::move(second_result).value();
    auto second_logger_result = second.get_logger("rocomm.motion.plan");
    CHECK(second_logger_result.ok());
    const auto second_logger = std::move(second_logger_result).value();
    CHECK(!second_logger.enabled(rocomm::logging::LogLevel::Error));

    CHECK(first.set_category_level("rocomm", rocomm::logging::LogLevel::Trace).ok());
    CHECK(first_logger.enabled(rocomm::logging::LogLevel::Info));
    CHECK(first.clear_category_level("rocomm.motion").ok());
    CHECK(first_logger.enabled(rocomm::logging::LogLevel::Trace));
    CHECK(!second_logger.enabled(rocomm::logging::LogLevel::Error));
    CHECK(first.shutdown(std::chrono::seconds(1)).ok());
    CHECK(second.shutdown(std::chrono::seconds(1)).ok());
}

void verify_factory_failure_boundary() {
    rocomm::logging::LoggingConfig failing_config;
    failing_config.set_backend_factory([] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            rocomm::Status(rocomm::StatusCode::ResourceExhausted));
    });
    auto failed_context = rocomm::logging::LoggingContext::create(std::move(failing_config));
    CHECK(!failed_context.ok());
    CHECK(failed_context.status().code() == rocomm::StatusCode::ResourceExhausted);

    rocomm::logging::LoggingConfig null_config;
    null_config.set_backend_factory([] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::unique_ptr<rocomm::logging::LoggingBackend>());
    });
    auto null_context = rocomm::logging::LoggingContext::create(std::move(null_config));
    CHECK(!null_context.ok());
    CHECK(null_context.status().code() == rocomm::StatusCode::InternalError);

    rocomm::logging::LoggingConfig oversized_config;
    oversized_config.set_queue_capacity(std::numeric_limits<std::size_t>::max())
        .set_backend_factory([] {
            return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
                std::make_unique<NoopBackend>());
        });
    auto oversized_context = rocomm::logging::LoggingContext::create(std::move(oversized_config));
    CHECK(!oversized_context.ok());
    CHECK(oversized_context.status().code() == rocomm::StatusCode::ResourceExhausted);
}

void verify_backend_recursion_fallback() {
    const auto logger_slot = std::make_shared<rocomm::logging::Logger*>(nullptr);
    rocomm::logging::LoggingConfig config;
    config.set_backend_factory([logger_slot] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<RecursiveBackend>(logger_slot));
    });
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.recursive");
    CHECK(logger_result.ok());
    auto logger = std::move(logger_result).value();
    *logger_slot = &logger;
    logger.log(rocomm::logging::LogLevel::Info, "outer record");
    CHECK(context.flush(std::chrono::seconds(1)).ok());
    CHECK(context.get_stats().recursive_fallbacks == 1);
    CHECK(context.shutdown(std::chrono::seconds(1)).ok());
    *logger_slot = nullptr;
}

void verify_admission_shutdown_race() {
    rocomm::logging::LoggingConfig config;
    config.set_queue_capacity(64).set_backend_factory([] {
        return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
            std::make_unique<NoopBackend>());
    });
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.race");
    CHECK(logger_result.ok());
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
    CHECK(context.shutdown(std::chrono::seconds(1)).ok());
    stop.store(true, std::memory_order_release);
    for (std::thread& producer : producers) {
        producer.join();
    }
    const auto stats = context.get_stats();
    CHECK(stats.accepted == stats.dispatched);
}

}  // namespace

int main() {
    verify_individual_flush_completion();
    verify_flush_error_propagation();
    verify_flush_wait_timeout_and_recovery();
    verify_invalid_mode_rejected();
    verify_disabled_lazy_path();
    verify_overflow_and_shutdown_rejection();
    verify_block_shutdown_cancellation();
    verify_factory_failure_boundary();
    verify_backend_recursion_fallback();
    verify_admission_shutdown_race();
    verify_category_inheritance_and_context_isolation();
    return 0;
}
