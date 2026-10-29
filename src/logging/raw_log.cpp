#include "raw_log.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace rocomm::logging {

void raw_log(std::string_view message) noexcept {
    constexpr int kMaxEintrRetries = 3;
    // Emergency output has a strictly bounded amount of work and never allocates.
    constexpr std::size_t kMaxEmergencyBytes = 2048;
    constexpr char kTruncated[] = " [truncated]\n";
    char buffer[kMaxEmergencyBytes];
    const bool truncated = message.size() > kMaxEmergencyBytes;
    const std::size_t suffix_size = truncated ? sizeof(kTruncated) - 1U : 0U;
    const std::size_t copy_size = std::min(message.size(), kMaxEmergencyBytes - suffix_size);
    if (copy_size != 0U) {
        std::memcpy(buffer, message.data(), copy_size);
    }
    if (truncated) {
        std::memcpy(buffer + copy_size, kTruncated, suffix_size);
    }
    const char* data = buffer;
    std::size_t remaining = copy_size + suffix_size;
    int retries = 0;

    while (remaining != 0U) {
#if defined(_WIN32)
        const auto chunk = static_cast<unsigned int>(
            remaining < static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())
                ? remaining
                : static_cast<std::size_t>(std::numeric_limits<unsigned int>::max()));
        const int written = ::_write(2, data, chunk);
#else
        const ssize_t written = ::write(STDERR_FILENO, data, remaining);
#endif
        if (written > 0) {
            const auto count = static_cast<std::size_t>(written);
            data += count;
            remaining -= count;
            continue;
        }
        if (written < 0 && errno == EINTR && retries++ < kMaxEintrRetries) {
            continue;
        }
        return;
    }
}

}  // namespace rocomm::logging
