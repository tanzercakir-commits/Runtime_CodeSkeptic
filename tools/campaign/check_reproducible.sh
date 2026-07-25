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

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$PROBE" vm --name reproducibility-a --output "$work/a.json" >/dev/null 2>&1 || {
    echo "$0: first probe run failed" >&2; exit 70; }
"$PROBE" vm --name reproducibility-b --output "$work/b.json" >/dev/null 2>&1 || {
    echo "$0: second probe run failed" >&2; exit 70; }

id_a=$("$PROFILE_TOOL" id "$work/a.json")
id_b=$("$PROFILE_TOOL" id "$work/b.json")

if [ "$id_a" = "$id_b" ]; then
    echo "reproducible: two separate probe processes agree"
    echo "  profile_id $id_a"
    exit 0
fi

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
        print(f"  {key}: {len(a.get(key, []))} vs {len(b.get(key, []))} entries")
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
