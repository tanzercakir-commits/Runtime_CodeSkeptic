#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Build the macOS arm64 CI verification package. Workflows upload it as an
# ephemeral Actions artifact; v0.2 has no permanent binary Release. Run it on an
# Apple Silicon Mac:
#
#     ./dist/build-macos-release.sh
#
# It produces dist/runtimeskeptic-v0.2.0-macos-arm64.tar.gz with the same
# analyzer, runtime SDK, sample, benchmark and evidence layout as Linux.
#
# macOS does not support fully static binaries (Apple ships no static libc), so
# these link the system libraries every Mac already has. That is the platform
# norm, not a dependency to install.
set -euo pipefail
umask 022

# CI run 31058058088 attributed every remaining byte difference exclusively to
# the two installed static archives. Apple's archive tools honor ZERO_AR_DATE
# by normalizing archive-member dates; propagate it to CMake's libtool calls.
export ZERO_AR_DATE=1

V=0.2.0
PKG="runtimeskeptic-v$V-macos-arm64"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${RS_BUILD_DIR:-$ROOT/build-rel-macos-arm64}"
JOBS="${RS_JOBS:-2}"
cd "$ROOT"

[ "$(uname -s)" = "Darwin" ] || { echo "run this on macOS (arm64)"; exit 1; }
[ "$(uname -m)" = "arm64" ] || { echo "run this natively on Apple Silicon (arm64)"; exit 1; }
for command_name in cmake tar gzip shasum grep python3 otool; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "missing required release tool: $command_name" >&2
        exit 1
    }
done

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DRS_WARNINGS_AS_ERRORS=ON -DRS_BUILD_TESTS=OFF >/dev/null
cmake --build "$BUILD_DIR" --parallel "$JOBS" --target \
    rs-check rs-env-probe rs-profile rs-replay rs-mcp \
    runtimeskeptic rs_trace rs-runtime-sample rs-runtime-benchmark >/dev/null

D="dist/$PKG"
case "$D" in
    dist/runtimeskeptic-v*-macos-arm64) ;;
    *) echo "refusing unsafe package path: $D" >&2; exit 1 ;;
esac
rm -rf -- "$D"
mkdir -p "$D/bin" "$D/contracts/campaign" "$D/profiles/measured"
cmake --install "$BUILD_DIR" --prefix "$D" --component RuntimeSDK >/dev/null
RUNTIME_DYLIB="$D/lib/libruntimeskeptic.$V.dylib"
if [ ! -f "$RUNTIME_DYLIB" ] ||
   ! otool -D "$RUNTIME_DYLIB" | grep -Eq \
       'libruntimeskeptic\.1\.dylib$'; then
    echo "release verification failed: runtime library lacks ABI-v1 install name" >&2
    exit 1
fi
if ! otool -L "$RUNTIME_DYLIB" | grep -Eq \
    'libruntimeskeptic\.1\.dylib.*compatibility version 1\.0\.0, current version 1\.2\.0'; then
    echo "release verification failed: invalid Mach-O ABI versions" >&2
    exit 1
fi
for b in rs-check rs-env-probe rs-profile rs-replay rs-mcp; do
    cp "$BUILD_DIR/bin/$b" "$D/bin/"
    strip "$D/bin/$b" 2>/dev/null || true
done
cp contracts/*.json "$D/contracts/"
cp contracts/campaign/*.json "$D/contracts/campaign/"
cp profiles/measured/*.measured.json "$D/profiles/measured/"
cp README.md LICENSE dist/QUICKSTART.md "$D/"
cp "dist/RELEASE-v$V.md" "$D/RELEASE.md"

# Prove the PACKAGE runs, contains its demo inputs, and returns the expected
# non-zero verdict. The old catch-all accepted every failure, including a
# missing binary, which made the release self-check a false green.
bash dist/verify-package.sh "$D"

# Normalize the complete tree, traversal order and gzip header. COPYFILE_DISABLE
# prevents AppleDouble metadata from making an otherwise identical package
# host-dependent.
find "$D" -exec touch -h -t 198001010000 {} +
(
    cd dist
    list="$(mktemp "${TMPDIR:-/tmp}/runtimeskeptic-package.XXXXXX")"
    trap 'rm -f -- "$list"' EXIT
    find "$PKG" -print | LC_ALL=C sort > "$list"
    COPYFILE_DISABLE=1 tar --no-xattrs --no-recursion \
        --uid 0 --gid 0 --uname root --gname wheel -cf - -T "$list" \
        | gzip -n > "$PKG.tar.gz"
)
echo "built dist/$PKG.tar.gz"
shasum -a 256 "dist/$PKG.tar.gz"
