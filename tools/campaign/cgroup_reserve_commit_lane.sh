#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Proves the POSIX half of RS-VM-0012 without pressuring the runner.
# Only the pressure worker enters a 64 MiB cgroup-v2 memory.max leaf.
# mmap(PROT_NONE) and mprotect(RW) must succeed; first touch must be the first
# failure point, and memory.events.local must attribute the OOM kill locally.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PROFILE="${1:-}"
LEDGER="${2:-}"
RS_CHECK="${RS_CHECK:-$ROOT/build/bin/rs-check}"
SOURCE="$ROOT/tests/groundtruth/linux/reserve_commit_pressure.c"
CONTRACT="$ROOT/tests/groundtruth/contracts/reserve-then-touch-cgroup.json"
LAUNCHER="$ROOT/tests/groundtruth/linux/cgroup_exec.sh"
LIMIT_BYTES=67108864

if [ -z "$PROFILE" ] || [ -z "$LEDGER" ]; then
    echo "usage: $0 MEASURED-PROFILE.json OUTPUT-LEDGER.jsonl" >&2
    exit 64
fi
[ -f "$PROFILE" ] || { echo "$0: profile not found: $PROFILE" >&2; exit 64; }
[ -x "$RS_CHECK" ] || { echo "$0: rs-check not found: $RS_CHECK" >&2; exit 64; }
[ -f "$LAUNCHER" ] || { echo "$0: cgroup launcher not found: $LAUNCHER" >&2; exit 64; }
[ "$(uname -s)" = "Linux" ] || { echo "$0: Linux only" >&2; exit 64; }
[ "$(stat -fc %T /sys/fs/cgroup)" = "cgroup2fs" ] || {
    echo "$0: unified cgroup v2 is required" >&2; exit 65; }
command -v setpriv >/dev/null || {
    echo "$0: setpriv is required to drop privilege inside the cgroup" >&2; exit 65; }
if ! sudo -n true; then
    echo "$0: passwordless sudo is required to create the bounded cgroup" >&2
    exit 65
fi

work="$(mktemp -d)"
worker="$work/reserve-commit-pressure"
status_file="$work/worker.status"
cgroup="/sys/fs/cgroup/rs-reserve-commit-$$"

terminate_cgroup() {
    [ -d "$cgroup" ] || return 0
    if printf '1\n' | sudo tee "$cgroup/cgroup.kill" >/dev/null 2>&1; then
        return 0
    fi
    members="$(cat "$cgroup/cgroup.procs" 2>/dev/null)"
    for member in $members; do
        case "$member" in
            *[!0-9]*|'') return 1 ;;
            *) sudo kill -KILL "$member" >/dev/null 2>&1 || return 1 ;;
        esac
    done
    return 0
}

cleanup() {
    case "$cgroup" in
        /sys/fs/cgroup/rs-reserve-commit-[0-9]*)
            if terminate_cgroup; then
                sudo rmdir "$cgroup" >/dev/null 2>&1 || true
            fi
            ;;
    esac
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

cc -std=c11 -O1 -Wall -Wextra -Werror -o "$worker" "$SOURCE" || {
    echo "$0: pressure worker did not compile" >&2; exit 70; }

if ! sudo mkdir "$cgroup"; then
    echo "$0: could not create the bounded cgroup leaf" >&2
    exit 70
fi

for control in cgroup.procs cgroup.kill memory.max memory.swap.max memory.events.local; do
    if [ ! -e "$cgroup/$control" ]; then
        echo "$0: cgroup v2 memory controller does not expose $control" >&2
        exit 70
    fi
done
if ! printf '%s\n' "$LIMIT_BYTES" | sudo tee "$cgroup/memory.max" >/dev/null; then
    echo "$0: could not set the cgroup memory.max limit" >&2
    exit 70
fi
if ! printf '0\n' | sudo tee "$cgroup/memory.swap.max" >/dev/null; then
    echo "$0: could not disable cgroup swap" >&2
    exit 70
