# SPDX-License-Identifier: Apache-2.0

# ---------------------------------------------------------------------------
# Embedded schemas. The published contract ships INSIDE the binary: the tools
# validate every input against these before any hand-parser runs, and an
# installed binary must not depend on schemas/*.json being on disk. configure_
# file substitutes each schema's bytes into a raw string literal; re-running
# when a schema changes is forced by CONFIGURE_DEPENDS below.
# ---------------------------------------------------------------------------
set(RS_SCHEMA_DIR ${CMAKE_SOURCE_DIR}/schemas)
file(READ ${RS_SCHEMA_DIR}/application-requirements.v1.json RS_SCHEMA_APPREQ)
file(READ ${RS_SCHEMA_DIR}/application-requirements-bundle.v1.json RS_SCHEMA_APPREQ_BUNDLE)
file(READ ${RS_SCHEMA_DIR}/environment-profile.v1.json RS_SCHEMA_ENVPROFILE)
file(READ ${RS_SCHEMA_DIR}/analysis-bundle.v1.json RS_SCHEMA_ANALYSIS_BUNDLE)
file(READ ${RS_SCHEMA_DIR}/runtime-trace-record.v1.json RS_SCHEMA_RUNTIME_TRACE)
file(READ ${RS_SCHEMA_DIR}/runtime-overhead.v1.json RS_SCHEMA_RUNTIME_OVERHEAD)
# MSVC rejects a single string literal longer than 16,380 bytes (C2026). The
# requirement and profile schemas are already larger than that. Emit adjacent
# raw literals in conservative 8 KiB chunks; C++ concatenates them at compile
# time, while every individual token stays below the strictest compiler limit.
function(rs_make_schema_literal input_var output_var)
    set(_schema "${${input_var}}")
    string(LENGTH "${_schema}" _length)
    set(_offset 0)
    set(_literal "")
    while(_offset LESS _length)
        math(EXPR _remaining "${_length} - ${_offset}")
        if(_remaining GREATER 8000)
            set(_take 8000)
        else()
            set(_take "${_remaining}")
        endif()
        string(SUBSTRING "${_schema}" ${_offset} ${_take} _chunk)
        string(APPEND _literal
            "R\"RSJSONSCHEMA(${_chunk})RSJSONSCHEMA\"\n    ")
        math(EXPR _offset "${_offset} + ${_take}")
    endwhile()
    set(${output_var} "${_literal}" PARENT_SCOPE)
endfunction()

rs_make_schema_literal(RS_SCHEMA_APPREQ RS_SCHEMA_APPREQ_LITERAL)
rs_make_schema_literal(RS_SCHEMA_APPREQ_BUNDLE RS_SCHEMA_APPREQ_BUNDLE_LITERAL)
rs_make_schema_literal(RS_SCHEMA_ENVPROFILE RS_SCHEMA_ENVPROFILE_LITERAL)
rs_make_schema_literal(RS_SCHEMA_ANALYSIS_BUNDLE RS_SCHEMA_ANALYSIS_BUNDLE_LITERAL)
rs_make_schema_literal(RS_SCHEMA_RUNTIME_TRACE RS_SCHEMA_RUNTIME_TRACE_LITERAL)
rs_make_schema_literal(RS_SCHEMA_RUNTIME_OVERHEAD RS_SCHEMA_RUNTIME_OVERHEAD_LITERAL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${RS_SCHEMA_DIR}/application-requirements.v1.json
    ${RS_SCHEMA_DIR}/application-requirements-bundle.v1.json
    ${RS_SCHEMA_DIR}/environment-profile.v1.json
    ${RS_SCHEMA_DIR}/analysis-bundle.v1.json
    ${RS_SCHEMA_DIR}/runtime-trace-record.v1.json
    ${RS_SCHEMA_DIR}/runtime-overhead.v1.json)
set(RS_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/core/schemas_embedded.hpp.in
    ${RS_GENERATED_DIR}/runtimeskeptic/core/schemas_embedded.hpp
    @ONLY)

# ---------------------------------------------------------------------------
# rs_core - JSON, hashing, evidence model. No platform dependencies.
# ---------------------------------------------------------------------------
add_library(rs_core STATIC
    core/json.cpp
    core/sha256.cpp
    core/evidence.cpp
    core/io.cpp
    core/schema.cpp
    core/schema_registry.cpp)

