# SPDX-License-Identifier: Apache-2.0

# ---------------------------------------------------------------------------
# rs_core - JSON, hashing, evidence model. No platform dependencies.
# ---------------------------------------------------------------------------
add_library(rs_core STATIC
    core/json.cpp
    core/sha256.cpp
    core/evidence.cpp
    core/io.cpp)

target_include_directories(rs_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>)
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
    vm/analyzer.cpp)

target_link_libraries(rs_vm PUBLIC rs_core PRIVATE rs_warnings)
add_library(RuntimeSkeptic::vm ALIAS rs_vm)

# ---------------------------------------------------------------------------
# rs_reports - rendering. Depends on the domain, never the other way round.
# ---------------------------------------------------------------------------
add_library(rs_reports STATIC
    reports/report.cpp)

target_link_libraries(rs_reports PUBLIC rs_vm PRIVATE rs_warnings)
add_library(RuntimeSkeptic::reports ALIAS rs_reports)

# ---------------------------------------------------------------------------
# rs_probe - platform measurement. Exactly one implementation compiles; the
# other is guarded out by the preprocessor so both files stay syntax-checked
# on every platform.
# ---------------------------------------------------------------------------
add_library(rs_probe STATIC
    probe/vm_probe_linux.cpp
    probe/vm_probe_unimplemented.cpp)

target_link_libraries(rs_probe PUBLIC rs_vm PRIVATE rs_warnings)
add_library(RuntimeSkeptic::probe ALIAS rs_probe)
