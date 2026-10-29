#include <rocomm/runtime.hpp>

#include <condition_variable>
#include <chrono>
#include <mutex>
#include <new>
#include <utility>

#include <rocomm/logging/logging_context.hpp>

namespace rocomm {

namespace {

std::chrono::steady_clock::time_point deadline_from(Duration timeout) noexcept {
    const auto now = std::chrono::steady_clock::now();
    const auto maximum = std::chrono::steady_clock::time_point::max();
    if (timeout.count() > 0 && maximum - now < timeout) {
        return maximum;
    }
    return now + timeout;
}

}  // namespace

struct Runtime::State {
    explicit State(logging::LoggingContext context, Duration configured_timeout) noexcept
    : logging_context(std::move(context)), shutdown_timeout(configured_timeout) {}

    std::mutex mutex;
    std::condition_variable completion_cv;
    logging::LoggingContext logging_context;
    Duration shutdown_timeout;
    bool stopped = false;
    bool stopping = false;
    bool stop_call_active = false;
    Status final_status;
};

Runtime::Runtime(Runtime&& other) noexcept : state_(std::move(other.state_)) {}

Runtime& Runtime::operator=(Runtime&& other) noexcept {
    if (this != &other) {
        (void)stop();
        state_ = std::move(other.state_);
    }
    return *this;
}

Runtime::~Runtime() noexcept { (void)stop(); }

Result<Runtime> Runtime::create(RuntimeConfig config) {
    if (config.shutdown_timeout_.count() < 0) {
        return Status(StatusCode::InvalidArgument);
    }
    auto logging_result = logging::LoggingContext::create(std::move(config.logging_config_));
    if (!logging_result.ok()) {
        return logging_result.status();
    }
    try {
        return Runtime(
            std::make_shared<State>(std::move(logging_result).value(), config.shutdown_timeout_));
    } catch (const std::bad_alloc&) {
        return Status(StatusCode::ResourceExhausted);
    } catch (...) {
        return Status(StatusCode::InternalError);
    }
}

Status Runtime::stop() noexcept {
    if (!state_) {
        return Status();
    }
    const auto deadline = deadline_from(state_->shutdown_timeout);
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (state_->stopped) {
        return state_->final_status;
    }
    while (state_->stop_call_active) {
        if (!state_->completion_cv.wait_until(
                lock, deadline, [this] { return state_->stopped || !state_->stop_call_active; })) {
            return Status(StatusCode::Timeout);
        }
        if (state_->stopped) {
            return state_->final_status;
        }
    }
    state_->stopping = true;
    state_->stop_call_active = true;
    lock.unlock();
    const auto now = std::chrono::steady_clock::now();
    const Duration remaining =
        now >= deadline ? Duration::zero() : std::chrono::duration_cast<Duration>(deadline - now);
    const Status result = state_->logging_context.shutdown(remaining);
    lock.lock();
    state_->stop_call_active = false;
    if (result.code() == StatusCode::Timeout) {
        state_->completion_cv.notify_all();
        return result;
    }
    state_->final_status = result;
    state_->stopped = true;
    state_->completion_cv.notify_all();
    return result;
}

Result<logging::Logger> Runtime::get_logger(std::string_view category) const {
    if (!state_) {
        return Status(StatusCode::InvalidState);
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopped || state_->stopping) {
        return Status(StatusCode::RuntimeStopped);
    }
    return state_->logging_context.get_logger(category);
}

}  // namespace rocomm
