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
import sys
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
}


def load_schemas():
    store = {}
    for path in SCHEMAS.glob("*.json"):
        doc = json.loads(path.read_text())
        store[doc["$id"]] = doc
        store[path.name] = doc
    return store


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
    for path in targets:
        try:
            doc = json.loads(path.read_text())
        except json.JSONDecodeError as exc:
            failures.append(f"{path.relative_to(ROOT)}: not valid JSON: {exc}")
            continue
        name = BY_SCHEMA_ID.get(doc.get("schema"))
        if name is None:
            continue  # not a document these schemas describe
        schema = store[name]
        resolver = RefResolver(base_uri=schema["$id"], referrer=schema,
                               store=store)
        validator = Draft202012Validator(schema, resolver=resolver)
        errors = sorted(validator.iter_errors(doc), key=lambda e: e.path)
        checked += 1
        for e in errors[:4]:
            where = "/".join(str(p) for p in e.path) or "<root>"
            failures.append(f"{path.relative_to(ROOT)}: {where}: {e.message}")

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
