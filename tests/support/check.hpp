#ifndef ROCOMM_TESTS_SUPPORT_CHECK_HPP_
#define ROCOMM_TESTS_SUPPORT_CHECK_HPP_

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                                        \
    do {                                                                                        \
        if (!(condition)) {                                                                     \
            std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            std::abort();                                                                       \
        }                                                                                       \
    } while (false)

#endif  // ROCOMM_TESTS_SUPPORT_CHECK_HPP_
