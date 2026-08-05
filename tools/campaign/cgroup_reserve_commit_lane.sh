#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Proves the POSIX half of RS-VM-0012 without pressuring the runner.
# Only a transient systemd service enters a 64 MiB cgroup-v2 MemoryMax leaf.
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
LIMIT_BYTES=67108864

if [ -z "$PROFILE" ] || [ -z "$LEDGER" ]; then
    echo "usage: $0 MEASURED-PROFILE.json OUTPUT-LEDGER.jsonl" >&2
    exit 64
fi
[ -f "$PROFILE" ] || { echo "$0: profile not found: $PROFILE" >&2; exit 64; }
[ -x "$RS_CHECK" ] || { echo "$0: rs-check not found: $RS_CHECK" >&2; exit 64; }
[ "$(uname -s)" = "Linux" ] || { echo "$0: Linux only" >&2; exit 64; }
[ "$(stat -fc %T /sys/fs/cgroup)" = "cgroup2fs" ] || {
    echo "$0: unified cgroup v2 is required" >&2; exit 65; }
command -v systemd-run >/dev/null || { echo "$0: systemd-run missing" >&2; exit 65; }
if ! sudo -n true; then
    echo "$0: passwordless sudo is required to create the transient scope" >&2
    exit 65
fi

work="$(mktemp -d)"
worker="$work/reserve-commit-pressure"
status_file="$work/worker.status"
unit="rs-reserve-commit-${GITHUB_RUN_ID:-local}-${GITHUB_RUN_ATTEMPT:-0}-$$.service"

cleanup() {
    sudo systemctl stop "$unit" >/dev/null 2>&1 || true
    sudo systemctl reset-failed "$unit" >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

cc -std=c11 -O1 -Wall -Wextra -Werror -o "$worker" "$SOURCE" || {
    echo "$0: pressure worker did not compile" >&2; exit 70; }

uid="$(id -u)"
gid="$(id -g)"
if ! sudo systemd-run --quiet --unit="$unit" --service-type=exec \
        --uid="$uid" --gid="$gid" \
        --property="MemoryMax=$LIMIT_BYTES" \
        --property="MemorySwapMax=0" \
        --property="OOMPolicy=stop" \
        --property="KillMode=control-group" \
        --property="RuntimeMaxSec=15s" \
        "$worker" "$status_file"; then
    echo "$0: could not start the bounded transient service" >&2
    exit 70
fi

control_group="$(sudo systemctl show "$unit" -p ControlGroup --value)"
events="/sys/fs/cgroup${control_group}/memory.events.local"
if [ -z "$control_group" ] || [ ! -r "$events" ]; then
    echo "$0: transient service exposes no readable memory.events.local" >&2
    exit 70
fi

ticks=0
while sudo systemctl is-active --quiet "$unit"; do
    ticks=$((ticks + 1))
    if [ "$ticks" -ge 150 ]; then
        echo "$0: worker did not terminate within 15 seconds" >&2
        exit 70
    fi
    sleep 0.1
done

result="$(sudo systemctl show "$unit" -p Result --value)"
main_code="$(sudo systemctl show "$unit" -p ExecMainCode --value)"
main_status="$(sudo systemctl show "$unit" -p ExecMainStatus --value)"
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
if [ "$result" != "oom-kill" ]; then
    echo "  service result was '$result', not oom-kill" >&2
    problems=$((problems + 1))
fi
if [ "$main_code" != "2" ] || [ "$main_status" != "9" ]; then
    echo "  worker termination was code=$main_code status=$main_status, not SIGKILL" >&2
    problems=$((problems + 1))
fi
if [ "$oom_kill" -lt 1 ]; then
    echo "  memory.events.local did not record oom_kill" >&2
    problems=$((problems + 1))
fi
if [ "$problems" -ne 0 ]; then
    echo "$0: containment evidence did not hold" >&2
    sudo systemctl status "$unit" --no-pager >&2 || true
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
    "pairing": "held",
    "finding_ids": findings,
}
with open(ledger_path, "w", encoding="utf-8") as out:
    out.write(json.dumps(ledger, sort_keys=True, separators=(",", ":")) + "\n")
PY

echo "bounded reserve/commit mismatch held:"
echo "  MemoryMax=$LIMIT_BYTES; reserve=268435456; mprotect=success"
echo "  first touch: SIGKILL; service result=$result; local oom_kill=$oom_kill"
