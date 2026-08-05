#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce the immutable-plan / consumable-todo / append-only-progress model.

The specialized guards validate TODO structure and PROGRESS history. This guard
owns the higher-level contract: the three records must exist, and the stable
plan must still match its deliberately recorded hash.
"""
import hashlib
import re
import sys
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAN = ROOT / "plan.md"
PIN = Path(__file__).resolve().parent / "plan.sha256"
TODO = ROOT / "docs" / "TODO.md"
PROGRESS = ROOT / "docs" / "PROGRESS.md"


def canonical_sha256(path: Path) -> str:
    # Text checkouts may materialize LF as CRLF. Pin semantic UTF-8 text so the
    # same plan has the same identity on every supported platform.
    content = path.read_text(encoding="utf-8")
    return hashlib.sha256(content.encode("utf-8")).hexdigest()

def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          encoding="utf-8", errors="replace")


def historical_plan_problem() -> str:
    """Reject plan edits even when the plan and its pin move together."""
    if not PLAN.exists() or not (ROOT / ".git").exists():
        return ""
    dirty = bool(git("status", "--porcelain", "--", "plan.md",
                     "tools/guards/plan.sha256").stdout.strip())
    baseline = "HEAD" if dirty else "HEAD^"
    before = git("show", f"{baseline}:plan.md")
    if before.returncode == 0:
        current = PLAN.read_text(encoding="utf-8")
        if current != before.stdout:
            return ("plan.md differs from its committed baseline; the plan and "
                    "pin may not be changed together")
        return ""
    if (ROOT / ".git" / "shallow").exists():
        return ("plan.md history is unavailable in a shallow checkout; fetch "
                "full history before asserting that the plan is immutable")
    # No plan in a reachable baseline is the one allowed transition: initial
    # adoption of this contract. Every later edit has a baseline and fails.
    return ""


def main() -> int:
    problems = []
    for path, role in ((PLAN, "immutable plan"),
                       (TODO, "consumable TODO"),
                       (PROGRESS, "append-only PROGRESS")):
        if not path.exists():
            problems.append(f"missing {role}: {path.relative_to(ROOT).as_posix()}")

    recorded = ""
    actual = ""
    if not PIN.exists():
        problems.append("tools/guards/plan.sha256 is missing; plan.md is not pinned")
    else:
        line = PIN.read_text(encoding="utf-8").strip()
        recorded = line.split()[0] if line else ""
        if not re.fullmatch(r"[0-9a-f]{64}", recorded):
            problems.append("tools/guards/plan.sha256 does not contain a SHA-256 hash")

    if PLAN.exists():
        actual = canonical_sha256(PLAN)
        if recorded and recorded != actual:
            problems.append(
                "plan.md changed, but it is the immutable project plan. Restore "
                "the pinned content; changing status belongs in docs/TODO.md or "
                "docs/PROGRESS.md.")
        history_problem = historical_plan_problem()
        if history_problem:
            problems.append(history_problem)

    shown = actual[:12] if actual else "unavailable"
    print(f"process contract: plan sha256:{shown}...; TODO consumable; "
          "PROGRESS append-only")
    if problems:
        print(f"\n{len(problems)} process contract problem(s):", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
