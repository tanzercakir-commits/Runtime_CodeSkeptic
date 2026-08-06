#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A shell variable that is assigned and never read is usually a missing use.

On 2026-07-26, the first macOS run in this project's history failed inside
`tests/groundtruth/selftest.sh`, on the harness's own precondition check:

    precondition failed - exact-mapping-in-carveout predicts UNSUPPORTED on
    this host, not UNKNOWN
      the selftest's rows are keyed on that; fix the row, not this check

The `unknown` rows were being checked against the MEASURED host, on the
assumption that no real machine knows anything about `0x1307200000`. True on
Linux. False on macOS, where the probe MEASURES the Rosetta/GPU carveout
`[0x1000000000, 0x7000000000)` as unavailable and the address falls inside it -
so the contract predicts UNSUPPORTED, and a row that is supposed to exercise
UNKNOWN was exercising nothing.

The fix was already sitting in the file, unused:

    PROFILE="$ROOT/profiles/fixtures/unknown-host.synthetic.json"

Assigned on the day the file was written, with a comment explaining that it
exists so "an UNKNOWN prediction is guaranteed rather than hoped for", and then
referenced ZERO times. `grep -c '\\$PROFILE'` returned 0. The author wrote down
the right answer and then did not use it, and nothing objected for the file's
entire life.

WHAT THIS CATCHES, AND WHAT IT DOES NOT.

It catches the symptom: a name assigned and never read. It does NOT catch the
disease, which was a test whose expected value depended on the host it happened
to run on. That is much harder to check mechanically, and this guard should not
be mistaken for having checked it.

What makes the cheap version worth having anyway is that here the symptom sat
directly on top of the disease. A one-line check would have pointed at the exact
line whose absence caused a real failure on a real runner three weeks later. The
same is true of the general case: an unused variable in a script this small is
almost never deliberate.

DELIBERATELY CONSERVATIVE. Only whole-statement assignments count, so a
`FOO=bar cmd` prefix - which sets the environment of a child process - is not a
local variable and is not flagged. `export` and `readonly -x` are skipped for the
same reason: something outside the script may read them. A name used only inside
a comment does not count as used, because a comment is not a use.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# A whole-statement assignment: nothing before it but optional `readonly`/
# `declare`, nothing after it but end-of-statement. A trailing command means the
# assignment is environment for a child process, not a variable of this script.
ASSIGN = re.compile(
    r"""^\s*
        (?:readonly\s+|declare\s+(?:-[a-zA-Z]+\s+)*|typeset\s+)?
        (?P<name>[A-Za-z_][A-Za-z0-9_]*)
        \+?=
        (?P<value>
            (?:"[^"]*"|'[^']*'|\$\([^)]*\)|\([^)]*\)|[^\s;#]*)
        )
        \s*$""",
    re.VERBOSE)

# Set for something else to read. Not this script's business whether it is used.
EXPORTED = re.compile(r"^\s*(?:export|declare\s+-[a-zA-Z]*x|local)\b")

COMMENT = re.compile(r"^\s*#")

# The opener of a here-document: `<<WORD`, `<< WORD`, `<<-WORD`, `<<'WORD'`.
# `$((a << 2))` does not match - a shift is followed by a number or `$var`, not
# a bare identifier. The body of a heredoc is DATA, frequently another language
# (a Python or awk program passed to `python3 -`/`awk`), so a `kwarg=value` in
# it - `print(x, file=sys.stderr)` - is not a shell assignment. The first script
# in this repo to embed a Python heredoc tripped exactly that false positive.
HEREDOC_OPEN = re.compile(r"<<-?\s*([\"']?)([A-Za-z_][A-Za-z0-9_]*)\1")


