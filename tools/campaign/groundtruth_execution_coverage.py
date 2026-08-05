#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Grade analyzer branches only when a real case outcome supplies an oracle.

Unlike the legacy finding counter, this tool never treats "rs-check emitted an
id" as execution coverage.  It consumes JSONL written by groundtruth/run.sh,
requires the case outcome named for that rule, and fails closed on malformed
or contradictory evidence.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tests" / "groundtruth" / "manifest.json"
TARGETS = ROOT / "tests" / "groundtruth" / "coverage_targets.json"
FINDINGS = ROOT / "include" / "runtimeskeptic" / "vm" / "finding.hpp"


def fail(message: str) -> None:
    raise ValueError(message)


def load_json(path: Path) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"{path}: {exc}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ledgers", nargs="+", type=Path)
    args = parser.parse_args(argv[1:])

    try:
        header = FINDINGS.read_text(encoding="utf-8")
        ids = re.findall(r'k[A-Za-z]+ = "(RS-VM-\d+)"', header)
        id_set = set(ids)
        manifest_doc = load_json(MANIFEST)
        target_doc = load_json(TARGETS)
        if not isinstance(manifest_doc, dict) or not isinstance(target_doc, dict):
            fail("manifest and target registry must be JSON objects")
        cases = {row["case"] for row in manifest_doc.get("cases", [])}
        targets = target_doc.get("targets")
        excluded = target_doc.get("not_checkable_by_current_method")
        if not isinstance(targets, dict) or not isinstance(excluded, dict):
            fail("coverage target registry has the wrong shape")
        for case, entries in targets.items():
            if case not in cases:
                fail(f"coverage target names unknown case {case!r}")
            if not isinstance(entries, list) or not entries:
                fail(f"coverage target {case!r} has no rule oracles")
            for entry in entries:
                rule = entry.get("rule") if isinstance(entry, dict) else None
                outcomes = entry.get("outcomes") if isinstance(entry, dict) else None
                if rule not in id_set:
                    fail(f"coverage target {case!r} names unknown rule {rule!r}")
                if not isinstance(outcomes, list) or not outcomes:
                    fail(f"coverage target {case!r}/{rule} has no outcomes")
        for rule, reason in excluded.items():
            if rule not in id_set or not isinstance(reason, str) or not reason.strip():
                fail(f"invalid not-checkable entry for {rule!r}")

        observed: dict[str, set[str]] = {}
        row_count = 0
        for ledger in args.ledgers:
            try:
                raw_lines = ledger.read_text(encoding="utf-8").splitlines()
            except OSError as exc:
                fail(f"{ledger}: {exc}")
            if not raw_lines:
                fail(f"{ledger}: empty execution ledger")
            for number, raw in enumerate(raw_lines, 1):
                try:
                    row = json.loads(raw)
                except json.JSONDecodeError as exc:
                    fail(f"{ledger}:{number}: invalid JSON: {exc}")
                required = {"schema", "profile_id", "case", "contract",
                            "analyzer_verdict", "outcome", "pairing",
                            "finding_ids"}
                if not isinstance(row, dict) or not required <= row.keys():
                    fail(f"{ledger}:{number}: incomplete execution row")
                if row["schema"] != "runtime-skeptic.groundtruth-execution.v1":
                    fail(f"{ledger}:{number}: unknown schema {row['schema']!r}")
                if row["case"] not in cases:
                    fail(f"{ledger}:{number}: unknown case {row['case']!r}")
                if row["pairing"] in {"CONTRADICTED", "CASE BROKEN"}:
                    fail(f"{ledger}:{number}: failing pairing {row['pairing']}")
                findings = row["finding_ids"]
                if not isinstance(findings, list) or any(x not in id_set for x in findings):
                    fail(f"{ledger}:{number}: invalid finding_ids")
                row_count += 1
                for target in targets.get(row["case"], []):
                    rule = target["rule"]
                    if rule in findings and row["outcome"] in target["outcomes"]:
                        witness = f"{row['case']}@{row['profile_id']}"
                        observed.setdefault(rule, set()).add(witness)

        const_to_id = dict(re.findall(
            r'(k[A-Za-z]+) = "(RS-VM-\d+)"', header))
        unit_refs: set[str] = set()
        for test in sorted((ROOT / "tests").rglob("*.cpp")):
            source = test.read_text(encoding="utf-8", errors="replace")
            for name, rule in const_to_id.items():
                if f"ids::{name}" in source:
                    unit_refs.add(rule)

        reasoned = {rule: reason for rule, reason in excluded.items()
                    if rule not in observed}
        synthetic = [rule for rule in ids
                     if rule not in observed and rule not in reasoned
                     and rule in unit_refs]
        missing = [rule for rule in ids
                   if rule not in observed and rule not in reasoned
                   and rule not in unit_refs]

        print(f"execution ledger rows: {row_count}")
        print(f"rule branches confirmed by a real outcome: {len(observed)}/{len(ids)}")
        for rule in sorted(observed):
            print(f"  {rule}  witness: {' '.join(sorted(observed[rule]))}")
        print()
        print("Not checkable by the current execution method:")
        for rule in sorted(reasoned):
            print(f"  {rule}  {reasoned[rule]}")
        print()
        print(f"Synthetic-only backlog ({len(synthetic)}):")
        for rule in synthetic:
            print(f"  {rule}")
        if missing:
            print()
            print(f"NO COVERAGE OF ANY KIND ({len(missing)}):")
            for rule in missing:
                print(f"  {rule}")
            return 1
        return 0
    except ValueError as exc:
        print(f"groundtruth execution coverage: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
