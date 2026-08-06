#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep platform evidence native, attributable, manual where it costs money."""

from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN = Path("docs/plans/platform-expansion-v2.md")
PIN = Path("tools/guards/platform-expansion-v2.sha256")
HOSTED = Path(".github/workflows/platform-expansion.yml")
RISCV = Path(".github/workflows/riscv64-native.yml")
HARNESS = Path("tools/platform/run_native_validation.sh")
VALIDATOR = Path("tools/ci/validate_native_profile.py")


def read(path: Path, errors: list[str]) -> str:
    target = ROOT / path
    try:
        return target.read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"missing required platform-expansion file {path}: {exc}")
        return ""


def require(text: str, needle: str, where: Path, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"{where}: required contract is missing: {needle}")


def main() -> int:
    errors: list[str] = []
    plan = read(PLAN, errors)
    pin = read(PIN, errors)
    hosted = read(HOSTED, errors)
    riscv = read(RISCV, errors)
    harness = read(HARNESS, errors)
    validator = read(VALIDATOR, errors)

    if plan and pin:
        expected_line = f"{hashlib.sha256(plan.encode()).hexdigest()}  {PLAN.as_posix()}"
        if pin.strip() != expected_line:
            errors.append(
                "platform expansion plan has changed under a stale pin; "
                "review the plan deliberately before updating its hash"
            )

    if "continue-on-error" in hosted:
        errors.append(f"{HOSTED}: continue-on-error is forbidden in evidence lanes")
    for label in ("runs-on: ubuntu-24.04-arm", "runs-on: windows-11-arm"):
        require(hosted, label, HOSTED, errors)
    if hosted.count("--compare-profile") < 2:
        errors.append(f"{HOSTED}: both ARM64 lanes must compare two probe processes")
    if hosted.count("rs-profile") < 4 or hosted.count(" verify ") < 4:
        errors.append(f"{HOSTED}: both profiles in both lanes must pass rs-profile verify")
    if hosted.count("--expected-arch aarch64") < 2:
        errors.append(f"{HOSTED}: both hosted lanes must enforce aarch64 profiles")
    if hosted.count("actions/upload-artifact@v7") < 2:
        errors.append(f"{HOSTED}: both hosted lanes must upload attributable evidence")
    if hosted.count("ctest --test-dir") < 2 or hosted.count("--build-config") < 2:
        errors.append(f"{HOSTED}: both hosted lanes must run configured CTest")
    for contract in (
        "--runner-class github_hosted_vm",
        "--provider github-actions",
        "test \"$(uname -m)\" = aarch64",
        "OSArchitecture",
    ):
        require(hosted, contract, HOSTED, errors)

    require(riscv, "workflow_dispatch:", RISCV, errors)
    if re.search(r"(?m)^  (push|pull_request|schedule):", riscv):
        errors.append(f"{RISCV}: RISC-V hardware execution must remain manual-only")
    for contract in (
        "inputs.confirmation == 'RUN'",
        "environment: riscv64-hardware",
        "timeout-minutes: 30",
        "StrictHostKeyChecking=yes",
        "UserKnownHostsFile=",
        "run_native_validation.sh riscv64",
        "RS_RUNNER_CLASS=bare_metal",
        "RS_RISCV_KNOWN_HOSTS",
        "if: always()",
    ):
        require(riscv, contract, RISCV, errors)
    if "StrictHostKeyChecking=no" in riscv:
        errors.append(f"{RISCV}: SSH host verification may not be disabled")
    forbidden = (
        "qemu-system",
        "terraform apply",
        "terraform destroy",
        "pulumi up",
        "aws ec2 run-instances",
        "hcloud server create",
        "scw instance server create",
    )
    for command in forbidden:
        if command in riscv.lower():
            errors.append(f"{RISCV}: provisioning/emulation command is forbidden: {command}")

    for contract in (
        'ACTUAL_ARCH="$(uname -m)"',
        'if [ "$ACTUAL_ARCH" != "$EXPECTED_ARCH" ]',
        "-DRS_WARNINGS_AS_ERRORS=ON",
        "ctest --test-dir",
        "--build-config RelWithDebInfo",
        "--compare-profile",
        "rs-profile\" verify",
        "tests/groundtruth/selftest.sh",
        "tests/groundtruth/run.sh",
        "trap cleanup EXIT HUP INT TERM",
    ):
        require(harness, contract, HARNESS, errors)

    for contract in (
        '"riscv64": 64',
        '"origin") != "measured"',
        "independent probes disagree",
        "not platform-family support",
        '"bare_metal"',
    ):
        require(validator, contract, VALIDATOR, errors)

    linux_probe = read(Path("src/probe/vm_probe_linux.cpp"), errors)
    profile_hpp = read(Path("include/runtimeskeptic/vm/profile.hpp"), errors)
    for contract in (
        "process_architecture()",
        "__riscv_xlen",
        "Architecture::Riscv64",
        "kAarch64DefaultMapWindow",
        "default_map_window_for",
    ):
        require(linux_probe, contract, Path("src/probe/vm_probe_linux.cpp"), errors)
    require(profile_hpp, "Riscv64", Path("include/runtimeskeptic/vm/profile.hpp"), errors)

    if errors:
        print("platform expansion guard: FAIL", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("platform expansion guard: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
