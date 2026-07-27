#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Compares what rs-check predicted against what the host actually did.
#
#   tests/groundtruth/run.sh MEASURED-PROFILE.json
#
# The profile must have been measured on the machine running this. Comparing a
# prediction derived from one host's facts against an execution on another
# measures nothing at all.
#
# Exit 0 if every asserted pairing held, 1 if any prediction was contradicted.
# See README.md for why CONDITIONALLY_SUPPORTED and UNKNOWN are recorded rather
# than asserted.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
RS_CHECK="${RS_CHECK:-$ROOT/build/bin/rs-check}"
BIN="${GT_BIN:-$ROOT/build/groundtruth}"
CASES="${GT_CASES:-$HERE/cases}"
MANIFEST="${GT_MANIFEST:-$HERE/manifest.json}"
PROFILE="${1:-}"

if [ -z "$PROFILE" ]; then
    echo "usage: $0 MEASURED-PROFILE.json" >&2
    exit 64
fi
[ -x "$RS_CHECK" ] || { echo "$0: $RS_CHECK not found; build first" >&2; exit 64; }
[ -f "$PROFILE" ] || { echo "$0: $PROFILE not found" >&2; exit 64; }

# --- build the cases -------------------------------------------------------
# Compiled here rather than by the project's CMake, on purpose: these programs
# must not acquire the project's include paths, defines or link libraries. A
# case that could reach RuntimeSkeptic code would be the tool agreeing with
# itself in a more expensive way.
mkdir -p "$BIN"
CC="${CC:-cc}"
# CC MAY CARRY FLAGS, and this harness assumed it could not.
#
# `"$CC"` - quoted - is one word to the shell, so `CC="cc -arch x86_64"` became a
# command named `cc -arch x86_64`:
#
#     run.sh: line 47: cc -arch x86_64: command not found      (exit 70)
#
# Latent since the harness was written. It works on every path where CC is a single
# word - `cc` natively, `gcc` and `clang` on Linux - and fires only on the one path
# that has to carry a flag: the Rosetta 2 lane of macos-probe.yml, which builds the
# case programs as x86-64 with `CC="cc -arch x86_64"`. That lane is also the only
# place this project can observe the translated address space, so the harness broke
# exactly where it was most needed and nowhere else.
#
# `CC` carrying flags is the make/autoconf convention, so the harness honours it
# rather than asking the caller to split them.
#
# `read -ra` rather than leaving `$CC` unquoted: an unquoted expansion word-splits
# AND globs, and a compiler path is not a glob. Splitting explicitly says that
# splitting is intended, which an unquoted expansion only implies.
#
# `${CC_ARGV[@]+...}` rather than `"${CC_ARGV[@]}"`: macOS ships bash 3.2, where an
# empty array's expansion is an unbound variable under `set -u`. That cost a macOS
# run of its own, and this is the same file it cost it in.
read -ra CC_ARGV <<<"$CC"
if [ "${#CC_ARGV[@]}" -eq 0 ]; then
    echo "$0: CC is empty" >&2
    exit 64
fi
# Fail here, with the compiler named, rather than inside the build loop where a
# missing compiler reads like a broken case.
if ! command -v "${CC_ARGV[0]}" >/dev/null; then
    echo "$0: no compiler '${CC_ARGV[0]}' on PATH (CC=$CC)" >&2
    exit 64
