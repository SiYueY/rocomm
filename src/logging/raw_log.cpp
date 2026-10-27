#include "raw_log.hpp"

#include <cerrno>
#include <cstddef>
#include <limits>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace rocomm::logging {

void raw_log(std::string_view message) noexcept {
    constexpr int kMaxEintrRetries = 3;
    const char* data = message.data();
    std::size_t remaining = message.size();
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
