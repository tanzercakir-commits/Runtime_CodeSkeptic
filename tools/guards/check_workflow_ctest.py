#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A workflow's ctest invocation must name a build configuration.

THE DEFECT THIS EXISTS FOR, AND IT IS THE THIRD OF ITS SHAPE.

`ci.yml`'s failure-diagnostics step ran

    ctest --test-dir build --rerun-failed --output-on-failure

and published the tail of that output to `refs/ci-logs/<sha>/<job>`. That is the
channel this project reads instead of the Actions tab. On Linux and macOS the
generator is single-config and the command works. MSVC's generator is
MULTI-CONFIG, and there ctest without `-C` does not run the test - it prints

    Test not available without configuration.  (Missing "-C <config>"?)

So for the entire life of the repository, every Windows failure delivered that
sentence in place of the failure. The one that provoked this guard was
`268bb3e`, where `test_probe`'s coverage assertion was made to carry the code
page, the heap page and `max_user_address` PRECISELY so a Windows runner would
report them - and the diagnostics ate the message and reported the missing flag.

The shape is the one this project keeps meeting: correct on every platform
anyone runs, useless on the platform nobody does. It had already been met twice
in this very channel - two `run:` blocks written in bash under `shell: pwsh`, so
`report` had failed on every Windows run ever and `refuse_wine` had never
executed once. A diagnostics channel is the worst possible place for it, because
a diagnostics step only speaks when something else has already gone wrong, and
nobody checks that the alarm works on the day nothing is on fire.

WHAT IS CHECKED.

  1. Every `ctest` invocation in `.github/workflows/*.yml` names a configuration
     (`-C`, `--build-config`, or `--config`). Naming one is harmless where the
     generator is single-config, which is why the rule is unconditional: a rule
     that has to know which job runs on which runner has kept the dependency it
     was written to remove.

  2. The configuration named matches a `-DCMAKE_BUILD_TYPE=` in the same file.
     A diagnostics run of a config that was never built reports nothing just as
     thoroughly as one that names no config at all.

This needs no runner, which is the point.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = sorted((ROOT / ".github" / "workflows").glob("*.yml"))

# `ctest` as a COMMAND WORD. Not `\bctest\b`, which was the first version and was
# wrong on this repository's own workflows: `.` is a word boundary, so
# `tail -c 30000 /tmp/diag/ctest.txt` matched and five correct lines were
# reported as defects. A guard that fires on the fixed tree is worse than none,
# because the next person silences it. So: preceded by start-of-line or a shell
# separator, followed by one - never by `.` or `_`.
# Comment lines are dropped before matching, so prose about ctest is not a call.
CTEST = re.compile(r"(?:^|[\s;&|(])ctest(?=[\s;&|)]|$)")
CONFIG_FLAG = re.compile(r"(?:^|\s)(?:-C|--build-config|--config)(?:=|\s+)"
                         r"([A-Za-z0-9_.-]+)")
BUILD_TYPE = re.compile(r"-DCMAKE_BUILD_TYPE=([A-Za-z0-9_.-]+)")


def commands(text: str):
    """Yield (line_number, joined_command) for each shell line, continuations
    folded in. Comment lines are dropped, so prose about ctest is not a call."""
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        raw = lines[i]
        stripped = raw.strip()
        if stripped.startswith("#"):
            i += 1
            continue
        start = i + 1
        joined = raw
        while joined.rstrip().endswith("\\") and i + 1 < len(lines):
            i += 1
            joined = joined.rstrip()[:-1] + " " + lines[i].strip()
        yield start, joined
        i += 1


def main() -> int:
    if not WORKFLOWS:
        print("no workflows found", file=sys.stderr)
        return 1

    problems = []
    checked = 0
    for path in WORKFLOWS:
        text = path.read_text(errors="replace")
        built = set(BUILD_TYPE.findall(text))
        rel = path.relative_to(ROOT)
        for lineno, cmd in commands(text):
            if not CTEST.search(cmd):
                continue
            checked += 1
            named = CONFIG_FLAG.findall(cmd)
            if not named:
                problems.append(
                    f"{rel}:{lineno}: ctest with no configuration. On MSVC's "
                    f"multi-config generator this prints 'Test not available "
                    f"without configuration' instead of running the test, which "
                    f"is what every Windows entry in refs/ci-logs/* said for the "
                    f"life of this repository. Add --build-config.")
                continue
            if built and not any(n in built for n in named):
                problems.append(
                    f"{rel}:{lineno}: ctest names config {named[0]!r}, which no "
                    f"-DCMAKE_BUILD_TYPE= in this file builds "
                    f"({', '.join(sorted(built))}). A config that was never "
                    f"built has no tests to run.")

    print(f"workflow ctest: {checked} invocation(s) across "
          f"{len(WORKFLOWS)} workflow(s)")
    if problems:
        print(f"\n{len(problems)} ctest invocation(s) that a Windows runner "
              f"cannot execute:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
