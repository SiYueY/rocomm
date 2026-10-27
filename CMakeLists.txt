cmake_minimum_required(VERSION 3.16)

project(rocomm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(ROCOMM_BUILD_LOGGING "Build the RoComm logging module" ON)
option(ROCOMM_BUILD_LOGGING_SPDLOG "Build the spdlog logging backend" ON)
option(ROCOMM_BUILD_BENCHMARKS "Build lightweight RoComm benchmarks" OFF)

set(ROCOMM_LOGGING_COMPILED_LEVEL 0 CACHE STRING
    "Lowest log level retained by logging macros (0=Trace, 6=Off)")

add_library(rocomm_core INTERFACE)
target_include_directories(rocomm_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(rocomm_core INTERFACE cxx_std_17)

if(ROCOMM_BUILD_LOGGING)
    add_subdirectory(src/logging)
    add_subdirectory(src/runtime)
endif()

if(BUILD_TESTING AND ROCOMM_BUILD_LOGGING)
    add_subdirectory(tests)
endif()

if(ROCOMM_BUILD_BENCHMARKS AND ROCOMM_BUILD_LOGGING)
    add_subdirectory(benchmarks)
endif()
