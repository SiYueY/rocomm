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
    find_package(spdlog QUIET)
    if(spdlog_FOUND)
        target_sources(rocomm_logging PRIVATE spdlog/spdlog_backend.cpp)
        target_include_directories(rocomm_logging PRIVATE ${PROJECT_SOURCE_DIR}/src/logging)
        target_link_libraries(rocomm_logging PRIVATE spdlog::spdlog)
        target_compile_definitions(rocomm_logging PRIVATE ROCOMM_HAS_SPDLOG_BACKEND=1)

        add_library(rocomm_logging_spdlog INTERFACE)
        add_library(rocomm::logging_spdlog ALIAS rocomm_logging_spdlog)
        target_link_libraries(rocomm_logging_spdlog INTERFACE rocomm::logging)
        target_compile_definitions(rocomm_logging_spdlog INTERFACE ROCOMM_HAS_SPDLOG_BACKEND=1)
    endif()
endif()
