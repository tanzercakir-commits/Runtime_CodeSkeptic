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
about checkable facts more than once - it reported that neither project
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

Check 2 needs one escape valve, and it needed it two commits after being
tightened. A date can be legitimately in the future: GitHub's billing page says
"Included usage limits reset in 6 days", which is 2026-08-01, and recording that
is not a wrong clock - it is a scheduled event. Check 2 cannot tell the two
apart, so an intended future date carries `<!-- future -->` on the same line or
the line before. An UNMARKED future date still fails, which is the case the
check exists for.

Worth noting the shape: this guard was too lax two commits ago and too strict
here. Both were real, and neither was found by reasoning about it.

Uncommitted lines blame as "not committed yet" and are dated from the working
tree, so a new entry written now is checked against now.

A TRUNCATED HISTORY CANNOT ANSWER CHECK 1, AND MUST NOT PRETEND TO.

`actions/checkout@v4` clones with `fetch-depth: 1` by default. In a repository
with one commit, `git blame` attributes EVERY line to HEAD - so every heading in
this log looks as though it were written at HEAD's date, and this guard produced
**thirteen** confident, wrong accusations on 2026-07-26. Locally it passed. It
had been doing that since the day it was added, invisibly, because CI was dark
for 33.5 hours over the same period.

The first fix was to skip check 1 whenever `git rev-parse
--is-shallow-repository` said true - and that was too blunt, which the machine
this was written on demonstrated immediately: it reports `true` (cloned with
`--depth` on 2026-07-24) while carrying 65 commits, and blame there had caught a
real heading error an hour earlier. Skipping would have thrown away a working
check.

The precise rule is not "is the clone shallow" but "is THIS LINE'S attribution a
truncation artifact". A line blamed to a **graft point** - a SHA listed in
`.git/shallow`, where history was cut - may really belong to an older commit that
is not present. A line blamed to anything else is attributed correctly, shallow
clone or not.

So each line is judged on its own blame. In a depth-1 clone every line blames to
HEAD, HEAD is the graft point, and all of them are skipped with a count. In a
clone deep enough to see the commit that wrote the line, the check runs.
`ci.yml` additionally gives the guards job `fetch-depth: 0`, so the check is
performed for real somewhere rather than merely not-failing everywhere.
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
# A date that is SUPPOSED to be in the future says so, on the same line or the
# line before. Without this, check 2 cannot tell a scheduled event from a clock
# that is wrong.
FUTURE_OK = re.compile(r"<!--\s*future\s*-->")

# A heading may sit one day either side of its commit ONLY when the commit is
# actually near midnight. The entry is often written just before midnight UTC
# and committed just after, and that case is real - but a flat +/-1 day made the
# guard useless for the case it exists for.
#
# It happened, hours after an external reviewer flagged the same class. A
# session ran past midnight; `438c13d` was committed at 2026-07-26T10:43Z with a
# heading reading `## 2026-07-25`, eleven and a half hours wrong. The guard
# passed, because 1 <= 1. The author's notion of "today" had gone stale in the
# middle of the work and the one check written to catch that shrugged.
#
# So the day of grace is conditional on the clock: within this many minutes of
# midnight, a one-day disagreement is a timezone artifact. Outside it, a
# one-day disagreement is a wrong date.
HEADING_TOLERANCE_DAYS = 1
MIDNIGHT_GRACE_MINUTES = 180

STALE_DAYS = 90

# Files whose dates are quoted rather than asserted: a guard explaining what it
# checks, a document recording what another document said.
QUOTES_DATES = {"tools/guards/check_dates.py", "tools/guards/check_docs.py",
                "tools/guards/check_non_goals.py", "tools/guards/selftest.py"}


def git(*args, cwd=ROOT):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True,
                          encoding="utf-8", errors="replace")


