#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""docs/PLAN.md is the spine, so it may not rot.

Three failure modes this catches:

  1. a `[done]` with no evidence - a claim that something is finished with
     nothing that runs to show for it. Every `[done]` must name a path, a
     command, or a test.
  2. an unknown status marker - a typo silently becomes a status nobody
     tracks.
  3. a path named in the plan that does not exist - the plan citing evidence
     that was moved or deleted.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAN = ROOT / "docs" / "PLAN.md"
VALID = {"done", "partial", "open", "blocked", "n/a"}
PATH = re.compile(r"`((?:src|tests|tools|include|docs|schemas|profiles|contracts|corpus|\.github)/[A-Za-z0-9_./+-]+)`")


def main() -> int:
    if not PLAN.exists():
        print("docs/PLAN.md is missing; it is the project's spine", file=sys.stderr)
        return 1

    problems = []
    counts = {k: 0 for k in VALID}
    lines = PLAN.read_text().splitlines()

    # The legend explains the markers; it is not a claim about the project.
    body_starts = next((i for i, l in enumerate(lines)
                        if l.startswith("## ")), 0)

    for i, line in enumerate(lines, 1):
        if i <= body_starts:
            continue
        # An entry continues onto its wrapped lines, and the evidence often
        # lands there. Judge the whole entry, not the first line of it.
        entry = line
        j = i
        while j < len(lines) and lines[j].startswith("  ") and not re.search(r"`\[[a-z/]+\]`", lines[j]):
            entry += " " + lines[j].strip()
            j += 1

        for marker in re.findall(r"`\[([a-z/]+)\]`", line):
            if marker not in VALID:
                problems.append(f"line {i}: unknown status marker `[{marker}]`")
                continue
            counts[marker] += 1
            if marker == "done":
                has_path = bool(PATH.search(entry))
                has_cmd = bool(re.search(r"`(rs-[a-z]+|ctest|cmake)[^`]*`", entry))
                has_id = bool(re.search(r"`RS-VM-\d+`", entry))
                has_artifact = bool(re.search(
                    r"`runtime-skeptic\.[a-z.-]+`|committed|measured on", entry))
                if not (has_path or has_cmd or has_id or has_artifact):
                    problems.append(
                        f"line {i}: `[done]` with no evidence - name a test, a "
                        f"tool invocation, a finding id, or a committed "
                        f"artifact: \"{line.strip()[:80]}\"")

        for p in PATH.findall(line):
            if "*" in p or "..." in p:
                continue
            if not (ROOT / p).exists():
                problems.append(f"line {i}: cites `{p}`, which does not exist")

    total = sum(counts.values())
    print("plan: " + "  ".join(f"{k}={v}" for k, v in sorted(counts.items()))
          + f"  (total {total})")
    if total == 0:
        problems.append("no status markers found at all - has the format changed?")

    if problems:
        print("\nplan guard failed:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
