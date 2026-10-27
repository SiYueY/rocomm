#include <rocomm/logging/logging_context.hpp>

#ifdef ROCOMM_HAS_SPDLOG_BACKEND
#include <rocomm/logging/backends/spdlog.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "logging_state.hpp"

namespace rocomm::logging {

thread_local unsigned int backend_invocation_depth = 0;
thread_local bool raw_fallback_active = false;

namespace {

class BackendInvocationGuard {
public:
    BackendInvocationGuard() noexcept { ++backend_invocation_depth; }
    ~BackendInvocationGuard() { --backend_invocation_depth; }
};

class RawFallbackGuard {
public:
    RawFallbackGuard() noexcept : active_(!raw_fallback_active) {
        if (active_) {
            raw_fallback_active = true;
        }
    }

    ~RawFallbackGuard() {
        if (active_) {
            raw_fallback_active = false;
        }
    }

    bool active() const noexcept { return active_; }

private:
    bool active_ = false;
};

bool is_threshold_level(LogLevel level) noexcept { return is_valid_log_level(level); }

std::chrono::steady_clock::time_point deadline_from(Duration timeout) noexcept {
    const auto now = std::chrono::steady_clock::now();
    const auto maximum = std::chrono::steady_clock::time_point::max();
    if (timeout.count() > 0 && maximum - now < timeout) {
        return maximum;
    }
    return now + timeout;
}

void emit_frontend_fallback(LogLevel level, std::string_view message) noexcept {
    if (level < LogLevel::Error) {
        return;
    }
    RawFallbackGuard guard;
    if (guard.active()) {
        raw_log(message);
    }
}

void emit_recursive_fallback(std::string_view message) noexcept {
    if (raw_fallback_active) {
        raw_log(message);
        return;
    }
    RawFallbackGuard guard;
    if (guard.active()) {
        raw_log(message);
    }
}

}  // namespace

bool category_is_valid(std::string_view category) noexcept {
    if (category.empty() || category.front() == '.' || category.back() == '.') {
        return false;
    }
    bool previous_dot = false;
    for (const char character_value : category) {
        const unsigned char character = static_cast<unsigned char>(character_value);
        const bool valid = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') || character == '_' ||
                           character == '-' || character == '.';
        if (!valid || (character == '.' && previous_dot)) {
            return false;
        }
        previous_dot = character == '.';
    }
    return true;
}

LogLevel Logger::State::resolve_level(std::string_view category) const noexcept {
    LogLevel result = default_level;
    std::string_view candidate = category;
    while (true) {
        for (const auto& override : overrides) {
            if (override.first == candidate) {
                return override.second;
            }
        }
        const std::size_t dot = candidate.rfind('.');
        if (dot == std::string_view::npos) {
            return result;
        }
        candidate = candidate.substr(0, dot);
    }
}

std::uint64_t Logger::State::begin_admission() noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    if (lifecycle != LoggingLifecycle::Running) {
        rejected_after_stop.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    try {
        const std::uint64_t token = next_token++;
        pending.emplace(token, true);
        return token;
    } catch (...) {
        frontend_failures.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
}

void Logger::State::terminalize(std::uint64_t token) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending.erase(token);
    }
    completion_cv.notify_all();
    work_cv.notify_all();
}

bool Logger::State::pending_through_locked(std::uint64_t token) const noexcept {
    const auto first = pending.begin();
    return first != pending.end() && first->first <= token;
}

