#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The finding registry, the code, and the docs must agree about the ids.

Finding ids are an interface: they appear in CI configs, suppression files and
other people's dashboards (docs/findings/registry.md section 6). Three places
describe them and nothing checked that they matched, so registry.md said "the
18 registered ids" while the header declared 25, and listed RS-VM-0018 as
unreachable months after a rule started emitting it.

Checks:
  1. every id declared in finding.hpp has a definition in finding.cpp
  2. every declared id has a row in docs/findings/registry.md
  3. the registry's own stated count matches the number of ids
  4. an id claimed unreachable in the docs is genuinely not emitted anywhere
  5. no id is emitted that was never declared
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "include" / "runtimeskeptic" / "vm" / "finding.hpp"
IMPL = ROOT / "src" / "vm" / "finding.cpp"
ANALYZER = ROOT / "src" / "vm" / "analyzer.cpp"
REGISTRY = ROOT / "docs" / "findings" / "registry.md"


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    impl = IMPL.read_text(encoding="utf-8")
    analyzer = ANALYZER.read_text(encoding="utf-8")
    registry = REGISTRY.read_text(encoding="utf-8")

    declared = dict(re.findall(r'k([A-Za-z]+)\s*=\s*"(RS-VM-\d+)"', header))
    ids = set(declared.values())
    problems = []

    if not ids:
        return fail(["no finding ids found in finding.hpp - has the format changed?"])

    for const, fid in declared.items():
        if f'ids::k{const}' not in impl and f'"{fid}"' not in impl:
            problems.append(f"{fid} (k{const}) is declared but has no definition in finding.cpp")
        if fid not in registry:
            problems.append(f"{fid} is declared in code but has no row in docs/findings/registry.md")

    # The registry's own prose count.
    stated = re.search(r"The (\d+) registered `RS-VM-\*` finding IDs", registry)
    if stated and int(stated.group(1)) != len(ids):
        problems.append(
            f"docs/findings/registry.md says '{stated.group(1)} registered ids' "
            f"but {len(ids)} are declared in finding.hpp")

    # An id the docs call unreachable must really not be emitted.
    for m in re.finditer(r"\*\*`(RS-VM-\d+)` is unreachable", registry):
        fid = m.group(1)
        const = next((c for c, f in declared.items() if f == fid), None)
        if const and f"ids::k{const}" in analyzer:
            problems.append(
                f"docs/findings/registry.md calls {fid} unreachable, but "
                f"src/vm/analyzer.cpp emits ids::k{const}")

    emitted = set(re.findall(r"ids::k([A-Za-z]+)", analyzer))
    for const in emitted:
        if const not in declared:
            problems.append(f"analyzer.cpp emits ids::k{const}, which finding.hpp does not declare")

    print(f"registry: {len(ids)} ids declared, {len(emitted)} emitted by rules")
    return fail(problems)


def fail(problems):
    if not problems:
        return 0
    print("\nregistry guard failed:", file=sys.stderr)
    for p in problems:
        print(f"  - {p}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
