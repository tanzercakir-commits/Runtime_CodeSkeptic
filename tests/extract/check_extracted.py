#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Asserts that a bundle rs-extract produced still says what it must.

The unit tests cover the recognizers. This covers the DOCUMENT: that the thing
handed to a consumer carries its own limits. A bundle whose requirements look
authoritative, with nothing saying the scanner is a text matcher, is worse than
no bundle - it invites exactly the confidence the evidence model spends the
rest of its effort refusing.
"""
import json
import sys

REQUIRED_ADDRESS = 0x1307200000   # the shadPS4 issue #4157 site in the fixture


def main(path: str) -> int:
    doc = json.load(open(path))
    problems = []

    if doc.get("schema") != "runtime-skeptic.application-requirements-bundle.v1":
        problems.append(f"unexpected schema: {doc.get('schema')}")
    if not doc.get("x_scan_notes"):
        problems.append("the bundle states no limits of its own scan")

    reqs = doc.get("requirements", [])
    if not reqs:
        problems.append("the scan produced no requirements at all")

    for r in reqs:
        name = r.get("name", "<unnamed>")
        if r.get("assumption_evidence") != "statically_inferred":
            problems.append(
                f"{name}: evidence is {r.get('assumption_evidence')!r}; a text "
                f"match may never claim more than statically_inferred")
        if not r.get("extraction_limitations"):
            problems.append(f"{name}: states no extraction limitation")

    addresses = {int(r["request"]["address"], 16)
                 for r in reqs
                 if r.get("request", {}).get("address")}
    if REQUIRED_ADDRESS not in addresses:
        problems.append(
            f"the fixture's {REQUIRED_ADDRESS:#x} site was not recovered; the "
            f"scanner found {[hex(a) for a in sorted(addresses)]}")

    if problems:
        print("extracted bundle is not acceptable:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    print(f"extracted bundle ok: {len(reqs)} requirement(s), all "
          f"statically_inferred, all stating their limits")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "-"))
