#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate and apply contract-specific ground-truth pairing oracles."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

VALID_VERDICTS = frozenset(
    {"SUPPORTED", "UNSUPPORTED", "CONDITIONAL", "UNKNOWN"})
ASSERTABLE_VERDICTS = frozenset({"SUPPORTED", "UNSUPPORTED"})
VALID_OUTCOMES = frozenset({
    "satisfied",
    "refused",
    "relocated",
    "faulted",
    "skipped",
    "lost",
    "satisfied-relocated",
    "tail-addressable",
    "misaligned",
    "misaligned-einval",
    "repeated-permanent-refusal",
    "oom-killed-after-touch",
    "below-bound-unavailable",
})


def fail(message: str) -> None:
    raise ValueError(message)


def load_json(path: Path) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"{path}: {exc}")


def validate_registry(
    manifest_doc: object, oracle_doc: object
) -> dict[str, dict[str, object]]:
    if not isinstance(manifest_doc, dict):
        fail("ground-truth manifest must be a JSON object")
    if manifest_doc.get("schema") != "runtime-skeptic.groundtruth-manifest.v1":
        fail("ground-truth manifest has the wrong schema")
    rows = manifest_doc.get("cases")
    if not isinstance(rows, list) or not rows:
        fail("ground-truth manifest cases must be a non-empty list")

    manifest_cases: set[str] = set()
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("case"), str):
            fail("ground-truth manifest contains an invalid case")
        case = row["case"]
        if case in manifest_cases:
            fail(f"ground-truth manifest repeats case {case!r}")
        manifest_cases.add(case)

    if not isinstance(oracle_doc, dict):
        fail("pairing oracle registry must be a JSON object")
    if oracle_doc.get("schema") != "runtime-skeptic.groundtruth-pairing-oracles.v1":
        fail("pairing oracle registry has the wrong schema")
    registry = oracle_doc.get("cases")
    if not isinstance(registry, dict):
        fail("pairing oracle registry cases must be an object")

    oracle_cases = set(registry)
    if oracle_cases != manifest_cases:
        missing = sorted(manifest_cases - oracle_cases)
        extra = sorted(oracle_cases - manifest_cases)
        fail(f"pairing oracle/manifest case drift: missing={missing} extra={extra}")

    for case, spec in registry.items():
        if not isinstance(spec, dict) or set(spec) != {"outcomes", "held_when"}:
            fail(f"{case}: oracle must contain only outcomes and held_when")
        outcomes = spec["outcomes"]
        if (not isinstance(outcomes, list) or not outcomes or
                any(not isinstance(value, str) for value in outcomes)):
            fail(f"{case}: outcomes must be a non-empty string list")
        if len(outcomes) != len(set(outcomes)):
            fail(f"{case}: outcomes contain duplicates")
        invalid = set(outcomes) - VALID_OUTCOMES
        if invalid:
            fail(f"{case}: invalid outcomes {sorted(invalid)}")
        if "skipped" in outcomes:
            fail(f"{case}: skipped is harness-wide and must not be case-bound")

        held_when = spec["held_when"]
        if not isinstance(held_when, dict) or not held_when:
            fail(f"{case}: held_when must be a non-empty object")
        invalid_verdicts = set(held_when) - ASSERTABLE_VERDICTS
        if invalid_verdicts:
            fail(f"{case}: invalid held_when verdicts {sorted(invalid_verdicts)}")
        for verdict, accepted in held_when.items():
            if (not isinstance(accepted, list) or not accepted or
                    any(not isinstance(value, str) for value in accepted)):
                fail(f"{case}/{verdict}: accepted outcomes must be non-empty")
            if len(accepted) != len(set(accepted)):
                fail(f"{case}/{verdict}: accepted outcomes contain duplicates")
            outside = set(accepted) - set(outcomes)
            if outside:
                fail(f"{case}/{verdict}: held outcomes outside domain "
                     f"{sorted(outside)}")

    return registry


def classify(
    registry: dict[str, dict[str, object]],
    case: str,
    verdict: str,
    outcome: str,
) -> str:
    if verdict not in VALID_VERDICTS:
        fail(f"{case}: invalid analyzer verdict {verdict!r}")
    if outcome == "skipped":
        return "skipped"
    if outcome not in VALID_OUTCOMES:
        fail(f"{case}: invalid outcome {outcome!r}")
    spec = registry.get(case)
    if spec is None:
        fail(f"pairing oracle names unknown case {case!r}")
    domain = spec["outcomes"]
    if outcome not in domain:
        fail(f"{case}: outcome {outcome!r} is outside its case oracle")
    held_when = spec["held_when"]
    if outcome in held_when.get(verdict, []):
        return "held"
    if verdict in {"SUPPORTED", "UNSUPPORTED"}:
        return "CONTRADICTED"
    return "not asserted"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("validate", "classify"))
    parser.add_argument("manifest", type=Path)
    parser.add_argument("oracles", type=Path)
    parser.add_argument("case", nargs="?")
    parser.add_argument("verdict", nargs="?")
    parser.add_argument("outcome", nargs="?")
    args = parser.parse_args(argv[1:])
    try:
        registry = validate_registry(
            load_json(args.manifest), load_json(args.oracles))
        if args.command == "validate":
            if any(value is not None for value in
                   (args.case, args.verdict, args.outcome)):
                fail("validate takes no case, verdict, or outcome")
            print(f"ground-truth pairing oracles: {len(registry)} cases valid")
            return 0
        if None in (args.case, args.verdict, args.outcome):
            fail("classify requires case, verdict, and outcome")
        print(classify(
            registry, args.case, args.verdict, args.outcome))
        return 0
    except ValueError as exc:
        print(f"ground-truth pairing: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
