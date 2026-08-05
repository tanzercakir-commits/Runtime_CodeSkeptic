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
rs_add_test(test_schema        SOURCES unit/test_schema.cpp        LIBRARIES rs_core)
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
rs_add_test(test_trace         SOURCES unit/test_trace.cpp         LIBRARIES rs_trace runtimeskeptic)

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

find_package(Threads REQUIRED)
rs_add_test(test_runtime SOURCES conformance/test_runtime.cpp
            LIBRARIES runtimeskeptic Threads::Threads)
rs_add_test(test_runtime_assert SOURCES conformance/test_runtime_assert.cpp
            LIBRARIES runtimeskeptic)

add_executable(test_runtime_c_api conformance/test_runtime_c_api.c)
target_link_libraries(test_runtime_c_api PRIVATE runtimeskeptic rs_warnings)
add_test(NAME test_runtime_c_api COMMAND test_runtime_c_api)
set_tests_properties(test_runtime_c_api PROPERTIES TIMEOUT 30)

# This target deliberately does not link libruntimeskeptic. The platform header
# aliases wrappers to native APIs, proving compile-time disable is link-free.
add_executable(test_runtime_compile_disabled
    conformance/test_runtime_compile_disabled.c)
target_include_directories(test_runtime_compile_disabled PRIVATE
    ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_runtime_compile_disabled PRIVATE rs_warnings)
add_test(NAME test_runtime_compile_disabled COMMAND test_runtime_compile_disabled)
set_tests_properties(test_runtime_compile_disabled PROPERTIES TIMEOUT 30)

add_test(NAME test_runtime_benchmark
    COMMAND $<TARGET_FILE:rs-runtime-benchmark> --iterations 16
            --output ${CMAKE_CURRENT_BINARY_DIR}/runtime-overhead.json)
set_tests_properties(test_runtime_benchmark PROPERTIES TIMEOUT 60)

add_test(NAME test_runtime_install
    COMMAND ${CMAKE_COMMAND}
        -DRS_BUILD_DIR=${CMAKE_BINARY_DIR}
        -DRS_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/runtime-install-test
        -DRS_CONFIG=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_runtime_install.cmake)
set_tests_properties(test_runtime_install PROPERTIES TIMEOUT 120)

# A native Windows control for the reserve/commit model. It is intentionally a
# standalone executable: ground-truth cases may use platform APIs, but may not
# include or link RuntimeSkeptic code and thereby let the tool grade itself.
# The child alone is placed under a 64 MiB Job Object commit limit, so the test
# cannot create machine-wide memory pressure.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(test_windows_reserve_commit_job
        groundtruth/windows/reserve_commit_job.cpp)
    target_link_libraries(test_windows_reserve_commit_job PRIVATE rs_warnings)
    add_test(NAME test_windows_reserve_commit_job
        COMMAND test_windows_reserve_commit_job)
    set_tests_properties(test_windows_reserve_commit_job PROPERTIES TIMEOUT 15)
endif()

# -- incidents --------------------------------------------------------------
# Golden tests over the on-disk fixtures in profiles/ and contracts/.
rs_add_test(test_incidents SOURCES incidents/test_incidents.cpp LIBRARIES rs_reports)
target_compile_definitions(test_incidents PRIVATE
    RS_REPO_ROOT="${CMAKE_SOURCE_DIR}")
