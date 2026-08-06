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

WHAT IS CHECKED. Constructs that bash 3.2 does not have, a couple of GNU-only
utility flags that fail on BSD userland, and one bash 3.2 SEMANTIC difference in a
construct that exists in both. Deliberately a short list of things that are
unambiguous and load-bearing - not a shell style checker.

THE SEMANTIC ONE, added after this guard missed the third instance of its own bug
class. On 2026-07-26 the macOS runner finally got past `declare -A`, past ctest and
past the guards, and died in the ground-truth harness:

    tests/groundtruth/run.sh: line 111: args[@]: unbound variable

`"${args[@]}"` where `args=()`. Under `set -u`, bash 3.2 treats an EMPTY array's
expansion as unbound; bash 4.4 does not. The construct is not a bash-4 feature, so
scanning for bash-4 features could never find it - and this guard's whole claim is
to catch this class without a macOS runner. Two cases produced no output, and the
harness correctly recorded `file-map-beyond-eof` as CASE BROKEN rather than as a
confirmed refusal.

The portable form is `${arr[@]+"${arr[@]}"}`, and `${#arr[@]}` has the same
problem. Only arrays that are assigned an EMPTY literal somewhere are flagged, so
a literal table like `ROWS=("a|b" "c|d")` is left alone.
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


# `set -u` in any form: -u, -eu, -uo pipefail, set -o nounset.
NOUNSET = re.compile(r"^\s*set\s+(-[a-zA-Z]*u[a-zA-Z]*\b|-o\s+nounset)")

# An array assigned an EMPTY literal. Those are the ones whose expansion can be
# reached while empty; a non-empty literal table cannot be.
EMPTY_ARRAY = re.compile(
    r"^\s*(?:declare\s+-a\s+|local\s+-a\s+|readonly\s+-a\s+)?"
    r"([A-Za-z_][A-Za-z0-9_]*)=\(\s*\)\s*(?:#.*)?$")

def bare_expansions(line: str):
    """Array names expanded without the `+` guard, and the form used.

    The guarded idiom is `${arr[@]+"${arr[@]}"}`, which CONTAINS a literal
    `${arr[@]}` - so matching the bare form and hoping the guarded one looks
    different does not work. It was tried, and the guard's first run reported all
    four sites it had just been used to fix.

    Balanced braces are not a regex's business, so the guard is detected by its
    only distinguishing mark: `${name[@]+` on the same line.
    """
    out = []
    for m in re.finditer(r"\$\{(#?)([A-Za-z_][A-Za-z0-9_]*)\[([@*])\]\}", line):
        name = m.group(2)
        if re.search(r"\$\{" + re.escape(name) + r"\[[@*]\]\+", line):
            continue
        out.append((name, m.group(0)))
    return out


def scan(path: Path):
    """(line number, construct, advice) for each hit outside comments."""
    hits = []
    text = path.read_text(errors="replace")
    lines = text.splitlines()

    nounset = any(NOUNSET.match(l) for l in lines if not COMMENT.match(l))
    starts_empty = {m.group(1)
                    for l in lines if not COMMENT.match(l)
                    for m in [EMPTY_ARRAY.match(l)] if m}

    for i, line in enumerate(lines, 1):
        if COMMENT.match(line):
            continue
        matched = False
        for pattern, what, instead in BASH4 + GNU_ONLY:
            if re.search(pattern, line):
                hits.append((i, what, instead, line.strip()[:70]))
                matched = True
                break
        if matched or not nounset:
            continue
        for name, form in bare_expansions(line):
            if name not in starts_empty:
                continue
            hits.append((
                i,
                f"{form} on an array that is assigned `{name}=()`, in a script "
                f"with `set -u` (bash 3.2 semantics)",
                f"${{{name}[@]+\"${{{name}[@]}}\"}} - and track a count in a "
                f"plain variable rather than asking for ${{#{name}[@]}}",
                line.strip()[:70]))
            break
    return hits


def main() -> int:
    problems = []
    scanned = 0
    for path in sorted(ROOT.rglob("*.sh")):
        if ".git" in path.parts or "build" in path.parts:
            continue
        scanned += 1
        rel = path.relative_to(ROOT).as_posix()
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
