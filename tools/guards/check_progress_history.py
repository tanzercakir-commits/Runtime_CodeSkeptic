#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""PROGRESS is prepend-only: new history may be added, old history may not move.

The log is newest-first, so a new session is inserted immediately after the
front-matter divider. "Append-only" here means immutable history, not physical
end-of-file order. This guard compares the working tree to HEAD when the file
is dirty, otherwise HEAD to its first parent (the shape CI sees after a commit).
The previous file must survive byte-for-byte as the front matter plus the old
entry tail; only one new block between them is permitted.
"""
import subprocess
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REL = "docs/PROGRESS.md"
PROGRESS = ROOT / REL
TODO_REL = "docs/TODO.md"
TODO = ROOT / TODO_REL
DIVIDER = "\n---\n"


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          text=True, encoding="utf-8")


def committed(ref: str, rel: str = REL):
    result = git("show", f"{ref}:{rel}")
    return result.stdout if result.returncode == 0 else None


def task_ids(text: str) -> set:
    return set(re.findall(r"^###\s+(T-\d{3})\b", text, re.MULTILINE))


def main() -> int:
    if not PROGRESS.exists():
        print(f"{REL} is missing; completed work has nowhere durable to go",
              file=sys.stderr)
        return 1
    if not (ROOT / ".git").exists():
        print("progress history: not a git checkout, history check skipped")
        return 0

    dirty = bool(git("status", "--porcelain", "--", REL, TODO_REL).stdout.strip())
    baseline_ref = "HEAD" if dirty else "HEAD^"
    before = committed(baseline_ref)
    if before is None:
        if (ROOT / ".git" / "shallow").exists():
            print("progress history guard failed: shallow checkout has no "
                  f"reachable {baseline_ref} baseline; fetch full history",
                  file=sys.stderr)
            return 1
        print(f"progress history: no {baseline_ref} version, initial history accepted")
        return 0

    before_todo = committed(baseline_ref, TODO_REL) or ""
    after_todo = TODO.read_text(encoding="utf-8") if TODO.exists() else ""
    removed_ids = sorted(task_ids(before_todo) - task_ids(after_todo))
    after = PROGRESS.read_text(encoding="utf-8")
    if after == before:
        if removed_ids:
            print("progress history guard failed:", file=sys.stderr)
            print("  TODO item(s) consumed without a new PROGRESS block: " +
                  ", ".join(removed_ids), file=sys.stderr)
            return 1
        print("progress history: unchanged")
        return 0

    marker = before.find(DIVIDER)
    if marker < 0:
        print(f"{REL} baseline has no front-matter divider; cannot locate the "
              "only legal insertion point", file=sys.stderr)
        return 1
    split = marker + len(DIVIDER)
    prefix, old_entries = before[:split], before[split:]

    if not after.startswith(prefix) or not after.endswith(old_entries):
        print("progress history guard failed:", file=sys.stderr)
        print("  old PROGRESS content was edited, deleted, reordered, or new "
              "content was inserted outside the newest-entry slot", file=sys.stderr)
        print("  keep all existing bytes unchanged and add the new session "
              "immediately after the first divider", file=sys.stderr)
        return 1

    inserted_end = len(after) - len(old_entries) if old_entries else len(after)
    inserted = after[split:inserted_end]
    if not inserted.strip() or not inserted.lstrip().startswith("## "):
        print("progress history guard failed:", file=sys.stderr)
        print("  the inserted block must begin with a dated session heading; "
              "check_dates.py validates the date", file=sys.stderr)
        return 1

    missing_ids = [task_id for task_id in removed_ids
                   if not re.search(rf"(?<![A-Z0-9-]){re.escape(task_id)}(?![A-Z0-9-])",
                                    inserted)]
    if missing_ids:
        print("progress history guard failed:", file=sys.stderr)
        print("  consumed TODO item(s) missing from the new PROGRESS block: " +
              ", ".join(missing_ids), file=sys.stderr)
        return 1

    print(f"progress history: {len(inserted.encode('utf-8'))} new byte(s), "
          "all prior history unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
