#ifndef ROCOMM_RUNTIME_HPP_
#define ROCOMM_RUNTIME_HPP_

#include <memory>
#include <string_view>

#include <rocomm/result.hpp>
#include <rocomm/runtime_config.hpp>
#include <rocomm/logging/logger.hpp>

namespace rocomm {

class Runtime {
public:
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&& other) noexcept;
    Runtime& operator=(Runtime&& other) noexcept;

    static Result<Runtime> create(RuntimeConfig config);

    Status stop() noexcept;
    Result<logging::Logger> get_logger(std::string_view category) const;

    ~Runtime() noexcept;

private:
    struct State;

    explicit Runtime(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

    std::shared_ptr<State> state_;
};

}  // namespace rocomm

#endif  // ROCOMM_RUNTIME_HPP_