void Logger::State::update_queue_high_watermark(std::size_t size) noexcept {
    std::uint64_t observed = queue_high_watermark.load(std::memory_order_relaxed);
    const std::uint64_t value = static_cast<std::uint64_t>(size);
    while (observed < value &&
           !queue_high_watermark.compare_exchange_weak(
               observed, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

void Logger::State::write_record(LogRecord& record) noexcept {
    std::lock_guard<std::mutex> backend_lock(backend_mutex);
    BackendInvocationGuard guard;
    backend->write(record);
    dispatched.fetch_add(1, std::memory_order_relaxed);
    if (flush_on_level != LogLevel::Off && record.level() >= flush_on_level) {
        (void)backend->flush();
    }
}

void Logger::State::execute_backend_flush() noexcept {
    std::lock_guard<std::mutex> backend_lock(backend_mutex);
    BackendInvocationGuard guard;
    flush_status = backend->flush();
}

void Logger::State::execute_backend_shutdown() noexcept {
    Status flush_result;
    Status shutdown_result;
    {
        std::lock_guard<std::mutex> backend_lock(backend_mutex);
        BackendInvocationGuard guard;
        flush_result = backend->flush();
        shutdown_result = backend->shutdown();
    }
    std::lock_guard<std::mutex> lock(mutex);
    shutdown_status = !flush_result.ok() ? flush_result : shutdown_result;
    shutdown_complete = true;
    lifecycle = LoggingLifecycle::Stopped;
    lifecycle_fast.store(
        static_cast<std::uint8_t>(LoggingLifecycle::Stopped), std::memory_order_release);
    completion_cv.notify_all();
}

void Logger::State::worker_loop() noexcept {
    while (true) {
        std::optional<WorkItem> work;
        bool do_flush = false;
        bool do_shutdown = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            work_cv.wait(lock, [this] {
                return queue_size != 0U ||
                       (flush_requested && !pending_through_locked(flush_boundary)) ||
                       (shutdown_requested && pending.empty());
            });
            if (queue_size != 0U) {
                work.emplace(std::move(*queue[queue_head]));
                queue[queue_head].reset();
                queue_head = (queue_head + 1U) % capacity;
                --queue_size;
                work_cv.notify_all();
            } else if (shutdown_requested && pending.empty()) {
                do_shutdown = true;
            } else if (flush_requested && !pending_through_locked(flush_boundary)) {
                do_flush = true;
                flush_requested = false;
            }
        }

        if (work) {
            write_record(work->record);
            terminalize(work->token);
            continue;
        }
        if (do_flush) {
            execute_backend_flush();
            {
                std::lock_guard<std::mutex> lock(mutex);
                flushed_boundary = flush_boundary;
            }
            completion_cv.notify_all();
            continue;
        }
        if (do_shutdown) {
            execute_backend_shutdown();
            return;
        }
    }
}

bool Logger::enabled(LogLevel level) const noexcept {
    if (!state_ || !category_ || !state_->enabled || !is_record_level(level)) {
        return false;
    }
    if (static_cast<LoggingLifecycle>(state_->lifecycle_fast.load(std::memory_order_acquire)) !=
        LoggingLifecycle::Running) {
        return false;
    }
    const LogLevel threshold =
        static_cast<LogLevel>(category_->effective_level.load(std::memory_order_relaxed));
    return is_enabled(level, threshold);
}

bool Logger::is_recursing() const noexcept {
    return state_ && state_->enabled && backend_invocation_depth != 0U;
}

bool Logger::begin_raw_fallback() const noexcept {
    if (raw_fallback_active) {
        return false;
    }
    raw_fallback_active = true;
    return true;
}

void Logger::end_raw_fallback() const noexcept { raw_fallback_active = false; }

void Logger::log_recursive(LogLevel level, std::string_view message) const noexcept {
    if (!state_ || !state_->enabled) {
        return;
    }
    state_->recursive_fallbacks.fetch_add(1, std::memory_order_relaxed);
    (void)level;
    emit_recursive_fallback(message);
}

std::string_view Logger::category() const noexcept {
    return category_ ? std::string_view(category_->name) : std::string_view();
}

void Logger::log_builder_failure(LogLevel level) const noexcept {
    if (!state_ || !state_->enabled) {
        return;
    }
    state_->frontend_failures.fetch_add(1, std::memory_order_relaxed);
    emit_frontend_fallback(level, "RoComm logging lazy builder failed\n");
}

void Logger::log_null_builder_result(LogLevel level) const noexcept {
    if (!state_ || !state_->enabled) {
        return;
    }
    state_->frontend_failures.fetch_add(1, std::memory_order_relaxed);
    emit_frontend_fallback(level, "RoComm logging lazy builder returned null\n");
}

void Logger::log(LogLevel level, std::string_view message, SourceLocation source) const noexcept {
    if (!state_ || !category_ || !state_->enabled) {
        return;
    }
    if (backend_invocation_depth != 0U) {
        log_recursive(level, message);
        return;
    }
    if (!is_record_level(level)) {
        state_->frontend_failures.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (static_cast<LoggingLifecycle>(state_->lifecycle_fast.load(std::memory_order_acquire)) ==
        LoggingLifecycle::Running) {
        const LogLevel threshold =
            static_cast<LogLevel>(category_->effective_level.load(std::memory_order_relaxed));
        if (!is_enabled(level, threshold)) {
            return;
        }
    }
    const std::uint64_t token = state_->begin_admission();
    if (token == 0U) {
        return;
    }
    try {
        submit_admitted(level, std::string(message), source, token);
    } catch (...) {
        state_->frontend_failures.fetch_add(1, std::memory_order_relaxed);
        state_->terminalize(token);
        emit_frontend_fallback(level, "RoComm logging message allocation failed\n");
    }
}

void Logger::log_owned(LogLevel level, std::string message, SourceLocation source) const noexcept {
    if (!state_ || !category_ || !state_->enabled) {
        return;
    }
    if (backend_invocation_depth != 0U) {
        log_recursive(level, message);
        return;
    }
    if (!is_record_level(level)) {
        state_->frontend_failures.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (static_cast<LoggingLifecycle>(state_->lifecycle_fast.load(std::memory_order_acquire)) ==
        LoggingLifecycle::Running) {
        const LogLevel threshold =
            static_cast<LogLevel>(category_->effective_level.load(std::memory_order_relaxed));
        if (!is_enabled(level, threshold)) {
            return;
        }
    }
    const std::uint64_t token = state_->begin_admission();
    if (token == 0U) {
        return;
    }
    try {
        submit_admitted(level, std::move(message), source, token);
    } catch (...) {
        state_->frontend_failures.fetch_add(1, std::memory_order_relaxed);
        state_->terminalize(token);
        emit_frontend_fallback(level, "RoComm logging message allocation failed\n");
    }
}

void Logger::submit_admitted(
    LogLevel level, std::string message, SourceLocation source,
    std::uint64_t token) const noexcept {
    try {
        LogRecord record(
            std::chrono::system_clock::now(), level, category_->name, std::move(message), source,
            std::this_thread::get_id());
        if (state_->mode == LoggingMode::Synchronous) {
            state_->accepted.fetch_add(1, std::memory_order_relaxed);
            state_->write_record(record);
            state_->terminalize(token);
            return;
        }
        bool dropped_oldest = false;
        LogLevel dropped_level = LogLevel::Trace;
        std::unique_lock<std::mutex> lock(state_->mutex);
        if (state_->overflow_policy == LogOverflowPolicy::Block) {
            state_->work_cv.wait(lock, [this] {
                return state_->queue_size < state_->capacity ||
                       state_->lifecycle != LoggingLifecycle::Running;
            });
            if (state_->lifecycle != LoggingLifecycle::Running) {
                lock.unlock();
                state_->terminalize(token);
                emit_frontend_fallback(level, "RoComm logging blocked record cancelled\n");
                return;
            }
        } else if (state_->queue_size >= state_->capacity) {
            if (state_->overflow_policy == LogOverflowPolicy::DropNewest) {
                state_->dropped_newest.fetch_add(1, std::memory_order_relaxed);
                lock.unlock();
                state_->terminalize(token);
                emit_frontend_fallback(level, "RoComm logging record dropped\n");
                return;
            }
            Logger::State::WorkItem dropped = std::move(*state_->queue[state_->queue_head]);
            state_->queue[state_->queue_head].reset();
            state_->queue_head = (state_->queue_head + 1U) % state_->capacity;
            --state_->queue_size;
            state_->dropped_oldest.fetch_add(1, std::memory_order_relaxed);
            state_->pending.erase(dropped.token);
            dropped_oldest = true;
            dropped_level = dropped.record.level();
        }
        state_->queue[state_->queue_tail].emplace(token, std::move(record));
        state_->queue_tail = (state_->queue_tail + 1U) % state_->capacity;
        ++state_->queue_size;
        state_->accepted.fetch_add(1, std::memory_order_relaxed);
        state_->update_queue_high_watermark(state_->queue_size);
        lock.unlock();
        if (dropped_oldest) {
            state_->completion_cv.notify_all();
            emit_frontend_fallback(dropped_level, "RoComm logging oldest record dropped\n");
        }
        state_->work_cv.notify_all();
    } catch (...) {
        state_->frontend_failures.fetch_add(1, std::memory_order_relaxed);
        state_->terminalize(token);
        emit_frontend_fallback(level, "RoComm logging record construction failed\n");
    }
}

LoggingContext::LoggingContext(LoggingContext&& other) noexcept : state_(std::move(other.state_)) {}

LoggingContext& LoggingContext::operator=(LoggingContext&& other) noexcept {
    if (this != &other) {
        (void)shutdown(Duration::zero());
        state_ = std::move(other.state_);
    }
    return *this;
}

LoggingContext::~LoggingContext() noexcept { (void)shutdown(Duration::zero()); }

Result<LoggingContext> LoggingContext::create(LoggingConfig config) {
    if (!is_threshold_level(config.default_level_) || !is_threshold_level(config.flush_on_level_) ||
        (config.mode_ == LoggingMode::Asynchronous && config.queue_capacity_ == 0U)) {
        return Status(StatusCode::InvalidArgument);
    }
    for (const auto& entry : config.category_levels_) {
        if (!category_is_valid(entry.first) || !is_threshold_level(entry.second)) {
            return Status(StatusCode::InvalidArgument);
        }
    }

    try {
        auto state = std::make_shared<Logger::State>(
            config.enabled_, config.mode_, config.queue_capacity_, config.overflow_policy_,
            config.flush_on_level_, config.default_level_, std::move(config.backend_factory_));
        for (const auto& entry : config.category_levels_) {
            state->overrides.emplace(entry);
        }
        if (!state->enabled) {
            return LoggingContext(std::move(state));
        }
#ifdef ROCOMM_HAS_SPDLOG_BACKEND
        if (!state->backend_factory) {
            TextBackendConfig backend_config;
            backend_config.outputs.emplace_back(ConsoleOutputConfig{});
            state->backend_factory = [backend_config] {
                return spdlog::create_backend(backend_config);
            };
        }
#endif
        if (!state->backend_factory) {
            return Status(StatusCode::UnsupportedConfiguration);
        }
        Result<std::unique_ptr<LoggingBackend>> backend_result{Status(StatusCode::InternalError)};
        try {
            backend_result = state->backend_factory();
        } catch (const std::bad_alloc&) {
            return Status(StatusCode::ResourceExhausted);
        } catch (...) {
            return Status(StatusCode::InternalError);
        }
        if (!backend_result.ok()) {
            return backend_result.status();
        }
        state->backend = std::move(backend_result).value();
        if (!state->backend) {
            return Status(StatusCode::InternalError);
        }
        {
            std::lock_guard<std::mutex> backend_lock(state->backend_mutex);
            BackendInvocationGuard guard;
            const Status status = state->backend->initialize();
            if (!status.ok()) {
                (void)state->backend->shutdown();
                return status;
            }
        }
        if (state->mode == LoggingMode::Asynchronous) {
            try {
                state->queue.resize(state->capacity);
            } catch (const std::bad_alloc&) {
                std::lock_guard<std::mutex> backend_lock(state->backend_mutex);
                BackendInvocationGuard guard;
                (void)state->backend->shutdown();
                return Status(StatusCode::ResourceExhausted);
            } catch (const std::length_error&) {
                std::lock_guard<std::mutex> backend_lock(state->backend_mutex);
                BackendInvocationGuard guard;
                (void)state->backend->shutdown();
                return Status(StatusCode::ResourceExhausted);
            }
        }
        try {
            std::thread([state] { state->worker_loop(); }).detach();
        } catch (const std::system_error&) {
            std::lock_guard<std::mutex> backend_lock(state->backend_mutex);
            BackendInvocationGuard guard;
            (void)state->backend->shutdown();
            return Status(StatusCode::ResourceExhausted);
        }
        return LoggingContext(std::move(state));
    } catch (const std::bad_alloc&) {
        return Status(StatusCode::ResourceExhausted);
    } catch (...) {
        return Status(StatusCode::InternalError);
    }
}

Result<Logger> LoggingContext::get_logger(std::string_view category) {
    if (!state_) {
        return Status(StatusCode::InvalidState);
    }
    if (!category_is_valid(category)) {
        return Status(StatusCode::InvalidArgument);
    }
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->lifecycle != LoggingLifecycle::Running) {
            return Status(StatusCode::RuntimeStopped);
        }
        const auto found = state_->categories.find(std::string(category));
        if (found != state_->categories.end()) {
            return Logger(state_, found->second);
        }
        const LogLevel level = state_->resolve_level(category);
        auto entry = std::make_shared<Logger::Category>(std::string(category), level);
        state_->categories.emplace(entry->name, entry);
        return Logger(state_, std::move(entry));
    } catch (const std::bad_alloc&) {
        return Status(StatusCode::ResourceExhausted);
    } catch (...) {
        return Status(StatusCode::InternalError);
    }
}

Status LoggingContext::set_default_level(LogLevel level) noexcept {
    if (!state_) {
        return Status(StatusCode::InvalidState);
    }
    if (!is_threshold_level(level)) {
        return Status(StatusCode::InvalidArgument);
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->lifecycle != LoggingLifecycle::Running) {
        return Status(StatusCode::RuntimeStopped);
    }
    state_->default_level = level;
    for (auto& entry : state_->categories) {
        entry.second->effective_level.store(
            static_cast<std::uint8_t>(state_->resolve_level(entry.first)),
            std::memory_order_relaxed);
    }
    return Status();
}

Status LoggingContext::set_category_level(std::string_view category, LogLevel level) noexcept {
    if (!state_) {
        return Status(StatusCode::InvalidState);
    }
    if (!category_is_valid(category) || !is_threshold_level(level)) {
        return Status(StatusCode::InvalidArgument);
    }
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->lifecycle != LoggingLifecycle::Running) {
            return Status(StatusCode::RuntimeStopped);
        }
        auto updated = state_->overrides;
        updated[std::string(category)] = level;
        state_->overrides.swap(updated);
        for (auto& entry : state_->categories) {
            entry.second->effective_level.store(
                static_cast<std::uint8_t>(state_->resolve_level(entry.first)),
                std::memory_order_relaxed);
        }
        return Status();
    } catch (const std::bad_alloc&) {
        return Status(StatusCode::ResourceExhausted);
    } catch (...) {
        return Status(StatusCode::InternalError);
    }
}

