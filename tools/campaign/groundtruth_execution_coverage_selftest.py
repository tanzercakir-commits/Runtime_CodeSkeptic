#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Adversarial checks for the execution-coverage ledger grader."""
from __future__ import annotations

import copy

import contextlib
import io
import json
import tempfile
from pathlib import Path

import groundtruth_execution_coverage as coverage


BASE = {
    "schema": "runtime-skeptic.groundtruth-execution.v1",
    "profile_id": "sha256:0000000000000000000000000000000000000000000000000000000000000000",
    "case": "exact-mapping-below-minimum",
    "contract": "contracts/exact-mapping-below-minimum.json",
    "analyzer_verdict": "UNSUPPORTED",
    "outcome": "refused",
    "pairing": "held",
    "finding_ids": ["RS-VM-0002"],
}

IDENTITY = dict(BASE, case="identity-without-translation",
                contract="contracts/identity-without-translation.json",
                analyzer_verdict="CONDITIONAL", outcome="relocated",
                pairing="not asserted", finding_ids=["RS-VM-0008"])



def grade(path: Path, *, strict: bool = False) -> tuple[int, str, str]:
    stdout, stderr = io.StringIO(), io.StringIO()
    argv = ["groundtruth_execution_coverage.py"]
    if strict:
        argv.append("--require-complete")
    argv.append(str(path))
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        code = coverage.main(argv)
    return code, stdout.getvalue(), stderr.getvalue()


def write(path: Path, rows: object) -> None:
    if not isinstance(rows, list):
        rows = [rows]
    text = "".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows)
    path.write_text(text, encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="rs-coverage-selftest-") as tmp:
        ledger = Path(tmp) / "ledger.jsonl"
        manifest = coverage.load_json(coverage.MANIFEST)
        oracles = coverage.load_json(coverage.ORACLES)
        assert isinstance(manifest, dict) and isinstance(oracles, dict)

        drifted = copy.deepcopy(manifest)
        bound = next(
            row for row in drifted["cases"]
            if row.get("program") == "exact_mapping")
        bound["args"][0] = "0x1"
        try:
            coverage.validate_bindings(drifted)
        except ValueError as exc:
            assert "manifest/contract drift" in str(exc)
        else:
            raise AssertionError("manifest/contract drift was accepted")
        bad_derive_field = copy.deepcopy(manifest)
        derived_case = next(
            row for row in bad_derive_field["cases"]
            if row.get("case") == "address-bound-below-minimum")
        derived_case["derive_request_field"] = "address_typo"
        try:
            coverage.validate_bindings(bad_derive_field)
        except ValueError as exc:
            assert "invalid derive_request_field" in str(exc)
        else:
            raise AssertionError("invalid derive_request_field was accepted")
        bad_derive_fact = copy.deepcopy(manifest)
        derived_case = next(
            row for row in bad_derive_fact["cases"]
            if row.get("case") == "address-bound-below-minimum")
        derived_case["derive_address_from"] = "min_map_adress"
        try:
            coverage.validate_bindings(bad_derive_fact)
        except ValueError as exc:
            assert "invalid derive_address_from" in str(exc)
        else:
            raise AssertionError("invalid derive_address_from was accepted")


        bad_derive_index = copy.deepcopy(manifest)
        derived_case = next(
            row for row in bad_derive_index["cases"]
            if row.get("case") == "address-bound-below-minimum")
        derived_case["derive_arg_index"] = 99
        try:
            coverage.validate_bindings(bad_derive_index)
        except ValueError as exc:
            assert "invalid derive_arg_index" in str(exc)
        else:
            raise AssertionError("invalid derive_arg_index was accepted")

        oracle_drift = copy.deepcopy(oracles)
        oracle_drift["cases"].pop("exact-mapping-below-minimum")
        try:
            coverage.pairing.validate_registry(manifest, oracle_drift)
        except ValueError as exc:
            assert "oracle/manifest case drift" in str(exc)
        else:
            raise AssertionError("pairing oracle/manifest drift was accepted")

        conditional_hold = copy.deepcopy(oracles)
        conditional_hold["cases"]["strong-reservation-alignment"][
            "held_when"]["CONDITIONAL"] = ["misaligned"]
        try:
            coverage.pairing.validate_registry(manifest, conditional_hold)
        except ValueError as exc:
            assert "invalid held_when verdicts" in str(exc)
        else:
            raise AssertionError("CONDITIONAL was allowed to become held")

        write(ledger, [BASE, IDENTITY])
        code, out, _ = grade(ledger)
        assert code == 0 and "RS-VM-0002  witness:" in out
        code, out, _ = grade(ledger, strict=True)
        assert code == 1 and "strict execution coverage" in out
        skipped = dict(BASE, outcome="skipped", pairing="skipped",
                       finding_ids=[])
        write(ledger, [skipped, IDENTITY])
        code, out, _ = grade(ledger)
        assert code == 0 and "RS-VM-0002  witness:" not in out
        try:
            coverage.validate_exclusion_tests(
                {"RS-VM-0099": "prose-only exclusion"}, set())
        except ValueError as exc:
            assert "lack synthetic/unit coverage" in str(exc)
        else:
            raise AssertionError("untested not-checkable rule was accepted")



        wrong_contract = dict(
            BASE, contract="contracts/identity-without-translation.json")
        write(ledger, [wrong_contract, IDENTITY])
        code, _, err = grade(ledger)
        assert code == 1 and "contract does not match manifest" in err

        wrong_outcome = dict(BASE, outcome="satisfied")
        write(ledger, [wrong_outcome, IDENTITY])
        code, _, err = grade(ledger)
        assert code == 1 and "pairing drift" in err

        invalid_outcome = dict(BASE, outcome="bananas")
        write(ledger, [invalid_outcome, IDENTITY])
        code, _, err = grade(ledger)
        assert code == 1 and "invalid outcome" in err

        invalid_profile = dict(BASE, profile_id="sha256:not-a-digest")
        write(ledger, [invalid_profile, IDENTITY])
        code, _, err = grade(ledger)
        assert code == 1 and "invalid profile_id" in err

        invalid_verdict = dict(BASE, analyzer_verdict="MAYBE")
        write(ledger, [invalid_verdict, IDENTITY])
        code, _, err = grade(ledger)
        assert code == 1 and "invalid analyzer_verdict" in err

        conditional_held = dict(IDENTITY, pairing="held")
        write(ledger, [BASE, conditional_held])
        code, _, err = grade(ledger)
        assert code == 1 and "pairing drift" in err

        contradiction = dict(BASE, pairing="CONTRADICTED")
        write(ledger, [contradiction, IDENTITY])
        code, _, err = grade(ledger)
        assert code == 1 and "failing pairing" in err

        ledger.write_text("not json\n", encoding="utf-8")
        code, _, err = grade(ledger)
        assert code == 1 and "invalid JSON" in err

    print("groundtruth execution coverage: fail-closed selftest passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
