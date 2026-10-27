add_library(rocomm_logging_spdlog spdlog_backend.cpp)
add_library(rocomm::logging_spdlog ALIAS rocomm_logging_spdlog)

target_include_directories(rocomm_logging_spdlog
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE ${PROJECT_SOURCE_DIR}/src/logging)
target_link_libraries(rocomm_logging_spdlog
    PUBLIC rocomm::logging
    PRIVATE spdlog::spdlog)
target_compile_features(rocomm_logging_spdlog PUBLIC cxx_std_17)
target_compile_definitions(rocomm_logging_spdlog PUBLIC ROCOMM_HAS_SPDLOG_BACKEND=1)
