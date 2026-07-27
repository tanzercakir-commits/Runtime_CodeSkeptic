#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Status markers are claims, so they may not rot.

Three failure modes this catches:

  1. a `[done]` with no evidence - a claim that something is finished with
     nothing that runs to show for it. Every `[done]` must name a path, a
     command, or a test.
  2. an unknown status marker - a typo silently becomes a status nobody
     tracks.
  3. a path named in the document that does not exist - a plan citing evidence
     that was moved or deleted.

Two documents use the vocabulary. `docs/PLAN.md` is the spine: where the
project stands against its own roadmap. `docs/scenarios/assessment.md` is the
same discipline pointed outward - what the tool does for each use case the
owner described - and it earns the same checking, because "this scenario works"
is exactly the kind of claim that is pleasant to write and expensive to be
wrong about.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# docs/PLAN.md is REQUIRED: it is the spine, and its absence is a failure.
# docs/scenarios/assessment.md is checked ONLY IF PRESENT, because the owner
# adopted the scenarios as provisional - "official while the repository is
# private, and we will remove them or write different ones later". A guard that
# made deleting them fail CI would convert a decision into a trap.
REQUIRED = ("docs/PLAN.md",)
IF_PRESENT = ("docs/scenarios/assessment.md",)
VALID = {"done", "partial", "open", "blocked", "n/a"}
# A marker in LEADING position: optionally a list bullet, then the marker.
# `docs/PLAN.md` writes entries as `- `[done]` ...`; `docs/scenarios/
# assessment.md` writes them as paragraphs opening with the marker. Both are
# matched; a marker in the middle of a sentence is prose about the markers.
# Note the group placement: `(?:\s*[-*]\s+)?` requires a BULLET whenever
# there is indentation, so an indented marker with no bullet is a wrapped
# continuation line and not a new entry. Written the other way round
# (`^\s*(?:[-*]\s+)?`) it swallowed continuations, and an entry whose
# evidence sat after one looked unsupported.
LEADING_MARKER = re.compile(r"^(?:\s*[-*]\s+)?`\[([a-z/]+)\]`")

PATH = re.compile(r"`((?:src|tests|tools|include|docs|schemas|profiles|contracts|corpus|\.github)/[A-Za-z0-9_./+-]+)`")


def check(rel: str, problems: list, counts: dict, required: bool) -> bool:
    """Returns whether the document was there to check."""
    doc = ROOT / rel
    if not doc.exists():
        if required:
            problems.append(f"{rel} is missing; it is the project's spine")
        return False

    lines = doc.read_text().splitlines()

    # The legend explains the markers; it is not a claim about the project.
    body_starts = next((i for i, l in enumerate(lines)
                        if l.startswith("## ")), 0)

    for i, line in enumerate(lines, 1):
        if i <= body_starts:
            continue
        # An entry continues onto its wrapped lines, and the evidence often
        # lands there. Judge the whole entry, not the first line of it.
        #
        # The continuation used to be recognised by a two-space indent, which
        # is how a list item wraps in docs/PLAN.md. docs/scenarios/assessment.md
        # writes its entries as paragraphs, so every one of its `[done]` rows
        # looked evidence-free while the evidence sat on the next line at
        # column 0. An entry runs to the next blank line or the next marker,
        # whichever comes first - true of both shapes.
        entry = line
        j = i
        while (j < len(lines) and lines[j].strip()
               and not LEADING_MARKER.search(lines[j])):
            entry += " " + lines[j].strip()
            j += 1

        # A STATUS MARKER INTRODUCES AN ENTRY. It is not any occurrence of the
        # token anywhere on any line, which is what this used to match.
        #
        # These documents describe their own markers - `docs/PLAN.md`'s legend
        # explains them and its prose argues about them ("Still `[partial]`
        # because ...", "this line read `[done]` on 2026-07-26 while ..."), and
        # `docs/scenarios/assessment.md` does the same twice. Every one of those
        # was being counted as a criterion, so the totals this guard prints were
        # wrong by four, and a sentence that merely MENTIONED `[done]` was
        # required to carry evidence - which made the guard fire on a correct
        # tree. That is the failure this directory exists to prevent, committed
        # by a guard rather than caught by one.
        #
        # It also truncated entries: the continuation loop above stopped at any
        # line containing a marker, so evidence sitting after a prose mention was
        # invisible. Both now key on the leading position.
        for marker in LEADING_MARKER.findall(line):
            if marker not in VALID:
                problems.append(f"{rel}:{i}: unknown status marker `[{marker}]`")
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
                        f"{rel}:{i}: `[done]` with no evidence - name a test, a "
                        f"tool invocation, a finding id, or a committed "
                        f"artifact: \"{line.strip()[:80]}\"")

        for p in PATH.findall(line):
            if "*" in p or "..." in p:
                continue
            if not (ROOT / p).exists():
                problems.append(f"{rel}:{i}: cites `{p}`, which does not exist")
    return True


def main() -> int:
    problems = []
    counts = {k: 0 for k in VALID}
    seen = 0

    for rel in REQUIRED:
        seen += check(rel, problems, counts, required=True)
    for rel in IF_PRESENT:
        seen += check(rel, problems, counts, required=False)

    total = sum(counts.values())
    print("plan: " + "  ".join(f"{k}={v}" for k, v in sorted(counts.items()))
          + f"  (total {total}, across {seen} document(s))")
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
