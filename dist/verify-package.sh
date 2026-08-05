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

for required in "$CONTRACT" "$ROOT_CONTRACT" "$PROFILE" "$D/QUICKSTART.md" "$D/LICENSE"; do
    if [ ! -e "$required" ]; then
        echo "release verification failed: missing $required" >&2
        exit 1
    fi
done
for tool in rs-check rs-env-probe rs-profile rs-replay rs-mcp; do
    candidate="$D/bin/$tool"
    if [ ! -x "$candidate" ]; then
        echo "release verification failed: missing or non-executable $candidate" >&2
        exit 1
    fi
done

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

echo "release verification: package complete; Redis demo UNSUPPORTED via RS-VM-0006"
