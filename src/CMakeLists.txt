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
    vm/analyzer.cpp
    vm/impact.cpp)

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