target_include_directories(rs_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${RS_GENERATED_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(rs_core PRIVATE rs_warnings PUBLIC rs_platform)
set_target_properties(rs_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(rs_core PROPERTIES EXPORT_NAME core)
add_library(RuntimeSkeptic::core ALIAS rs_core)

# ---------------------------------------------------------------------------
# libruntimeskeptic - stable C ABI wrappers and fixed-capacity event recorder.
# The wrapper path is allocation-free; JSONL serialization happens only when
# the caller explicitly flushes after the observed operations are quiescent.
# ---------------------------------------------------------------------------
add_library(runtimeskeptic SHARED
    runtime/runtime.cpp
    runtime/posix.cpp
    runtime/windows.cpp)
set_target_properties(runtimeskeptic PROPERTIES
    OUTPUT_NAME runtimeskeptic
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(runtimeskeptic PRIVATE RS_RUNTIME_BUILDING_LIBRARY=1)
target_include_directories(runtimeskeptic PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(runtimeskeptic PRIVATE rs_core rs_warnings rs_platform)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(runtimeskeptic PRIVATE Threads::Threads)
endif()
set_target_properties(runtimeskeptic PROPERTIES EXPORT_NAME runtime)
if(APPLE)
    # Mach-O requires current_version >= compatibility_version. The public
    # product version is still 0.2.0 and the loader ABI is 1; encode both
    # without asking dyld to accept the impossible 0.2.0 >= 1.0.0 relation.
    set_target_properties(runtimeskeptic PROPERTIES
        MACHO_CURRENT_VERSION 1.2.0
        MACHO_COMPATIBILITY_VERSION 1.0.0)
endif()
add_library(RuntimeSkeptic::runtime ALIAS runtimeskeptic)

add_library(rs_trace STATIC runtime/trace.cpp)
target_include_directories(rs_trace PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(rs_trace PRIVATE rs_core rs_platform rs_warnings)
target_compile_features(rs_trace INTERFACE cxx_std_20)
set_target_properties(rs_trace PROPERTIES EXPORT_NAME trace)
set_target_properties(rs_platform PROPERTIES EXPORT_NAME platform)
set_target_properties(rs_warnings PROPERTIES EXPORT_NAME warnings)
add_library(RuntimeSkeptic::trace ALIAS rs_trace)

install(TARGETS runtimeskeptic rs_trace rs_core rs_platform rs_warnings
        EXPORT RuntimeSkepticTargets
        RUNTIME DESTINATION bin COMPONENT RuntimeSDK
        LIBRARY DESTINATION lib COMPONENT RuntimeSDK
        ARCHIVE DESTINATION lib COMPONENT RuntimeSDK)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/runtimeskeptic/runtime
        DESTINATION include/runtimeskeptic
        COMPONENT RuntimeSDK)
install(EXPORT RuntimeSkepticTargets
        FILE RuntimeSkepticTargets.cmake
        NAMESPACE RuntimeSkeptic::
        DESTINATION lib/cmake/RuntimeSkeptic
        COMPONENT RuntimeSDK)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/RuntimeSkepticConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/RuntimeSkepticConfig.cmake
    INSTALL_DESTINATION lib/cmake/RuntimeSkeptic)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/RuntimeSkepticConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/RuntimeSkepticConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/RuntimeSkepticConfigVersion.cmake
    DESTINATION lib/cmake/RuntimeSkeptic
    COMPONENT RuntimeSDK)

# ---------------------------------------------------------------------------
# rs_vm - the virtual-memory domain: profiles, requirements, rules, findings.
# ---------------------------------------------------------------------------
add_library(rs_vm STATIC
    vm/address_range.cpp
    vm/profile.cpp
    vm/requirement.cpp
    vm/finding.cpp
    vm/analyzer.cpp
    vm/impact.cpp)

target_link_libraries(rs_vm PUBLIC rs_core PRIVATE rs_warnings)
add_library(RuntimeSkeptic::vm ALIAS rs_vm)

# ---------------------------------------------------------------------------
# rs_reports - rendering. Depends on the domain, never the other way round.
# ---------------------------------------------------------------------------
add_library(rs_reports STATIC
    reports/report.cpp
    reports/bundle.cpp)

target_link_libraries(rs_reports PUBLIC rs_vm PRIVATE rs_warnings)
add_library(RuntimeSkeptic::reports ALIAS rs_reports)

# ---------------------------------------------------------------------------
# rs_probe - platform measurement. Exactly one implementation compiles; the
# other is guarded out by the preprocessor so both files stay syntax-checked
# on every platform.
# ---------------------------------------------------------------------------
# arena_walk.cpp is deliberately NOT guarded by a platform macro: it is the
# arena logic with the platform calls injected, so it compiles and is tested
# everywhere. The first version of that logic lived inside the macOS probe,
# where the only way to check it was a runner - and it was wrong.
# windows_regions.cpp is unguarded for the same reason and a stronger one: the
# Windows probe sat in the tree for a day having never executed on any machine
# including Windows, `test_probe` was 14/14 on a real runner while it established
# zero ranges, and the diagnostics channel dropped every Windows failure for the
# life of the repository. Three chances to notice, none taken, because everything
# about Windows here was reachable only from Windows.
add_library(rs_probe STATIC
    probe/arena_walk.cpp
    probe/windows_regions.cpp
    probe/vm_probe_linux.cpp
    probe/vm_probe_macos.cpp
    probe/vm_probe_windows.cpp
    probe/vm_probe_unimplemented.cpp)

target_link_libraries(rs_probe PUBLIC rs_vm PRIVATE rs_warnings)
add_library(RuntimeSkeptic::probe ALIAS rs_probe)

# ---------------------------------------------------------------------------
# rs_server - Model Context Protocol. Depends on everything else; nothing
# depends on it.
# ---------------------------------------------------------------------------
add_library(rs_server STATIC
    server/mcp_server.cpp)

target_link_libraries(rs_server PUBLIC rs_probe rs_reports PRIVATE rs_warnings)
add_library(RuntimeSkeptic::server ALIAS rs_server)
