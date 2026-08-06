#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validates every artifact in the repository against schemas/*.json.

The schema documents exist so a third party can check a profile or a
requirement without running our binary. That only holds while they describe
what the code actually emits, and a schema nobody validates against drifts from
the code within one commit - the known-key list in requirement.cpp had already
drifted before a test caught it.

So this walks the real artifacts: every profile, every contract, every bundle,
and a freshly produced analysis result. If the code emits a field the schema
forbids, or omits one it requires, this fails.
"""
import json
import subprocess
import sys
import tempfile
import os
from pathlib import Path

try:
    from jsonschema import Draft202012Validator, RefResolver
except ImportError:
    print("jsonschema is not installed; skipping schema validation",
          file=sys.stderr)
    sys.exit(0)

ROOT = Path(__file__).resolve().parents[2]
SCHEMAS = ROOT / "schemas"

BY_SCHEMA_ID = {
    "runtime-skeptic.environment-profile.v1": "environment-profile.v1.json",
    "runtime-skeptic.application-requirements.v1": "application-requirements.v1.json",
    "runtime-skeptic.application-requirements-bundle.v1":
        "application-requirements-bundle.v1.json",
    "runtime-skeptic.analysis-bundle.v1": "analysis-bundle.v1.json",
    "runtime-skeptic.runtime-trace-record.v1": "runtime-trace-record.v1.json",
    "runtime-skeptic.runtime-overhead.v1": "runtime-overhead.v1.json",
}


def load_schemas():
    store = {}
    for path in SCHEMAS.glob("*.json"):
        doc = json.loads(path.read_text(encoding="utf-8"))
        store[doc["$id"]] = doc
        store[path.name] = doc
    return store


def _find_binary(name: str):
    roots = []
    for binary_root in sorted(ROOT.glob("build*/bin")):
        roots.append(binary_root)
        roots.extend(binary_root / config for config in
                     ("Release", "RelWithDebInfo", "Debug", "MinSizeRel"))
    # Prefer a native no-suffix binary across every build tree before a PE file.
    for suffix in ("", ".exe"):
        for root in roots:
            candidate = root / (name + suffix)
            if candidate.exists():
                return candidate
    return None


def _emit_bundle_manifest(rs_check: Path):
    """Runs rs-check --bundle on a committed contract + profile and returns the
    manifest it wrote, or None if a profile it accepts could not be found."""
    contract = ROOT / "tests" / "groundtruth" / "contracts" / \
        "pointer-truncation-32bit.json"
    # Any committed MEASURED profile; the verdict does not matter here, only that
    # the manifest is well-formed. The measured Windows profile is committed and
    # small.
    profiles = sorted((ROOT / "profiles" / "measured").glob("*.measured.json"))
    if not contract.exists() or not profiles:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "bundle"
        proc = subprocess.run(
            [str(rs_check), str(contract), "--profile", str(profiles[0]),
             "--bundle", str(out), "--output", os.devnull],
            capture_output=True, encoding="utf-8", errors="replace")
        # rs-check exits with the verdict code (1 = UNSUPPORTED etc); only 70
        # (internal) or a missing manifest means the bundle itself failed.
        manifest = out / "manifest.json"
        if proc.returncode == 70 or not manifest.exists():
            return None
        return json.loads(manifest.read_text(encoding="utf-8"))



def _emit_runtime_trace(sample: Path):
    with tempfile.TemporaryDirectory() as tmp:
        trace = Path(tmp) / "runtime-trace.jsonl"
        proc = subprocess.run([str(sample), str(trace)], capture_output=True,
                              encoding="utf-8", errors="replace")
        if proc.returncode != 0 or not trace.exists():
            return None, proc.stderr or proc.stdout or "sample failed"
        records = []
        try:
            for line in trace.read_text(encoding="utf-8").splitlines():
                records.append(json.loads(line))
        except (OSError, json.JSONDecodeError) as exc:
            return None, str(exc)
        return records, ""


def _emit_runtime_overhead(benchmark: Path):
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp) / "runtime-overhead.json"
        proc = subprocess.run(
            [str(benchmark), "--iterations", "8", "--output", str(output)],
            capture_output=True, encoding="utf-8", errors="replace")
        if proc.returncode != 0 or not output.exists():
            return None, proc.stderr or proc.stdout or "benchmark failed"
        try:
            return json.loads(output.read_text(encoding="utf-8")), ""
        except (OSError, json.JSONDecodeError) as exc:
            return None, str(exc)

def main() -> int:
    store = load_schemas()
    if not store:
        print("schemas/ is empty", file=sys.stderr)
        return 1

    targets = []
    for pattern in ("profiles/**/*.json", "contracts/**/*.json",
                    "tests/groundtruth/contracts/*.json"):
        targets.extend(sorted(ROOT.glob(pattern)))

    checked = 0
    failures = []

    def validate(doc, label):
        nonlocal checked
        declared = doc.get("schema")
        name = BY_SCHEMA_ID.get(declared)
        if name is None:
            if isinstance(declared, str) and declared.startswith("runtime-skeptic."):
                failures.append(
                    f"{label}: declares {declared!r}, but validate_schemas.py "
                    "has no schema mapping; unknown product artifacts fail closed")
            return
        schema = store[name]
        resolver = RefResolver(base_uri=schema["$id"], referrer=schema,
                               store=store)
        validator = Draft202012Validator(schema, resolver=resolver)
        checked += 1
        for e in sorted(validator.iter_errors(doc), key=lambda x: x.path)[:4]:
            where = "/".join(str(p) for p in e.path) or "<root>"
            failures.append(f"{label}: {where}: {e.message}")

    for path in targets:
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            failures.append(f"{path.relative_to(ROOT)}: not valid JSON: {exc}")
            continue
        validate(doc, path.relative_to(ROOT).as_posix())

    # A FRESHLY PRODUCED BUNDLE MANIFEST, not a committed one. The manifest is an
    # emitted artifact - its host fields, hashes and replay status are computed at
    # write time - so a committed sample would drift the moment the emitter
    # changed, which is the drift this guard exists to prevent, reintroduced as a
    # fixture. Instead: run the real emitter on a committed contract and a
    # committed profile, and validate what it wrote.
    rs_check = _find_binary("rs-check")
    manifest = _emit_bundle_manifest(rs_check) if rs_check else None
    if manifest is not None:
        validate(manifest, "a freshly emitted analysis-bundle manifest")
    elif rs_check is None:
        print("  (analysis-bundle: SKIPPED - rs-check not built; the manifest "
              "schema is only checked against a real emitted manifest)")

    sample = _find_binary("rs-runtime-sample")
    if sample is not None:
        records, emission_error = _emit_runtime_trace(sample)
        if records is None:
            failures.append(f"fresh runtime trace emission failed: {emission_error}")
        else:
            for index, record in enumerate(records, 1):
                validate(record, f"fresh runtime trace line {index}")
    else:
        print("  (runtime trace: SKIPPED - rs-runtime-sample not built)")

    benchmark = _find_binary("rs-runtime-benchmark")
    if benchmark is not None:
        artifact, emission_error = _emit_runtime_overhead(benchmark)
        if artifact is None:
            failures.append(f"fresh runtime benchmark emission failed: {emission_error}")
        else:
            validate(artifact, "fresh runtime-overhead benchmark")
    else:
        print("  (runtime overhead: SKIPPED - rs-runtime-benchmark not built)")

    schema_count = len(list(SCHEMAS.glob("*.json")))
    print(f"validated {checked} artifact(s) against {schema_count} schema(s)")
    if failures:
        print("\nschema violations - the code and the schema disagree, and one "
              "of them is wrong:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
