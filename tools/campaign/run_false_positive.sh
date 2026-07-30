#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# The false-positive measurement, end to end and reproducible.
#
#   tools/campaign/run_false_positive.sh [OUTDIR]
#
# Requires: strace, a built rs-check and rs-env-probe, and as many of the
# listed programs as this host happens to have. A program that is absent is
# SKIPPED AND COUNTED - the report must never imply a coverage it did not have.
#
# What this measures and why it is not circular is documented in
# tools/campaign/false_positive_rate.py. In one line: the contracts are not
# written by anyone, they are transcribed from what real programs actually did.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$ROOT/build/false-positive}"
BIN="${RS_BIN:-$ROOT/build/bin}"
RUNS="${RUNS:-3}"

for tool in "$BIN/rs-check" "$BIN/rs-env-probe"; do
    [ -x "$tool" ] || { echo "$0: no $tool; build first (RS_BIN=... to override)" >&2; exit 64; }
done
# The tracer is the OS's: strace on Linux, dtrace on macOS (T-018). The
# feasibility measurement (refs/measurements/3af0f9f/dtrace-feasibility)
# established that SIP is disabled on the macos-14 runners and dtrace runs;
# observe_requirements.py picks the tracer by platform, this only checks the
# instrument exists before spending three runs per program finding out.
case "$(uname -s)" in
    Darwin)
        command -v dtrace >/dev/null || { echo "$0: dtrace is required on macOS" >&2; exit 64; }
        sudo -n true 2>/dev/null || { echo "$0: dtrace needs passwordless sudo" >&2; exit 64; }
        ;;
    *)
        command -v strace >/dev/null || { echo "$0: strace is required" >&2; exit 64; }
        ;;
esac

rm -rf "$OUT"; mkdir -p "$OUT"

# A workload per program, chosen so the program does real work rather than
# printing a version string - a JIT that never compiles anything maps nothing
# interesting. Each must exit 0 or it is not evidence that "this works here".
cat > "$OUT/jit.js" <<'JS'
function hot(n){let s=0;for(let i=0;i<n;i++)s=(s+i*7)%1000003;return s}
let r=0; for(let k=0;k<2000;k++) r+=hot(5000);
console.log(r>0?"ok":"no");
JS
cat > "$OUT/jit.java" <<'JAVA'
public class jit { public static void main(String[] a){ long s=0;
  for(int k=0;k<2000;k++){ for(int i=0;i<5000;i++) s=(s+i*7L)%1000003L; }
  System.out.println(s>0?"ok":"no"); } }
JAVA

observe() {
    local label="$1"; shift
    local probe="$1"; shift
    command -v "$probe" >/dev/null || {
        echo "skipped: $label (no $probe on this host)" | tee -a "$OUT/skipped.txt"
        return 0
    }
    python3 "$HERE/observe_requirements.py" --out "$OUT" --runs "$RUNS" \
        --label "$label" -- "$@" >/dev/null 2>>"$OUT/observe.log" \
        && echo "observed: $label" \
        || echo "FAILED   : $label (see $OUT/observe.log)" | tee -a "$OUT/skipped.txt"
}

observe python3 python3 python3 -c 'import json,re,hashlib;print(hashlib.sha256(b"x").hexdigest()[:8])'
observe perl    perl    perl -e '$s=0; $s+=$_ for 1..100000; print "$s\n"'
observe ruby    ruby    ruby -e 's=0;20000.times{|i| s+=i};puts s'
observe php     php     php -r 'echo array_sum(range(1,10000)),PHP_EOL;'
observe git     git     git --version
# /etc/hostname does not exist on macOS; /etc/hosts exists on both.
DOC=/etc/hostname
[ -f "$DOC" ] || DOC=/etc/hosts
observe openssl openssl openssl dgst -sha256 "$DOC"
observe jq      jq      jq -n '1+1'
observe redis   redis-server redis-server --version
observe gzip    gzip    bash -c 'gzip -c /etc/services > /dev/null'
observe xz      xz      bash -c "xz -c -0 $DOC > /dev/null"
observe ffmpeg  ffmpeg  ffmpeg -hide_banner -f lavfi -i testsrc=size=64x64:rate=1 -frames:v 2 -f null -
observe node-jit node   node "$OUT/jit.js"
if command -v javac >/dev/null && javac -d "$OUT" "$OUT/jit.java" 2>/dev/null; then
    observe java-jit java java -cp "$OUT" jit
else
    echo "skipped: java-jit (no javac)" | tee -a "$OUT/skipped.txt"
fi

"$BIN/rs-env-probe" vm --output "$OUT/host.json" >/dev/null 2>&1 || {
    echo "$0: could not probe this host" >&2; exit 70; }

python3 "$HERE/false_positive_rate.py" --dir "$OUT" --profile "$OUT/host.json" \
    --rs-check "$BIN/rs-check" --json "$OUT/false-positive-rate.json"

if [ -s "$OUT/skipped.txt" ]; then
    echo
    echo "NOT COVERED on this host:"
    sed 's/^/  /' "$OUT/skipped.txt"
fi
