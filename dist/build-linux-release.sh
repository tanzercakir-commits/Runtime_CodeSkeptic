#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Build the reproducible Linux x86-64 prebuilt package for a RuntimeSkeptic
# release. Run it from a clean Linux clone (WSL2 is supported):
#
#     ./dist/build-linux-release.sh
#
# The five analyzer tools are fully statically linked. The runtime SDK is a
# shared library plus headers, samples and a benchmark. Archive metadata and the
# gzip header are normalized, so identical inputs produce the same SHA-256.
set -euo pipefail
umask 022

V=0.2.0
PKG="runtimeskeptic-v$V-linux-x86_64"
ROOT="${RS_SOURCE_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_DIR="${RS_BUILD_DIR:-$ROOT/build-rel-linux-static}"
SDK_BUILD_DIR="${RS_SDK_BUILD_DIR:-$ROOT/build-rel-linux-sdk}"
JOBS="${RS_JOBS:-2}"

[ "$(uname -s)" = "Linux" ] || { echo "run this on Linux"; exit 1; }
[ "$(uname -m)" = "x86_64" ] || {
    echo "run this natively on Linux x86_64"; exit 1;
}
for command_name in cmake tar gzip readelf file strip sha256sum grep python3; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "missing required release tool: $command_name" >&2
        exit 1
    }
done

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DRS_WARNINGS_AS_ERRORS=ON -DCMAKE_EXE_LINKER_FLAGS=-static >/dev/null
cmake --build "$BUILD_DIR" --parallel "$JOBS" \
    --target rs-check rs-env-probe rs-profile rs-replay rs-mcp >/dev/null

cmake -S "$ROOT" -B "$SDK_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DRS_WARNINGS_AS_ERRORS=ON -DRS_BUILD_TESTS=OFF >/dev/null
cmake --build "$SDK_BUILD_DIR" --parallel "$JOBS" \
    --target runtimeskeptic rs_trace rs-runtime-sample \
             rs-runtime-benchmark >/dev/null

# Two independent checks pin the release claim. readelf verifies that the ELF
# has no runtime interpreter; file verifies the linker's classification.
for b in rs-check rs-env-probe rs-profile rs-replay rs-mcp; do
    binary="$BUILD_DIR/bin/$b"
    if readelf -l "$binary" | grep -q 'INTERP'; then
        echo "release build is dynamically linked: $binary has INTERP" >&2
        exit 1
    fi
    if ! file "$binary" | grep -q 'statically linked'; then
        echo "release build is not classified as static: $(file "$binary")" >&2
        exit 1
    fi
done

D="$ROOT/dist/$PKG"
case "$D" in
    "$ROOT"/dist/runtimeskeptic-v*-linux-x86_64) ;;
    *) echo "refusing unsafe package path: $D" >&2; exit 1 ;;
esac
rm -rf -- "$D"
mkdir -p "$D/bin" "$D/contracts/campaign" "$D/profiles/measured"
cmake --install "$SDK_BUILD_DIR" --prefix "$D" \
    --component RuntimeSDK >/dev/null
RUNTIME_SO="$D/lib/libruntimeskeptic.so.$V"
if [ ! -f "$RUNTIME_SO" ] ||
   ! readelf -d "$RUNTIME_SO" | grep -Eq \
       'SONAME.*\[libruntimeskeptic\.so\.1\]'; then
    echo "release verification failed: runtime library lacks ABI-v1 SONAME" >&2
    exit 1
fi
for b in rs-check rs-env-probe rs-profile rs-replay rs-mcp; do
    cp "$BUILD_DIR/bin/$b" "$D/bin/"
    strip "$D/bin/$b"
done
cp "$ROOT"/contracts/*.json "$D/contracts/"
cp "$ROOT"/contracts/campaign/*.json "$D/contracts/campaign/"
cp "$ROOT"/profiles/measured/*.measured.json "$D/profiles/measured/"
cp "$ROOT"/README.md "$ROOT"/LICENSE "$ROOT"/dist/QUICKSTART.md "$D/"
cp "$ROOT/dist/RELEASE-v$V.md" "$D/RELEASE.md"

# Prove the packaged binaries run and the demo returns the exact expected
# non-zero verdict. Missing tools, inputs, or the wrong finding fail closed.
bash "$ROOT/dist/verify-package.sh" "$D"

# GNU tar and gzip normalization make the archive byte-reproducible. This is a
# release property, not merely a convenient filename.
(
    cd "$ROOT/dist"
    tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
        --mode='u+rwX,go+rX,go-w' \
        -cf - "$PKG" | gzip -n > "$PKG.tar.gz"
)

echo "built dist/$PKG.tar.gz"
sha256sum "$ROOT/dist/$PKG.tar.gz"
