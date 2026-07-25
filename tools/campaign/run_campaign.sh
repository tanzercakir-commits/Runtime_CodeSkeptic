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

# An expectation is only meaningful about a kind of host. Comparing a verdict
# against one recorded on a different platform manufactures disagreements out
# of a host difference, which is the opposite of what a campaign is for.
HOST_KEY=$(python3 "$(dirname "$0")/host_key.py" "$PROFILE") || exit 65

total=0
unqualified=0
declare -A counts=( [SUPPORTED]=0 [UNSUPPORTED]=0 [CONDITIONAL]=0 [UNKNOWN]=0 )

echo "host: $HOST_KEY"
echo
printf '%-44s %-13s %-13s %s\n' CONTRACT VERDICT EXPECTED FINDINGS
printf '%.0s-' {1..110}; echo

for contract in "$CONTRACT_DIR"/*.json; do
    [ -e "$contract" ] || continue
    name=$(basename "$contract" .json)

    # An expectation lives in the contract itself so it cannot drift away from
    # the case it describes, and it is keyed by the host it was recorded on.
    #
    #   expected_verdict_by_host[<key>]   preferred, explicit
    #   expected_verdict                  applies only on expected_on_host
    #
    # Anything else prints "n/a" - not a pass, not a failure. We have no prior
    # belief about this contract on this host, and saying so is the honest
    # answer. Filling those in from the tool's own output would be the tool
    # grading its own homework.
    expected=$(HOST_KEY="$HOST_KEY" python3 -c "
import json,os,sys
key=os.environ['HOST_KEY']
try:
    c=json.load(open('$contract')).get('x_campaign',{})
    by_host=c.get('expected_verdict_by_host') or {}
    if key in by_host:
        print(by_host[key])
    elif c.get('expected_verdict') and c.get('expected_on_host')==key:
        print(c['expected_verdict'])
    else:
        print('n/a')
except Exception:
    print('n/a')
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
    if [ "$expected" = "n/a" ]; then
        unqualified=$((unqualified + 1))
    elif [ "$expected" != "$verdict" ]; then
        mark='!'
    fi
    printf '%s%-43s %-13s %-13s %s\n' "$mark" "$name" "$verdict" "$expected" "$findings"
done

echo
echo "total: $total   supported: ${counts[SUPPORTED]}   unsupported: ${counts[UNSUPPORTED]}   conditional: ${counts[CONDITIONAL]}   unknown: ${counts[UNKNOWN]}"
echo "rows marked ! disagree with an expectation recorded for $HOST_KEY."
if [ "$unqualified" -gt 0 ]; then
    echo "$unqualified of $total contracts have no expectation recorded for this host (EXPECTED n/a)."
    echo "Those rows are unreviewed output, not agreement. Record an expectation by"
    echo "hand, after reading the report - never by copying the verdict back in."
fi
