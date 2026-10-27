add_library(rocomm_runtime runtime.cpp)
add_library(rocomm::runtime ALIAS rocomm_runtime)

target_include_directories(rocomm_runtime
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(rocomm_runtime PUBLIC rocomm::logging)
target_compile_features(rocomm_runtime PUBLIC cxx_std_17)

if(TARGET rocomm::logging_spdlog)
    target_link_libraries(rocomm_runtime PRIVATE rocomm::logging_spdlog)
    target_compile_definitions(rocomm_runtime PRIVATE ROCOMM_HAS_SPDLOG_BACKEND=1)
endif()
