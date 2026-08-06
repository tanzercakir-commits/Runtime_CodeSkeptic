#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Enter one already-configured cgroup and only then execute the pressure worker.
# This is separate from the orchestration script so failure-before-exec can be
# tested without root or memory pressure.
set -u

if [ "$#" -ne 7 ]; then
    echo "usage: $0 CGROUP WORKER STATUS EXPECTED-MEMBERSHIP SETPRIV UID GID" >&2
    exit 64
fi

cgroup=$1
worker=$2
status_file=$3
expected_membership=$4
setpriv_bin=$5
uid=$6
gid=$7

printf '%s\n' "$$" > "$cgroup/cgroup.procs" || exit 70
actual=$(cut -d: -f3 /proc/self/cgroup) || exit 71
[ "$actual" = "$expected_membership" ] || exit 71

exec "$setpriv_bin" --reuid "$uid" --regid "$gid" --clear-groups -- \
    "$worker" "$status_file"
