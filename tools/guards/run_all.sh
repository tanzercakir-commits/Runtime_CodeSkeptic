#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Every project rule that can be checked mechanically, in one command.
#
#   tools/guards/run_all.sh
#
# These exist because the failure mode this project actually suffers is not a
# wrong algorithm - it is drift. A document asserts something true, the code
# moves, and nothing objects. Over a long session, or across a change of
# author, the repository slowly starts lying about itself.
#
# Every guard here was written after finding a real instance:
#
#   check_docs       13 documented claims about the code had become false
#   check_registry   registry.md said "18 ids" while 25 were declared, and
#                    called RS-VM-0018 unreachable while a rule emitted it
#   validate_schemas the schemas/ directory was empty for the project's whole
#                    life, so no third party could check an artifact
#   check_plan       a [done] with no evidence is just a claim
#   check_non_goals  a normative commitment was broken inside one session
#   check_dates      the author is a language model that saw three different
#                    dates for one day in a single session, and a
#                    `<!-- checked: -->` marker is worthless if the date in it
#                    is whatever the author believed at the time
#   check_todo       four documents carry this project - the spirit, the
#                    compass, the map and the past - and the way it gets lost
#                    is two of them quietly disagreeing. It read `[open]` and
#                    `[blocked]` and NOT `[partial]` - the one marker this
#                    project uses when something is half-true and needs saying
#                    out loud. A gate was held open on a ground with no item on
#                    the compass at all, and on a second ground owned by an
#                    item that was `[done]`
#   check_campaign   a measured number is a claim like any other, and prose
#                    drifts from data exactly the way prose drifts from code
#   check_includes   the first real Windows build failed on std::back_inserter
#                    without <iterator> - a class of defect that is invisible on
#                    every platform this project builds on locally, and costs a
#                    Windows runner minute to find
#   check_shell...   the first real macOS run died on `declare -A`, because macOS
#                    ships bash 3.2. Same class, other language: green wherever
#                    anyone runs it, fatal where nobody does
#   check_probe...   the Windows probe had NEVER run, including on Windows: the
#                    unimplemented stub's guard did not exclude RS_PLATFORM_WINDOWS,
#                    so both defined probe_virtual_memory and the linker took the
#                    stub. Green build, 14/14 ctest, and a synthetic profile
#   check_shell_vars the SECOND thing the first macOS run found: the selftest's
#                    `unknown` rows were keyed on the measured host, and the
#                    profile that would have made them host-independent was
#                    assigned and never read since the day the file was written
#   check_workflow.. the diagnostics channel this project reads INSTEAD of the
#                    Actions tab ran `ctest` with no `-C` on MSVC's multi-config
#                    generator, so every Windows failure it ever published said
#                    "Missing -C <config>?" in place of the failure
#   check_windows... the Windows arena was cross-compiled by hand before pushing,
#                    with a flag list typed from memory that omitted -Wshadow.
#                    MSVC calls that C4456, /WX made it an error, rs_probe did not
#                    build, and the diagnostics published a ctest run of a tree
#                    that had never compiled. It reads the flags out of
#                    CMakeLists.txt now instead of anyone restating them
#   check_profiles.. an external reviewer running the tool on their own M1 found
#                    a committed macOS profile carrying a ceiling bug (max_user_
#                    address 0x600000000000, no max_single_reservation) that two
#                    commits had already fixed - the profile was written before
#                    the fix and never regenerated, and no guard asked whether a
#                    committed measurement still matches the probe that made it.
#                    A measurement older than its instrument records what the old
#                    probe would have said, not the host
#   check_roadmap    the owner's rule, stated 2026-07-30: ROADMAP and PLAN carry
#                    the project's spirit and must not break; TODO and PROGRESS
#                    change with the work. ROADMAP is frozen by hash (edited
#                    exactly once in its life: the commit that created it), and
#                    PLAN must keep mirroring every phase it defines
#   check_progress   PROGRESS called itself append-only, but no check stopped an
#                    old lesson being edited or deleted. Newest-first means new
#                    sessions are prepended after the header; every older byte
#                    must survive unchanged
#   check_release    v0.2 introduced four independent version declarations and
#                    two CI archive names; a package built under one version and
#                    documented under another is a false-green release
#
# `selftest` runs FIRST and is not one of them. Every other guard here passes on
# a repository that has already been fixed - and so would a guard whose patterns
# never match anything at all. It makes each check fail on purpose, against a
# deliberately wrong throwaway tree, before any of them is believed.
#
# Runs on every push. A guard that only runs when someone remembers is a
# comment.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
failed=0
declare -a broken=()

run() {
    local name="$1"; shift
    printf '\n=== %s ===\n' "$name"
    if "$@"; then
        return 0
    fi
    failed=$((failed + 1))
    broken+=("$name")
}

run "guards themselves"     python3 "$HERE/selftest.py"
run "process records"       python3 "$HERE/check_process_contract.py"
run "plan structure"        python3 "$HERE/check_plan.py"
run "documentation drift"   python3 "$HERE/check_docs.py"
run "finding registry"      python3 "$HERE/check_registry.py"
run "schemas vs code"       python3 "$HERE/validate_schemas.py"
run "runtime boundary safety" python3 "$HERE/check_runtime_safety.py"
run "release version consistency" python3 "$HERE/check_release_consistency.py"
run "package verifier fail-closed" python3 "$HERE/../../dist/verify_package_selftest.py"
run "execution coverage ledger" \
    python3 "$HERE/../campaign/groundtruth_execution_coverage_selftest.py"
run "cgroup launcher fail-closed" \
    bash "$HERE/../../tests/groundtruth/cgroup_launcher_selftest.sh"
run "input boundary matrix" python3 "$HERE/../audit/boundary_matrix.py"
run "normative non-goals"   python3 "$HERE/check_non_goals.py"
run "dates against git"     python3 "$HERE/check_dates.py"
run "progress is append-only" python3 "$HERE/check_progress_history.py"
run "compass vs map"        python3 "$HERE/check_todo.py"
run "published numbers"     python3 "$HERE/check_campaign.py"
run "corpus rules"          python3 "$HERE/check_corpus.py"
run "includes vs MSVC"      python3 "$HERE/check_includes.py"
run "shell vs bash 3.2"     python3 "$HERE/check_shell_portability.py"
run "shell dead variables"  python3 "$HERE/check_shell_vars.py"
run "one probe per platform" python3 "$HERE/check_probe_platforms.py"
run "ctest names a config"  python3 "$HERE/check_workflow_ctest.py"
run "windows cross-compile" python3 "$HERE/check_windows_compiles.py"
run "roadmap is frozen"     python3 "$HERE/check_roadmap.py"
run "profiles are fresh"    python3 "$HERE/check_profiles_fresh.py"

echo
if [ "$failed" -eq 0 ]; then
    echo "all guards passed"
    exit 0
fi
echo "$failed guard(s) failed: ${broken[*]+${broken[*]}}" >&2
echo >&2
echo "These are project rules, not style. Fix the repository or change the" >&2
echo "rule deliberately - do not silence the guard." >&2
exit 1
