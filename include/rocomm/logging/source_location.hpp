#ifndef ROCOMM_LOGGING_SOURCE_LOCATION_HPP_
#define ROCOMM_LOGGING_SOURCE_LOCATION_HPP_

#include <cstdint>

namespace rocomm::logging {

class SourceLocation {
public:
    constexpr SourceLocation() noexcept = default;

    static constexpr SourceLocation from_static(
        const char* file, const char* function, std::uint32_t line) noexcept {
        return SourceLocation(file, function, line);
    }

    constexpr const char* file() const noexcept { return file_; }

    constexpr const char* function() const noexcept { return function_; }

    constexpr std::uint32_t line() const noexcept { return line_; }

private:
    constexpr SourceLocation(const char* file, const char* function, std::uint32_t line) noexcept
    : file_(file), function_(function), line_(line) {}

    const char* file_ = nullptr;
    const char* function_ = nullptr;
    std::uint32_t line_ = 0;
};

}  // namespace rocomm::logging

#define ROCOMM_SOURCE_LOCATION                      \
    ::rocomm::logging::SourceLocation::from_static( \
        __FILE__, __func__, static_cast<std::uint32_t>(__LINE__))

#endif  // ROCOMM_LOGGING_SOURCE_LOCATION_HPP_