def mask_heredocs(lines):
    """Blank every here-document BODY, keeping line numbers aligned so error
    messages still point at the right line. A body is masked only when a closing
    delimiter line is actually found below the opener; an unmatched `<<` (an
    arithmetic shift, say) is left alone, so this never hides more than a real
    heredoc."""
    out = list(lines)
    i, n = 0, len(lines)
    while i < n:
        line = lines[i]
        if not line.lstrip().startswith("#"):
            m = HEREDOC_OPEN.search(line)
            if m:
                delim, strip = m.group(2), "<<-" in line
                j, closed_at = i + 1, None
                while j < n:
                    cand = lines[j].lstrip("\t") if strip else lines[j]
                    if cand == delim:
                        closed_at = j
                        break
                    j += 1
                if closed_at is not None:
                    for k in range(i + 1, closed_at):
                        out[k] = ""
                    i = closed_at + 1
                    continue
        i += 1
    return out

# Loop and read targets are assignments too, but of a kind whose non-use is a
# different (and often legitimate) thing - `while read -r a b _` discards fields
# on purpose.
BOUND_ELSEWHERE = re.compile(
    r"\bfor\s+([A-Za-z_][A-Za-z0-9_]*)\s+in\b"
    r"|\bread\s+(?:-[a-zA-Z]+\s+)*((?:[A-Za-z_][A-Za-z0-9_]*\s*)+)"
    r"|\bgetopts\s+\S+\s+([A-Za-z_][A-Za-z0-9_]*)")

# A name this project sets for a reader that is not a shell: CI, cmake, a child
# process invoked by something other than a prefix assignment.
ALWAYS_LIVE = {"IFS", "PS4", "LC_ALL", "LANG", "PATH"}


def statements(line: str):
    """`a=1; b=2` is two statements. Split on `;` outside quotes, crudely."""
    out, buf, quote = [], "", None
    for ch in line:
        if quote:
            buf += ch
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
            buf += ch
        elif ch == ";":
            out.append(buf)
            buf = ""
        else:
            buf += ch
    out.append(buf)
    return out


def scan(path: Path):
    """[(line, name)] for each variable assigned as a statement and never read."""
    lines = mask_heredocs(path.read_text(errors="replace").splitlines())

    assigned = {}          # name -> first line it is assigned on
    assign_lines = set()    # line numbers that are pure assignments
    for i, line in enumerate(lines, 1):
        if COMMENT.match(line) or EXPORTED.match(line):
            continue
        for stmt in statements(line):
            m = ASSIGN.match(stmt)
            if not m:
                continue
            name = m.group("name")
            if name in ALWAYS_LIVE:
                continue
            assigned.setdefault(name, i)
            assign_lines.add(i)

    # A name that appears anywhere outside a comment and outside its own
    # assignment's left-hand side is read. Deliberately broad: this guard would
    # rather miss a dead variable than accuse a live one.
    body = []
    for i, line in enumerate(lines, 1):
        if COMMENT.match(line):
            continue
        if i in assign_lines:
            # Keep the right-hand side - `A="$A more"` reads A - and drop the
            # left, so an assignment is not its own use.
            line = re.sub(r"^\s*(?:readonly\s+|declare\s+(?:-[a-zA-Z]+\s+)*)?"
                          r"[A-Za-z_][A-Za-z0-9_]*\+?=", "", line)
        body.append(line)
    text = "\n".join(body)

    also_bound = set()
    for m in BOUND_ELSEWHERE.finditer(text):
        for group in m.groups():
            if group:
                also_bound.update(group.split())

    dead = []
    for name, line in sorted(assigned.items(), key=lambda kv: kv[1]):
        if name in also_bound:
            continue
        if re.search(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])",
                     text):
            continue
        dead.append((line, name))
    return dead


def main() -> int:
    problems = []
    scanned = 0
    for path in sorted(ROOT.rglob("*.sh")):
        if ".git" in path.parts or "build" in path.parts:
            continue
        scanned += 1
        rel = path.relative_to(ROOT).as_posix()
        for line, name in scan(path):
            problems.append(
                f"{rel}:{line}: `{name}` is assigned and never read. In "
                f"`selftest.sh` this exact shape was the fix for a real macOS "
                f"failure, written down and then not used, for the file's whole "
                f"life. Either use it or delete it.")

    print(f"shell vars: {scanned} script(s) checked for assigned-and-never-read")
    if problems:
        print(f"\n{len(problems)} variable(s) assigned and never read:",
              file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
