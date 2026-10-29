add_library(rocomm_logging
    logging_context.cpp
    raw_log.cpp
    text_formatter.cpp)

add_library(rocomm::logging ALIAS rocomm_logging)

target_include_directories(rocomm_logging
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(rocomm_logging PUBLIC rocomm_core)
target_compile_features(rocomm_logging PUBLIC cxx_std_17)
target_compile_definitions(rocomm_logging
    PUBLIC ROCOMM_LOGGING_COMPILED_LEVEL=${ROCOMM_LOGGING_COMPILED_LEVEL})

if(ROCOMM_BUILD_LOGGING_SPDLOG)
    set(ROCOMM_SPDLOG_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/spdlog")
    if(NOT EXISTS "${ROCOMM_SPDLOG_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "spdlog submodule is unavailable. Run: git submodule update --init --recursive")
    endif()

    set(SPDLOG_BUILD_ALL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_SYSTEM_INCLUDES ON CACHE BOOL "" FORCE)

    set(ROCOMM_RESTORE_EMPTY_BUILD_TYPE OFF)
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(ROCOMM_RESTORE_EMPTY_BUILD_TYPE ON)
        set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
    endif()
    add_subdirectory("${ROCOMM_SPDLOG_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/spdlog" EXCLUDE_FROM_ALL)
    if(ROCOMM_RESTORE_EMPTY_BUILD_TYPE)
        unset(CMAKE_BUILD_TYPE CACHE)
        unset(CMAKE_BUILD_TYPE)
    endif()

    # Build a separate fully linked variant for the default spdlog policy.
    # The base rocomm::logging target remains entirely third-party-free.
    # These are mutually exclusive implementation variants: do not link both
    # into the same final binary, because each owns the LoggingContext symbols.
    add_library(rocomm_logging_spdlog
        logging_context.cpp
        raw_log.cpp
        text_formatter.cpp
        spdlog/spdlog_backend.cpp)
    add_library(rocomm::logging_spdlog ALIAS rocomm_logging_spdlog)
    target_include_directories(rocomm_logging_spdlog
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE ${PROJECT_SOURCE_DIR}/src/logging)
    target_link_libraries(rocomm_logging_spdlog
        PUBLIC rocomm_core
        PRIVATE spdlog::spdlog)
    target_compile_features(rocomm_logging_spdlog PUBLIC cxx_std_17)
    target_compile_definitions(rocomm_logging_spdlog
        PRIVATE ROCOMM_HAS_SPDLOG_BACKEND=1
        PUBLIC ROCOMM_LOGGING_COMPILED_LEVEL=${ROCOMM_LOGGING_COMPILED_LEVEL})

endif()
