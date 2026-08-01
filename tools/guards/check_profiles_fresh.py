#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A committed measured profile must not be older than the probe that makes it.

Found on 2026-08-01 by an external reviewer running the tool on their own M1:
`profiles/measured/macos-14-arm64-native.measured.json` carried
`max_user_address = 0x600000000000` and no `max_single_reservation`. Both are
the exact defect that commits d6abf18 and 9311e1c ("macOS reported its own
layout as the top of the address space" / "the ceiling asked whether an address
was free, not whether it existed") fixed on 2026-07-27. The profile was last
written by a509cca on 2026-07-25 - BEFORE the fix - and was never regenerated.
So the repository shipped a "measured" profile carrying a bug the code had
already corrected, and every guard passed, because none of them asked whether a
committed measurement still matches the probe that produces it.

The rule this project runs on: a fact recorded once and never re-checked drifts.
A measured profile is a fact about a host, produced by a specific version of the
probe. When the probe changes how it measures - the ceiling, the arena walk, a
new field like `max_single_reservation` - every committed profile made by the
old probe is now a record of what the old probe would have said, not of the
host. That is drift, and this guard makes it loud.

MECHANISM, deliberately the same shape as check_dates.py (git is the record):
for each `profiles/measured/*.measured.json`, compare the committer timestamp of
the commit that last touched the profile against the commit that last touched
the probe SOURCE for that profile's OS. If any probe source is newer than the
profile, the profile is stale and must be regenerated from a fresh measurement
(the platform's own CI workflow publishes one to refs/measurements/<sha>/<job>)
and re-committed.

Freezing by git date rather than by a hard-coded "expected value" is the point:
the guard does not need to know what the ceiling SHOULD be on any host - only
that a measurement older than the instrument that took it cannot be trusted.

It is deliberately willing to be a little aggressive: a probe refactor that does
not change any measured field will still trip this until the profiles are
regenerated. That is the honest default for this project - regenerating a
measured profile is one CI run, and a silently stale one cost a shipped bug.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MEASURED = ROOT / "profiles" / "measured"

# The probe sources whose change invalidates a measurement, per OS. The shared
# header and the arena walk are in every set because they shape the ranges and
# the ceiling every platform reports.
SHARED = ["include/runtimeskeptic/probe/vm_probe.hpp"]
PROBE_SOURCES = {
    "macos": ["src/probe/vm_probe_macos.cpp", "src/probe/arena_walk.cpp",
              "include/runtimeskeptic/probe/arena_walk.hpp"] + SHARED,
    "windows": ["src/probe/vm_probe_windows.cpp",
                "src/probe/windows_regions.cpp"] + SHARED,
    "linux": ["src/probe/vm_probe_linux.cpp", "src/probe/arena_walk.cpp",
              "include/runtimeskeptic/probe/arena_walk.hpp"] + SHARED,
}


def git(*args):
    r = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def last_commit_ts(rel_path):
    """Committer unix timestamp of the last commit touching rel_path, or None."""
    out = git("log", "-1", "--format=%ct", "--", rel_path)
    return int(out) if out.isdigit() else None


def main() -> int:
    problems = []
    checked = 0

    # A shallow clone can attribute every file to one grafted commit, which
    # would make every profile look exactly as old as every source. check_dates
    # skips its git checks in that case for the same reason; so does this.
    if (ROOT / ".git" / "shallow").exists():
        print("profiles fresh: shallow clone, git dates unreliable - skipped")
        return 0

    if not MEASURED.exists():
        print("profiles fresh: no profiles/measured/ - nothing to check")
        return 0

    for profile in sorted(MEASURED.glob("*.measured.json")):
        rel = str(profile.relative_to(ROOT))
        try:
            os_name = json.loads(profile.read_text())["platform"]["os"]
        except (json.JSONDecodeError, KeyError) as exc:
            problems.append(f"{rel}: cannot read platform.os ({exc})")
            continue

        sources = PROBE_SOURCES.get(os_name)
        if sources is None:
            problems.append(
                f"{rel}: platform.os is {os_name!r}, which this guard has no "
                f"probe-source mapping for - add it to PROBE_SOURCES so the "
                f"profile cannot drift unwatched")
            continue

        profile_ts = last_commit_ts(rel)
        if profile_ts is None:
            # Uncommitted (e.g. staged in a working tree). Nothing to compare.
            continue

        checked += 1
        for src in sources:
            if not (ROOT / src).exists():
                continue
            src_ts = last_commit_ts(src)
            if src_ts is None:
                continue
            if src_ts > profile_ts:
                problems.append(
                    f"{rel} is STALE: it was last committed before "
                    f"{src}, which is part of the probe that produces it. A "
                    f"measurement older than its instrument records what the "
                    f"old probe would have said, not the host. Regenerate it "
                    f"from a fresh measurement - the platform's CI workflow "
                    f"publishes one to refs/measurements/<sha>/<job> - and "
                    f"re-commit it.")
                break  # one stale source is enough; do not repeat per source

    print(f"profiles fresh: {checked} measured profile(s) checked against the "
          f"probe sources that produce them")
    if problems:
        print(f"\n{len(problems)} stale or unmapped profile(s):",
              file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
