#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reject official GitHub actions that still target the retired Node runtime.

The 2026-08-06 hosted evidence runs were green but every job carried GitHub's
warning that checkout@v4, upload-artifact@v4 and download-artifact@v4 target
Node.js 20 and were being forced onto Node.js 24. Green is not current: once
the compatibility bridge disappears those same workflows stop before CMake.

The minimum majors below are the first releases that run on Node.js 24 by
default for the actions this repository uses. This guard checks every workflow,
rejects moving tags such as @main, and makes the runtime floor a repository
contract instead of another maintenance note.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
MINIMUM = {
    "checkout": 6,
    "upload-artifact": 7,
    "download-artifact": 8,
}
USE = re.compile(
    r"(?m)^\s*-?\s*uses:\s*actions/"
    r"(checkout|upload-artifact|download-artifact)@([^\s#]+)"
)
MAJOR = re.compile(r"v([0-9]+)$")


def main() -> int:
    if not WORKFLOWS.is_dir():
        print("action runtime guard: no .github/workflows directory", file=sys.stderr)
        return 1

    problems: list[str] = []
    checked = 0
    for path in sorted(WORKFLOWS.glob("*.yml")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in USE.finditer(text):
            action, ref = match.groups()
            checked += 1
            line = text.count("\n", 0, match.start()) + 1
            major = MAJOR.fullmatch(ref)
            if major is None:
                problems.append(
                    f"{path.relative_to(ROOT)}:{line}: actions/{action}@{ref} "
                    "is not a reviewable major release tag"
                )
                continue
            actual = int(major.group(1))
            required = MINIMUM[action]
            if actual < required:
                problems.append(
                    f"{path.relative_to(ROOT)}:{line}: actions/{action}@v{actual} "
                    f"is below the Node.js 24 minimum v{required}"
                )

    if checked == 0:
        problems.append("no guarded official actions were found")

    if problems:
        print("action runtime guard: FAIL", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print(f"action runtimes: {checked} use(s) meet the Node.js 24 release floor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
