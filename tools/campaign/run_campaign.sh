#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Runs every campaign contract against a measured host profile and prints one
# row per contract. Used to produce docs/campaigns/*.md.
#
# The point of a campaign is not to collect green ticks. It is to find out
# where the analyzer is WRONG - where it says UNSUPPORTED about code that
# demonstrably runs, or SUPPORTED about code that demonstrably does not. Those
# rows are the valuable ones; read the expectation column before the verdict.
set -uo pipefail

RS_CHECK="${RS_CHECK:-build/bin/rs-check}"
PROFILE="${1:-}"
CONTRACT_DIR="${2:-contracts/campaign}"

if [ -z "$PROFILE" ]; then
    echo "usage: $0 HOST-PROFILE.json [CONTRACT_DIR]" >&2
    exit 64
fi
if [ ! -x "$RS_CHECK" ]; then
    echo "$0: $RS_CHECK not found; build first" >&2
    exit 64
fi

verdict_of() {
    case "$1" in
        0) echo "SUPPORTED" ;;
        1) echo "UNSUPPORTED" ;;
        2) echo "CONDITIONAL" ;;
        3) echo "UNKNOWN" ;;
        *) echo "error($1)" ;;
    esac
}

total=0
declare -A counts=( [SUPPORTED]=0 [UNSUPPORTED]=0 [CONDITIONAL]=0 [UNKNOWN]=0 )

printf '%-44s %-13s %-13s %s\n' CONTRACT VERDICT EXPECTED FINDINGS
printf '%.0s-' {1..110}; echo

for contract in "$CONTRACT_DIR"/*.json; do
    [ -e "$contract" ] || continue
    name=$(basename "$contract" .json)

    # An expectation lives in the contract itself so it cannot drift away from
    # the case it describes. Absent means "we have no prior belief".
    expected=$(python3 -c "
import json,sys
try:
    d=json.load(open('$contract'))
    print(d.get('x_campaign',{}).get('expected_verdict','-'))
except Exception:
    print('-')
")

    json_out=$("$RS_CHECK" "$contract" --profile "$PROFILE" --format json 2>/dev/null)
    code=$?
    verdict=$(verdict_of "$code")
    total=$((total + 1))
    [ -n "${counts[$verdict]+x}" ] && counts[$verdict]=$(( counts[$verdict] + 1 ))

    findings=$(printf '%s' "$json_out" | python3 -c "
import json,sys
try:
    d=json.load(sys.stdin)
    ids=[]
    for r in d.get('results',[d]):
        for f in r.get('findings',[]):
            ids.append(f\"{f['id']}/{f['confidence'][:4]}\")
    print(','.join(ids) if ids else '-')
except Exception:
    print('?')
")

    mark=' '
    if [ "$expected" != "-" ] && [ "$expected" != "$verdict" ]; then
        mark='!'
    fi
    printf '%s%-43s %-13s %-13s %s\n' "$mark" "$name" "$verdict" "$expected" "$findings"
done

echo
echo "total: $total   supported: ${counts[SUPPORTED]}   unsupported: ${counts[UNSUPPORTED]}   conditional: ${counts[CONDITIONAL]}   unknown: ${counts[UNKNOWN]}"
echo "rows marked ! disagree with the expectation recorded in the contract."
