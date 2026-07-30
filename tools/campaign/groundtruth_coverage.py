#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Which analyzer rules has an execution ever checked?

    tools/campaign/groundtruth_coverage.py PROFILE.json [PROFILE.json ...]

Runs every ground-truth contract against every profile given and reports which
RS-VM-* findings actually fire. A rule that never fires has never had a
prediction compared against reality, whatever the case count says.

Written because the first attempt at this number was wrong in the flattering
direction: it grepped the manifest for rule ids mentioned in prose, found the
ones a commit message claimed, and reported them as covered. The commit claimed
RS-VM-0012 among them. RS-VM-0012 does not fire for that contract at all - the
contract sets `commit_is_checked_call: true`, which is the POSIX idiom, and the
rule correctly returns early. The case and the contract were consistent; the
coverage claim was not. Counting what the tool actually emits is the only
version of this number worth printing.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RS_CHECK = ROOT / "build" / "bin" / "rs-check"
CONTRACTS = sorted((ROOT / "tests" / "groundtruth" / "contracts").glob("*.json"))

# Rules a ground-truth case cannot check, with the reason. Kept here rather than
# left as an unexplained gap, because "not yet written" and "not checkable this
# way" are different facts and only one of them is a backlog item.
#
# RS-VM-0004 was briefly listed here and should not have been. It has two
# branches. One compares the address against an alignment the program itself
# declares - request-internal, and its own output says "not consulted: the
# request is internally inconsistent", so nothing can be executed for it. The
# other compares the address against the host's MEASURED allocation
# granularity, which is a real host question: 0x800 is not a multiple of 4096,
# and the kernel refuses it with EINVAL. The rule as a whole is checked. A
# per-branch coverage number would be more honest still, and this tool does not
# produce one.
NOT_EXECUTABLE = {
    "RS-VM-0012": "claims failures MOVE from a checked call to an unchecked "
                  "access under memory pressure; provoking that means "
                  "exhausting the runner",
    # RS-VM-0013 was listed here with the reason "needs a program that stores a
    # truncated pointer and then dereferences it". That was DISPROVEN by
    # tests/groundtruth/cases/pointer_truncation.c: no dereference is needed.
    # The case proves the mapping valid through the full pointer, then shows
    # the address does not survive 32-bit storage, and reports `lost` - the
    # sixth outcome, added for exactly this. The entry stayed here for a day
    # after the case landed, invisible because a fired rule is dropped from
    # the blocked list before printing - a stale excuse that would only have
    # spoken up on the day the case broke. Removed rather than reworded.
    "RS-VM-0014": "an internal contradiction in the requirement; no host is "
                  "consulted, so there is nothing to execute",
    "RS-VM-0015": "about the shape of the subject's retry loop, not about a "
                  "host response",
    "RS-VM-0024": "fires precisely because the constraint is not evaluable "
                  "from a profile; an execution cannot settle it either",
}


def verdict(code: int) -> str:
    return {0: "SUPPORTED", 1: "UNSUPPORTED", 2: "CONDITIONAL",
            3: "UNKNOWN"}.get(code, f"error({code})")


def main(argv) -> int:
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 64
    if not RS_CHECK.exists():
        print(f"{RS_CHECK} not found; build first", file=sys.stderr)
        return 64

    header = (ROOT / "include" / "runtimeskeptic" / "vm" / "finding.hpp").read_text()
    ids = re.findall(r'k[A-Za-z]+ = "(RS-VM-\d+)"', header)

    # Which rules the UNIT TESTS exercise, against synthetic profiles. Tests
    # reference the id constants (`ids::kPointerTruncation`), so the mapping is
    # constant-name -> id from the header, then a scan for references. This is
    # the "synthetic-only" bucket T-005 asked for: a rule covered here and
    # nowhere else has been argued with, but has never met a kernel - and a
    # single percentage that mixed the two was this tool's original sin.
    const_to_id = dict(re.findall(r'(k[A-Za-z]+) = "(RS-VM-\d+)"', header))
    unit_refs: set[str] = set()
    for test in sorted((ROOT / "tests").rglob("*.cpp")):
        text = test.read_text(errors="replace")
        for name, rule_id in const_to_id.items():
            if f"ids::{name}" in text:
                unit_refs.add(rule_id)

    fired: dict[str, set[str]] = {}
    for profile in argv[1:]:
        label = Path(profile).stem
        for contract in CONTRACTS:
            proc = subprocess.run(
                [str(RS_CHECK), str(contract), "--profile", profile,
                 "--format", "json"],
                capture_output=True, text=True)
            try:
                doc = json.loads(proc.stdout)
            except json.JSONDecodeError:
                continue
            for result in doc.get("results", [doc]):
                for finding in result.get("findings", []):
                    fired.setdefault(finding["id"], set()).add(label)

    uncovered = [i for i in ids if i not in fired]
    blocked = [i for i in uncovered if i in NOT_EXECUTABLE]
    synthetic_only = [i for i in uncovered
                      if i not in NOT_EXECUTABLE and i in unit_refs]
    nothing = [i for i in uncovered
               if i not in NOT_EXECUTABLE and i not in unit_refs]

    print(f"contracts: {len(CONTRACTS)}   profiles: {len(argv) - 1}")
    print(f"rules exercised by a real-kernel execution: {len(fired)}/{len(ids)}")
    print()
    for i in sorted(fired):
        print(f"  {i}  checked on: {' '.join(sorted(fired[i]))}")

    if blocked:
        print()
        print("Not checkable by executing a request, with the reason:")
        for i in sorted(blocked):
            print(f"  {i}  {NOT_EXECUTABLE[i]}")

    if synthetic_only:
        print()
        print(f"Exercised ONLY against synthetic profiles, in unit tests "
              f"({len(synthetic_only)}):")
        print("argued with, never shown a kernel - the backlog for new "
              "ground-truth cases:")
        for i in sorted(synthetic_only):
            print(f"  {i}")

    if nothing:
        print()
        print(f"No coverage of any kind ({len(nothing)}) - no execution, no "
              f"unit test:")
        for i in sorted(nothing):
            print(f"  {i}")

    reachable = len(ids) - len(blocked)
    print()
    print(f"of the {reachable} rules an execution could check, {len(fired)} "
          f"are executed against a real kernel ({100 * len(fired) // reachable}%);")
    print(f"{len(synthetic_only)} more have only synthetic coverage, "
          f"{len(nothing)} have none at all")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
