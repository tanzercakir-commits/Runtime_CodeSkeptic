#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# A containment regression must fail before the marker worker can execute.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER="$HERE/linux/cgroup_exec.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/marker-worker.sh" <<'SH'
#!/bin/sh
: > "$1"
SH
chmod +x "$WORK/marker-worker.sh"

run_case() {
    name=$1
    cgroup=$2
    expected_code=$3
    marker="$WORK/$name.marker"

    sh "$LAUNCHER" "$cgroup" "$WORK/marker-worker.sh" "$marker" \
        "/definitely-not-this-cgroup" /bin/true "$(id -u)" "$(id -g)" \
        >/dev/null 2>&1
    code=$?
    if [ "$code" -ne "$expected_code" ]; then
        echo "cgroup launcher selftest: $name returned $code, expected $expected_code" >&2
        exit 1
    fi
    if [ -e "$marker" ]; then
        echo "cgroup launcher selftest: $name executed worker outside containment" >&2
        exit 1
    fi
}

run_case migration-write-failure "$WORK/missing" 70
mkdir "$WORK/fake-cgroup"
: > "$WORK/fake-cgroup/cgroup.procs"
run_case membership-mismatch "$WORK/fake-cgroup" 71

echo "cgroup launcher: migration and membership failures abort before exec"
