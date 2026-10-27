#include <cassert>

#include <rocomm/logging/macros.hpp>

int main() {
    rocomm::logging::Logger logger;
    int logger_evaluations = 0;
    int message_evaluations = 0;
    ROCOMM_LOG_TRACE((++logger_evaluations, logger), (++message_evaluations, "trace"));
    ROCOMM_LOG_CRITICAL((++logger_evaluations, logger), (++message_evaluations, "critical"));
    assert(logger_evaluations == 0);
    assert(message_evaluations == 0);
    return 0;
}
