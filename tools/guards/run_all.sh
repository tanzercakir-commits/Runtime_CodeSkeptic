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

run "plan structure"        python3 "$HERE/check_plan.py"
run "documentation drift"   python3 "$HERE/check_docs.py"
run "finding registry"      python3 "$HERE/check_registry.py"
run "schemas vs code"       python3 "$HERE/validate_schemas.py"
run "normative non-goals"   python3 "$HERE/check_non_goals.py"

echo
if [ "$failed" -eq 0 ]; then
    echo "all guards passed"
    exit 0
fi
echo "$failed guard(s) failed: ${broken[*]}" >&2
echo >&2
echo "These are project rules, not style. Fix the repository or change the" >&2
echo "rule deliberately - do not silence the guard." >&2
exit 1
