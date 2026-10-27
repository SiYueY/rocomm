#include <rocomm/logging/backends/spdlog.hpp>

#include <atomic>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

#include "text_formatter.hpp"

namespace rocomm::logging::spdlog {

namespace {

::spdlog::level::level_enum to_spdlog_level(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return ::spdlog::level::trace;
        case LogLevel::Debug:
            return ::spdlog::level::debug;
        case LogLevel::Info:
            return ::spdlog::level::info;
        case LogLevel::Warn:
            return ::spdlog::level::warn;
        case LogLevel::Error:
            return ::spdlog::level::err;
        case LogLevel::Critical:
            return ::spdlog::level::critical;
        case LogLevel::Off:
            return ::spdlog::level::off;
    }
    return ::spdlog::level::off;
}

bool valid_output_level(LogLevel level) noexcept { return is_valid_log_level(level); }

class SpdlogBackend final : public LoggingBackend {
public:
    explicit SpdlogBackend(TextBackendConfig config) noexcept
    : config_(std::move(config)), formatter_(config_.format) {}

    std::string_view name() const noexcept override { return "spdlog"; }

    Status initialize() noexcept override {
        if (initialized_ || stopped_) {
            return Status(StatusCode::InvalidState);
        }
        try {
            std::vector<::spdlog::sink_ptr> sinks;
            sinks.reserve(config_.outputs.size());
            for (const TextOutputConfig& output : config_.outputs) {
                std::visit(
                    [&sinks](const auto& config) {
                        using Config = std::decay_t<decltype(config)>;
                        ::spdlog::sink_ptr sink;
                        if constexpr (std::is_same_v<Config, ConsoleOutputConfig>) {
                            const bool stdout_stream = config.stream == ConsoleStream::Stdout;
                            const bool color = config.color != ColorMode::Never;
                            if (color) {
                                if (stdout_stream) {
                                    sink =
                                        std::make_shared<::spdlog::sinks::stdout_color_sink_mt>();
                                } else {
                                    sink =
                                        std::make_shared<::spdlog::sinks::stderr_color_sink_mt>();
                                }
                            } else {
                                if (stdout_stream) {
                                    sink = std::make_shared<::spdlog::sinks::stdout_sink_mt>();
                                } else {
                                    sink = std::make_shared<::spdlog::sinks::stderr_sink_mt>();
                                }
                            }
                        } else if constexpr (std::is_same_v<Config, FileOutputConfig>) {
                            sink = std::make_shared<::spdlog::sinks::basic_file_sink_mt>(
                                config.path, config.truncate);
                        } else {
                            sink = std::make_shared<::spdlog::sinks::rotating_file_sink_mt>(
                                config.path, static_cast<std::size_t>(config.max_file_size_bytes),
                                config.max_archived_files);
                        }
                        sink->set_level(to_spdlog_level(config.min_level));
                        sinks.push_back(std::move(sink));
                    },
                    output);
            }
            logger_ =
                std::make_unique<::spdlog::logger>("rocomm-local", sinks.begin(), sinks.end());
            logger_->set_level(::spdlog::level::trace);
            logger_->set_pattern("%v");
            logger_->set_error_handler([this](const std::string&) {
                stats_.write_errors.fetch_add(1, std::memory_order_relaxed);
            });
            initialized_ = true;
            return {};
        } catch (const std::bad_alloc&) {
            return Status(StatusCode::ResourceExhausted);
        } catch (...) {
            return Status(StatusCode::InternalError);
        }
    }

    void write(const LogRecord& record) noexcept override {
        if (!initialized_ || stopped_ || !logger_) {
            stats_.write_errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        std::string rendered;
        if (!formatter_.format(record, rendered)) {
            stats_.write_errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        try {
            stats_.write_calls.fetch_add(1, std::memory_order_relaxed);
            logger_->log(to_spdlog_level(record.level()), "{}", rendered);
        } catch (...) {
            stats_.write_errors.fetch_add(1, std::memory_order_relaxed);
        }
    }

    Status flush() noexcept override {
        if (!initialized_ || stopped_ || !logger_) {
            ++stats_.flush_errors;
            return Status(StatusCode::InvalidState);
        }
        try {
            logger_->flush();
            return {};
        } catch (...) {
            stats_.flush_errors.fetch_add(1, std::memory_order_relaxed);
            return Status(StatusCode::InternalError);
        }
    }

    Status shutdown() noexcept override {
        if (stopped_) {
            return shutdown_status_;
        }
        stopped_ = true;
        Status result;
        if (logger_) {
            try {
                logger_->flush();
            } catch (...) {
                stats_.shutdown_errors.fetch_add(1, std::memory_order_relaxed);
                result = Status(StatusCode::InternalError);
            }
        }
        logger_.reset();
        shutdown_status_ = result;
        return shutdown_status_;
    }

    LoggingBackendStats get_stats() const noexcept override {
        return {
            stats_.write_calls.load(std::memory_order_relaxed),
            stats_.write_errors.load(std::memory_order_relaxed),
            stats_.flush_errors.load(std::memory_order_relaxed),
            stats_.shutdown_errors.load(std::memory_order_relaxed)};
    }

private:
    struct AtomicStats {
        std::atomic<std::uint64_t> write_calls{0};
        std::atomic<std::uint64_t> write_errors{0};
        std::atomic<std::uint64_t> flush_errors{0};
        std::atomic<std::uint64_t> shutdown_errors{0};
    };

    TextBackendConfig config_;
    TextFormatter formatter_;
    std::unique_ptr<::spdlog::logger> logger_;
    AtomicStats stats_;
    bool initialized_ = false;
    bool stopped_ = false;
    Status shutdown_status_;
};

}  // namespace

Result<std::unique_ptr<LoggingBackend>> create_backend(TextBackendConfig config) {
    if (config.outputs.empty()) {
        return Status(StatusCode::InvalidArgument);
    }
    for (const TextOutputConfig& output : config.outputs) {
        const bool valid = std::visit(
            [](const auto& value) {
                using Config = std::decay_t<decltype(value)>;
                if (!valid_output_level(value.min_level)) {
                    return false;
                }
                if constexpr (std::is_same_v<Config, FileOutputConfig>) {
                    return !value.path.empty();
                }
                if constexpr (std::is_same_v<Config, RotatingFileOutputConfig>) {
                    return !value.path.empty() && value.max_file_size_bytes != 0U &&
                           value.max_archived_files != 0U;
                }
                return true;
            },
            output);
        if (!valid) {
            return Status(StatusCode::InvalidArgument);
        }
    }
    try {
        return std::unique_ptr<LoggingBackend>(new SpdlogBackend(std::move(config)));
    } catch (const std::bad_alloc&) {
        return Status(StatusCode::ResourceExhausted);
    } catch (...) {
        return Status(StatusCode::InternalError);
    }
}

}  // namespace rocomm::logging::spdlog
