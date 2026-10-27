#ifndef ROCOMM_SRC_LOGGING_LOGGING_STATE_HPP_
#define ROCOMM_SRC_LOGGING_LOGGING_STATE_HPP_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <rocomm/logging/logger.hpp>
#include <rocomm/logging/log_record.hpp>
#include <rocomm/logging/logging_backend.hpp>
#include <rocomm/logging/logging_config.hpp>

namespace rocomm::logging {

enum class LoggingLifecycle : std::uint8_t {
    Running,
    Stopping,
    Stopped,
};

struct Logger::Category {
    explicit Category(std::string value, LogLevel level)
    : name(std::move(value)), effective_level(static_cast<std::uint8_t>(level)) {}

    std::string name;
    std::atomic<std::uint8_t> effective_level;
};

struct Logger::State : std::enable_shared_from_this<Logger::State> {
    struct WorkItem {
        std::uint64_t token = 0;
        LogRecord record;

        WorkItem(std::uint64_t value, LogRecord log_record) noexcept
        : token(value), record(std::move(log_record)) {}

        WorkItem(WorkItem&&) noexcept = default;
        WorkItem& operator=(WorkItem&&) noexcept = default;
        WorkItem(const WorkItem&) = delete;
        WorkItem& operator=(const WorkItem&) = delete;
    };

    State(
        bool configured_enabled, LoggingMode configured_mode, std::size_t configured_capacity,
        LogOverflowPolicy configured_overflow_policy, LogLevel configured_flush_on_level,
        LogLevel configured_default_level,
        LoggingBackendFactory configured_backend_factory) noexcept
    : enabled(configured_enabled),
      mode(configured_mode),
      capacity(configured_capacity),
      overflow_policy(configured_overflow_policy),
      flush_on_level(configured_flush_on_level),
      default_level(configured_default_level),
      backend_factory(std::move(configured_backend_factory)) {}

    bool enabled = false;
    LoggingMode mode = LoggingMode::Asynchronous;
    std::size_t capacity = 0;
    LogOverflowPolicy overflow_policy = LogOverflowPolicy::DropNewest;
    LogLevel flush_on_level = LogLevel::Off;
    LogLevel default_level = LogLevel::Info;
    LoggingBackendFactory backend_factory;
    std::unique_ptr<LoggingBackend> backend;

    std::mutex mutex;
    std::condition_variable work_cv;
    std::condition_variable completion_cv;
    LoggingLifecycle lifecycle = LoggingLifecycle::Running;
    std::atomic<std::uint8_t> lifecycle_fast{static_cast<std::uint8_t>(LoggingLifecycle::Running)};
    std::uint64_t next_token = 1;
    std::map<std::uint64_t, bool> pending;
    std::vector<std::optional<WorkItem>> queue;
    std::size_t queue_head = 0;
    std::size_t queue_tail = 0;
    std::size_t queue_size = 0;
    std::map<std::string, std::shared_ptr<Category>> categories;
    std::map<std::string, LogLevel> overrides;
    bool flush_requested = false;
    std::uint64_t flush_boundary = 0;
    std::uint64_t flushed_boundary = 0;
    Status flush_status;
    bool shutdown_requested = false;
    bool shutdown_complete = false;
    Status shutdown_status;

    std::mutex backend_mutex;
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> dispatched{0};
    std::atomic<std::uint64_t> dropped_newest{0};
    std::atomic<std::uint64_t> dropped_oldest{0};
    std::atomic<std::uint64_t> rejected_after_stop{0};
    std::atomic<std::uint64_t> frontend_failures{0};
    std::atomic<std::uint64_t> recursive_fallbacks{0};
    std::atomic<std::uint64_t> queue_high_watermark{0};

    std::uint64_t begin_admission() noexcept;
    void terminalize(std::uint64_t token) noexcept;
    bool pending_through_locked(std::uint64_t token) const noexcept;
    void worker_loop() noexcept;
    void write_record(LogRecord& record) noexcept;
    void execute_backend_flush() noexcept;
    void execute_backend_shutdown() noexcept;
    void update_queue_high_watermark(std::size_t size) noexcept;
    LogLevel resolve_level(std::string_view category) const noexcept;
};

bool category_is_valid(std::string_view category) noexcept;
void raw_log(std::string_view message) noexcept;

extern thread_local unsigned int backend_invocation_depth;
extern thread_local bool raw_fallback_active;

}  // namespace rocomm::logging

#endif  // ROCOMM_SRC_LOGGING_LOGGING_STATE_HPP_
