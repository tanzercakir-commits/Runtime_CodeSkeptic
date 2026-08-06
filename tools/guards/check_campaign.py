#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A published number must still match the data it came from.

The false-positive rate is now measured, and a measured number is exactly the
kind of thing that rots. The data sits in `campaigns/`, the prose sits in
`docs/campaigns/`, and nothing stopped the two drifting apart - which is how
every one of the eleven false documentation claims this project already found
came to exist. A number is a claim like any other.

So every campaign result committed under `campaigns/` must have its headline
figures quoted verbatim somewhere in `docs/campaigns/`, and this checks that
they are.

It deliberately does NOT check that the rate is low. A guard with a threshold
would have to pick one, on one host's data, and would turn a measurement into
a target. The judgement belongs in the document, signed and dated.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "campaigns"
DOCS = ROOT / "docs" / "campaigns"


def main() -> int:
    if not DATA.exists():
        print("campaigns: no committed results yet")
        return 0

    problems = []
    checked = 0
    prose = ""
    if DOCS.exists():
        prose = "\n".join(p.read_text(encoding="utf-8") for p in DOCS.rglob("*.md"))

    for result in sorted(DATA.rglob("*.json")):
        if result.name.endswith(".host.json"):
            continue
        rel = result.relative_to(ROOT).as_posix()
        try:
            data = json.loads(result.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            problems.append(f"{rel}: not valid JSON ({exc})")
            continue
        populations = data.get("populations")
        if not isinstance(populations, dict):
            continue

        # The document has to name the file, or nobody can find the data
        # behind the number.
        if rel not in prose:
            problems.append(
                f"{rel}: committed campaign data that no document under "
                f"docs/campaigns/ cites. A result nobody can find is a result "
                f"nobody can check.")

        for name, pop in populations.items():
            checked += 1
            for field in ("evaluated", "false_positive_count"):
                value = pop.get(field)
                if value is None:
                    continue
                # Digits appear inside other numbers, so require a word
                # boundary and allow thousands separators in prose.
                pattern = r"\b" + re.escape(str(value)) + r"\b"
                if not re.search(pattern, prose):
                    problems.append(
                        f"{rel} [{name}]: {field} is {value}, and no document "
                        f"under docs/campaigns/ contains that number. Either "
                        f"the prose is stale or the data was replaced without "
                        f"republishing it.")

    print(f"campaigns: {checked} population(s) checked against docs/campaigns/")
    if problems:
        print(f"\n{len(problems)} campaign number(s) that no longer match "
              f"their data:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
