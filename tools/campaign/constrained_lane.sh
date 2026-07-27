#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# A SECOND HOST, MADE RATHER THAN WAITED FOR.
#
# T-015 exists because a 5-level-paging runner contradicted the analyzer: 4 PiB
# fits below a 56-bit `max_user_address`, the analyzer said SUPPORTED, and the
# kernel refused anyway. Fitting is not sufficient. `RS-VM-0026` was the answer -
# compare the request against the largest reservation the host actually granted.
#
# Proving that fix needed a host where the two reasons DISAGREE, and the item said
# that meant an LA57 machine. GitHub does not let anyone choose one; it turns up
# occasionally and cannot be arranged. So the fix sat unverified against a real
# kernel, with only a synthetic 56-bit profile behind it.
#
# THE ASSUMPTION WAS THAT THE ONLY WAY TO OPEN THE GAP IS TO RAISE THE CEILING.
# Lowering what the host will grant opens exactly the same gap, and `RLIMIT_AS`
# does that on any Linux host, deterministically, for free:
#
#     ulimit -v unlimited   max_user_address 0x7ffffffff000   granted 2^46
#     ulimit -v 4194304     max_user_address 0x7ffffffff000   granted 2^31
#     ulimit -v 1048576     max_user_address 0x7ffffffff000   granted 2^29
#
# The ceiling does not move, and that asymmetry is not luck either - it is which
# call each measurement uses. `find_max_single_reservation()` maps gigabytes with
# `mmap`, which the kernel charges against `RLIMIT_AS` in `may_expand_vm()`;
# `MAP_NORESERVE` is not an exemption. `find_max_user_address()` probes ONE PAGE
# at a time with `MAP_FIXED_NOREPLACE`, and one page never reaches the limit.
#
# So a 1 TiB request under this limit:
#
#     2^40 < 2^47   fits the address space  -> RS-VM-0021's reason is SILENT
#     2^40 > 2^32   above 2 x granted       -> RS-VM-0026, PROVEN, UNSUPPORTED
#     kernel        ENOMEM
#
# Same contract, unconstrained: SUPPORTED, and the kernel grants it. One document,
# two hosts, opposite verdicts, both pairings held - which is the divergence, and
# it needs no particular hardware.
#
# HONEST ABOUT THE MECHANISM. The `ENOMEM` here is `RLIMIT_AS`, not overcommit or
# VA accounting. `RS-VM-0026` does not ask why: it compares a request against a
# measured grant. The claim under test - "fitting is not sufficient" - is
# identical, and this lane reaches two bands the LA57 round never touches.
#
# LINUX ONLY. macOS ignores `ulimit -v` and Windows has no equivalent, so this is
# wired into the Linux job alone and says so rather than passing vacuously.
#
#   tools/campaign/constrained_lane.sh [LIMIT_KIB]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PROBE="${PROBE:-$ROOT/build/bin/rs-env-probe}"
LIMIT="${1:-4194304}"          # KiB, as `ulimit -v` takes it. 4 GiB.
REQUEST=1099511627776          # 2^40, the size the discriminating contract asks for

[ -x "$PROBE" ] || { echo "$0: $PROBE not found; build first" >&2; exit 64; }

case "$(uname -s)" in
    Linux) ;;
    *)
        echo "$0: SKIPPED - RLIMIT_AS constrains mmap only on Linux; macOS"
        echo "  ignores 'ulimit -v' and Windows has no equivalent. This lane is"
        echo "  wired into the Linux job for that reason."
        exit 0
        ;;
esac

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "constrained lane: RLIMIT_AS = ${LIMIT} KiB"
echo

"$PROBE" vm --name unconstrained --output "$work/free.json" >/dev/null 2>&1 || {
    echo "$0: the unconstrained probe run failed" >&2; exit 70; }

# The probe must run UNDER the limit, or the profile describes a host nobody
# ran the cases on.
( ulimit -v "$LIMIT" 2>/dev/null || exit 65
  "$PROBE" vm --name constrained --output "$work/tight.json" >/dev/null 2>&1 ) || {
    echo "$0: could not run the probe under RLIMIT_AS ${LIMIT} KiB" >&2; exit 70; }