fi
CONTRACT_ROOT="${GT_CONTRACT_ROOT:-$HERE}"
# -Werror is load-bearing, not tidiness. The first version of this harness
# compiled a case with an implicit declaration of strsignal(), the compiler
# assumed it returned int, printf("%s", ...) got an integer where a pointer
# belonged, and the case segfaulted while reporting the very signal it existed
# to observe. The warning was printed and thrown away.
for src in "$CASES"/*.c; do
    out="$BIN/$(basename "$src" .c)"
    if ! "${CC_ARGV[@]+${CC_ARGV[@]}}" \
            -std=gnu11 -O1 -Wall -Wextra -Werror -o "$out" "$src" \
            2>"$BIN/build.log"; then
        echo "$0: failed to build $(basename "$src"):" >&2
        cat "$BIN/build.log" >&2
        exit 70
    fi
    if [ -s "$BIN/build.log" ]; then
        echo "$0: $(basename "$src") built with diagnostics:" >&2
        cat "$BIN/build.log" >&2
    fi
done

verdict_of() {
    case "$1" in
        0) echo "SUPPORTED" ;;   1) echo "UNSUPPORTED" ;;
        2) echo "CONDITIONAL" ;; 3) echo "UNKNOWN" ;;
        *) echo "error($1)" ;;
    esac
}

host=$(python3 "$ROOT/tools/campaign/host_key.py" "$PROFILE") || exit 65
echo "host: $host"
echo "profile: $(basename "$PROFILE")"
echo

printf '%-34s %-13s %-11s %s\n' CASE PREDICTED REALITY PAIRING
printf '%.0s-' {1..108}; echo

total=0; held=0; contradicted=0; unasserted=0
declare -a failures=()

count=$(python3 -c "import json;print(len(json.load(open('$MANIFEST'))['cases']))")
for i in $(seq 0 $((count - 1))); do
    read -r name contract program <<<"$(python3 -c "
import json
c = json.load(open('$MANIFEST'))['cases'][$i]
print(c['case'], c['contract'], c['program'])
")"
    # `mapfile` is bash 4+ and macOS ships bash 3.2, so this file would have
    # died here the moment the selftest above stopped dying first. Read the
    # array the portable way.
    args=()
    # Counted here rather than asked for later: `${#args[@]}` on an array that
    # may be empty is an unbound variable under `set -u` in bash 3.2, which is
    # what macOS ships. tools/guards/check_shell_portability.py caught this one
    # on the push that introduced it, before any runner did.
    nargs=0
    while IFS= read -r _arg; do
        args+=("$_arg")
        nargs=$((nargs + 1))
    done < <(python3 -c "
import json
for a in json.load(open('$MANIFEST'))['cases'][$i]['args']: print(a)
")

    # A CONTRACT MAY NOT NAME A CEILING AS A CONSTANT. See derive_contract.py:
    # `exact-mapping-above-user-space` asked for 0x800000000000 and its name
    # asserts that is above user space - true on a 4-level host, false by 63 PiB
    # on a 5-level one, where the constant sits in ordinary mappable space. The
    # sibling case `oversized-reservation-4pib` was CONTRADICTED on exactly such
    # a runner for exactly this reason.
    #
    # A profile that does not carry the fact derives NOTHING and the committed
    # constant stands, so a synthetic profile cannot quietly become a
    # measurement - selftest.sh drives that path on purpose.
    use_contract="$CONTRACT_ROOT/$contract"
    derive_from=$(python3 -c "
import json
print(json.load(open('$MANIFEST'))['cases'][$i].get('derive_address_from', ''))
")
    if [ -n "$derive_from" ]; then
        derived="$BIN/derived-$name.json"
        if derived_addr=$(python3 "$HERE/derive_contract.py" \
                "$CONTRACT_ROOT/$contract" "$PROFILE" "$derive_from" "$derived"); then
            use_contract="$derived"
            # The case program takes the same address; substituting only the
            # contract would compare a prediction about one address against an
            # observation of another.
            if [ "$nargs" -gt 0 ]; then args[0]="$derived_addr"; fi
        fi
    fi

    "$RS_CHECK" "$use_contract" --profile "$PROFILE" --format json >/dev/null 2>&1
    predicted=$(verdict_of $?)

    # A case may only settle a prediction if it checks EVERY postcondition its
    # contract states. exact-mapping-misaligned reported `satisfied` having
    # verified placement and ignored the alignment clause next to it, which
    # would have contradicted a correct UNSUPPORTED. Anything not declaring
    # full coverage is recorded, never asserted.
    full=$(python3 -c "
import json
c = json.load(open('$MANIFEST'))['cases'][$i]
print('yes' if c.get('verifies_all_postconditions') is True else 'no')
")
    if [ "$full" != "yes" ]; then predicted="RECORD-ONLY"; fi

    # "${args[@]+...}" and not "${args[@]}", because bash 3.2 under `set -u`
    # treats an EMPTY array's expansion as an unbound variable - and macOS ships
    # bash 3.2. This died as `run.sh: line 111: args[@]: unbound variable` on the
    # first macOS run that got this far, and the harness correctly recorded
    # file-map-beyond-eof as CASE BROKEN rather than as a confirmed refusal.
    if ! observed_json=$("$BIN/$program" "$name" ${args[@]+"${args[@]}"} 2>/dev/null); then
        observed_json=""
    fi
    outcome=$(printf '%s' "$observed_json" | python3 -c "
import json,sys
try: print(json.load(sys.stdin)['outcome'])
except Exception: print('no-output')
")
    detail=$(printf '%s' "$observed_json" | python3 -c "
import json,sys
try: print(json.load(sys.stdin).get('detail',''))
except Exception: print('the case produced no parsable output')
")

    total=$((total + 1))
    case "$predicted" in
        SUPPORTED)
            if [ "$outcome" = "satisfied" ]; then
                mark="held"; held=$((held + 1))
            elif [ "$outcome" = "skipped" ]; then
                mark="skipped"; unasserted=$((unasserted + 1))
            elif [ "$outcome" = "no-output" ]; then
                mark="CASE BROKEN"; contradicted=$((contradicted + 1))
                failures+=("$name: the case produced no parsable output, so nothing was observed")
            else
                mark="CONTRADICTED"; contradicted=$((contradicted + 1))
                failures+=("$name: predicted SUPPORTED, reality $outcome - $detail")
            fi
            ;;
        UNSUPPORTED)
            if [ "$outcome" = "satisfied" ]; then
                mark="CONTRADICTED"; contradicted=$((contradicted + 1))
                failures+=("$name: predicted UNSUPPORTED, but the operation succeeded - $detail")
            elif [ "$outcome" = "no-output" ]; then
                mark="CASE BROKEN"; contradicted=$((contradicted + 1))
                failures+=("$name: the case produced no parsable output, so nothing was observed - a broken case must not read as a confirmed refusal")
            elif [ "$outcome" = "skipped" ]; then
                mark="skipped"; unasserted=$((unasserted + 1))
            else
                mark="held"; held=$((held + 1))
            fi
            ;;
        *)
            mark="not asserted"; unasserted=$((unasserted + 1))
            ;;
    esac

    printf '%-34s %-13s %-11s %s\n' "$name" "$predicted" "$outcome" "$mark"
    printf '    %s\n' "$detail"
done

echo
echo "cases: $total   held: $held   contradicted: $contradicted   not asserted: $unasserted"

if [ "$contradicted" -gt 0 ]; then
    echo
    echo "CONTRADICTED PREDICTIONS - each is a finding, not a flake:"
    for f in ${failures[@]+"${failures[@]}"}; do echo "  - $f"; done
    echo
    echo "Either the analyzer is wrong about this platform or the contract"
    echo "misdescribes the program. Decide which by reading the case source; do"
    echo "not adjust the expectation to match the output."
    exit 1
fi

if [ "$unasserted" -eq "$total" ]; then
    echo
    echo "WARNING: nothing was asserted. Every prediction was CONDITIONAL," >&2
    echo "UNKNOWN or skipped, so this run compared nothing." >&2
fi
exit 0
