#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Reads a measurement published by the macOS CI workflow.
#
# The development sandbox for this project reaches the repository over the git
# protocol only - no Actions REST API, no artifact downloads - so the workflow
# publishes what matters as namespaced refs. This fetches one back.
#
#   tools/campaign/fetch_measurement.sh <sha> [job]
#
# job defaults to rosetta-x86_64; the other is native-arm64.
set -euo pipefail

SHA="${1:-}"
JOB="${2:-rosetta-x86_64}"
if [ -z "$SHA" ]; then
    echo "usage: $0 <commit-sha> [native-arm64|rosetta-x86_64]" >&2
    exit 64
fi

echo "== job status =="
git ls-remote origin "refs/status/${SHA}/${JOB}/*" | sed 's#.*/##' || true

echo
echo "== measurement =="
if git fetch -q origin "refs/measurements/${SHA}/${JOB}:refs/rs-measurement" 2>/dev/null; then
    case "$JOB" in
        native-arm64)    FILE=macos-arm64-native.json ;;
        rosetta-x86_64)  FILE=macos-rosetta-x86_64.json ;;
        *)               FILE=$(git show --name-only --format= refs/rs-measurement | head -1) ;;
    esac
    git show "refs/rs-measurement:${FILE}"
    git update-ref -d refs/rs-measurement
else
    echo "no measurement ref for ${SHA}/${JOB}" >&2
    echo "(the job may have failed before measuring; try refs/ci-logs/${SHA}/${JOB})" >&2
    exit 1
fi
