cmake_minimum_required(VERSION 3.20)

project(RuntimeSkeptic
    VERSION 0.1.0
    DESCRIPTION "Cross-layer runtime compatibility analyzer"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(RS_BUILD_TESTS "Build RuntimeSkeptic tests" ON)
option(RS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# RuntimeSkeptic deliberately has ZERO external dependencies.
# Rationale: the tool must produce byte-identical canonical output across
# platforms and toolchains. Every serialization, hashing and formatting
# decision is therefore owned by this repository.  See docs/architecture/
# determinism.md.

add_library(rs_warnings INTERFACE)
if(MSVC)
    target_compile_options(rs_warnings INTERFACE /W4 /permissive- /EHsc)
    target_compile_definitions(rs_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    if(RS_WARNINGS_AS_ERRORS)
        target_compile_options(rs_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(rs_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    if(RS_WARNINGS_AS_ERRORS)
        target_compile_options(rs_warnings INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Platform detection exposed to the code as RS_PLATFORM_*
# ---------------------------------------------------------------------------

add_library(rs_platform INTERFACE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(rs_platform INTERFACE RS_PLATFORM_LINUX=1)
elseif(APPLE)
    target_compile_definitions(rs_platform INTERFACE RS_PLATFORM_MACOS=1)
elseif(WIN32)
    target_compile_definitions(rs_platform INTERFACE RS_PLATFORM_WINDOWS=1)
else()
    target_compile_definitions(rs_platform INTERFACE RS_PLATFORM_UNKNOWN=1)
endif()

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

add_subdirectory(src)
add_subdirectory(tools)

if(RS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "RuntimeSkeptic ${PROJECT_VERSION}")
message(STATUS "  build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  system       : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  tests        : ${RS_BUILD_TESTS}")
