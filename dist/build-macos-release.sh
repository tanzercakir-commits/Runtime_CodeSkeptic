#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Build the macOS arm64 prebuilt package for a RuntimeSkeptic release, to attach
# to the same GitHub Release as the Linux one. Run it from a clean clone on an
# Apple Silicon Mac:
#
#     ./dist/build-macos-release.sh
#
# It produces dist/runtimeskeptic-v0.1.0-macos-arm64.tar.gz with the same layout
# as the Linux package: bin/ + contracts/ + profiles/measured/ + QUICKSTART.
#
# macOS does not support fully static binaries (Apple ships no static libc), so
# these link the system libraries every Mac already has. That is the platform
# norm, not a dependency to install.
set -euo pipefail

V=0.1.0
PKG="runtimeskeptic-v$V-macos-arm64"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

[ "$(uname -s)" = "Darwin" ] || { echo "run this on macOS (arm64)"; exit 1; }
[ "$(uname -m)" = "arm64" ] || { echo "run this natively on Apple Silicon (arm64)"; exit 1; }

cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-rel -j --target rs-check rs-env-probe rs-profile rs-replay rs-mcp >/dev/null

D="dist/$PKG"
rm -rf "$D"
mkdir -p "$D/bin" "$D/contracts/campaign" "$D/profiles/measured"
for b in rs-check rs-env-probe rs-profile rs-replay rs-mcp; do
    cp "build-rel/bin/$b" "$D/bin/"
    strip "$D/bin/$b" 2>/dev/null || true
done
cp contracts/*.json "$D/contracts/"
cp contracts/campaign/*.json "$D/contracts/campaign/"
cp profiles/measured/*.measured.json "$D/profiles/measured/"
cp README.md LICENSE dist/QUICKSTART.md "$D/"

# Prove the PACKAGE runs, contains its demo inputs, and returns the expected
# non-zero verdict. The old catch-all accepted every failure, including a
# missing binary, which made the release self-check a false green.
bash dist/verify-package.sh "$D"

( cd dist && tar -czf "$PKG.tar.gz" "$PKG" )
echo "built dist/$PKG.tar.gz"
shasum -a 256 "dist/$PKG.tar.gz"
