#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""ROADMAP.md is frozen, and docs/PLAN.md must mirror its phases.

THE FOUR-DOCUMENT MODEL HAS TWO KINDS OF FILE, and the owner stated the rule
outright on 2026-07-30: ROADMAP and PLAN carry the project's spirit and
integrity and must not break; TODO and PROGRESS change as the work develops.

What that means mechanically, and what this guard enforces:

  ROADMAP.md      THE SPECIFICATION. Written once (51b4e21, 2026-07-24) and
                  never edited since - measured from git, not asserted. This
                  guard freezes it by hash, so an accidental edit fails CI and
                  a deliberate one becomes a loud decision: update
                  `tools/guards/roadmap.sha256` in the same commit and record
                  WHY in docs/PROGRESS.md. The project's whole method is that
                  the map is graded against a fixed spec; a spec that moves
                  under the map grades nothing.

  docs/PLAN.md    THE MAP. Its CRITERIA are the ROADMAP's criteria and its
                  summary table must name every phase the ROADMAP defines -
                  that is the mirror this checks. What may change is only the
                  honest status of each criterion ([open] -> [done] and the
                  evidence behind it), which `check_plan.py` already polices.

  docs/TODO.md    THE COMPASS and THE PAST. Free to change; `check_todo.py`,
  docs/PROGRESS.md  `check_plan.py` and `check_dates.py` keep them honest.

Freezing by hash rather than by `git diff` is deliberate: guards run on the
tree, not on a diff, so they catch the state however it was reached - including
a bad merge, a stray editor write, or a session that forgot the rule.
"""
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ROADMAP = ROOT / "ROADMAP.md"
RECORDED = Path(__file__).resolve().parent / "roadmap.sha256"
PLAN = ROOT / "docs" / "PLAN.md"

# The phases the ROADMAP defines. Read from the ROADMAP itself rather than
# restated, for the same reason check_windows_compiles.py reads the compiler
# flags out of CMakeLists.txt: a guard holding its own copy of the thing it
# checks drifts from it.
PHASE_HEADING = re.compile(r"^## Phase (\d+) — ", re.MULTILINE)


def main() -> int:
    problems = []

    if not ROADMAP.exists():
        print("ROADMAP.md is missing; it is the project's specification",
              file=sys.stderr)
        return 1

    actual = hashlib.sha256(ROADMAP.read_bytes()).hexdigest()

    if not RECORDED.exists():
        problems.append(
            "tools/guards/roadmap.sha256 does not exist, so nothing pins the "
            "specification. Create it deliberately:\n"
            "      sha256sum ROADMAP.md > tools/guards/roadmap.sha256")
    else:
        recorded_line = RECORDED.read_text().strip()
        recorded = recorded_line.split()[0] if recorded_line else ""
        if recorded != actual:
            problems.append(
                "ROADMAP.md has changed. It is the project's fixed "
                "specification - the map in docs/PLAN.md is graded against it, "
                "and a spec that moves under the map grades nothing. If this "
                "change is DELIBERATE, say so out loud: update "
                "tools/guards/roadmap.sha256 in the same commit (sha256sum "
                "ROADMAP.md > tools/guards/roadmap.sha256) and record why in "
                "docs/PROGRESS.md. If it is not deliberate, revert it.")

    # The mirror: every phase the ROADMAP defines must appear in the PLAN.
    # "Phase N" as a plain string is enough - the PLAN names phases both in its
    # summary table and as section headings, and either counts. A phase the
    # PLAN stops mentioning is a criterion nobody is grading any more.
    if not PLAN.exists():
        problems.append("docs/PLAN.md is missing; it is the map")
    else:
        roadmap_phases = sorted(
            {int(m) for m in PHASE_HEADING.findall(ROADMAP.read_text())})
        if not roadmap_phases:
            problems.append(
                "no '## Phase N —' headings found in ROADMAP.md; the phase "
                "pattern this guard reads has drifted from the document")
        plan_text = PLAN.read_text()
        missing = [n for n in roadmap_phases
                   if f"Phase {n}" not in plan_text]
        for n in missing:
            problems.append(
                f"docs/PLAN.md no longer mentions Phase {n}, which ROADMAP.md "
                f"defines. The map must mirror the specification's phases - "
                f"only the status markers move.")

    count = len(PHASE_HEADING.findall(ROADMAP.read_text()))
    print(f"roadmap: frozen at sha256:{actual[:12]}..., "
          f"{count} phase(s) mirrored in the plan")
    if problems:
        print(f"\n{len(problems)} integrity problem(s):", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
