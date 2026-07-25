#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Tests the harness itself.
#
# run.sh decides, for every case, whether a prediction was upheld or
# contradicted. Nothing tested that decision. It was exercised only by whatever
# the host happened to do, which on Linux meant `satisfied` and `faulted` and
# nothing else - so the mapping for `relocated`, for a SUPPORTED prediction
# meeting a refusal, and for a case that dies without printing had never once
# been executed. A comparison table that is silently wrong makes every green
# ground-truth run meaningless.
#
# So: a stub case that reports whatever outcome it is told, paired with
# contracts whose predictions on a known profile are known, and an assertion
# about what run.sh concluded.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
RS_CHECK="${RS_CHECK:-$ROOT/build/bin/rs-check}"
[ -x "$RS_CHECK" ] || { echo "$0: build first" >&2; exit 64; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/cases" "$WORK/contracts"

# A case that reports the outcome named in its argv. This is the only program in
# this directory allowed to lie about what a host did; everything else measures.
cat > "$WORK/cases/stub.c" <<'STUB'
#include <stdio.h>
int main(int argc, char** argv) {
    if (argc < 3) return 64;
    if (argv[2][0] == '-') return 1;   /* "-" means: print nothing, exit 1 */
    printf("{\"case\":\"%s\",\"outcome\":\"%s\",\"detail\":\"stub\"}\n",
           argv[1], argv[2]);
    return 0;
}
STUB

# A profile with nothing known, so a contract's prediction is easy to pin.
PROFILE="$ROOT/profiles/fixtures/unknown-host.synthetic.json"

# Real host facts, to get SUPPORTED and UNSUPPORTED predictions.
MEASURED="$WORK/measured.json"
"$ROOT/build/bin/rs-env-probe" vm --output "$MEASURED" >/dev/null 2>&1 || {
    echo "$0: could not probe this host" >&2; exit 70; }

# Contracts whose verdict on $MEASURED is known, checked here rather than
# assumed - if a rule change moves one of these the selftest says so instead of
# quietly testing the wrong row.
declare -A want=(
    [supported]="page-size-at-most-64kib:SUPPORTED"
    [unsupported]="file-map-beyond-eof:UNSUPPORTED"
    [unknown]="exact-mapping-in-carveout:UNKNOWN"
)
verdict_of() {
    case "$1" in
        0) echo SUPPORTED ;; 1) echo UNSUPPORTED ;;
        2) echo CONDITIONAL ;; 3) echo UNKNOWN ;; *) echo "error($1)" ;;
    esac
}
for key in "${!want[@]}"; do
    name="${want[$key]%%:*}"; expect="${want[$key]##*:}"
    "$RS_CHECK" "$HERE/contracts/$name.json" --profile "$MEASURED" \
        --format json >/dev/null 2>&1
    got=$(verdict_of $?)
    if [ "$got" != "$expect" ]; then
        echo "$0: precondition failed - $name predicts $got on this host, not $expect" >&2
        echo "  the selftest's rows are keyed on that; fix the row, not this check" >&2
        exit 70
    fi
    cp "$HERE/contracts/$name.json" "$WORK/contracts/"
done

# --- the table under test --------------------------------------------------
# prediction-source | stub outcome | expected classification
ROWS=(
    "supported|satisfied|held"
    "supported|refused|CONTRADICTED"
    "supported|relocated|CONTRADICTED"
    "supported|faulted|CONTRADICTED"
    "supported|skipped|skipped"
    "supported|-|CASE BROKEN"
    "unsupported|refused|held"
    "unsupported|relocated|held"
    "unsupported|faulted|held"
    "unsupported|satisfied|CONTRADICTED"
    "unsupported|skipped|skipped"
    "unsupported|-|CASE BROKEN"
    "unknown|satisfied|not asserted"
    "unknown|refused|not asserted"
)

pass=0; fail=0
printf '%-13s %-11s %-14s %s\n' PREDICTION OUTCOME EXPECTED RESULT
printf '%.0s-' {1..64}; echo

for row in "${ROWS[@]}"; do
    IFS='|' read -r key outcome expected <<<"$row"
    name="${want[$key]%%:*}"

    python3 - "$WORK/manifest.json" "$name" "$outcome" <<'PY'
import json, sys
out = sys.argv[3]
json.dump({"schema": "runtime-skeptic.groundtruth-manifest.v1", "cases": [{
    "case": "selftest", "contract": f"contracts/{sys.argv[2]}.json",
    "program": "stub", "args": [out], "why": "harness selftest"}]},
    open(sys.argv[1], "w"))
PY

    got=$(GT_MANIFEST="$WORK/manifest.json" GT_CASES="$WORK/cases" \
          GT_BIN="$WORK/bin" GT_CONTRACT_ROOT="$WORK" \
          bash "$HERE/run.sh" "$MEASURED" 2>/dev/null \
          | awk '/^selftest /{ $1=""; $2=""; $3=""; sub(/^ +/,""); print }' | head -1)
    got="${got:-<no row>}"

    if [ "$got" = "$expected" ]; then
        pass=$((pass + 1)); result="ok"
    else
        fail=$((fail + 1)); result="MISMATCH (got '$got')"
    fi
    printf '%-13s %-11s %-14s %s\n' "${want[$key]##*:}" "$outcome" "$expected" "$result"
done

echo
echo "rows: $((pass + fail))   ok: $pass   mismatched: $fail"
[ "$fail" -eq 0 ] || exit 1
