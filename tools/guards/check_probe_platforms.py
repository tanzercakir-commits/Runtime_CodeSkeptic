#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exactly one probe implementation may compile for any platform.

THE DEFECT THIS EXISTS FOR. `src/probe/vm_probe_unimplemented.cpp` guarded itself
with

    #if !defined(RS_PLATFORM_LINUX) && !defined(RS_PLATFORM_MACOS)

which is TRUE on Windows. `vm_probe_windows.cpp` guards itself with
`#if defined(RS_PLATFORM_WINDOWS)`. So on Windows both objects defined
`rs::probe::probe_virtual_memory`, the linker took one, and it took the stub.

Nothing objected. The build succeeded, `ctest` passed 14/14 on a real Windows
runner, and `ci.yml`'s `windows---msvc` job was green - it runs `rs-profile verify`,
and a synthetic profile verifies perfectly well. The first Windows profile anyone
ever looked at said:

    profile_name windows-x86_64   origin SYNTHETIC   host_arch unknown
    page_size, allocation_granularity, max_user_address, ... all unknown

So the Windows probe had never run, on any machine, including Windows. Every claim
in this project that rested on "the Windows probe measures X" rested on a stub that
measures nothing and says so honestly - to a reader who looked, which nobody had.

WHAT IS CHECKED. The `#if` on the platform macros at the top of each
`src/probe/vm_probe_*.cpp` is evaluated against every combination of
RS_PLATFORM_{LINUX,MACOS,WINDOWS,UNKNOWN} that CMakeLists.txt can produce, and
exactly one file must be active for each. This needs no compiler and no platform,
which is the point: the version of this fix that requires a Windows runner to reveal
the problem has not removed the dependency, it has moved it.
"""
import itertools
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBES = sorted((ROOT / "src" / "probe").glob("vm_probe_*.cpp"))

# The mutually exclusive sets CMakeLists.txt defines: exactly one is ever set.
PLATFORMS = ["RS_PLATFORM_LINUX", "RS_PLATFORM_MACOS", "RS_PLATFORM_WINDOWS",
             "RS_PLATFORM_UNKNOWN"]

GUARD = re.compile(r"^#if\s+(.*?)(?://.*)?$")


def guard_expression(path: Path) -> str:
    """The first `#if` mentioning a platform macro, joined across continuations."""
    lines = path.read_text(errors="replace").splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("#if") and "RS_PLATFORM_" in line:
            expr = line
            while expr.rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                expr = expr.rstrip()[:-1] + " " + lines[i]
            m = GUARD.match(expr)
            return m.group(1).strip() if m else expr
        i += 1
    return ""


def evaluate(expr: str, defined: set) -> bool:
    """Evaluate a C preprocessor expression over the platform macros only."""
    py = expr
    for name in PLATFORMS:
        py = py.replace(f"defined({name})", "True" if name in defined else "False")
    py = py.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
    # Anything left that is not a boolean operator means the guard uses something
    # this checker does not model, and a wrong answer is worse than no answer.
    if re.search(r"[A-Za-z_]\w*", py.replace("True", "").replace("False", "")
                                     .replace("and", "").replace("or", "")
                                     .replace("not", "")):
        raise ValueError(f"unmodelled token in guard: {expr}")
    return bool(eval(py))  # noqa: S307 - input is this repository's own source


def main() -> int:
    if not PROBES:
        print("no probe implementations found", file=sys.stderr)
        return 1

    guards = {}
    for path in PROBES:
        expr = guard_expression(path)
        if not expr:
            print(f"{path.relative_to(ROOT)}: no platform guard found",
                  file=sys.stderr)
            return 1
        guards[path.name] = expr

    problems = []
    # Exactly one platform macro is defined at a time, which is what CMake does.
    for platform in PLATFORMS:
        defined = {platform}
        active = []
        for name, expr in guards.items():
            try:
                if evaluate(expr, defined):
                    active.append(name)
            except ValueError as e:
                problems.append(f"{name}: {e}")
        if len(active) != 1:
            problems.append(
                f"{platform}: {len(active)} implementation(s) compile - "
                f"{', '.join(active) if active else 'NONE'}. Exactly one must. "
                f"Two means both define rs::probe::probe_virtual_memory and the "
                f"linker picks; on Windows it picked the stub, and every Windows "
                f"'measurement' was synthetic while every job stayed green. Zero "
                f"means the link fails.")

    # And the belt-and-braces case: no platform macro at all.
    active_none = [n for n, e in guards.items() if evaluate(e, set())]
    if len(active_none) != 1:
        problems.append(
            f"with no RS_PLATFORM_* defined at all, {len(active_none)} "
            f"implementation(s) compile: "
            f"{', '.join(active_none) if active_none else 'NONE'}")

    print(f"probe platforms: {len(guards)} implementation(s), checked against "
          f"{len(PLATFORMS) + 1} platform settings")
    if problems:
        print(f"\n{len(problems)} platform(s) without exactly one probe:",
              file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
