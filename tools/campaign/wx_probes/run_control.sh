#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# The W^X positive control: two tiny programs, one incompatibility apart.
#
# naive_rwx.c maps a page PROT_READ|WRITE|EXEC and runs code in it - the
# box64-dynarec shape (RS-VM-0009): writable and executable at the same time,
# never flipped. wx_flip.c maps it writable, fills it, then mprotect()s it
# executable - the W^X-compliant JIT shape, never both at once.
#
# On x86_64 Linux both SUCCEED (W^X is not enforced); the point is not whether
# they run but what the OBSERVER captures and what rs-check PREDICTS about a
# W^X host from it:
#
#   naive_rwx -> a simultaneous-W+X requirement -> UNSUPPORTED on Apple Silicon,
#                RS-VM-0009 (simultaneous W+X restricted) + RS-VM-0011 (JIT
#                entitlement), at OBSERVED confidence (it came from a real trace).
#   wx_flip   -> a W->X transition, NO simultaneous-W+X requirement -> the flip
#                is NOT flagged for RS-VM-0009; it still needs RS-VM-0011.
#
# That RS-VM-0009-present-vs-absent split is the whole W^X decision tree, and
# this script asserts it end to end: observe -> predict -> seal a bundle ->
# rs-replay it. Run it on an M1 before trusting a box64 verdict; if the control
# does not pass, the instrument is wrong, not the target.
#
# Usage: run_control.sh [W^X_PROFILE.json]
#   Default profile: live-measured on macOS (rs-env-probe), else the committed
#   profiles/measured/macos-14-arm64-native.measured.json.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
RS_CHECK="$ROOT/build/bin/rs-check"
RS_REPLAY="$ROOT/build/bin/rs-replay"
RS_PROBE="$ROOT/build/bin/rs-env-probe"
OBSERVE="$ROOT/tools/campaign/observe_requirements.py"
OUT="${TMPDIR:-/tmp}/wx-control"

if [ ! -x "$RS_CHECK" ]; then
    echo "$0: $RS_CHECK not found; build the project first" >&2
    exit 64
fi

rm -rf "$OUT"
mkdir -p "$OUT"

# The W^X reference host. On Apple Silicon, measure the real one; otherwise fall
# back to the committed macOS-arm64 profile, which is a genuine 16K/W^X host.
PROFILE="${1:-}"
if [ -z "$PROFILE" ]; then
    if [ "$(uname -s)" = "Darwin" ] && [ -x "$RS_PROBE" ]; then
        "$RS_PROBE" vm --output "$OUT/host.json"
        PROFILE="$OUT/host.json"
        echo "control: measured this host -> $PROFILE"
    else
        PROFILE="$ROOT/profiles/measured/macos-14-arm64-native.measured.json"
        echo "control: not macOS (or no probe); using committed W^X profile"
    fi
fi

cc -O0 -o "$OUT/naive_rwx" "$HERE/naive_rwx.c"
cc -O0 -o "$OUT/wx_flip" "$HERE/wx_flip.c"

python3 "$OBSERVE" --out "$OUT/obs-naive" --runs 3 --label naive-rwx -- "$OUT/naive_rwx"
python3 "$OBSERVE" --out "$OUT/obs-flip" --runs 3 --label wx-flip -- "$OUT/wx_flip"

# Pull the one simultaneous-W+X requirement out of the naive observation.
python3 - "$OUT/obs-naive/naive-rwx.shape.json" "$OUT/naive-rwx-req.json" <<'PY'
import json, sys
src, dst = sys.argv[1], sys.argv[2]
d = json.load(open(src))
reqs = d.get("requirements", d if isinstance(d, list) else d.get("results", []))
rwx = [r for r in reqs
       if r.get("request", {}).get("protection", {}).get("write")
       and r.get("request", {}).get("protection", {}).get("execute")]
if len(rwx) != 1:
    print(f"control FAIL: expected exactly 1 simultaneous-W+X req, got {len(rwx)}",
          file=sys.stderr)
    sys.exit(1)
r = rwx[0]
if "schema" not in r:
    r = {"schema": "runtime-skeptic.application-requirements.v1", **r}
json.dump(r, open(dst, "w"), indent=2)
PY

fail=0

# 1. naive RWX must be UNSUPPORTED on the W^X host, with 0009 AND 0011.
# rs-check writes to a file; the checker reads it via argv, not stdin - a
# heredoc program cannot also be fed piped stdin (the heredoc wins), so the
# JSON travels as a file.
"$RS_CHECK" "$OUT/naive-rwx-req.json" --profile "$PROFILE" --format json \
    > "$OUT/naive-verdict.json" 2>/dev/null || true
python3 - "$OUT/naive-verdict.json" <<'PY' || fail=1
import json, sys
d = json.load(open(sys.argv[1]))
r = d.get("results", [d])[0]
ids = [f["id"] for f in r.get("findings", [])]
ok = ("RS-VM-0009" in ids) and ("RS-VM-0011" in ids)
print(f"  naive_rwx  -> {r.get('overall')}  findings={sorted(set(ids))}  "
      f"{'OK' if ok else 'FAIL (want RS-VM-0009 + RS-VM-0011)'}")
sys.exit(0 if ok else 1)
PY

# 2. the flip must NOT be flagged for simultaneous W+X (no RS-VM-0009).
"$RS_CHECK" "$OUT/obs-flip/wx-flip.shape.json" --profile "$PROFILE" --format json \
    > "$OUT/flip-verdict.json" 2>/dev/null || true
python3 - "$OUT/flip-verdict.json" <<'PY' || fail=1
import json, sys
d = json.load(open(sys.argv[1]))
ids = [f["id"] for r in d.get("results", [d]) for f in r.get("findings", [])]
ok = "RS-VM-0009" not in ids
print(f"  wx_flip    -> RS-VM-0009 {'absent' if ok else 'PRESENT'}  "
      f"findings={sorted(set(ids))}  {'OK' if ok else 'FAIL (flip must not be simultaneous W+X)'}")
sys.exit(0 if ok else 1)
PY

# 3. seal a tamper-evident bundle and replay it.
"$RS_CHECK" "$OUT/naive-rwx-req.json" --profile "$PROFILE" --bundle "$OUT/bundle" >/dev/null 2>&1 || true
if "$RS_REPLAY" "$OUT/bundle" >/dev/null 2>&1; then
    echo "  bundle     -> rs-replay reproduced the recorded verdict  OK"
else
    echo "  bundle     -> rs-replay FAILED"
    fail=1
fi

echo
if [ "$fail" -eq 0 ]; then
    echo "W^X control: PASS (observe -> predict -> bundle -> replay, all green)"
else
    echo "W^X control: FAIL"
fi
exit "$fail"
