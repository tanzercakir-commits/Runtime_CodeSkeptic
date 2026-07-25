#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Derives the key an expectation is recorded against, from a host profile.

An expectation like "this contract should come out UNSUPPORTED" is only
meaningful about a particular kind of host. The July 2026 campaign recorded 26
of them against an x86-64 Linux profile and stored them unqualified; running
the same contracts against macOS then marked twelve rows as disagreements when
most were a different host correctly giving a different answer.

The key is deliberately coarse. It names the things that change the ANSWER -
operating system, the architecture the process actually runs as, and whether a
translation layer is in the way - and nothing that merely changes the machine.
Two x86-64 Linux boxes with different page sizes share a key, and a contract
whose verdict differs between them is telling you something the key should not
have hidden.

Translation mode is always appended, including when it is `unknown`. The Linux
probe does not detect translation in v0.1 and says so, which is why the
reference key reads `linux/x86_64+unknown`: an expectation recorded there was
recorded without knowing whether a translator was underneath it. Prettier keys
were available. This one does not let that fact go quiet.
"""
import json
import sys


def host_key(profile: dict) -> str:
    platform = profile.get("platform", {})
    os_name = platform.get("os", "?")
    arch = platform.get("process_arch") or platform.get("host_arch", "?")
    translation = platform.get("translation_mode", "unknown")
    return f"{os_name}/{arch}+{translation}"


def main(argv) -> int:
    if len(argv) != 2:
        print("usage: host_key.py PROFILE.json", file=sys.stderr)
        return 64
    try:
        with open(argv[1]) as handle:
            print(host_key(json.load(handle)))
    except (OSError, ValueError) as exc:
        print(f"host_key.py: {exc}", file=sys.stderr)
        return 65
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
