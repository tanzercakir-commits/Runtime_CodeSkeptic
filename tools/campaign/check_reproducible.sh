#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Runs rs-env-probe twice, as two SEPARATE PROCESSES, and fails if the two
# profiles do not have the same profile_id.
#
# WHY A SCRIPT AND NOT A UNIT TEST. There is already a conformance test called
# `repeated_runs_produce_the_same_profile_id`, and it passes. It calls
# probe_virtual_memory() twice inside one process, so both calls see the same
# image base, the same __PAGEZERO, the same everything ASLR decides at exec
# time. It cannot observe cross-process variance, which is the only kind that
# exists here. "Repeated runs" meant two calls; it needed to mean two runs.
#
# WHAT IT CAUGHT. On macOS under Rosetta 2, min_map_address was recorded as a
# measured_capability host fact, derived from a binary search for the lowest
# placeable page - which finds the end of the probe's OWN image:
#
#   run 1 (2d9ea6c)  0x10a8f2000
#   run 2 (fd649c7)  0x10d841000
#
# A translated image is slid by ASLR, so the "host fact" moved ~48 MiB between
# two runs of the same CI job on the same runner. Six campaign contracts were
# returning a confident UNSUPPORTED off the back of it, and profile_id - which
# is supposed to identify a host - differed between two measurements of one
# machine.
#
# A fact that changes when nothing about the host changed is not a fact about
# the host. This script is the thing that says so out loud.
set -uo pipefail

PROBE="${PROBE:-build/bin/rs-env-probe}"
PROFILE_TOOL="${PROFILE_TOOL:-build/bin/rs-profile}"

for f in "$PROBE" "$PROFILE_TOOL"; do
    [ -x "$f" ] || { echo "$0: $f not found; build first" >&2; exit 64; }
done

# HOW MANY RUNS, AND WHY IT IS NOT TWO.
#
# Two runs catch a defect that makes every run differ. They catch a BISTABLE one
# - a probe that alternates between exactly two outputs depending on an ASLR coin
# flip - only when the flip lands differently, which is half the time.
#
# That is not hypothetical. macOS alternated between exactly two profile_ids
# (621881a4... / 5f5f73a7...) across every push for two days, because a landmark
# our own mapping happened to sit on was recorded with a different `note` from the
# same landmark when free, and the note is inside the hashed facts subtree. CI was
# red on roughly every second push and green on the others, which reads as flake
# and is not.
#
# Five runs miss a 50/50 defect with probability 2^-4 = 6%, for four extra probe
# executions of a few hundred milliseconds each. The whole argument of this script
# is that asking once is not asking; asking twice is the smallest version of the
# same mistake.
RUNS="${RUNS:-5}"
if [ "$RUNS" -lt 2 ]; then
    echo "$0: RUNS must be at least 2 (asking once is not asking)" >&2
    exit 64
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# bash 3.2: no `mapfile`, and an empty array's expansion is unbound under `set -u`
# (tools/guards/check_shell_portability.py knows both). Seeded, never empty.
ids="" ; first=""
n=1
while [ "$n" -le "$RUNS" ]; do
    out="$work/run-$n.json"
    "$PROBE" vm --name "reproducibility-$n" --output "$out" >/dev/null 2>&1 || {
        echo "$0: probe run $n of $RUNS failed" >&2; exit 70; }
    id=$("$PROFILE_TOOL" id "$out")
    ids="$ids$n $id
"
    [ -z "$first" ] && first="$id"
    n=$((n + 1))
done

# The FIRST run that disagrees with run 1, so the diff below compares a real pair
# rather than two arbitrary ones.
odd=""
n=2
while [ "$n" -le "$RUNS" ]; do
    if [ "$("$PROFILE_TOOL" id "$work/run-$n.json")" != "$first" ]; then
        odd="$n"
        break
    fi
    n=$((n + 1))
done

if [ -z "$odd" ]; then
    echo "reproducible: $RUNS separate probe processes agree"
    echo "  profile_id $first"
    exit 0
fi

cp "$work/run-1.json" "$work/a.json"
cp "$work/run-$odd.json" "$work/b.json"
id_a="$first"
id_b=$("$PROFILE_TOOL" id "$work/b.json")
echo "$RUNS runs, and run $odd is the one that disagrees with run 1:" >&2
printf '%s' "$ids" >&2

