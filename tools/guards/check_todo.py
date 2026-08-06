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

EIGHT CHECKS.

1. EVERY UNFINISHED PLAN CRITERION IS ACCOUNTED FOR. It must carry the id of an
   item in TODO.md, or be tagged `(untracked)` and named in TODO.md's
   "Deliberately not tracked" section with a reason. There is no third option:
   work is either on the compass or explicitly off it.

   `[partial]` COUNTS AS UNFINISHED, and it did not until 2026-07-30. The
   guard read `[open]` and `[blocked]` only, so `[partial]` - the marker this
   project reaches for precisely when something is half-true and needs saying
   out loud - was the one state nobody checked. Gate B sat `[partial]` on two
   named grounds; one of them (`RS-VM-0005` fires on 42% of real mappings,
   correct and unusable in a gate) had no item on the compass at all, and
   nothing objected. Same shape as every other defect in this repository: the
   state nobody looks at.

2. EVERY ID RESOLVES. A `(T-nnn)` tag in the plan must name an item that
   exists. An item that is deleted takes its tags with it.

8. STRUCTURAL HEADINGS FAIL CLOSED. Section headings must be unique and may
   not contain patch-marker debris; every task-like `### T-nnn` line must
   match the canonical item shape; duplicate task ids are forbidden. Otherwise
   a malformed heading can make a real task invisible while the guard stays
   green.

7. AN UNFINISHED CRITERION MAY NOT BE OWNED BY A FINISHED ITEM. Found the same
   day, one line below: Gate B was `[partial]` and cited `(T-004)`, which is
   `[done]`. Read literally that says the work holding the gate open was
   completed - so nobody is doing it and nothing says so. The tag had been
   correct once and the reason underneath it changed.

3. `NOW` HOLDS AT MOST THREE ITEMS. A list where everything is urgent is not a
   compass, it is a wall. This is the check most likely to be resented and it
   is the one worth keeping.

4. EVERY ITEM SAYS WHAT WOULD PROVE IT DONE. "Done when" must name something
   that runs. An item that cannot be finished is a mood.

5. FINISHED WORK IS CONSUMED. A `[done]` item may not remain in TODO.md.
   Completion removes it from the queue; check_progress_history.py separately
   requires the same change to append its evidence to docs/PROGRESS.md.

6. THE MARKER AND THE SECTION MUST AGREE. An item carrying `[now]` has to sit
   under `## Now`. This was missing and the compass contradicted itself in three
   places at once: `T-004` carried `[now]`, sat under `## Next`, and `## Now`
   said "(`Now` is empty)". Checks 3 and 4 read markers, a human reads sections,
   and the two had drifted apart - so `docs/PROGRESS.md` came to assert
   "T-004 stays in Now" about an item that was in neither state. An external
   reviewer found it; nothing here could have.

   ONE ESCAPE VALVE, and it exists because of how this was found. Promoting an
   item is a decision, and `docs/TODO.md` warns in its own text against
   promotion "by drift". A guard that forced the move would make the decision as
   a side effect of a consistency fix - the exact thing the file forbids. So a
   disagreement is permitted when the `## Now` section carries
   `<!-- pending-promotion: T-nnn -->`, which turns a silent contradiction into
   a written, visible, dated open decision.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TODO = ROOT / "docs" / "TODO.md"
PLAN = ROOT / "docs" / "PLAN.md"
PROGRESS = ROOT / "docs" / "PROGRESS.md"

ITEM = re.compile(r"^###\s+(T-\d{3})\s+—\s+(.+?)\s+`\[([a-z]+)\]`\s*$")
PENDING = re.compile(r"<!--\s*pending-promotion:\s*(T-\d{3})\s*-->")

# marker -> the section heading it must sit under.
SECTION_FOR = {"now": "Now", "next": "Next", "later": "Later",
               "blocked": "Blocked", "done": "Done"}
TAG = re.compile(r"\((T-\d{3}|untracked)\)")
MARKER = re.compile(r"`\[([a-z/]+)\]`")
# A PLAN criterion, not a marker mentioned in prose. Either the status opens
# the line with no indent, or a bullet introduces it. Both forms occur in the
# plan; what CANNOT be a criterion is an indented line with no bullet, because
# that is a wrapped continuation of the criterion above it.
#
# Two false positives were needed to arrive at this, in one sitting:
#   1. `^.*` anywhere matched "Still `[partial]` because the rest of the prose
#      is unchecked" mid-sentence. check_plan.py had the identical bug against
#      `[done]` and the identical fix, five days earlier.
#   2. Allowing leading whitespace before the status then matched a WRAPPED
#      line that happened to begin with one: "...wrong again.\n  `[partial]`
#      while the synthetic-only bucket is a backlog". The criterion above it
#      lost its own tag to the split and was reported as unowned.
# A guard that scans its own project's prose for status markers will keep
# inventing criteria out of sentences unless the position is pinned exactly.
CRITERION = re.compile(r"^(?:\s*-\s+)?`\[([a-z/]+)\]`")
STATES = {"now", "next", "later", "blocked", "done"}
MAX_NOW = 3


def parse_todo():
    """id -> {title, state, section, body}, the untracked section, and the
    set of promotions explicitly recorded as pending."""
    items, order = {}, []
    current, body = None, []
    untracked_section, now_section = [], []
    in_untracked = in_now = False
    section = None

    for line in TODO.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            section = line[3:].strip()
            in_untracked = "not tracked" in line.lower()
            in_now = section == "Now"
        if in_untracked:
            untracked_section.append(line)
        if in_now:
            now_section.append(line)

        m = ITEM.match(line)
        if m:
            if current:
                items[current]["body"] = "\n".join(body)
            ident, title, state = m.groups()
            items[ident] = {"title": title, "state": state, "body": "",
                            "section": section}
            order.append(ident)
            current, body = ident, []
        elif current:
            body.append(line)
    if current:
        items[current]["body"] = "\n".join(body)
    pending = set(PENDING.findall("\n".join(now_section)))
    return items, order, "\n".join(untracked_section), pending