Status LoggingContext::clear_category_level(std::string_view category) noexcept {
    if (!state_) {
        return Status(StatusCode::InvalidState);
    }
    if (!category_is_valid(category)) {
        return Status(StatusCode::InvalidArgument);
    }
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->lifecycle != LoggingLifecycle::Running) {
            return Status(StatusCode::RuntimeStopped);
        }
        auto updated = state_->overrides;
        updated.erase(std::string(category));
        state_->overrides.swap(updated);
        for (auto& entry : state_->categories) {
            entry.second->effective_level.store(
                static_cast<std::uint8_t>(state_->resolve_level(entry.first)),
                std::memory_order_relaxed);
        }
        return Status();
    } catch (const std::bad_alloc&) {
        return Status(StatusCode::ResourceExhausted);
    } catch (...) {
        return Status(StatusCode::InternalError);
    }
}

Status LoggingContext::flush(Duration timeout) noexcept {
    if (!state_) {
        return Status(StatusCode::InvalidState);
    }
    if (!state_->enabled) {
        return Status();
    }
    if (timeout.count() < 0) {
        return Status(StatusCode::InvalidArgument);
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (state_->lifecycle != LoggingLifecycle::Running) {
        const auto deadline = deadline_from(timeout);
        if (!state_->completion_cv.wait_until(
                lock, deadline, [this] { return state_->shutdown_complete; })) {
            return Status(StatusCode::Timeout);
        }
        return state_->shutdown_status;
    }
    const std::uint64_t boundary = state_->next_token - 1;
    state_->flush_requested = true;
    state_->flush_boundary = std::max(state_->flush_boundary, boundary);
    lock.unlock();
    state_->work_cv.notify_all();
    lock.lock();
    const auto deadline = deadline_from(timeout);
    if (!state_->completion_cv.wait_until(lock, deadline, [this, boundary] {
            return state_->shutdown_complete || state_->flushed_boundary >= boundary;
        })) {
        return Status(StatusCode::Timeout);
    }
    return state_->shutdown_complete ? state_->shutdown_status : state_->flush_status;
}

Status LoggingContext::shutdown(Duration timeout) noexcept {
    if (!state_) {
        return Status();
    }
    if (!state_->enabled) {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->lifecycle = LoggingLifecycle::Stopped;
        state_->lifecycle_fast.store(
            static_cast<std::uint8_t>(LoggingLifecycle::Stopped), std::memory_order_release);
        state_->shutdown_complete = true;
        return Status();
    }
    if (timeout.count() < 0) {
        return Status(StatusCode::InvalidArgument);
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (state_->lifecycle == LoggingLifecycle::Running) {
        state_->lifecycle = LoggingLifecycle::Stopping;
        state_->lifecycle_fast.store(
            static_cast<std::uint8_t>(LoggingLifecycle::Stopping), std::memory_order_release);
        state_->shutdown_requested = true;
        lock.unlock();
        state_->work_cv.notify_all();
        lock.lock();
    }
    const auto deadline = deadline_from(timeout);
    if (!state_->completion_cv.wait_until(
            lock, deadline, [this] { return state_->shutdown_complete; })) {
        state_->lifecycle = LoggingLifecycle::Stopped;
        state_->lifecycle_fast.store(
            static_cast<std::uint8_t>(LoggingLifecycle::Stopped), std::memory_order_release);
        return Status(StatusCode::Timeout);
    }
    return state_->shutdown_status;
}

LoggingStats LoggingContext::get_stats() const noexcept {
    LoggingStats stats;
    if (!state_) {
        return stats;
    }
    stats.accepted = state_->accepted.load(std::memory_order_relaxed);
    stats.dispatched = state_->dispatched.load(std::memory_order_relaxed);
    stats.dropped_newest = state_->dropped_newest.load(std::memory_order_relaxed);
    stats.dropped_oldest = state_->dropped_oldest.load(std::memory_order_relaxed);
    stats.rejected_after_stop = state_->rejected_after_stop.load(std::memory_order_relaxed);
    stats.frontend_failures = state_->frontend_failures.load(std::memory_order_relaxed);
    stats.recursive_fallbacks = state_->recursive_fallbacks.load(std::memory_order_relaxed);
    stats.queue_high_watermark = state_->queue_high_watermark.load(std::memory_order_relaxed);
    if (state_->backend) {
        stats.backend = state_->backend->get_stats();
    }
    return stats;
}

}  // namespace rocomm::logging
