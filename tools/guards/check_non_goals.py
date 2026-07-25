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
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NON_GOALS = ROOT / "docs" / "non_goals.md"
EXTRACT_TOOL = ROOT / "tools" / "rs-extract"
EXTRACT_LIB = ROOT / "src" / "extract"


def main() -> int:
    problems = []
    text = NON_GOALS.read_text() if NON_GOALS.exists() else ""

    if not NON_GOALS.exists():
        print("docs/non_goals.md is missing; it is normative", file=sys.stderr)
        return 1

    exception = re.search(r"NON-GOAL-18-EXCEPTION:\s*(\d{4}-\d{2}-\d{2})", text)
    extractor_present = EXTRACT_TOOL.exists() or EXTRACT_LIB.exists()

    if extractor_present and not exception:
        problems.append(
            "docs/non_goals.md section 18 reserves contract extraction and "
            "fatal-sink identification for CodeSkeptic, and tools/rs-extract "
            "does both. Resolve it: remove the tool, rename it so it claims "
            "nothing, or add `NON-GOAL-18-EXCEPTION: YYYY-MM-DD` to "
            "docs/non_goals.md with the reasoning and an expiry. "
            "See docs/PLAN.md, Phase 5.")

    # Section 18 must still say what it says; silently deleting the commitment
    # is not a resolution.
    if "We will not duplicate CodeSkeptic" not in text:
        problems.append(
            "non_goals.md no longer contains section 18. A commitment is "
            "withdrawn deliberately and in writing, not by deletion.")

    if problems:
        print("non-goal guard failed:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    print("non-goals: section 18 reconciled" if exception
          else "non-goals: no conflicts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