fi
if [ -e "$cgroup/memory.oom.group" ]; then
    printf '1\n' | sudo tee "$cgroup/memory.oom.group" >/dev/null || {
        echo "$0: could not make cgroup OOM termination atomic" >&2; exit 70; }
fi

events="$cgroup/memory.events.local"
expected_membership="${cgroup#/sys/fs/cgroup}"
original_uid="$(id -u)"
original_gid="$(id -g)"
setpriv_bin="$(command -v setpriv)"
sudo sh "$LAUNCHER" "$cgroup" "$worker" "$status_file" "$expected_membership" \
    "$setpriv_bin" "$original_uid" "$original_gid" &
launcher_pid=$!

ticks=0
timed_out=0
while kill -0 "$launcher_pid" >/dev/null 2>&1; do
    ticks=$((ticks + 1))
    if [ "$ticks" -ge 150 ]; then
        timed_out=1
        if ! terminate_cgroup; then
            echo "$0: could not terminate the bounded cgroup" >&2
            sudo kill -KILL "$launcher_pid" >/dev/null 2>&1 || true
        fi
        break
    fi
    sleep 0.1
done

wait "$launcher_pid"
worker_status=$?
oom_kill="$(awk '$1 == "oom_kill" {print $2}' "$events")"
oom_kill="${oom_kill:-0}"

expected="reserve_ok=1 mprotect_ok=1 commit_call_present=0 touch_started=1"
observed=""
if [ -f "$status_file" ]; then observed="$(tr -d '\r\n' < "$status_file")"; fi

problems=0
if [ "$observed" != "$expected" ]; then
    echo "  worker phase record mismatch: '$observed'" >&2
    problems=$((problems + 1))
fi
if [ "$timed_out" -ne 0 ]; then
    echo "  worker exceeded the 15-second deadline" >&2
    problems=$((problems + 1))
fi
if [ "$worker_status" -ne 137 ]; then
    echo "  bounded worker status was $worker_status, not SIGKILL (137)" >&2
    problems=$((problems + 1))
fi
if [ "$oom_kill" -lt 1 ]; then
    echo "  memory.events.local did not record oom_kill" >&2
    problems=$((problems + 1))
fi
if [ "$problems" -ne 0 ]; then
    echo "$0: containment evidence did not hold" >&2
    cat "$events" >&2 || true
    exit 1
fi

analysis_json=$("$RS_CHECK" "$CONTRACT" --profile "$PROFILE" --format json 2>"$work/rs-check.err")
analysis_code=$?
if [ "$analysis_code" -ne 2 ]; then
    echo "$0: RS-VM-0012 contract expected CONDITIONAL (2), got $analysis_code" >&2
    cat "$work/rs-check.err" >&2
    exit 1
fi

python3 - "$PROFILE" "$LEDGER" "$analysis_json" <<'PY'
import json
import sys

profile_path, ledger_path, analysis_text = sys.argv[1:]
profile = json.load(open(profile_path, encoding="utf-8"))
analysis = json.loads(analysis_text)
rows = analysis.get("results", [analysis])
findings = sorted({f["id"] for row in rows for f in row.get("findings", [])})
if "RS-VM-0012" not in findings:
    raise SystemExit("RS-VM-0012 did not fire for the bounded pressure contract")
ledger = {
    "schema": "runtime-skeptic.groundtruth-execution.v1",
    "profile_id": profile["profile_id"],
    "case": "reserve-then-touch-cgroup",
    "contract": "contracts/reserve-then-touch-cgroup.json",
    "analyzer_verdict": "CONDITIONAL",
    "outcome": "oom-killed-after-touch",
    "pairing": "not asserted",
    "finding_ids": findings,
}
with open(ledger_path, "w", encoding="utf-8") as out:
    out.write(json.dumps(ledger, sort_keys=True, separators=(",", ":")) + "\n")
PY

echo "bounded reserve/commit mismatch held:"
echo "  memory.max=$LIMIT_BYTES; reserve=268435456; mprotect=success"
echo "  first touch: SIGKILL; status=$worker_status; local oom_kill=$oom_kill"
