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

import groundtruth_pairing as pairing

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tests" / "groundtruth" / "manifest.json"
TARGETS = ROOT / "tests" / "groundtruth" / "coverage_targets.json"
ORACLES = ROOT / "tests" / "groundtruth" / "pairing_oracles.json"
FINDINGS = ROOT / "include" / "runtimeskeptic" / "vm" / "finding.hpp"
VALID_PAIRINGS = frozenset(
    {"held", "CONTRADICTED", "CASE BROKEN", "skipped", "not asserted"})
PROFILE_ID_PATTERN = re.compile(r"sha256:[0-9a-f]{64}")


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
    parser.add_argument("--require-complete", action="store_true",
                        help="fail while any synthetic-only backlog remains")
    args = parser.parse_args(argv[1:])

    try:
        header = FINDINGS.read_text(encoding="utf-8")
        ids = re.findall(r'k[A-Za-z]+ = "(RS-VM-\d+)"', header)
        id_set = set(ids)
        manifest_doc = load_json(MANIFEST)
        target_doc = load_json(TARGETS)
        oracle_doc = load_json(ORACLES)
        if (not isinstance(manifest_doc, dict) or
                not isinstance(target_doc, dict)):
            fail("manifest and target registry must be JSON objects")
        registry = pairing.validate_registry(manifest_doc, oracle_doc)
        validate_bindings(manifest_doc)
        cases = {row["case"]: row["contract"]
                 for row in manifest_doc.get("cases", [])}
        targets = target_doc.get("targets")
        excluded = target_doc.get("not_checkable_by_current_method")
        if not isinstance(targets, dict) or not isinstance(excluded, dict):
            fail("coverage target registry has the wrong shape")
        targeted_rules: set[str] = set()
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
                if (not isinstance(outcomes, list) or not outcomes or
                        any(not isinstance(value, str) for value in outcomes)):
                    fail(f"coverage target {case!r}/{rule} has no outcomes")
                if len(outcomes) != len(set(outcomes)):
                    fail(f"coverage target {case!r}/{rule} repeats outcomes")
                outside = set(outcomes) - set(registry[case]["outcomes"])
                if outside:
                    fail(f"coverage target {case!r}/{rule} names outcomes "
                         f"outside the case oracle: {sorted(outside)}")
                targeted_rules.add(rule)
        for rule, reason in excluded.items():
            if rule not in id_set or not isinstance(reason, str) or not reason.strip():
                fail(f"invalid not-checkable entry for {rule!r}")
            if rule in targeted_rules:
                fail(f"{rule}: cannot be both executable and not-checkable")

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
                case = row["case"]
                if not isinstance(case, str) or case not in cases:
                    fail(f"{ledger}:{number}: unknown case {case!r}")
                profile_id = row["profile_id"]
                if (not isinstance(profile_id, str) or
                        PROFILE_ID_PATTERN.fullmatch(profile_id) is None):
                    fail(f"{ledger}:{number}: invalid profile_id {profile_id!r}")
                if row["contract"] != cases[case]:
                    fail(f"{ledger}:{number}: contract does not match manifest "
                         f"for {case!r}")
                verdict = row["analyzer_verdict"]
                outcome = row["outcome"]
                actual_pairing = row["pairing"]
                if verdict not in pairing.VALID_VERDICTS:
                    fail(f"{ledger}:{number}: invalid analyzer_verdict {verdict!r}")
                if outcome not in pairing.VALID_OUTCOMES:
                    fail(f"{ledger}:{number}: invalid outcome {outcome!r}")
                if actual_pairing not in VALID_PAIRINGS:
                    fail(f"{ledger}:{number}: invalid pairing {actual_pairing!r}")
                if actual_pairing in {"CONTRADICTED", "CASE BROKEN"}:
                    fail(f"{ledger}:{number}: failing pairing {actual_pairing}")
                expected_pairing = pairing.classify(
                    registry, case, verdict, outcome)
                if actual_pairing != expected_pairing:
                    fail(f"{ledger}:{number}: pairing drift for {case!r}: "
                         f"ledger={actual_pairing!r} expected={expected_pairing!r}")
                findings = row["finding_ids"]
                if (not isinstance(findings, list) or
                        len(findings) != len(set(findings)) or
                        any(value not in id_set for value in findings)):
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

        validate_exclusion_tests(excluded, unit_refs)
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
        if args.require_complete and synthetic:
            print("strict execution coverage: synthetic backlog remains")
            return 1
        return 0
    except ValueError as exc:
        print(f"groundtruth execution coverage: {exc}", file=sys.stderr)
        return 1


