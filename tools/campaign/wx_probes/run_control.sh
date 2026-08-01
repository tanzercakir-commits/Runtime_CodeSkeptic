#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# The W^X positive control, split by host ROLE - because a single machine cannot
# always play both halves, and pretending it can is what broke the first draft.
#
# Two tiny programs, one incompatibility apart:
#   naive_rwx.c  maps PROT_READ|WRITE|EXEC and runs code in it - the box64-dynarec
#                shape (RS-VM-0009): writable and executable at once, never flipped.
#   wx_flip.c    maps writable, fills, mprotect()s to executable - the compliant
#                W->X flip, never both at once.
#
# THE CIRCULARITY THIS SPLIT RESOLVES. naive_rwx needs to RUN to be observed, but a
# W^X host (Apple Silicon macOS) REFUSES a naive RWX map (EACCES) - the very thing
# it models is the thing it cannot do there. And on a stock personal Mac the
# tracer is unavailable too: dtrace needs SIP disabled and an interactive sudo.
# So the machine that the prediction is ABOUT is often not a machine that can
# OBSERVE. The two roles are therefore separate subcommands:
#
#   observe   PERMISSIVE host only (x86_64 Linux, or aarch64 Linux/Asahi where RWX
#             is granted) with a usable tracer. Runs the probes, captures shapes.
#             Self-gates: on a W^X or untraceable host it SKIPS with a plain reason
#             (exit 0), it does not fail.
#   predict   ANY host, including a hardened M1. Takes the committed shapes in
#             fixtures/ (captured on a permissive host), predicts against a W^X
#             profile, seals a bundle, and rs-replays it. THIS is the half an M1
#             validates. Its verdict is the control's verdict.
#
# The RS-VM-0009-present (naive) vs RS-VM-0009-absent (flip) split is the whole
# W^X decision tree, and predict asserts it from the fixtures end to end.
#
# Usage:
#   run_control.sh [predict] [W^X_PROFILE.json]   # default; runs anywhere
#   run_control.sh observe                        # permissive host only
#   run_control.sh auto  [W^X_PROFILE.json]       # predict, plus observe if able
set -eu

MODE="${1:-predict}"
case "$MODE" in predict|observe|auto) PROFILE_ARG="${2:-}";;
    *) MODE="predict"; PROFILE_ARG="${1:-}";;   # bare profile path, no subcommand
esac

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
RS_CHECK="$ROOT/build/bin/rs-check"
RS_REPLAY="$ROOT/build/bin/rs-replay"
RS_PROBE="$ROOT/build/bin/rs-env-probe"
OBSERVE="$ROOT/tools/campaign/observe_requirements.py"
FIXDIR="$HERE/fixtures"
OUT="${TMPDIR:-/tmp}/wx-control"

[ -x "$RS_CHECK" ] || { echo "$0: $RS_CHECK not found; build the project first" >&2; exit 64; }
rm -rf "$OUT"; mkdir -p "$OUT"

# The W^X reference host for prediction. On Apple Silicon measure the real one
# (rs-env-probe needs no dtrace, so SIP does not block it); otherwise fall back to
# the committed macOS-arm64 profile, a genuine 16K/W^X host.
resolve_profile() {
    if [ -n "$PROFILE_ARG" ]; then echo "$PROFILE_ARG"; return; fi
    if [ "$(uname -s)" = "Darwin" ] && [ -x "$RS_PROBE" ] \
       && "$RS_PROBE" vm --output "$OUT/host.json" >/dev/null 2>&1; then
        echo "$OUT/host.json"
    else
        echo "$ROOT/profiles/measured/macos-14-arm64-native.measured.json"
    fi
}

