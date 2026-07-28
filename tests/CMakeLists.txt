# SPDX-License-Identifier: Apache-2.0

add_library(rs_test_support INTERFACE)
target_include_directories(rs_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

function(rs_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE
        rs_test_support rs_warnings ${ARG_LIBRARIES})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

# -- unit -------------------------------------------------------------------
rs_add_test(test_json          SOURCES unit/test_json.cpp          LIBRARIES rs_core)
rs_add_test(test_sha256        SOURCES unit/test_sha256.cpp        LIBRARIES rs_core)
rs_add_test(test_evidence      SOURCES unit/test_evidence.cpp      LIBRARIES rs_core)
rs_add_test(test_address_range SOURCES unit/test_address_range.cpp LIBRARIES rs_vm)
rs_add_test(test_profile       SOURCES unit/test_profile.cpp       LIBRARIES rs_vm)
rs_add_test(test_analyzer      SOURCES unit/test_analyzer.cpp      LIBRARIES rs_vm)
rs_add_test(test_mutation      SOURCES unit/test_mutation.cpp      LIBRARIES rs_vm)
rs_add_test(test_impact        SOURCES unit/test_impact.cpp        LIBRARIES rs_vm)
target_compile_definitions(test_impact PRIVATE RS_REPO_ROOT="${CMAKE_SOURCE_DIR}")
rs_add_test(test_report        SOURCES unit/test_report.cpp        LIBRARIES rs_reports)
rs_add_test(test_bundle        SOURCES unit/test_bundle.cpp        LIBRARIES rs_vm)
target_compile_definitions(test_bundle PRIVATE RS_REPO_ROOT="${CMAKE_SOURCE_DIR}")
rs_add_test(test_evidence_bundle SOURCES unit/test_evidence_bundle.cpp LIBRARIES rs_reports)

rs_add_test(test_mcp           SOURCES unit/test_mcp.cpp           LIBRARIES rs_server)

# The arena walk is the macOS scan-window logic with the Mach calls injected, so
# it runs HERE - on every platform - against layouts a runner measured. It is
# deliberately not in the conformance section below: it asserts nothing about the
# host it runs on, which is the entire point of it existing.
rs_add_test(test_arena_walk    SOURCES unit/test_arena_walk.cpp    LIBRARIES rs_probe)

# -- conformance ------------------------------------------------------------
# The probe conformance suite measures the real host, so it is only meaningful
# where a probe is implemented. On other platforms it asserts the honest
# fallback behaviour instead.
rs_add_test(test_probe SOURCES conformance/test_probe.cpp LIBRARIES rs_probe)

# -- incidents --------------------------------------------------------------
# Golden tests over the on-disk fixtures in profiles/ and contracts/.
rs_add_test(test_incidents SOURCES incidents/test_incidents.cpp LIBRARIES rs_reports)
target_compile_definitions(test_incidents PRIVATE
    RS_REPO_ROOT="${CMAKE_SOURCE_DIR}")
