#ifndef ROCOMM_LOGGING_MACROS_HPP_
#define ROCOMM_LOGGING_MACROS_HPP_

#include <cstdint>

#include <rocomm/logging/logger.hpp>

#ifndef ROCOMM_LOGGING_COMPILED_LEVEL
#define ROCOMM_LOGGING_COMPILED_LEVEL 0
#endif

#define ROCOMM_LOG_LEVEL_TRACE 0
#define ROCOMM_LOG_LEVEL_DEBUG 1
#define ROCOMM_LOG_LEVEL_INFO 2
#define ROCOMM_LOG_LEVEL_WARN 3
#define ROCOMM_LOG_LEVEL_ERROR 4
#define ROCOMM_LOG_LEVEL_CRITICAL 5
#define ROCOMM_LOG_LEVEL_OFF 6

#define ROCOMM_LOGGING_DEFINE(level_name, level_value, logger_expression, message_expression)  \
    do {                                                                                       \
        auto&& rocomm_logger = (logger_expression);                                            \
        rocomm_logger.log_lazy(                                                                \
            ::rocomm::logging::LogLevel::level_name,                                           \
            [&]() -> decltype(auto) { return (message_expression); }, ROCOMM_SOURCE_LOCATION); \
    } while (false)

#if ROCOMM_LOGGING_COMPILED_LEVEL <= ROCOMM_LOG_LEVEL_TRACE
#define ROCOMM_LOG_TRACE(logger_expression, message_expression) \
    ROCOMM_LOGGING_DEFINE(Trace, ROCOMM_LOG_LEVEL_TRACE, logger_expression, message_expression)
#else
#define ROCOMM_LOG_TRACE(logger_expression, message_expression) \
    do {                                                        \
    } while (false)
#endif

#if ROCOMM_LOGGING_COMPILED_LEVEL <= ROCOMM_LOG_LEVEL_DEBUG
#define ROCOMM_LOG_DEBUG(logger_expression, message_expression) \
    ROCOMM_LOGGING_DEFINE(Debug, ROCOMM_LOG_LEVEL_DEBUG, logger_expression, message_expression)
#else
#define ROCOMM_LOG_DEBUG(logger_expression, message_expression) \
    do {                                                        \
    } while (false)
#endif

#if ROCOMM_LOGGING_COMPILED_LEVEL <= ROCOMM_LOG_LEVEL_INFO
#define ROCOMM_LOG_INFO(logger_expression, message_expression) \
    ROCOMM_LOGGING_DEFINE(Info, ROCOMM_LOG_LEVEL_INFO, logger_expression, message_expression)
#else
#define ROCOMM_LOG_INFO(logger_expression, message_expression) \
    do {                                                       \
    } while (false)
#endif

#if ROCOMM_LOGGING_COMPILED_LEVEL <= ROCOMM_LOG_LEVEL_WARN
#define ROCOMM_LOG_WARN(logger_expression, message_expression) \
    ROCOMM_LOGGING_DEFINE(Warn, ROCOMM_LOG_LEVEL_WARN, logger_expression, message_expression)
#else
#define ROCOMM_LOG_WARN(logger_expression, message_expression) \
    do {                                                       \
    } while (false)
#endif

#if ROCOMM_LOGGING_COMPILED_LEVEL <= ROCOMM_LOG_LEVEL_ERROR
#define ROCOMM_LOG_ERROR(logger_expression, message_expression) \
    ROCOMM_LOGGING_DEFINE(Error, ROCOMM_LOG_LEVEL_ERROR, logger_expression, message_expression)
#else
#define ROCOMM_LOG_ERROR(logger_expression, message_expression) \
    do {                                                        \
    } while (false)
#endif

#if ROCOMM_LOGGING_COMPILED_LEVEL <= ROCOMM_LOG_LEVEL_CRITICAL
#define ROCOMM_LOG_CRITICAL(logger_expression, message_expression) \
    ROCOMM_LOGGING_DEFINE(                                         \
        Critical, ROCOMM_LOG_LEVEL_CRITICAL, logger_expression, message_expression)
#else
#define ROCOMM_LOG_CRITICAL(logger_expression, message_expression) \
    do {                                                           \
    } while (false)
#endif

#endif  // ROCOMM_LOGGING_MACROS_HPP_
