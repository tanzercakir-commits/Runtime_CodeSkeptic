#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Adversarial checks for the execution-coverage ledger grader."""
from __future__ import annotations

import contextlib
import io
import json
import tempfile
from pathlib import Path

import groundtruth_execution_coverage as coverage


BASE = {
    "schema": "runtime-skeptic.groundtruth-execution.v1",
    "profile_id": "sha256:selftest",
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



def grade(path: Path) -> tuple[int, str, str]:
    stdout, stderr = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        code = coverage.main(["groundtruth_execution_coverage.py", str(path)])
    return code, stdout.getvalue(), stderr.getvalue()


def write(path: Path, rows: object) -> None:
    if not isinstance(rows, list):
        rows = [rows]
    text = "".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows)
    path.write_text(text, encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="rs-coverage-selftest-") as tmp:
        ledger = Path(tmp) / "ledger.jsonl"
        write(ledger, [BASE, IDENTITY])
        code, out, _ = grade(ledger)
        assert code == 0 and "RS-VM-0002  witness:" in out

        wrong_outcome = dict(BASE, outcome="satisfied")
        write(ledger, [wrong_outcome, IDENTITY])
        code, out, _ = grade(ledger)
        assert code == 0 and "RS-VM-0002  witness:" not in out

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