predict() {
    profile=$(resolve_profile)
    echo "predict: fixtures/ vs W^X profile $(basename "$profile")"
    pfail=0

    "$RS_CHECK" "$FIXDIR/naive-rwx.shape.json" --profile "$profile" \
        --format json > "$OUT/naive.json" 2>/dev/null || true
    python3 - "$OUT/naive.json" naive <<'PY' || pfail=1
import json, sys
d = json.load(open(sys.argv[1]))
ids = sorted({f["id"] for r in d.get("results", [d]) for f in r.get("findings", [])})
ok = d.get("overall") == "UNSUPPORTED" and "RS-VM-0009" in ids and "RS-VM-0011" in ids
print(f"  naive_rwx  -> {d.get('overall')}  {ids}  "
      f"{'OK' if ok else 'FAIL (want UNSUPPORTED + RS-VM-0009 + RS-VM-0011)'}")
sys.exit(0 if ok else 1)
PY

    "$RS_CHECK" "$FIXDIR/wx-flip.shape.json" --profile "$profile" \
        --format json > "$OUT/flip.json" 2>/dev/null || true
    python3 - "$OUT/flip.json" flip <<'PY' || pfail=1
import json, sys
d = json.load(open(sys.argv[1]))
ids = sorted({f["id"] for r in d.get("results", [d]) for f in r.get("findings", [])})
ok = "RS-VM-0009" not in ids
print(f"  wx_flip    -> {d.get('overall')}  {ids}  "
      f"RS-VM-0009 {'absent OK' if ok else 'PRESENT FAIL (flip is not simultaneous W+X)'}")
sys.exit(0 if ok else 1)
PY

    "$RS_CHECK" "$FIXDIR/naive-rwx.shape.json" --profile "$profile" \
        --bundle "$OUT/bundle" >/dev/null 2>&1 || true
    if "$RS_REPLAY" "$OUT/bundle" >/dev/null 2>&1; then
        echo "  bundle     -> rs-replay reproduced the recorded verdict  OK"
    else
        echo "  bundle     -> rs-replay FAILED"; pfail=1
    fi
    return "$pfail"
}

# Observe on a PERMISSIVE host. Self-gates: a W^X or untraceable host is not a
# failure, it is the wrong host - say so and skip.
observe() {
    if [ "$(uname -s)" = "Darwin" ]; then
        sip=$(csrutil status 2>/dev/null || echo "status unknown")
        echo "observe: SKIP on this Mac - naive RWX is refused here (W^X), and the"
        echo "  tracer is out of reach (dtrace needs SIP disabled: '$sip'; and an"
        echo "  interactive sudo). Observe on a permissive host - x86_64 Linux, or"
        echo "  aarch64 Linux/Asahi - then run 'predict' here against your M1 profile."
        SKIP_REASON="not a permissive host (macOS enforces W^X; dtrace needs SIP off + sudo)"
        return 2
    fi
    command -v strace >/dev/null 2>&1 || {
        echo "observe: SKIP - no strace on PATH"
        SKIP_REASON="no strace on PATH"; return 2; }
    cc -O0 -o "$OUT/naive" "$HERE/naive_rwx.c"
    cc -O0 -o "$OUT/flip" "$HERE/wx_flip.c"
    python3 "$OBSERVE" --out "$OUT/obs-naive" --runs 3 --label naive-rwx -- "$OUT/naive" >/dev/null 2>&1 || true
    python3 "$OBSERVE" --out "$OUT/obs-flip" --runs 3 --label wx-flip -- "$OUT/flip" >/dev/null 2>&1 || true
    python3 - "$OUT/obs-naive/naive-rwx.shape.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
reqs = d.get("requirements", d if isinstance(d, list) else d.get("results", []))
n = sum(1 for r in reqs
        if r.get("request", {}).get("protection", {}).get("write")
        and r.get("request", {}).get("protection", {}).get("execute"))
print(f"observe: fresh capture on this permissive host - "
      f"{n} simultaneous-W+X requirement(s) (fixtures/ carries 1). "
      f"{'consistent' if n >= 1 else 'INCONSISTENT with the committed fixture'}")
sys.exit(0 if n >= 1 else 1)
PY
}

# A skipped phase is not a pass. observe() returns 2 (skip) distinctly from 0
# (ran, passed) and 1 (ran, failed), and the summary says SKIPPED - matching the
# vocabulary the mingw guard already uses - so a log scrape for PASS/FAIL never
# banks a green tick for a phase that did not run. Skips keep exit 0.
SKIP_REASON=""
prc=0; orc=0
case "$MODE" in
    predict) if predict; then prc=0; else prc=$?; fi ;;
    observe) if observe; then orc=0; else orc=$?; fi ;;
    auto)    if predict; then prc=0; else prc=$?; fi; echo
             if observe; then orc=0; else orc=$?; fi ;;
esac

say() {   # $1 = phase, $2 = rc: 0 PASS, 2 SKIPPED, else FAIL
    if [ "$2" -eq 0 ]; then echo "W^X control ($1): PASS"
    elif [ "$2" -eq 2 ]; then echo "W^X control ($1): SKIPPED - $SKIP_REASON"
    else echo "W^X control ($1): FAIL"; fi
}

echo
fail=0
case "$MODE" in
    predict) say predict "$prc"; [ "$prc" -eq 0 ] || fail=1 ;;
    observe) say observe "$orc"; [ "$orc" -ne 1 ] || fail=1 ;;
    auto)    say predict "$prc"; [ "$prc" -eq 0 ] || fail=1
             say observe "$orc"; [ "$orc" -ne 1 ] || fail=1 ;;
esac
exit "$fail"
