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
# Overridable for the same reason RS_CHECK is. It was not, and a caller aiming
# at an out-of-tree build overrode one binary while the other still came from
# ./build - failing with "could not probe this host" when the cause was a path.
RS_PROBE="${RS_PROBE:-$ROOT/build/bin/rs-env-probe}"
[ -x "$RS_CHECK" ] || { echo "$0: no rs-check at $RS_CHECK; build first" >&2; exit 64; }
[ -x "$RS_PROBE" ] || { echo "$0: no rs-env-probe at $RS_PROBE; build first" >&2; exit 64; }

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

# A profile with nothing known, so an UNKNOWN prediction is guaranteed rather
# than hoped for.
#
# This variable was assigned and NEVER USED, from the day the file was written.
# The `unknown` rows were pinned against the MEASURED host instead, on the
# assumption that a real profile would not know anything about
# `0x1307200000` - true on Linux, and false on macOS, where the probe MEASURES
# the Rosetta/GPU carveout [0x1000000000, 0x7000000000) as unavailable and the
# address falls inside it. So on macOS the contract predicts UNSUPPORTED and the
# selftest's own precondition check fired:
#
#   precondition failed - exact-mapping-in-carveout predicts UNSUPPORTED on this
#   host, not UNKNOWN ... fix the row, not this check
#
# Which is exactly what that check was written to do, and it took the first macOS
# run in the project's history to fire it. The row is fixed here, the way the
# unused variable shows it was always meant to be: an UNKNOWN prediction comes
# from a profile that knows nothing, not from a measured host that happens not to
# know one address.
PROFILE="$ROOT/profiles/fixtures/unknown-host.synthetic.json"

# Real host facts, to get SUPPORTED and UNSUPPORTED predictions.
MEASURED="$WORK/measured.json"
"$RS_PROBE" vm --output "$MEASURED" >/dev/null 2>&1 || {
    echo "$0: could not probe this host" >&2; exit 70; }

# Contracts whose verdict on $MEASURED is known, checked here rather than
# assumed - if a rule change moves one of these the selftest says so instead of
# quietly testing the wrong row.
# A LOOKUP FUNCTION, not an associative array.
#
# This was `declare -A want=(...)` with `${want[$key]}` in three places, and it
# worked on every Linux runner and every developer machine. macOS ships bash 3.2
# - GPLv2, frozen in 2007 - which has no associative arrays: `[supported]=...` is
# parsed as an arithmetic subscript, `supported` is read as a variable name, and
# under `set -u` the script dies with `line 57: supported: unbound variable`.
#
# Broken since this file was written, and invisible because macOS only ran in
# ci.yml's `expensive-platforms`, which was gated off pushes. Same class as the
# missing `<iterator>` found the same day: green on every platform anyone was
# running, broken on the one nobody was.
#
# A first attempt replaced it with `printf | while read`, which introduced a
# worse bug: the loop body ran in a subshell, so its `exit 70` on a failed
# precondition would have exited the subshell and let the script carry on
# testing the wrong rows. A function has no subshell and no array.
#
# tools/guards/check_shell_portability.py now catches the class.
want_for() {
    case "$1" in
        supported)   echo "page-size-at-most-64kib:SUPPORTED" ;;
        unsupported) echo "file-map-beyond-eof:UNSUPPORTED" ;;
        unknown)     echo "exact-mapping-in-carveout:UNKNOWN" ;;
        *) echo "" ;;
    esac
}

# Which profile makes a key's prediction true ON EVERY HOST.
profile_for() {
    case "$1" in
        unknown) echo "$PROFILE" ;;    # knows nothing, so UNKNOWN is structural
        *)       echo "$MEASURED" ;;   # needs real facts to predict at all
    esac
}
WANT_KEYS="supported unsupported unknown"

verdict_of() {
    case "$1" in
        0) echo SUPPORTED ;; 1) echo UNSUPPORTED ;;
        2) echo CONDITIONAL ;; 3) echo UNKNOWN ;; *) echo "error($1)" ;;
    esac
}

# Contracts whose verdict on $MEASURED is known, checked here rather than
# assumed - if a rule change moves one of these the selftest says so instead of
# quietly testing the wrong row.
for key in $WANT_KEYS; do
    pair="$(want_for "$key")"
    name="${pair%%:*}"; expect="${pair##*:}"
    "$RS_CHECK" "$HERE/contracts/$name.json" --profile "$(profile_for "$key")" \
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
    name="$(want_for "$key")"; name="${name%%:*}"

    python3 - "$WORK/manifest.json" "$name" "$outcome" <<'PY'
import json, sys
out = sys.argv[3]
json.dump({"schema": "runtime-skeptic.groundtruth-manifest.v1", "cases": [{
    "case": "selftest", "contract": f"contracts/{sys.argv[2]}.json",
    "program": "stub", "args": [out], "why": "harness selftest",
    # The selftest is testing the comparison table, so its rows must be
    # assertable; without this every row becomes RECORD-ONLY and the selftest
    # silently stops testing anything.
    "verifies_all_postconditions": True}]},
    open(sys.argv[1], "w"))
PY

    got=$(GT_MANIFEST="$WORK/manifest.json" GT_CASES="$WORK/cases" \
          GT_BIN="$WORK/bin" GT_CONTRACT_ROOT="$WORK" \
          bash "$HERE/run.sh" "$(profile_for "$key")" 2>/dev/null \
          | awk '/^selftest /{ $1=""; $2=""; $3=""; sub(/^ +/,""); print }' | head -1)
    got="${got:-<no row>}"

    if [ "$got" = "$expected" ]; then
        pass=$((pass + 1)); result="ok"
    else
        fail=$((fail + 1)); result="MISMATCH (got '$got')"
    fi
    shown="$(want_for "$key")"
    printf '%-13s %-11s %-14s %s\n' "${shown##*:}" "$outcome" "$expected" "$result"
done

echo
echo "rows: $((pass + fail))   ok: $pass   mismatched: $fail"
[ "$fail" -eq 0 ] || exit 1