def as_u64(value: object, label: str) -> int:
    if isinstance(value, bool):
        fail(f"{label}: boolean is not an integer binding")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            fail(f"{label}: invalid integer {value!r}")
    fail(f"{label}: missing integer binding")


def validate_exclusion_tests(
    excluded: dict[str, object], unit_refs: set[str]
) -> None:
    untested = sorted(set(excluded) - unit_refs)
    if untested:
        fail("not-checkable rules lack synthetic/unit coverage: "
             + ", ".join(untested))


def validate_bindings(manifest_doc: dict[str, object]) -> None:
    """Keep literal program argv identical to the analyzed contract fields."""
    specs = {
        "exact_mapping": [
            (0, "request", "address", "hex"),
            (1, "request", "size", "auto"),
        ],
        "exact_mapping_alignment": [
            (0, "request", "address", "hex"),
            (1, "request", "size", "auto"),
        ],
        "exact_mapping_retry": [
            (0, "request", "address", "hex"),
            (1, "request", "size", "auto"),
            (2, "assumptions", "max_retries", "auto"),
        ],
        "hinted_mapping_at": [
            (0, "request", "address", "hex"),
            (1, "request", "size", "auto"),
        ],
        "mapping_tail": [
            (0, "request", "size", "auto"),
        ],
        "mapping_alignment": [
            (0, "request", "size", "auto"),
            (1, "request", "required_alignment", "auto"),
        ],
        "bounded_mapping_below": [
            (0, "request", "address_max", "hex"),
            (1, "request", "size", "auto"),
        ],
    }
    base = MANIFEST.parent.resolve()
    rows = manifest_doc.get("cases", [])
    if not isinstance(rows, list):
        fail("manifest cases must be a list")
    for row in rows:
        if not isinstance(row, dict):
            fail("manifest case must be an object")
        program = row.get("program")
        contract_rel = row.get("contract")
        args = row.get("args")
        if not isinstance(contract_rel, str) or not isinstance(args, list):
            fail(f"{row.get('case')}: missing contract or args")

        derive_from = row.get("derive_address_from")
        has_derive_metadata = any(
            key in row for key in ("derive_request_field", "derive_arg_index"))
        if derive_from is None and has_derive_metadata:
            fail(f"{row.get('case')}: dangling derivation metadata")
        if derive_from is not None:
            if not isinstance(derive_from, str) or not derive_from.strip():
                fail(f"{row.get('case')}: derive_address_from must be non-empty")
            if derive_from not in {"min_map_address", "max_user_address"}:
                fail(f"{row.get('case')}: invalid derive_address_from")
            request_field = row.get("derive_request_field", "address")
            arg_index = row.get("derive_arg_index", 0)
            if request_field not in {"address", "address_max"}:
                fail(f"{row.get('case')}: invalid derive_request_field")
            if (isinstance(arg_index, bool) or not isinstance(arg_index, int) or
                    arg_index < 0 or arg_index >= len(args)):
                fail(f"{row.get('case')}: invalid derive_arg_index")
            if program not in specs:
                fail(f"{row.get('case')}: derived program has no binding spec")
            if not any(
                index == arg_index and section == "request" and
                field == request_field
                for index, section, field, _ in specs[program]
            ):
                fail(f"{row.get('case')}: derivation is not bound to program argv")
        if program not in specs:
            continue
        contract_path = (base / contract_rel).resolve()
        if base != contract_path.parent and base not in contract_path.parents:
            fail(f"{row.get('case')}: contract escapes groundtruth root")
        contract = load_json(contract_path)
        if not isinstance(contract, dict):
            fail(f"{row.get('case')}: contract must be an object")
        for index, section, field, mode in specs[program]:
            if index >= len(args):
                fail(f"{row.get('case')}: argv missing binding for {section}.{field}")
            source = contract.get(section)
            if not isinstance(source, dict):
                fail(f"{row.get('case')}: contract missing {section}")
            expected = as_u64(source.get(field), f"{row.get('case')}:{section}.{field}")
            try:
                observed = int(str(args[index]), 16 if mode == "hex" else 0)
            except ValueError:
                fail(f"{row.get('case')}: argv[{index}] is not an integer")
            if observed != expected:
                fail(f"{row.get('case')}: manifest/contract drift for "
                     f"{section}.{field}: argv={observed} contract={expected}")



if __name__ == "__main__":
    sys.exit(main(sys.argv))
