#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A shell script here has to run on the bash macOS actually ships.

macOS ships **bash 3.2**, released in 2007 and frozen there because every later
version is GPLv3. Every Linux runner and every developer machine in this project
has bash 5. So a bash-4 construct is invisible locally, invisible in CI's Linux
matrix, and fatal on the one platform half this project's claims are about.

It was fatal, on 2026-07-26, the first time macOS ran in `ci.yml` on a push:

    tests/groundtruth/selftest.sh: line 57: supported: unbound variable

`declare -A want=( [supported]=... )`. Bash 3.2 has no associative arrays, so
`[supported]` is parsed as an arithmetic subscript, `supported` is read as a
variable name, and `set -u` kills the script. It had been broken since the file
was written. Nobody saw it because `ci.yml` ran macOS only in
`expensive-platforms`, gated off pushes - so the harness that validates this
project's comparison table had never once run on macOS.

Writing this guard found two more of the same class immediately, one of them the
NEXT failure that was queued behind the first:

    tests/groundtruth/run.sh:85        mapfile -t args < <(...)
    tools/campaign/run_campaign.sh:43  declare -A counts=( ... )

`run.sh` is the ground-truth harness itself. It would have died on macOS the
moment `selftest.sh` stopped dying first.

Same shape as the missing `<iterator>` found the same day, and the reason both
guards exist: green on every platform anyone runs, broken on the one nobody does,
and a runner minute to discover.

WHAT IS CHECKED. Constructs that bash 3.2 does not have, and a couple of GNU-only
utility flags that fail on BSD userland. Deliberately a short list of things that
are unambiguous and load-bearing - not a shell style checker.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# pattern -> (what it needs, what to do instead)
BASH4 = [
    (r"declare\s+-A\b",
     "associative arrays (bash 4)",
     "a case/esac lookup function, or plain per-key variables"),
    (r"local\s+-A\b",
     "associative arrays (bash 4)",
     "a case/esac lookup function"),
    (r"\bmapfile\b|\breadarray\b",
     "mapfile/readarray (bash 4)",
     "arr=(); while IFS= read -r l; do arr+=(\"$l\"); done < <(...)"),
    (r"\$\{[A-Za-z_][A-Za-z0-9_]*,,",
     "${var,,} lowercasing (bash 4)",
     "tr '[:upper:]' '[:lower:]'"),
    (r"\$\{[A-Za-z_][A-Za-z0-9_]*\^\^",
     "${var^^} uppercasing (bash 4)",
     "tr '[:lower:]' '[:upper:]'"),
    (r"&>>",
     "&>> append-both-streams (bash 4)",
     ">>file 2>&1"),
    (r"\bwait\s+-n\b",
     "wait -n (bash 4.3)",
     "wait on explicit PIDs"),
    (r"\$\{[A-Za-z_][A-Za-z0-9_]*@[QEPAa]\}",
     "${var@Q} transformations (bash 4.4)",
     "printf %q"),
]

# GNU coreutils flags that BSD/macOS userland does not accept.
GNU_ONLY = [
    (r"\bsed\s+-i\s+-e|\bsed\s+-i\s+'", "sed -i without a backup suffix",
     "sed -i.bak, or write to a temp file and mv"),
    (r"\breadlink\s+-f\b", "readlink -f (GNU)",
     "cd \"$(dirname \"$0\")\" && pwd"),
    (r"\bdate\s+-d\b", "date -d (GNU)", "date -u -j -f on BSD, or python3"),
    (r"\bstat\s+-c\b", "stat -c (GNU)", "stat -f on BSD, or python3"),
    (r"\bgrep\s+-P\b", "grep -P (GNU)", "grep -E, or python3"),
    (r"\btimeout\s", "timeout (GNU coreutils; absent on stock macOS)",
     "guard with `command -v timeout` or accept it may be missing"),
]

# A line that is a comment, and a heredoc body, are not code.
COMMENT = re.compile(r"^\s*#")


def scan(path: Path):
    """(line number, construct, advice) for each hit outside comments."""
    hits = []
    lines = path.read_text(errors="replace").splitlines()
    for i, line in enumerate(lines, 1):
        if COMMENT.match(line):
            continue
        for pattern, what, instead in BASH4 + GNU_ONLY:
            if re.search(pattern, line):
                hits.append((i, what, instead, line.strip()[:70]))
                break
    return hits


def main() -> int:
    problems = []
    scanned = 0
    for path in sorted(ROOT.rglob("*.sh")):
        if ".git" in path.parts or "build" in path.parts:
            continue
        scanned += 1
        rel = str(path.relative_to(ROOT))
        for line, what, instead, text in scan(path):
            problems.append(
                f"{rel}:{line}: uses {what}. macOS ships bash 3.2 and BSD "
                f"userland, where this fails - and nothing in this project's "
                f"Linux matrix will tell you. Use {instead}. Offending line: "
                f"\"{text}\"")

    print(f"shell: {scanned} script(s) checked against bash 3.2 and BSD userland")
    if problems:
        print(f"\n{len(problems)} construct(s) that will not run on macOS:",
              file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
