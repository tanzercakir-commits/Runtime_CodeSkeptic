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

# Binaries carry a .exe suffix on Windows and MSVC's multi-config generator
# puts them under build/bin/RelWithDebInfo/. Resolve BIN to wherever rs-check
# actually is, so the same script drives all three operating systems.
EXE=""
if [ ! -x "$BIN/rs-check" ] && [ -x "$BIN/rs-check.exe" ]; then EXE=".exe"; fi
if [ ! -x "$BIN/rs-check$EXE" ] && [ -x "$BIN/RelWithDebInfo/rs-check.exe" ]; then
    BIN="$BIN/RelWithDebInfo"; EXE=".exe"
fi
for tool in "$BIN/rs-check$EXE" "$BIN/rs-env-probe$EXE"; do
    [ -x "$tool" ] || { echo "$0: no $tool; build first (RS_BIN=... to override)" >&2; exit 64; }
done
# The tracer is the OS's: strace on Linux, dtrace on macOS, ETW on Windows
# (T-018/T-022). observe_requirements.py picks it by platform; this only
# checks the instrument exists before spending three runs per program finding
# out. macos: SIP is disabled on the runners (dtrace-feasibility round).
# windows: logman/tracerpt are built in, and the NT Kernel Logger needs the
# admin token the GitHub runner user already carries (etw-feasibility round).
case "$(uname -s)" in
    Darwin)
        command -v dtrace >/dev/null || { echo "$0: dtrace is required on macOS" >&2; exit 64; }
        sudo -n true 2>/dev/null || { echo "$0: dtrace needs passwordless sudo" >&2; exit 64; }
        ;;
    MINGW*|MSYS*|CYGWIN*|Windows*)
        command -v logman >/dev/null || { echo "$0: logman is required on Windows" >&2; exit 64; }
        command -v tracerpt >/dev/null || { echo "$0: tracerpt is required on Windows" >&2; exit 64; }
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

# The interpreter is `python3` on Linux/macOS and `python` on Windows. Both
# observe_requirements.py and false_positive_rate.py run through it.
PY="$(command -v python3 2>/dev/null || command -v python 2>/dev/null)"
[ -n "$PY" ] || { echo "$0: no python interpreter found" >&2; exit 64; }

observe() {
    local label="$1"; shift
    local probe="$1"; shift
    command -v "$probe" >/dev/null || {
        echo "skipped: $label (no $probe on this host)" | tee -a "$OUT/skipped.txt"
        return 0
    }
    "$PY" "$HERE/observe_requirements.py" --out "$OUT" --runs "$RUNS" \
        --label "$label" -- "$@" >>"$OUT/observe.log" 2>&1 \
        && echo "observed: $label" \
        || echo "FAILED   : $label (see $OUT/observe.log)" | tee -a "$OUT/skipped.txt"
}

# Workloads are chosen PATH-FREE wherever the interpreter is a native Windows
# binary (python, node): a native exe launched from Git Bash does not
# understand a POSIX path argument, and passing one is a portability trap that
# costs a CI round. `-c`/`-e` inline code sidesteps it. File-reading workloads
# (gzip, openssl) use the repo's own README.md, and the MSYS binaries that
# provide them on Windows do their own path translation.
DOC="$ROOT/README.md"
observe python3 "$PY" "$PY" -c 'import json,re,hashlib;print(hashlib.sha256(b"x").hexdigest()[:8])'
observe perl    perl    perl -e '$s=0; $s+=$_ for 1..100000; print "$s\n"'
observe ruby    ruby    ruby -e 's=0;20000.times{|i| s+=i};puts s'
observe php     php     php -r 'echo array_sum(range(1,10000)),PHP_EOL;'
observe git     git     git --version
observe openssl openssl openssl rand -hex 32
observe jq      jq      jq -n '1+1'
observe redis   redis-server redis-server --version
observe gzip    gzip    gzip -c "$DOC"
observe xz      xz      xz -c -0 "$DOC"
observe ffmpeg  ffmpeg  ffmpeg -hide_banner -f lavfi -i testsrc=size=64x64:rate=1 -frames:v 2 -f null -
observe node-jit node   node -e 'function hot(n){let s=0;for(let i=0;i<n;i++)s=(s+i*7)%1000003;return s}let r=0;for(let k=0;k<2000;k++)r+=hot(5000);console.log(r>0?"ok":"no");'
# java takes a classpath PATH argument, so it is only wired where the launcher
# understands POSIX paths (not native Windows). javac failing on a POSIX path
# under a native JDK falls through to the skip, which is the Windows outcome.
if command -v javac >/dev/null && javac -d "$OUT" "$OUT/jit.java" 2>/dev/null; then
    observe java-jit java java -cp "$OUT" jit
else
    echo "skipped: java-jit (no javac, or a native JDK that cannot read the POSIX path)" \
        | tee -a "$OUT/skipped.txt"
fi

"$BIN/rs-env-probe$EXE" vm --output "$OUT/host.json" >/dev/null 2>&1 || {
    echo "$0: could not probe this host" >&2; exit 70; }

"$PY" "$HERE/false_positive_rate.py" --dir "$OUT" --profile "$OUT/host.json" \
    --rs-check "$BIN/rs-check$EXE" --json "$OUT/false-positive-rate.json"

if [ -s "$OUT/skipped.txt" ]; then
    echo
    echo "NOT COVERED on this host:"
    sed 's/^/  /' "$OUT/skipped.txt"
fi
