#ifndef ROCOMM_LOGGING_LOGGER_HPP_
#define ROCOMM_LOGGING_LOGGER_HPP_

#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <rocomm/logging/log_level.hpp>
#include <rocomm/logging/source_location.hpp>

namespace rocomm::logging {

class LoggingContext;

class Logger {
public:
    Logger() noexcept = default;

    bool enabled(LogLevel level) const noexcept;

    void log(LogLevel level, std::string_view message, SourceLocation source = {}) const noexcept;

    template <typename Builder>
    void log_lazy(LogLevel level, Builder&& builder, SourceLocation source = {}) const noexcept {
        using Result = std::decay_t<std::invoke_result_t<Builder&>>;
        static_assert(
            std::is_same_v<Result, std::string> || std::is_same_v<Result, std::string_view> ||
                std::is_same_v<Result, const char*>,
            "LogLazy builder must return std::string, std::string_view, "
            "or const char*.");

        if (handle_recursion(level, builder)) {
            return;
        }
        if (!enabled(level)) {
            return;
        }

        try {
            if constexpr (std::is_same_v<Result, std::string>) {
                log_owned(level, builder(), source);
            } else if constexpr (std::is_same_v<Result, std::string_view>) {
                log(level, builder(), source);
            } else {
                const char* message = builder();
                if (message == nullptr) {
                    log_null_builder_result(level);
                    return;
                }
                log(level, message, source);
            }
        } catch (...) {
            log_builder_failure(level);
        }
    }

    std::string_view category() const noexcept;

private:
    struct State;
    struct Category;

    friend class LoggingContext;

    Logger(std::shared_ptr<State> state, std::shared_ptr<Category> category) noexcept
    : state_(std::move(state)), category_(std::move(category)) {}

    void log_owned(LogLevel level, std::string message, SourceLocation source) const noexcept;
    void submit_admitted(
        LogLevel level, std::string message, SourceLocation source,
        std::uint64_t token) const noexcept;
    void log_builder_failure(LogLevel level) const noexcept;
    void log_null_builder_result(LogLevel level) const noexcept;

    template <typename Builder>
    bool handle_recursion(LogLevel level, Builder& builder) const noexcept {
        using Result = std::decay_t<std::invoke_result_t<Builder&>>;
        if (!is_recursing()) {
            return false;
        }
        if (!begin_raw_fallback()) {
            return true;
        }
        try {
            if constexpr (std::is_same_v<Result, std::string>) {
                log_recursive(level, builder());
            } else if constexpr (std::is_same_v<Result, std::string_view>) {
                log_recursive(level, builder());
            } else {
                const char* message = builder();
                log_recursive(level, message == nullptr ? std::string_view() : message);
            }
        } catch (...) {
            log_recursive(level, "RoComm recursive logging lazy builder failed\n");
        }
        end_raw_fallback();
        return true;
    }

    bool is_recursing() const noexcept;
    bool begin_raw_fallback() const noexcept;
    void end_raw_fallback() const noexcept;
    void log_recursive(LogLevel level, std::string_view message) const noexcept;

    std::shared_ptr<State> state_;
    std::shared_ptr<Category> category_;
};

}  // namespace rocomm::logging

#endif  // ROCOMM_LOGGING_LOGGER_HPP_
