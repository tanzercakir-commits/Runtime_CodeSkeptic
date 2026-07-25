#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Measures how much of the analysis is actually about the host.
#
# The July 2026 campaign recorded this as defect 8: "One reviewer predicted all
# twelve of their verdicts correctly by reading analyzer.cpp and never opening
# the profile... it is the honest measure of how much of the analysis is
# currently platform-sensitive, and it is the number to move." It was never
# measured, because measuring it needs more than one host and there was only
# one.
#
# THE TEST. Run every contract twice: once against a real measured profile, and
# once against a profile in which every fact is unknown. If both runs produce
# the same verdict AND the same findings, the host contributed nothing - the
# answer came out of the rule list. That is not an inference from two hosts
# happening to agree; it is a direct demonstration that the profile was not
# consulted.
#
# A verdict of UNKNOWN on the empty profile is the tool working correctly: it
# depends on a fact and refuses to guess. Those rows are the platform-sensitive
# ones and they are what the number should grow.
#
#   tools/campaign/host_sensitivity.sh REAL-PROFILE.json [CONTRACT_DIR]
set -uo pipefail

RS_CHECK="${RS_CHECK:-build/bin/rs-check}"
REAL="${1:-}"
CONTRACT_DIR="${2:-contracts/campaign}"
EMPTY="${EMPTY_PROFILE:-profiles/fixtures/unknown-host.synthetic.json}"

if [ -z "$REAL" ]; then
    echo "usage: $0 REAL-PROFILE.json [CONTRACT_DIR]" >&2
    exit 64
fi
for f in "$RS_CHECK" "$EMPTY" "$REAL"; do
    [ -e "$f" ] || { echo "$0: $f not found" >&2; exit 64; }
done

verdict_of() {
    case "$1" in
        0) echo "SUPPORTED" ;; 1) echo "UNSUPPORTED" ;;
        2) echo "CONDITIONAL" ;; 3) echo "UNKNOWN" ;;
        *) echo "error($1)" ;;
    esac
}

# Verdict plus the finding ids that produced it. Two runs agreeing on the
# verdict for different reasons are NOT the same answer.
#
# --no-unknowns matters here and the first version of this script was wrong
# without it. RS-VM-0017 ("a platform fact this request depends on was never
# established") fires on the empty profile BY CONSTRUCTION - every fact is
# unknown - and never on a measured one. Comparing signatures that include it
# means every contract differs, and the script reported 100% platform-sensitive
# while `redis-jemalloc-hinted-identity` was sitting there returning
# UNSUPPORTED / RS-VM-0014 / COUNTEREXAMPLE on both profiles.
#
# The flag suppresses the informational finding without changing the verdict,
# which leaves exactly the reasoning that drove the answer.
signature() {
    local out
    out=$("$RS_CHECK" "$1" --profile "$2" --format json --no-unknowns 2>/dev/null)
    local code=$?
    printf '%s|%s' "$(verdict_of $code)" "$(printf '%s' "$out" | python3 -c "
import json,sys
try:
    d=json.load(sys.stdin)
    ids=sorted(f['id'] for r in d.get('results',[d]) for f in r.get('findings',[]))
    print(','.join(ids) if ids else '-')
except Exception:
    print('?')
")"
}

total=0; insensitive=0; sensitive=0
insensitive_names=()

printf '%-44s %-13s %-13s %s\n' CONTRACT 'REAL HOST' 'NO HOST' CONSULTED_THE_HOST
printf '%.0s-' {1..104}; echo

for contract in "$CONTRACT_DIR"/*.json; do
    [ -e "$contract" ] || continue
    name=$(basename "$contract" .json)
    real_sig=$(signature "$contract" "$REAL")
    empty_sig=$(signature "$contract" "$EMPTY")
    total=$((total + 1))

    if [ "$real_sig" = "$empty_sig" ]; then
        consulted="no - derivable from the rules alone"
        insensitive=$((insensitive + 1))
        insensitive_names+=("$name")
    else
        consulted="yes"
        sensitive=$((sensitive + 1))
    fi
    printf '%-44s %-13s %-13s %s\n' "$name" "${real_sig%%|*}" "${empty_sig%%|*}" "$consulted"
done

echo
echo "contracts: $total   consulted the host: $sensitive   did not: $insensitive"
if [ "$total" -gt 0 ]; then
    echo "platform-sensitive: $(( sensitive * 100 / total ))%   <- this is the number to move"
fi
if [ "${#insensitive_names[@]}" -gt 0 ]; then
    echo
    echo "Answered without opening the profile:"
    printf '  %s\n' "${insensitive_names[@]}"
    echo
    echo "These are not necessarily wrong. They are the rows where the tool"
    echo "restated its own rule list, and a reviewer could have predicted them"
    echo "without a host in front of them."
fi