echo "NOT REPRODUCIBLE: two runs of the probe on this host disagree" >&2
echo "  run 1  $id_a" >&2
echo "  run 2  $id_b" >&2
echo >&2
echo "Facts that differ:" >&2
python3 - "$work/a.json" "$work/b.json" >&2 <<'PY'
import json, sys
a = json.load(open(sys.argv[1]))["virtual_memory"]
b = json.load(open(sys.argv[2]))["virtual_memory"]
for key in sorted(set(a) | set(b)):
    if a.get(key) == b.get(key):
        continue
    if key.endswith("_ranges") or key.endswith("_codes"):
        la, lb = a.get(key, []), b.get(key, [])
        print(f"  {key}: {len(la)} vs {len(lb)} entries")
        # AND WHICH ONES, because the count is the least informative thing this
        # can say and it was all it said. On 2026-07-26 the macOS runner reported
        #
        #     available_ranges: 22 vs 22 entries
        #
        # and stopped there - the ONE case where the count carries no information
        # is the case where the counts are equal, which is exactly the shape of
        # the bug this check exists for (the same ranges, differently placed).
        # Whoever read that had to reproduce it on a macOS host to learn anything,
        # and this project's whole method is that a failure carries its evidence.
        # KEYED ON THE BOUNDS, BUT COMPARED IN FULL, and the difference between
        # those two is not academic. The first version of this printed only what
        # was in one list and not the other, keyed on `[start, end)` - and the
        # first macOS failure it met had 22 ranges with IDENTICAL bounds in both
        # runs, differing in the `note`. So it printed the count, then nothing,
        # exactly like the version it replaced. A diagnostic that reports "these
        # differ" and cannot say how is the shape this whole session kept meeting.
        def sig(entry):
            if isinstance(entry, dict):
                return f"[{entry.get('start')}, {entry.get('end')})"
            return repr(entry)

        def brief(v, n=160):
            s = v if isinstance(v, str) else repr(v)
            return s if len(s) <= n else s[:n] + f"... (+{len(s) - n} chars)"

        by_a, by_b = {}, {}
        for x in la:
            by_a.setdefault(sig(x), []).append(x)
        for x in lb:
            by_b.setdefault(sig(x), []).append(x)
        only_a = [k for k in by_a if k not in by_b]
        only_b = [k for k in by_b if k not in by_a]
        # Same bounds, different content: the case the bounds-only diff missed.
        changed = [k for k in by_a if k in by_b and by_a[k] != by_b[k]]

        for s in only_a[:8]:
            print(f"    only in run 1: {s}")
        if len(only_a) > 8:
            print(f"    ... and {len(only_a) - 8} more only in run 1")
        for s in only_b[:8]:
            print(f"    only in run 2: {s}")
        if len(only_b) > 8:
            print(f"    ... and {len(only_b) - 8} more only in run 2")

        for s in changed[:8]:
            ea, eb = by_a[s][0], by_b[s][0]
            print(f"    same bounds, different content: {s}")
            fields = sorted(set(ea) | set(eb)) if isinstance(ea, dict) else []
            for f in fields:
                if ea.get(f) != eb.get(f):
                    print(f"      {f}:")
                    print(f"        run 1: {brief(ea.get(f))}")
                    print(f"        run 2: {brief(eb.get(f))}")
        if len(changed) > 8:
            print(f"    ... and {len(changed) - 8} more with the same bounds")

        if not only_a and not only_b and not changed:
            print("    every entry matches by bounds AND content, so the lists "
                  "differ only in ORDER - which the canonical form is supposed "
                  "to remove, making this a serialization bug rather than a "
                  "probe one")
        continue
    va = (a.get(key) or {}).get("value")
    vb = (b.get(key) or {}).get("value")
    print(f"  {key}: {va!r} vs {vb!r}")
PY
echo >&2
echo "A fact that changes while the host does not is a fact about the PROBE." >&2
echo "Record it as a probe_run warning, not as a host capability - a measured" >&2
echo "capability permits a PROVEN verdict, and this one cannot support it." >&2
exit 1
