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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${RS_SCHEMA_DIR}/application-requirements.v1.json
    ${RS_SCHEMA_DIR}/application-requirements-bundle.v1.json
    ${RS_SCHEMA_DIR}/environment-profile.v1.json
    ${RS_SCHEMA_DIR}/analysis-bundle.v1.json)
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
    $<BUILD_INTERFACE:${RS_GENERATED_DIR}>)
target_link_libraries(rs_core PRIVATE rs_warnings PUBLIC rs_platform)
add_library(RuntimeSkeptic::core ALIAS rs_core)

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