def main() -> int:
    problems, notes = [], []

    if not TODO.exists():
        print("docs/TODO.md is missing; it is the compass", file=sys.stderr)
        return 1

    todo_text = TODO.read_text(encoding="utf-8")
    todo_lines = todo_text.splitlines()
    items, order, untracked, pending = parse_todo()

    section_titles = [line[3:].strip() for line in todo_lines
                      if line.startswith("## ")]
    for title in sorted(set(section_titles)):
        if section_titles.count(title) > 1:
            problems.append(f"duplicate section heading: `## {title}`")
    for line_no, line in enumerate(todo_lines, 1):
        if line.startswith("## ") and line.rstrip().endswith("---"):
            problems.append(
                f"docs/TODO.md:{line_no}: malformed section heading `{line}`")
        if line.startswith("###") and "T-" in line and not ITEM.match(line):
            problems.append(
                f"docs/TODO.md:{line_no}: malformed task heading `{line}`")

    valid_ids = [m.group(1) for line in todo_lines
                 if (m := ITEM.match(line))]
    for ident in sorted(set(valid_ids)):
        if valid_ids.count(ident) > 1:
            problems.append(f"duplicate TODO id: {ident}")

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
        if v["state"] == "done":
            problems.append(
                f"{ident} is `[done]` but remains in docs/TODO.md. Finished "
                "items must be consumed from the queue; their evidence belongs "
                "in append-only docs/PROGRESS.md.")
        if v["state"] != "done" and "**Done when:**" not in v["body"]:
            problems.append(
                f"{ident} ({v['title']}) has no **Done when:** - name "
                f"something that RUNS and what it must print. An item that "
                f"cannot be finished is a mood.")
        # Check 6: the marker and the section it sits under must agree.
        want = SECTION_FOR.get(v["state"])
        if want is not None and v.get("section") != want:
            if ident in pending:
                notes.append(
                    f"{ident} is `[{v['state']}]` under `## {v.get('section')}` "
                    f"with promotion recorded as pending - an open decision, "
                    f"not a contradiction")
            else:
                problems.append(
                    f"{ident} carries `[{v['state']}]` but sits under "
                    f"`## {v.get('section')}`; it belongs under `## {want}`. "
                    f"Markers are what the guards read and sections are what a "
                    f"human reads - when they disagree the compass points two "
                    f"ways. If the promotion is a decision waiting to be taken, "
                    f"put `<!-- pending-promotion: {ident} -->` in the `## Now` "
                    f"section and say so out loud.")
        if v["state"] == "blocked" and "**Blocker:**" not in v["body"]:
            problems.append(
                f"{ident} is `[blocked]` with no **Blocker:** named. A "
                f"blocker nobody wrote down is an excuse.")


    # --- checks 1 and 2: the plan and the compass agree ------------------
    plan_tagged = set()
    if PLAN.exists():
        lines = PLAN.read_text(encoding="utf-8").splitlines()
        body_starts = next((i for i, l in enumerate(lines)
                            if l.startswith("## ")), 0)
        for i, line in enumerate(lines, 1):
            if i <= body_starts:
                continue
            head = CRITERION.match(line)
            if not head or head.group(1) not in ("open", "blocked", "partial"):
                continue
            marker = head.group(1)
            # The tag may land on a wrapped line, like the evidence does.
            entry, j = line, i
            while (j < len(lines) and lines[j].strip()
                   and not CRITERION.match(lines[j])
                   and not lines[j].startswith(("#", "|", "```"))):
                entry += " " + lines[j].strip()
                j += 1

            found = TAG.findall(entry)
            if not found:
                problems.append(
                    f"docs/PLAN.md:{i}: `[{marker}]` with no owner - add "
                    f"`(T-nnn)` naming the docs/TODO.md item that will close "
                    f"it, or `(untracked)` and a reason in TODO.md's "
                    f"\"Deliberately not tracked\": "
                    f"\"{line.strip()[:70]}\"")
            for tag in found:
                plan_tagged.add(tag)
                if tag == "untracked":
                    continue
                if tag not in items:
                    problems.append(
                        f"docs/PLAN.md:{i}: cites `{tag}`, which is not an "
                        f"item in docs/TODO.md")
                elif items[tag]["state"] == "done":
                    problems.append(
                        f"docs/PLAN.md:{i}: `[{marker}]` cites `{tag}`, "
                        f"which is `[done]` in docs/TODO.md. An unfinished "
                        f"criterion owned by a finished item reads as: the work "
                        f"holding this open was completed. Nobody is doing it "
                        f"and nothing says so. Retag it to the item that will "
                        f"actually close it, or file one: "
                        f"\"{line.strip()[:70]}\"")

    if "untracked" in plan_tagged and "not tracked" not in untracked.lower():
        problems.append(
            "docs/PLAN.md marks something `(untracked)` but docs/TODO.md has "
            "no \"Deliberately not tracked\" section to justify it")

    by_state = {}
    for v in items.values():
        by_state[v["state"]] = by_state.get(v["state"], 0) + 1
    print("todo: " + "  ".join(f"{k}={v}" for k, v in sorted(by_state.items()))
          + f"  ({len(items)} items, {len(plan_tagged)} plan tag(s))")
    for n in notes:
        print(f"  open decision: {n}")

    if problems:
        print(f"\n{len(problems)} disagreement(s) between the compass and the "
              f"map:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
