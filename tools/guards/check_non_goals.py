#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Normative non-goals are commitments, so a component may not quietly break one.

docs/non_goals.md declares itself **normative**: "These are commitments, not a
description of current limitations." Nothing checked them, and one was broken
within a single session.

Section 18, "We will not duplicate CodeSkeptic", reserves for CodeSkeptic:
static C/C++ semantic analysis, dataflow, CONTRACT EXTRACTION, error-path
analysis, FATAL-SINK IDENTIFICATION, and semantic API comparison.

`tools/rs-extract` performs contract extraction and fatal-sink identification
from C/C++ source. That is the reserved list, and the conflict is real
regardless of how carefully the tool describes itself as "bounded" and "a
reference implementation".

This guard does not decide the question - it is the owner's to decide. It
fails while the question is open, so it cannot be forgotten, and it is
satisfied by ANY of the three resolutions recorded in docs/PLAN.md:

  1. remove rs-extract
  2. amend non_goals.md section 18 with a stated exception and an expiry
  3. rename it so it makes no claim to being an extractor

Resolution 2 is recognised by the marker `NON-GOAL-18-EXCEPTION:` appearing in
docs/non_goals.md with a date.

The owner chose resolution 1 on 2026-07-25, and that produced a SECOND check.
Deleting the code did not delete the claims about it: a JSON schema still said
requirements could be "written by rs-extract", and the shadPS4 case study still
said the tool "now recovers a bounded subset from source text" - both describing
a program that no longer exists, both written truthfully the day before. So the
name of anything removed by decision is now itself checked, everywhere except
the two files whose job is to remember it.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NON_GOALS = ROOT / "docs" / "non_goals.md"

# Names of components removed by an explicit decision, with the date and the
# reason. A mention outside the historical record means a document is describing
# something that is not there.
REMOVED = {
    "rs-extract": "removed 2026-07-25 (d6276e0); extraction belongs to "
                  "CodeSkeptic per non_goals.md section 18",
    "rs_extract": "same component, underscore spelling",
}

# The two files that exist to record what was removed. Everywhere else a
# mention is a false claim. README.md is deliberately NOT here: it says "no
# extractor is bundled", a statement about the present that must stay true on
# its own. Anything under tools/guards/ is exempt as a class, because a guard
# has to name what it forbids in order to forbid it.
REMEMBERS = {"docs/PROGRESS.md", "docs/PLAN.md"}
REMEMBERS_DIRS = ("tools/guards/",)

TEXT_SUFFIXES = {".md", ".json", ".yml", ".yaml", ".cpp", ".hpp", ".h", ".c",
                 ".txt", ".sh", ".py", ".cmake"}
SKIP_DIRS = {".git", "build", "external", "third_party", "node_modules"}


def check_removed_names() -> list:
    """A removed component may not survive as a claim in a document."""
    problems = []
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or path.suffix not in TEXT_SUFFIXES:
            continue
        if SKIP_DIRS & set(path.parts):
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel in REMEMBERS or rel.startswith(REMEMBERS_DIRS):
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        for i, line in enumerate(lines, 1):
            for name, reason in REMOVED.items():
                if name in line:
                    problems.append(
                        f"{rel}:{i}: names `{name}`, which was {reason}. "
                        f"The code is gone; this claim is not. Fix the "
                        f"sentence, or record it in docs/PROGRESS.md where "
                        f"history belongs: \"{line.strip()[:72]}\"")
    return problems
# Any extractor, not just the one that was removed. A rename would slip past a
# single hard-coded path, and the commitment is about the CAPABILITY, not about
# a name. This does not stop deliberate evasion - no guard does - it stops the
# capability drifting back in under a plausible name while nobody is looking.
EXTRACT_GLOBS = ("tools/*extract*", "src/*extract*", "include/**/*extract*",
                 "tools/*scanner*", "src/*scanner*")


def main() -> int:
    problems = []
    text = NON_GOALS.read_text(encoding="utf-8") if NON_GOALS.exists() else ""

    if not NON_GOALS.exists():
        print("docs/non_goals.md is missing; it is normative", file=sys.stderr)
        return 1

    exception = re.search(r"NON-GOAL-18-EXCEPTION:\s*(\d{4}-\d{2}-\d{2})", text)
    found = sorted({p.relative_to(ROOT).as_posix()
                    for g in EXTRACT_GLOBS for p in ROOT.glob(g)})

    if found and not exception:
        problems.append(
            "docs/non_goals.md section 18 reserves contract extraction and "
            "fatal-sink identification for CodeSkeptic. These paths look like "
            "an extractor growing back here: " + ", ".join(found) +
            ". Resolve it: remove them, or add "
            "`NON-GOAL-18-EXCEPTION: YYYY-MM-DD` to docs/non_goals.md with the "
            "reasoning and an expiry. One was removed on 2026-07-25; see "
            "docs/PROGRESS.md for what it learned, so a future extractor in "
            "the right repository does not have to rediscover it.")

    # Section 18 must still say what it says; silently deleting the commitment
    # is not a resolution.
    if "We will not duplicate CodeSkeptic" not in text:
        problems.append(
            "non_goals.md no longer contains section 18. A commitment is "
            "withdrawn deliberately and in writing, not by deletion.")

    stale = check_removed_names()

    if problems or stale:
        print("non-goal guard failed:", file=sys.stderr)
        for p in problems + stale:
            print(f"  - {p}", file=sys.stderr)
        return 1
    print("non-goals: no conflicts, no stale names"
          if not exception else
          "non-goals: section 18 reconciled, no stale names")
    return 0


if __name__ == "__main__":
    sys.exit(main())