read -r free_ceiling free_granted tight_ceiling tight_granted <<EOF
$(python3 - "$work/free.json" "$work/tight.json" <<'PY'
import json, sys


def facts(path):
    vm = json.load(open(path))["virtual_memory"]
    ceiling = vm.get("max_user_address", {}).get("value")
    granted = vm.get("max_single_reservation", {}).get("value")
    if isinstance(ceiling, str):
        ceiling = int(ceiling, 16 if ceiling.startswith("0x") else 10)
    return ceiling or 0, granted or 0


a = facts(sys.argv[1])
b = facts(sys.argv[2])
print(a[0], a[1], b[0], b[1])
PY
)
EOF

printf '  %-14s %-22s %s\n' "" max_user_address max_single_reservation
printf '  %-14s %-22s %s\n' unconstrained "$(printf '%#x' "$free_ceiling")" "$free_granted"
printf '  %-14s %-22s %s\n' constrained   "$(printf '%#x' "$tight_ceiling")" "$tight_granted"
echo

# THE PRECONDITIONS THIS LANE IS ONLY MEANINGFUL UNDER, checked rather than
# assumed. A lane that quietly stops discriminating is worse than no lane: it
# reports green for a fix nothing tested.
problems=0

if [ "$tight_ceiling" != "$free_ceiling" ]; then
    echo "  NOT DIVERGENT: RLIMIT_AS moved max_user_address as well" >&2
    echo "    The whole point is that the ceiling does not move, so that the" >&2
    echo "    fits-in-the-address-space reason stays silent. If the kernel has" >&2
    echo "    started charging the single-page probe against RLIMIT_AS, this" >&2
    echo "    lane is measuring something else and must be re-derived." >&2
    problems=$((problems + 1))
fi

if [ "$tight_granted" -ge "$free_granted" ]; then
    echo "  NOT DIVERGENT: the limit did not lower max_single_reservation" >&2
    echo "    ($tight_granted vs $free_granted). RLIMIT_AS is not reaching the" >&2
    echo "    reservation probe, so the two reasons still agree." >&2
    problems=$((problems + 1))
fi

if [ "$REQUEST" -ge "$tight_ceiling" ]; then
    echo "  NOT DIVERGENT: the request does not fit the address space" >&2
    echo "    ($REQUEST vs ceiling $tight_ceiling), so RS-VM-0021 would decide" >&2
    echo "    the verdict and RS-VM-0026 would not have to." >&2
    problems=$((problems + 1))
fi

# 2 x granted is RS-VM-0026's PROVEN band. Below it the rule answers
# CONDITIONAL, which the harness records rather than asserts.
if [ "$REQUEST" -le $((tight_granted * 2)) ]; then
    echo "  NOT DIVERGENT: the request is not above 2 x granted, so RS-VM-0026" >&2
    echo "    answers CONDITIONAL and nothing is asserted." >&2
    problems=$((problems + 1))
fi

if [ "$problems" -gt 0 ]; then
    echo >&2
    echo "The constrained lane did not open the gap it exists to open." >&2
    echo "Do not lower the limit until the reason is understood." >&2
    exit 1
fi

echo "  divergence confirmed: $REQUEST fits the address space and exceeds"
echo "  2 x $tight_granted, so RS-VM-0026 decides alone."
echo

# And now the pairing itself, with BOTH the probe and the cases under the limit.
( ulimit -v "$LIMIT"
  GT_BIN="$work/bin" CC="${CC:-cc} -O0" \
    bash "$ROOT/tests/groundtruth/run.sh" "$work/tight.json" )
status=$?

echo
if [ "$status" -eq 0 ]; then
    echo "constrained lane: every asserted pairing held on a host that grants"
    echo "  $tight_granted bytes while offering $(printf '%#x' "$tight_ceiling") of address space"
fi
exit "$status"
