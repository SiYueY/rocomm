#include "../support/check.hpp"
#include <chrono>
#include <memory>

#include <rocomm/logging/backends/spdlog.hpp>
#include <rocomm/logging/logging_context.hpp>

int main() {
    rocomm::logging::TextBackendConfig invalid_config;
    CHECK(!rocomm::logging::spdlog::create_backend(std::move(invalid_config)).ok());

    rocomm::logging::TextBackendConfig backend_config;
    backend_config.outputs.emplace_back(rocomm::logging::ConsoleOutputConfig{});
    auto backend_result = rocomm::logging::spdlog::create_backend(backend_config);
    CHECK(backend_result.ok());
    auto backend = std::move(backend_result).value();
    CHECK(backend->initialize().ok());
    CHECK(backend->initialize().code() == rocomm::StatusCode::InvalidState);
    CHECK(backend->flush().ok());
    CHECK(backend->shutdown().ok());
    CHECK(backend->shutdown().ok());

    rocomm::logging::LoggingConfig default_config;
    auto default_context_result =
        rocomm::logging::LoggingContext::create(std::move(default_config));
    CHECK(default_context_result.ok());
    auto default_context = std::move(default_context_result).value();
    CHECK(default_context.shutdown(std::chrono::seconds(1)).ok());

    rocomm::logging::LoggingConfig config;
    config.set_backend_factory(
        [backend_config] { return rocomm::logging::spdlog::create_backend(backend_config); });
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    CHECK(context_result.ok());
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.spdlog");
    CHECK(logger_result.ok());
    std::move(logger_result).value().log(rocomm::logging::LogLevel::Info, "spdlog test");
    CHECK(context.flush(std::chrono::seconds(1)).ok());
    CHECK(context.shutdown(std::chrono::seconds(1)).ok());
    return 0;
}
