#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>

#include <rocomm/logging/logging_context.hpp>

namespace {

class NullBackend final : public rocomm::logging::LoggingBackend {
public:
    std::string_view name() const noexcept override { return "benchmark-null"; }
    rocomm::Status initialize() noexcept override { return {}; }
    void write(const rocomm::logging::LogRecord&) noexcept override {}
    rocomm::Status flush() noexcept override { return {}; }
    rocomm::Status shutdown() noexcept override { return {}; }
};

template <typename Function>
std::chrono::nanoseconds measure(std::uint64_t iterations, Function&& function) {
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < iterations; ++index) {
        function();
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start);
}

void print_result(
    std::string_view name, std::chrono::nanoseconds elapsed, std::uint64_t iterations) {
    const double per_call = static_cast<double>(elapsed.count()) / static_cast<double>(iterations);
    std::cout << name << ": " << per_call << " ns/call\n";
}

}  // namespace

int main() {
    constexpr std::uint64_t iterations = 1'000'000;
    rocomm::logging::LoggingConfig config;
    config.set_mode(rocomm::logging::LoggingMode::Asynchronous)
        .set_queue_capacity(8192)
        .set_default_level(rocomm::logging::LogLevel::Warn)
        .set_backend_factory([] {
            return rocomm::Result<std::unique_ptr<rocomm::logging::LoggingBackend>>(
                std::make_unique<NullBackend>());
        });
    auto context_result = rocomm::logging::LoggingContext::create(std::move(config));
    if (!context_result.ok()) {
        return 1;
    }
    auto context = std::move(context_result).value();
    auto logger_result = context.get_logger("rocomm.benchmark");
    if (!logger_result.ok()) {
        return 1;
    }
    const auto logger = std::move(logger_result).value();

    print_result(
        "enabled(Info) filtered",
        measure(
            iterations,
            [&] { static_cast<void>(logger.enabled(rocomm::logging::LogLevel::Info)); }),
        iterations);
    print_result(
        "log(Info) filtered",
        measure(iterations, [&] { logger.log(rocomm::logging::LogLevel::Info, "filtered"); }),
        iterations);
    if (!context.shutdown(std::chrono::seconds(1)).ok()) {
        return 1;
    }
    return 0;
}
