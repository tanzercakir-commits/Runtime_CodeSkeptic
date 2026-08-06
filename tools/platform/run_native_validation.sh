#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Build, test and measure on the machine executing this script.  This is the
# remote half of the manual RISC-V64 evidence workflow; it never provisions a
# host and refuses to continue if the native ISA is not the requested one.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
EXPECTED_ARCH="${1:-}"
OUTPUT_DIR="${2:-}"

if [ -z "$EXPECTED_ARCH" ] || [ -z "$OUTPUT_DIR" ]; then
    echo "usage: $0 EXPECTED-ARCH OUTPUT-DIR" >&2
    exit 64
fi

for variable in RS_SOURCE_REVISION RS_RUNNER_CLASS RS_PROVIDER RS_RUNNER_IMAGE; do
    eval "value=\${$variable:-}"
    if [ -z "$value" ]; then
        echo "$0: $variable is required for attributable evidence" >&2
        exit 64
    fi
done

case "$RS_RUNNER_CLASS" in
    github_hosted_vm|self_hosted_vm|bare_metal) ;;
    *) echo "$0: unsupported RS_RUNNER_CLASS=$RS_RUNNER_CLASS" >&2; exit 64 ;;
esac

ACTUAL_ARCH="$(uname -m)"
if [ "$ACTUAL_ARCH" != "$EXPECTED_ARCH" ]; then
    echo "$0: native architecture mismatch: expected $EXPECTED_ARCH, got $ACTUAL_ARCH" >&2
    exit 65
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/runtimeskeptic-native.XXXXXX")"
cleanup() {
    rm -rf -- "$WORK"
}
trap cleanup EXIT HUP INT TERM

BUILD="$WORK/build"
mkdir -p "$OUTPUT_DIR"

cmake -S "$ROOT" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DRS_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD" --config RelWithDebInfo --parallel
ctest --test-dir "$BUILD" --build-config RelWithDebInfo --output-on-failure

BIN="$BUILD/bin"
PROFILE_A="$OUTPUT_DIR/profile-a.json"
PROFILE_B="$OUTPUT_DIR/profile-b.json"
"$BIN/rs-env-probe" vm --name "native-$EXPECTED_ARCH-a" --output "$PROFILE_A"
"$BIN/rs-env-probe" vm --name "native-$EXPECTED_ARCH-b" --output "$PROFILE_B"
"$BIN/rs-profile" verify "$PROFILE_A"
"$BIN/rs-profile" verify "$PROFILE_B"

python3 "$ROOT/tools/ci/validate_native_profile.py" \
    --profile "$PROFILE_A" \
    --compare-profile "$PROFILE_B" \
    --expected-os linux \
    --expected-arch "$EXPECTED_ARCH" \
    --expected-translation-mode unknown \
    --runner-class "$RS_RUNNER_CLASS" \
    --provider "$RS_PROVIDER" \
    --runner-image "$RS_RUNNER_IMAGE" \
    --source-revision "$RS_SOURCE_REVISION" \
    --output "$OUTPUT_DIR/validation.json"

RS_CHECK="$BIN/rs-check" RS_PROBE="$BIN/rs-env-probe" \
    bash "$ROOT/tests/groundtruth/selftest.sh"

RS_CHECK="$BIN/rs-check" \
GT_BIN="$WORK/groundtruth" \
GT_LEDGER="$OUTPUT_DIR/groundtruth.ledger.jsonl" \
    bash "$ROOT/tests/groundtruth/run.sh" "$PROFILE_A" \
    2>&1 | tee "$OUTPUT_DIR/groundtruth.txt"

echo "native validation complete: $EXPECTED_ARCH -> $OUTPUT_DIR"
