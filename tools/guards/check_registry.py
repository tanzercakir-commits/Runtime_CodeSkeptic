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


def section(text: str, start_marker: str, end_marker: str) -> str | None:
    start = text.find(start_marker)
    if start < 0:
        return None
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        return None
    return text[start:end]


def table_rows(text: str) -> tuple[dict[str, list[str]], set[str]]:
    rows: dict[str, list[str]] = {}
    duplicates: set[str] = set()
    for line in text.splitlines():
        if not line.lstrip().startswith("|"):
            continue
        cells = [
            cell.strip().strip(chr(96))
            for cell in line.strip().strip("|").split("|")
        ]
        if not cells or not re.fullmatch(r"RS-VM-\d+", cells[0]):
            continue
        if cells[0] in rows:
            duplicates.add(cells[0])
        rows[cells[0]] = cells
    return rows, duplicates


def compare_ids(
    label: str, documented: set[str], declared: set[str], problems: list[str]
) -> None:
    missing = sorted(declared - documented)
    extra = sorted(documented - declared)
    if missing or extra:
        problems.append(
            f"{label} ID drift: missing={missing or 'none'}, extra={extra or 'none'}"
        )


def parse_count(token: str) -> int | None:
    if token.isdigit():
        return int(token)
    words = {
        "zero": 0,
        "one": 1,
        "two": 2,
        "three": 3,
        "four": 4,
        "five": 5,
        "six": 6,
        "seven": 7,
        "eight": 8,
        "nine": 9,
        "ten": 10,
    }
    return words.get(token.lower())


def main() -> int:
    header = HEADER.read_text(encoding="utf-8")
    impl = IMPL.read_text(encoding="utf-8")
    analyzer = ANALYZER.read_text(encoding="utf-8")
    registry = REGISTRY.read_text(encoding="utf-8")

    declared = dict(re.findall(r'k([A-Za-z]+)\s*=\s*"(RS-VM-\d+)"', header))
    ids = set(declared.values())
    problems: list[str] = []

    if not ids:
        return fail(["no finding ids found in finding.hpp - has the format changed?"])

    for const, fid in declared.items():
        if f"ids::k{const}" not in impl and f'"{fid}"' not in impl:
            problems.append(
                f"{fid} (k{const}) is declared but has no definition in finding.cpp"
            )

    emitted_consts = set(re.findall(r"ids::k([A-Za-z]+)", analyzer))
    for const in sorted(emitted_consts):
        if const not in declared:
            problems.append(
                f"analyzer.cpp emits ids::k{const}, which finding.hpp does not declare"
            )
    for const, fid in declared.items():
        if const not in emitted_consts:
            problems.append(
                f"{fid} (ids::k{const}) is declared but analyzer.cpp never emits it"
            )

    stated = re.search(r"The (\d+) registered `RS-VM-\*` finding IDs", registry)
    if not stated:
        problems.append("registry prose is missing the registered-ID count")
    elif int(stated.group(1)) != len(ids):
        problems.append(
            f"registry prose says {stated.group(1)} registered ids, "
            f"but {len(ids)} are declared"
        )

    status = re.search(r"\bAll (\d+) IDs\b", registry)
    if not status:
        problems.append("registry status is missing its 'All N IDs' count")
    elif int(status.group(1)) != len(ids):
        problems.append(
            f"registry status says All {status.group(1)} IDs, "
            f"but {len(ids)} are declared"
        )

    registry_section = section(registry, "## 2. The registry", "### 2.1")
    if registry_section is None:
        problems.append("registry table section markers are missing or out of order")
        registry_rows: dict[str, list[str]] = {}
    else:
        registry_rows, duplicates = table_rows(registry_section)
        compare_ids("registry table", set(registry_rows), ids, problems)
        if duplicates:
            problems.append(f"registry table has duplicate IDs: {sorted(duplicates)}")

    rules_section = section(registry, "### 2.1", "### 2.2")
    if rules_section is None:
        problems.append("emitting-rule table section markers are missing or out of order")
        rule_rows: dict[str, list[str]] = {}
    else:
        rule_rows, duplicates = table_rows(rules_section)
        compare_ids("emitting-rule table", set(rule_rows), ids, problems)
        if duplicates:
            problems.append(
                f"emitting-rule table has duplicate IDs: {sorted(duplicates)}"
            )

    emitted_ids = {declared[c] for c in emitted_consts if c in declared}
    for fid in sorted(emitted_ids):
        row = registry_rows.get(fid)
        if row and any("not emitted" in cell.lower() for cell in row[4:6]):
            problems.append(
                f"registry table describes emitted {fid} as 'not emitted'"
            )
        rule_row = rule_rows.get(fid)
        if rule_row:
            rule_cell = rule_row[1].strip().lower() if len(rule_row) > 1 else ""
            if (not rule_cell or rule_cell in {"none", "*none*"} or
                    "not emitted" in rule_cell):
                problems.append(
                    f"emitting-rule table has no rule for emitted {fid}"
                )

    for match in re.finditer(r"\*\*`(RS-VM-\d+)` is unreachable", registry):
        fid = match.group(1)
        const = next((c for c, value in declared.items() if value == fid), None)
        if const and const in emitted_consts:
            problems.append(
                f"docs/findings/registry.md calls {fid} unreachable, but "
                f"src/vm/analyzer.cpp emits ids::k{const}"
            )

    campaign = section(
        registry,
        "## Added after the July 2026 real-world campaign",
        "### Two ids whose meaning changed",
    )
    if campaign is None:
        problems.append("real-world campaign section markers are missing or out of order")
    else:
        campaign_rows, duplicates = table_rows(campaign)
        count_match = re.search(r"\bThese\s+([A-Za-z]+|\d+)\s+IDs\b", campaign)
        stated_count = parse_count(count_match.group(1)) if count_match else None
        if stated_count is None:
            problems.append("campaign prose is missing a parseable 'These N IDs' count")
        elif stated_count != len(campaign_rows):
            problems.append(
                f"campaign prose says {stated_count} IDs, "
                f"but its table has {len(campaign_rows)}"
            )
        if duplicates:
            problems.append(f"campaign table has duplicate IDs: {sorted(duplicates)}")

    print(
        f"registry: {len(ids)} ids declared, "
        f"{len(emitted_ids)} emitted by rules, docs aligned"
    )
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
