#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The Windows probe must compile, on a machine that is not Windows.

THE DEFECT THIS EXISTS FOR.

`b661c94` added the Windows arena. It was cross-compiled with mingw before the
push - by hand, with flags typed from memory:

    -Wall -Wextra -Wconversion -Wsign-conversion

The project's actual set, in `CMakeLists.txt`, is

    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion

`-Wshadow` was the one missing, and the arena's local `walk` shadowed the
`VirtualQuery` walk's `walk` sixty lines above it. MSVC's `/W4` calls that C4456
and `/WX` makes it an error, so `rs_probe` did not build, so every target that
depends on it did not build, so the diagnostics published a ctest run of a tree
that had never compiled. Ten green tests and eight "Could not find executable"
lines, describing nothing.

A hand-typed subset of a flag list is a check that passes for the wrong reason,
which this project has met before under other names. So the cross-compile is not
a thing anyone types: it is this file, it reads the flags out of `CMakeLists.txt`
rather than restating them, and it runs on every push.

WHAT IT IS AND IS NOT.

mingw is not MSVC, and this cannot promise a green Windows build - MSVC has
diagnostics gcc lacks. What it does is move an entire class of defect from a 2x
Windows runner minutes away to a 1x Linux runner seconds away, which is the same
trade `check_includes.py` makes for missing standard headers - a guard written
after the first real Windows build failed on `std::back_inserter` without
`<iterator>`.

SKIPPING IS LOUD. Without a mingw cross-compiler this prints what it did not
check and passes, because a developer's laptop must not need one. CI installs it,
so the check is really performed somewhere rather than skipped everywhere - the
distinction `check_dates.py` learned when `fetch-depth: 1` made it silently
vacuous for its whole life.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CMAKELISTS = ROOT / "CMakeLists.txt"
COMPILERS = ["x86_64-w64-mingw32-g++", "x86_64-w64-mingw32-c++"]

# Every translation unit that is Windows-only, i.e. empty on this machine and
# therefore unchecked by the ordinary build.
SOURCES = ["src/probe/vm_probe_windows.cpp", "src/runtime/windows.cpp"]


def project_warning_flags() -> list:
    """The GNU-syntax flag list, READ from CMakeLists.txt rather than restated.

    A guard that keeps its own copy of the thing it is checking drifts from it,
    which is the failure mode every check in this directory was written after.
    """
    text = CMAKELISTS.read_text(errors="replace")
    # The GNU branch: target_compile_options(rs_warnings INTERFACE -Wall ...)
    match = re.search(
        r"target_compile_options\(rs_warnings\s+INTERFACE\s+((?:\s*-W[\w=-]+)+)\s*\)",
        text)
    if not match:
        return []
    return match.group(1).split()


def main() -> int:
    compiler = next((c for c in COMPILERS if shutil.which(c)), None)
    if compiler is None:
        print("windows cross-compile: SKIPPED - no mingw cross-compiler on PATH")
        print("  wanted one of: " + ", ".join(COMPILERS))
        print("  Debian/Ubuntu: apt-get install g++-mingw-w64-x86-64")
        print("  This is the check that would have caught C4456 in b661c94 "
              "without a Windows runner.")
        return 0

    flags = project_warning_flags()
    if not flags:
        print("could not read the warning flags out of CMakeLists.txt; a guard "
              "that guesses them is the defect it exists to prevent",
              file=sys.stderr)
        return 1

    failures = []
    for rel in SOURCES:
        source = ROOT / rel
        if not source.exists():
            failures.append(f"{rel}: not found")
            continue
        cmd = [compiler, "-std=c++20", "-fsyntax-only",
               "-I", str(ROOT / "include"),
               "-I", str(ROOT / "src" / "runtime"),
               "-DRS_PLATFORM_WINDOWS", "-DNOMINMAX",
               "-D_CRT_SECURE_NO_WARNINGS", "-Werror", *flags, str(source)]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            failures.append(f"{rel}:\n" +
                            (proc.stdout + proc.stderr).strip())

    print(f"windows cross-compile: {len(SOURCES)} Windows-only translation "
          f"unit(s) via {compiler}, with the project's own flags "
          f"({' '.join(flags)})")
    if failures:
        print(f"\n{len(failures)} Windows-only source(s) that will not build:",
              file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        print("\nThis is a 1x Linux second standing in for a 2x Windows minute. "
              "mingw is not MSVC and green here does not promise green there - "
              "but red here is red there.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
