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
NOT_EXECUTABLE = {
    "RS-VM-0012": "claims failures MOVE from a checked call to an unchecked "
                  "access under memory pressure; provoking that means "
                  "exhausting the runner",
    "RS-VM-0013": "needs a program that stores a truncated pointer and then "
                  "dereferences it - the fault is in the subject's code, not "
                  "in a call this harness can make",
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
    backlog = [i for i in uncovered if i not in NOT_EXECUTABLE]

    print(f"contracts: {len(CONTRACTS)}   profiles: {len(argv) - 1}")
    print(f"rules exercised by an execution: {len(fired)}/{len(ids)}")
    print()
    for i in sorted(fired):
        print(f"  {i}  checked on: {' '.join(sorted(fired[i]))}")

    if blocked:
        print()
        print("Not checkable by executing a request, with the reason:")
        for i in sorted(blocked):
            print(f"  {i}  {NOT_EXECUTABLE[i]}")

    if backlog:
        print()
        print(f"No ground-truth case yet ({len(backlog)}) - these are the "
              f"backlog, and they are")
        print("the rules whose predictions nothing has ever contradicted or "
              "confirmed:")
        for i in sorted(backlog):
            print(f"  {i}")

    reachable = len(ids) - len(blocked)
    print()
    print(f"of the {reachable} rules an execution could check, "
          f"{len(fired)} are checked "
          f"({100 * len(fired) // reachable}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
