#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The compass and the map may not point in different directions.

Four documents carry this project and each has one job:

    docs/scenarios/   the spirit  - why, and for whom
    docs/TODO.md      the compass - what we are doing, in order
    docs/PLAN.md      the map     - where we stand against the ROADMAP
    docs/PROGRESS.md  the past    - what changed and what was learned

The failure mode is not that one of them is wrong. It is that two of them stop
agreeing and nothing notices - the plan still lists a criterion as open while
the todo has quietly dropped it, or the todo grows an item that answers to
nothing. Six months later there is no way to tell which document is stale.

FIVE CHECKS.

1. EVERY OPEN OR BLOCKED PLAN CRITERION IS ACCOUNTED FOR. It must carry the id
   of an item in TODO.md, or be tagged `(untracked)` and named in TODO.md's
   "Deliberately not tracked" section with a reason. There is no third option:
   work is either on the compass or explicitly off it.

2. EVERY ID RESOLVES. A `(T-nnn)` tag in the plan must name an item that
   exists. An item that is deleted takes its tags with it.

3. `NOW` HOLDS AT MOST THREE ITEMS. A list where everything is urgent is not a
   compass, it is a wall. This is the check most likely to be resented and it
   is the one worth keeping.

4. EVERY ITEM SAYS WHAT WOULD PROVE IT DONE. "Done when" must name something
   that runs. An item that cannot be finished is a mood.

5. FINISHED WORK LEAVES A TRACE. An item marked `[done]` in TODO.md must be
   mentioned in docs/PROGRESS.md, so that what was learned survives the item
   being crossed off. Work that completes silently gets redone.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TODO = ROOT / "docs" / "TODO.md"
PLAN = ROOT / "docs" / "PLAN.md"
PROGRESS = ROOT / "docs" / "PROGRESS.md"

ITEM = re.compile(r"^###\s+(T-\d{3})\s+—\s+(.+?)\s+`\[([a-z]+)\]`\s*$")
TAG = re.compile(r"\((T-\d{3}|untracked)\)")
MARKER = re.compile(r"`\[([a-z/]+)\]`")
STATES = {"now", "next", "later", "blocked", "done"}
MAX_NOW = 3


def parse_todo():
    """id -> {title, state, body}, plus the untracked-reasons section."""
    items, order = {}, []
    current, body = None, []
    untracked_section = []
    in_untracked = False

    for line in TODO.read_text().splitlines():
        if line.startswith("## "):
            in_untracked = "not tracked" in line.lower()
        if in_untracked:
            untracked_section.append(line)

        m = ITEM.match(line)
        if m:
            if current:
                items[current]["body"] = "\n".join(body)
            ident, title, state = m.groups()
            items[ident] = {"title": title, "state": state, "body": ""}
            order.append(ident)
            current, body = ident, []
        elif current:
            body.append(line)
    if current:
        items[current]["body"] = "\n".join(body)
    return items, order, "\n".join(untracked_section)


def main() -> int:
    problems = []

    if not TODO.exists():
        print("docs/TODO.md is missing; it is the compass", file=sys.stderr)
        return 1

    items, order, untracked = parse_todo()

    if not items:
        problems.append("docs/TODO.md has no `### T-nnn — title `[state]`` "
                        "items - has the format changed?")

    # --- check 3 and 4: shape of the list itself --------------------------
    now = [i for i, v in items.items() if v["state"] == "now"]
    if len(now) > MAX_NOW:
        problems.append(
            f"`Now` holds {len(now)} items ({', '.join(sorted(now))}); the "
            f"limit is {MAX_NOW}. Move something to `next`. A list where "
            f"everything is urgent is not a compass.")

    for ident, v in items.items():
        if v["state"] not in STATES:
            problems.append(f"{ident}: unknown state `[{v['state']}]` "
                            f"(expected one of {', '.join(sorted(STATES))})")
        if v["state"] != "done" and "**Done when:**" not in v["body"]:
            problems.append(
                f"{ident} ({v['title']}) has no **Done when:** - name "
                f"something that RUNS and what it must print. An item that "
                f"cannot be finished is a mood.")
        if v["state"] == "blocked" and "**Blocker:**" not in v["body"]:
            problems.append(
                f"{ident} is `[blocked]` with no **Blocker:** named. A "
                f"blocker nobody wrote down is an excuse.")

    # --- check 5: finished work leaves a trace ---------------------------
    progress_text = PROGRESS.read_text() if PROGRESS.exists() else ""
    for ident, v in items.items():
        if v["state"] == "done" and ident not in progress_text:
            problems.append(
                f"{ident} is `[done]` but docs/PROGRESS.md never mentions it. "
                f"Crossing an item off is not the same as recording what it "
                f"taught.")

    # --- checks 1 and 2: the plan and the compass agree ------------------
    plan_tagged = set()
    if PLAN.exists():
        lines = PLAN.read_text().splitlines()
        body_starts = next((i for i, l in enumerate(lines)
                            if l.startswith("## ")), 0)
        for i, line in enumerate(lines, 1):
            if i <= body_starts:
                continue
            markers = [m for m in MARKER.findall(line)
                       if m in ("open", "blocked")]
            if not markers:
                continue
            # The tag may land on a wrapped line, like the evidence does.
            entry, j = line, i
            while (j < len(lines) and lines[j].strip()
                   and not MARKER.search(lines[j])
                   and not lines[j].startswith(("#", "|", "```"))):
                entry += " " + lines[j].strip()
                j += 1

            found = TAG.findall(entry)
            if not found:
                problems.append(
                    f"docs/PLAN.md:{i}: `[{markers[0]}]` with no owner - add "
                    f"`(T-nnn)` naming the docs/TODO.md item that will close "
                    f"it, or `(untracked)` and a reason in TODO.md's "
                    f"\"Deliberately not tracked\": "
                    f"\"{line.strip()[:70]}\"")
            for tag in found:
                plan_tagged.add(tag)
                if tag != "untracked" and tag not in items:
                    problems.append(
                        f"docs/PLAN.md:{i}: cites `{tag}`, which is not an "
                        f"item in docs/TODO.md")

    if "untracked" in plan_tagged and "not tracked" not in untracked.lower():
        problems.append(
            "docs/PLAN.md marks something `(untracked)` but docs/TODO.md has "
            "no \"Deliberately not tracked\" section to justify it")

    by_state = {}
    for v in items.values():
        by_state[v["state"]] = by_state.get(v["state"], 0) + 1
    print("todo: " + "  ".join(f"{k}={v}" for k, v in sorted(by_state.items()))
          + f"  ({len(items)} items, {len(plan_tagged)} plan tag(s))")

    if problems:
        print(f"\n{len(problems)} disagreement(s) between the compass and the "
              f"map:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