def graft_points() -> set:
    """SHAs where history was truncated; blame at one of these is unreliable."""
    path = ROOT / ".git" / "shallow"
    if not path.exists():
        return set()
    return {line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()}


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


def blame_datetimes(rel_path):
    """line number (1-based) -> UTC datetime the line was last touched.

    The time, not just the date: whether a one-day disagreement is forgivable
    depends on how close the commit was to midnight.
    """
    r = git("blame", "--line-porcelain", "--", rel_path)
    if r.returncode != 0:
        return {}
    out, line_no, pending, sha = {}, 0, None, None
    for line in r.stdout.splitlines():
        m = re.match(r"^([0-9a-f]{40}) \d+ (\d+)", line)
        if m:
            sha = m.group(1)
            line_no = int(m.group(2))
            pending = None
        elif line.startswith("author-time "):
            pending = int(line.split()[1])
        elif line.startswith("\t") and line_no:
            if pending is not None:
                out[line_no] = (datetime.fromtimestamp(pending, tz=timezone.utc),
                                sha)
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
    grafts = graft_points()
    truncated = 0
    if PROGRESS.exists():
        blame = blame_datetimes("docs/PROGRESS.md")
        for i, line in enumerate(PROGRESS.read_text(encoding="utf-8").splitlines(), 1):
            m = HEADING.match(line)
            if not m:
                continue
            claimed = date.fromisoformat(m.group(1))
            entry = blame.get(i)
            if entry is None:           # written now, not yet committed
                when, sha = datetime.now(timezone.utc), None
            else:
                when, sha = entry
            if sha is not None and sha in grafts:
                # History was cut here; this line may belong to a commit that is
                # not present. Not evidence of anything.
                truncated += 1
                continue
            actual = when.date()
            delta = abs((claimed - actual).days)
            if delta == 0:
                continue

            # How far from midnight was the commit?
            minutes = when.hour * 60 + when.minute
            from_midnight = min(minutes, 24 * 60 - minutes)
            forgivable = (delta <= HEADING_TOLERANCE_DAYS and
                          from_midnight <= MIDNIGHT_GRACE_MINUTES)
            if not forgivable:
                detail = (f"{delta} day(s) apart" if delta > 1 else
                          f"one day apart, and the commit was "
                          f"{from_midnight // 60}h{from_midnight % 60:02d}m from "
                          f"midnight - too far for a timezone artifact")
                problems.append(
                    f"docs/PROGRESS.md:{i}: entry is dated {claimed}, but git "
                    f"says the line was written "
                    f"{when.strftime('%Y-%m-%d %H:%MZ')} ({detail}). The log "
                    f"records when work happened; it is not a place to put the "
                    f"date somebody thought it was.")

    # --- checks 2 and 3: no future dates, and stale markers are named ------
    for path in sorted(ROOT.rglob("*.md")):
        if ".git" in path.parts:
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel in QUOTES_DATES:
            continue
        lines = path.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines, 1):
            window = "\n".join(lines[max(0, i - 2):i])
            future_allowed = bool(FUTURE_OK.search(window))
            for raw in ANY_DATE.findall(line):
                try:
                    d = date.fromisoformat(raw)
                except ValueError:
                    continue
                if d > today and not future_allowed:
                    problems.append(
                        f"{rel}:{i}: dated {raw}, which is after the newest "
                        f"commit ({today}). Either the clock is wrong or the "
                        f"date was guessed; both are worth stopping for. If it "
                        f"is a scheduled event, mark the line `<!-- future -->`.")
            for raw in CHECKED.findall(line):
                age = (today - date.fromisoformat(raw)).days
                if age > STALE_DAYS:
                    notes.append(
                        f"{rel}:{i}: last checked {raw}, {age} days ago")

    print(f"dates: checked against git, newest commit {today}"
          + (f"  ({truncated} heading(s) skipped: blamed to a shallow graft "
             f"point, where attribution is a truncation artifact)"
             if truncated else ""))
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
