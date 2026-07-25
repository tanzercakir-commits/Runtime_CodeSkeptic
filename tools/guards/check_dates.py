#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A date typed into a document is a claim. A date in git is a record.

The owner asked whether the plan and progress files need dating, and offered to
supply today's date. The answer is yes - and the date should come from neither
of us.

WHY THIS GUARD EXISTS, stated plainly. The author of these documents is a
language model, and this session alone produced three dates for the same day:
the environment header said 2026-07-24, a later reminder said 2026-07-25, and
the container clock said 2026-07-25T20:34Z. It has also been confidently wrong
about checkable facts more than once - it reported that neither CodeSkeptic
clone had a GitHub remote when one did, and it wrote a commit message
describing a fix the diff did not contain. A date it types is exactly that
class of claim: plausible, unverified, and load-bearing.

It is load-bearing because `tools/guards/check_docs.py` accepts a
`<!-- checked: YYYY-MM-DD -->` marker as proof that somebody looked. If the
date in that marker is whatever the author believed at the time, the marker
proves nothing at all, and a stale claim can be re-blessed forever by an author
who thinks it is still Tuesday.

So every date in the documents is checked against git, which records when a
line was actually written and cannot be talked into a different answer.

THREE CHECKS.

1. A `## YYYY-MM-DD` heading in docs/PROGRESS.md must match the author date of
   the commit that introduced that heading, as `git blame` reports it. An entry
   written today and dated last week fails; so does one dated tomorrow.

2. No date anywhere may be in the future relative to the newest commit. This is
   the check that catches a wrong clock, and it is the reason the guard exists
   at all.

3. A `<!-- checked: -->` marker older than STALE_DAYS is reported. It is not a
   failure - an old claim can still be true - but it stops "somebody looked"
   from meaning "somebody looked, once, eight months ago".

Uncommitted lines blame as "not committed yet" and are dated from the working
tree, so a new entry written now is checked against now.
"""
import re
import subprocess
import sys
from datetime import date, datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROGRESS = ROOT / "docs" / "PROGRESS.md"

HEADING = re.compile(r"^##\s+(\d{4}-\d{2}-\d{2})\b")
CHECKED = re.compile(r"<!--\s*checked:\s*(\d{4}-\d{2}-\d{2})\s*-->")
ANY_DATE = re.compile(r"\b(20\d{2}-\d{2}-\d{2})\b")

# A heading may sit one day either side of its commit: the entry is often
# written just before midnight UTC and committed just after, and the reverse
# happens when a commit is amended the next morning. Two days apart is not a
# timezone, it is a wrong date.
HEADING_TOLERANCE_DAYS = 1

STALE_DAYS = 90

# Files whose dates are quoted rather than asserted: a guard explaining what it
# checks, a document recording what another document said.
QUOTES_DATES = {"tools/guards/check_dates.py", "tools/guards/check_docs.py",
                "tools/guards/check_non_goals.py", "tools/guards/selftest.py"}


def git(*args, cwd=ROOT):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True,
                          text=True)


def newest_commit_date():
    """The latest author date in the repository, as the upper bound on 'now'.

    Deliberately NOT the system clock. If the container clock is wrong, every
    date this guard blesses is wrong with it - but git's record of when work
    was actually committed is not something a bad clock can retroactively
    change for commits that already exist.
    """
    r = git("log", "-1", "--format=%aI")
    if r.returncode != 0 or not r.stdout.strip():
        return None
    return datetime.fromisoformat(r.stdout.strip()).date()


def blame_dates(rel_path):
    """line number (1-based) -> date the line was last touched."""
    r = git("blame", "--line-porcelain", "--", rel_path)
    if r.returncode != 0:
        return {}
    out, line_no, pending = {}, 0, None
    for line in r.stdout.splitlines():
        m = re.match(r"^[0-9a-f]{40} \d+ (\d+)", line)
        if m:
            line_no = int(m.group(1))
            pending = None
        elif line.startswith("author-time "):
            pending = int(line.split()[1])
        elif line.startswith("\t") and line_no:
            if pending is not None:
                out[line_no] = datetime.fromtimestamp(
                    pending, tz=timezone.utc).date()
            line_no = 0
    return out


def main() -> int:
    problems, notes = [], []

    if not (ROOT / ".git").exists():
        print("dates: not a git checkout, nothing to check against")
        return 0

    today = newest_commit_date()
    if today is None:
        print("dates: no commits yet, nothing to check against")
        return 0

    # Uncommitted work is newer than the newest commit by definition.
    if git("status", "--porcelain").stdout.strip():
        sys_today = datetime.now(timezone.utc).date()
        if sys_today > today:
            today = sys_today

    # --- check 1: PROGRESS.md headings match when they were written --------
    if PROGRESS.exists():
        blame = blame_dates("docs/PROGRESS.md")
        for i, line in enumerate(PROGRESS.read_text().splitlines(), 1):
            m = HEADING.match(line)
            if not m:
                continue
            claimed = date.fromisoformat(m.group(1))
            actual = blame.get(i)
            if actual is None:          # written now, not yet committed
                actual = datetime.now(timezone.utc).date()
            delta = abs((claimed - actual).days)
            if delta > HEADING_TOLERANCE_DAYS:
                problems.append(
                    f"docs/PROGRESS.md:{i}: entry is dated {claimed}, but git "
                    f"says the line was written {actual} ({delta} days apart). "
                    f"The log records when work happened; it is not a place to "
                    f"put the date somebody thought it was.")

    # --- checks 2 and 3: no future dates, and stale markers are named ------
    for path in sorted(ROOT.rglob("*.md")):
        if ".git" in path.parts:
            continue
        rel = str(path.relative_to(ROOT))
        if rel in QUOTES_DATES:
            continue
        for i, line in enumerate(path.read_text().splitlines(), 1):
            for raw in ANY_DATE.findall(line):
                try:
                    d = date.fromisoformat(raw)
                except ValueError:
                    continue
                if d > today:
                    problems.append(
                        f"{rel}:{i}: dated {raw}, which is after the newest "
                        f"commit ({today}). Either the clock is wrong or the "
                        f"date was guessed; both are worth stopping for.")
            for raw in CHECKED.findall(line):
                age = (today - date.fromisoformat(raw)).days
                if age > STALE_DAYS:
                    notes.append(
                        f"{rel}:{i}: last checked {raw}, {age} days ago")

    print(f"dates: checked against git, newest commit {today}")
    for n in notes:
        print(f"  stale: {n}")

    if problems:
        print(f"\n{len(problems)} date(s) that git does not agree with:",
              file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
