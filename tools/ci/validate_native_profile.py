#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail-closed policy gate for native platform evidence.

`rs-profile verify` owns schema and canonical profile-id verification.  This
gate owns the CI claim: the measured profile must describe the runner that the
workflow says it used, and the resulting envelope must not overstate that
single environment as platform-family support.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Any


PROFILE_SCHEMA = "runtime-skeptic.environment-profile.v1"
EVIDENCE_SCHEMA = "runtime-skeptic.platform-validation.v1"
PROFILE_ID = re.compile(r"^sha256:[0-9a-f]{64}$")
POINTER_WIDTH = {
    "x86_64": 64,
    "aarch64": 64,
    "riscv64": 64,
    "x86": 32,
    "arm": 32,
}
RUNNER_CLASSES = ("github_hosted_vm", "self_hosted_vm", "bare_metal")


class PolicyError(ValueError):
    pass


def load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PolicyError(f"cannot read JSON profile {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise PolicyError("profile root must be an object")
    return value


def require_object(parent: dict[str, Any], key: str) -> dict[str, Any]:
    value = parent.get(key)
    if not isinstance(value, dict):
        raise PolicyError(f"{key} must be an object")
    return value


def require_string(parent: dict[str, Any], key: str, where: str) -> str:
    value = parent.get(key)
    if not isinstance(value, str) or not value:
        raise PolicyError(f"{where}.{key} must be a non-empty string")
    return value


def require_integer(value: Any, where: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise PolicyError(f"{where} must be an integer")
    return value


def measured_fact(vm: dict[str, Any], key: str) -> tuple[Any, str]:
    fact = require_object(vm, key)
    if "value" not in fact:
        raise PolicyError(f"virtual_memory.{key}.value is required")
    evidence = require_string(fact, "evidence", f"virtual_memory.{key}")
    if evidence == "unknown":
        raise PolicyError(f"virtual_memory.{key} must carry measured evidence")
    return fact["value"], evidence


def validate_profile(
    profile: dict[str, Any],
    *,
    expected_os: str,
    expected_arch: str,
    expected_translation_mode: str | None,
    compare_profile: dict[str, Any] | None = None,
) -> dict[str, Any]:
    if profile.get("schema") != PROFILE_SCHEMA:
        raise PolicyError(f"schema must be exactly {PROFILE_SCHEMA}")
    if profile.get("origin") != "measured":
        raise PolicyError("origin must be measured")

    profile_id = profile.get("profile_id")
    if not isinstance(profile_id, str) or not PROFILE_ID.fullmatch(profile_id):
        raise PolicyError("profile_id must be sha256:<64 lowercase hex>")

    platform = require_object(profile, "platform")
    for field in ("os", "host_arch", "process_arch"):
        expected = expected_os if field == "os" else expected_arch
        actual = platform.get(field)
        if actual != expected:
            raise PolicyError(
                f"platform.{field} mismatch: expected {expected!r}, got {actual!r}"
            )

    expected_width = POINTER_WIDTH.get(expected_arch)
    if expected_width is None:
        raise PolicyError(f"validator has no pointer-width policy for {expected_arch!r}")
    if platform.get("pointer_width_bits") != expected_width:
        raise PolicyError(
            "platform.pointer_width_bits mismatch: "
            f"expected {expected_width}, got {platform.get('pointer_width_bits')!r}"
        )

    translation = platform.get("translation_mode")
    if expected_translation_mode is not None and translation != expected_translation_mode:
        raise PolicyError(
            "platform.translation_mode mismatch: "
            f"expected {expected_translation_mode!r}, got {translation!r}"
        )
    if expected_os == "windows" and translation in {"wow64", "qemu_user", "other"}:
        raise PolicyError("Windows native evidence cannot come from a translated process")

    vm = require_object(profile, "virtual_memory")
    page_raw, _ = measured_fact(vm, "page_size")
    page_size = require_integer(page_raw, "virtual_memory.page_size.value")
    if page_size < 4096 or page_size > 65536 or page_size & (page_size - 1):
        raise PolicyError("page size must be a power of two in [4096, 65536]")

    gran_raw, _ = measured_fact(vm, "allocation_granularity")
    granularity = require_integer(
        gran_raw, "virtual_memory.allocation_granularity.value"
    )
    if granularity < page_size or granularity % page_size != 0:
        raise PolicyError(
            "allocation granularity must be a positive multiple of page size"
        )

    anonymous_raw, _ = measured_fact(vm, "anonymous_mapping_supported")
    if anonymous_raw is not True:
        raise PolicyError("anonymous mapping must be measured as supported")

    run = require_object(profile, "probe_run")
    require_string(run, "tool_version", "probe_run")
    require_string(run, "probe_version", "probe_run")
    require_string(run, "timestamp_utc", "probe_run")
    duration = require_integer(run.get("duration_ms"), "probe_run.duration_ms")
    if duration < 0:
        raise PolicyError("probe_run.duration_ms cannot be negative")
    warnings = run.get("warnings")
    if not isinstance(warnings, list) or any(not isinstance(v, str) for v in warnings):
        raise PolicyError("probe_run.warnings must be an array of strings")

    if compare_profile is not None:
        other_id = compare_profile.get("profile_id")
        if other_id != profile_id:
            raise PolicyError(
                "independent probes disagree: "
                f"{profile_id!r} != {other_id!r}"
            )

    return {
        "profile_id": profile_id,
        "os": expected_os,
        "architecture": expected_arch,
        "pointer_width_bits": expected_width,
        "translation_mode": translation,
        "page_size": page_size,
        "allocation_granularity": granularity,
    }


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_envelope(args: argparse.Namespace, facts: dict[str, Any]) -> None:
    claim_scope = (
        "measured_hardware_environment"
        if args.runner_class == "bare_metal"
        else "validated_hosted_environment"
    )
    envelope = {
        "schema": EVIDENCE_SCHEMA,
        "claim_scope": claim_scope,
        "claim_limit": "one named execution environment; not platform-family support",
        "source_revision": args.source_revision,
        "runner": {
            "class": args.runner_class,
            "provider": args.provider,
            "image": args.runner_image,
        },
        "profile": {
            **facts,
            "artifact_sha256": file_sha256(args.profile),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(envelope, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def valid_fixture() -> dict[str, Any]:
    return {
        "schema": PROFILE_SCHEMA,
        "origin": "measured",
        "profile_id": "sha256:" + "a" * 64,
        "platform": {
            "os": "linux",
            "host_arch": "riscv64",
            "process_arch": "riscv64",
            "pointer_width_bits": 64,
            "translation_mode": "unknown",
        },
        "virtual_memory": {
            "page_size": {"value": 4096, "evidence": "measured_capability"},
            "allocation_granularity": {
                "value": 4096,
                "evidence": "measured_capability",
            },
            "anonymous_mapping_supported": {
                "value": True,
                "evidence": "measured_capability",
            },
        },
        "probe_run": {
            "tool_version": "selftest",
            "probe_version": "selftest",
            "timestamp_utc": "2026-08-06T00:00:00Z",
            "probe_binary_hash": "sha256:" + "b" * 64,
            "duration_ms": 1,
            "warnings": [],
        },
    }


def selftest() -> int:
    cases: list[tuple[str, dict[str, Any], dict[str, Any] | None, bool]] = []
    good = valid_fixture()
    cases.append(("valid native RISC-V64", good, copy.deepcopy(good), False))

    wrong_arch = copy.deepcopy(good)
    wrong_arch["platform"]["process_arch"] = "x86_64"
    cases.append(("process architecture mismatch", wrong_arch, None, True))

    synthetic = copy.deepcopy(good)
    synthetic["origin"] = "synthetic"
    cases.append(("synthetic origin", synthetic, None, True))

    bad_granularity = copy.deepcopy(good)
    bad_granularity["virtual_memory"]["allocation_granularity"]["value"] = 5000
    cases.append(("invalid granularity", bad_granularity, None, True))

    disagreeing = copy.deepcopy(good)
    disagreeing["profile_id"] = "sha256:" + "c" * 64
    cases.append(("independent probe disagreement", good, disagreeing, True))

    failures: list[str] = []
    for name, profile, compare, should_fail in cases:
        try:
            validate_profile(
                profile,
                expected_os="linux",
                expected_arch="riscv64",
                expected_translation_mode="unknown",
                compare_profile=compare,
            )
            failed = False
        except PolicyError:
            failed = True
        if failed != should_fail:
            failures.append(name)

    print(f"native profile validator selftest: {len(cases) - len(failures)}/{len(cases)}")
    if failures:
        print("unexpected result: " + ", ".join(failures), file=sys.stderr)
        return 1
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--compare-profile", type=Path)
    parser.add_argument("--expected-os")
    parser.add_argument("--expected-arch")
    parser.add_argument("--expected-translation-mode")
    parser.add_argument("--runner-class", choices=RUNNER_CLASSES)
    parser.add_argument("--provider")
    parser.add_argument("--runner-image")
    parser.add_argument("--source-revision")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return args
    required = (
        "profile",
        "expected_os",
        "expected_arch",
        "runner_class",
        "provider",
        "runner_image",
        "source_revision",
        "output",
    )
    missing = [name for name in required if not getattr(args, name)]
    if missing:
        parser.error("required arguments missing: " + ", ".join(missing))
    return args


def main() -> int:
    args = parse_args()
    if args.selftest:
        return selftest()
    try:
        profile = load_object(args.profile)
        compare = load_object(args.compare_profile) if args.compare_profile else None
        facts = validate_profile(
            profile,
            expected_os=args.expected_os,
            expected_arch=args.expected_arch,
            expected_translation_mode=args.expected_translation_mode,
            compare_profile=compare,
        )
        write_envelope(args, facts)
    except PolicyError as exc:
        print(f"native profile policy: FAIL: {exc}", file=sys.stderr)
        return 1
    print(
        "native profile policy: PASS: "
        f"{facts['os']}/{facts['architecture']} {facts['profile_id']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
