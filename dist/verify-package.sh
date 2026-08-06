#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Fail-closed verification shared by every prebuilt package builder.
set -uo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 PACKAGE_DIR" >&2
    exit 64
fi

D="$1"
CHECK="$D/bin/rs-check"
CONTRACT="$D/contracts/campaign/redis-jemalloc-page-size-lg12.json"
ROOT_CONTRACT="$D/contracts/emulator-highmem-guest-mapping.json"
PROFILE="$D/profiles/measured/macos-14-arm64-native.measured.json"

if command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
elif command -v python >/dev/null 2>&1; then
    PYTHON=python
else
    echo "release verification failed: Python 3 is required to validate artifacts" >&2
    exit 1
fi

for required in "$CONTRACT" "$ROOT_CONTRACT" "$PROFILE" \
    "$D/QUICKSTART.md" "$D/RELEASE.md" "$D/LICENSE" \
    "$D/include/runtimeskeptic/runtime/runtime.h" \
    "$D/include/runtimeskeptic/runtime/trace.hpp" \
    "$D/lib/cmake/RuntimeSkeptic/RuntimeSkepticConfig.cmake"; do
    if [ ! -e "$required" ]; then
        echo "release verification failed: missing $required" >&2
        exit 1
    fi
done
for tool in rs-check rs-env-probe rs-profile rs-replay rs-mcp \
            rs-runtime-sample rs-runtime-benchmark; do
    candidate="$D/bin/$tool"
    if [ ! -x "$candidate" ]; then
        echo "release verification failed: missing or non-executable $candidate" >&2
        exit 1
    fi
done

runtime_library=
for candidate in "$D"/lib/libruntimeskeptic.* "$D"/bin/runtimeskeptic.dll; do
    if [ -f "$candidate" ]; then
        runtime_library=$candidate
        break
    fi
done
if [ -z "$runtime_library" ]; then
    echo "release verification failed: missing libruntimeskeptic" >&2
    exit 1
fi

output="$("$CHECK" "$CONTRACT" --profile "$PROFILE" 2>&1)"
status=$?
if [ "$status" -ne 1 ]; then
    echo "release verification failed: Redis demo exit $status, expected 1 (UNSUPPORTED)" >&2
    printf '%s\n' "$output" >&2
    exit 1
fi
case "$output" in
    *UNSUPPORTED*RS-VM-0006*) ;;
    *)
        echo "release verification failed: Redis demo lacks UNSUPPORTED / RS-VM-0006" >&2
        printf '%s\n' "$output" >&2
        exit 1
        ;;
esac

TRACE="$D/.verify-runtime-trace.jsonl"
OVERHEAD="$D/.verify-runtime-overhead.json"
"$D/bin/rs-runtime-sample" "$TRACE" >/dev/null || {
    echo "release verification failed: runtime sample did not execute" >&2
    exit 1
}
runtime_output="$("$D/bin/rs-replay" trace "$TRACE" 2>&1)"
runtime_status=$?
if [ "$runtime_status" -ne 0 ]; then
    echo "release verification failed: runtime replay exit $runtime_status, expected 0" >&2
    printf '%s\n' "$runtime_output" >&2
    exit 1
fi
case "$runtime_output" in
    *reproduced*without*issuing*OS*calls*) ;;
    *)
        echo "release verification failed: runtime trace did not replay" >&2
        printf '%s\n' "$runtime_output" >&2
        exit 1
        ;;
esac
"$D/bin/rs-runtime-benchmark" --iterations 8 --output "$OVERHEAD" \
    >/dev/null || {
        echo "release verification failed: runtime benchmark did not execute" >&2
        exit 1
    }
if [ ! -s "$OVERHEAD" ]; then
    echo "release verification failed: runtime benchmark artifact is empty" >&2
    exit 1
fi
"$PYTHON" - "$OVERHEAD" <<'PY' || exit 1
import json
import sys

path = sys.argv[1]
try:
    with open(path, encoding="utf-8") as stream:
        doc = json.load(stream)
except (OSError, json.JSONDecodeError) as exc:
    raise SystemExit(f"release verification failed: invalid benchmark JSON: {exc}")

root_keys = {"schema", "iterations", "unit", "modes"}
mode_keys = {"name", "median_ns", "p95_ns", "delta_ns", "ratio_milli"}
names = {"native", "compile_disabled", "runtime_disabled",
         "enabled_buffered", "enabled_plus_flush"}
integer = lambda value: isinstance(value, int) and not isinstance(value, bool)
if not isinstance(doc, dict) or set(doc) != root_keys:
    raise SystemExit("release verification failed: benchmark root fields violate schema")
if doc["schema"] != "runtime-skeptic.runtime-overhead.v1":
    raise SystemExit("release verification failed: wrong benchmark schema id")
if not integer(doc["iterations"]) or not 1 <= doc["iterations"] <= 2048:
    raise SystemExit("release verification failed: benchmark iterations violate schema")
if doc["unit"] != "nanoseconds":
    raise SystemExit("release verification failed: benchmark unit violates schema")
modes = doc["modes"]
if not isinstance(modes, list) or len(modes) != 5:
    raise SystemExit("release verification failed: benchmark modes violate schema")
if {mode.get("name") for mode in modes if isinstance(mode, dict)} != names:
    raise SystemExit("release verification failed: benchmark mode names violate schema")
for mode in modes:
    if not isinstance(mode, dict) or set(mode) != mode_keys:
        raise SystemExit("release verification failed: benchmark mode fields violate schema")
    for field in ("median_ns", "p95_ns", "delta_ns", "ratio_milli"):
        if not integer(mode[field]):
            raise SystemExit(f"release verification failed: {field} is not an integer")
    if min(mode["median_ns"], mode["p95_ns"], mode["ratio_milli"]) < 0:
        raise SystemExit("release verification failed: negative benchmark measurement")
PY
rm -f -- "$TRACE" "$OVERHEAD"

echo "release verification: package complete; analyzer demo and runtime replay passed"
