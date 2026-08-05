#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail when the public release version or package names drift apart."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(rel: str, problems: list[str]) -> str:
    path = ROOT / rel
    if not path.is_file():
        problems.append(f"missing release input: {rel}")
        return ""
    return path.read_text(encoding="utf-8")


def capture(pattern: str, text: str, label: str, problems: list[str]) -> str:
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        problems.append(f"cannot read release version from {label}")
        return ""
    return match.group(1)


def main() -> int:
    problems: list[str] = []
    cmake = read("CMakeLists.txt", problems)
    header = read("include/runtimeskeptic/version.hpp", problems)
    linux = read("dist/build-linux-release.sh", problems)
    macos = read("dist/build-macos-release.sh", problems)
    quickstart = read("dist/QUICKSTART.md", problems)
    workflow = read(".github/workflows/ci.yml", problems)

    versions = {
        "CMakeLists.txt": capture(
            r"project\s*\(RuntimeSkeptic\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
            cmake, "CMakeLists.txt", problems),
        "version.hpp": capture(
            r'kToolVersion\s*=\s*"runtimeskeptic/([0-9]+\.[0-9]+\.[0-9]+)"',
            header, "version.hpp", problems),
        "build-linux-release.sh": capture(
            r"^V=([0-9]+\.[0-9]+\.[0-9]+)$", linux,
            "build-linux-release.sh", problems),
        "build-macos-release.sh": capture(
            r"^V=([0-9]+\.[0-9]+\.[0-9]+)$", macos,
            "build-macos-release.sh", problems),
    }
    expected = versions["CMakeLists.txt"]
    for source, actual in versions.items():
        if expected and actual and actual != expected:
            problems.append(
                f"release version drift: {source} has {actual}, expected {expected}")

    if expected:
        release_rel = f"dist/RELEASE-v{expected}.md"
        release = read(release_rel, problems)
        if release and not release.startswith(f"# RuntimeSkeptic v{expected}\n"):
            problems.append(f"{release_rel} has the wrong release heading")
        if quickstart and not quickstart.startswith(
                f"# RuntimeSkeptic v{expected} "):
            problems.append("dist/QUICKSTART.md has the wrong release heading")
        for platform in ("linux-x86_64", "macos-arm64"):
            archive = f"runtimeskeptic-v{expected}-{platform}.tar.gz"
            if workflow.count(archive) < 2:
                problems.append(
                    f"CI does not both verify and upload exact archive {archive}")
        for name, script in (("Linux", linux), ("macOS", macos)):
            if "RELEASE-v$V.md" not in script:
                problems.append(
                    f"{name} package script does not derive release notes from V")

    if problems:
        for problem in problems:
            print(f"release consistency: {problem}", file=sys.stderr)
        return 1
    print(f"release consistency: v{expected}; scripts, SDK docs and CI agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
