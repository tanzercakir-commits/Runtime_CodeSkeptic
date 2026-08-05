#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Adversarial tests for release-archive reproducibility diagnostics."""
from __future__ import annotations

import gzip
import io
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

COMPARE = Path(__file__).with_name("compare_release_archives.py")


def make_archive(
    path: Path,
    members: list[tuple[str, bytes, int]],
) -> None:
    with path.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as zipped:
            with tarfile.open(fileobj=zipped, mode="w") as archive:
                for name, payload, mode in members:
                    member = tarfile.TarInfo(name)
                    member.size = len(payload)
                    member.mode = mode
                    member.uid = 0
                    member.gid = 0
                    member.uname = "root"
                    member.gname = "root"
                    member.mtime = 0
                    archive.addfile(member, io.BytesIO(payload))


def make_header_only_archive(
    path: Path,
    member: tarfile.TarInfo,
) -> None:
    with path.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as zipped:
            zipped.write(member.tobuf(format=tarfile.PAX_FORMAT))


def invoke(first: Path, second: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(COMPARE), str(first), str(second)],
        capture_output=True,
        text=True,
        check=False,
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="rs-archive-selftest-") as tmp:
        root = Path(tmp)
        first = root / "first.tar.gz"
        equal = root / "equal.tar.gz"
        content = root / "content.tar.gz"
        metadata = root / "metadata.tar.gz"
        one = [("package/file.txt", b"same\n", 0o644)]
        make_archive(first, one)
        make_archive(equal, one)
        make_archive(content, [("package/file.txt", b"different\n", 0o644)])
        make_archive(metadata, [("package/file.txt", b"same\n", 0o600)])

        result = invoke(first, equal)
        if result.returncode != 0 or "byte-identical" not in result.stdout:
            print("archive diagnostic selftest: identical archives were rejected")
            print(result.stdout + result.stderr)
            return 1

        result = invoke(first, content)
        if result.returncode != 1 or "content_sha256" not in result.stdout:
            print("archive diagnostic selftest: content drift was not attributed")
            print(result.stdout + result.stderr)
            return 1

        result = invoke(first, metadata)
        if result.returncode != 1 or ": mode:" not in result.stdout:
            print("archive diagnostic selftest: metadata drift was not attributed")
            print(result.stdout + result.stderr)
            return 1

        invalid = root / "invalid.tar.gz"
        invalid.write_bytes(b"not an archive")
        result = invoke(first, invalid)
        if result.returncode != 2 or "failed closed" not in result.stdout:
            print("archive diagnostic selftest: corrupt input did not fail closed")
            print(result.stdout + result.stderr)
            return 1

        many_first = root / "many-first.tar.gz"
        many_second = root / "many-second.tar.gz"
        make_archive(
            many_first,
            [(f"package/{index:03}.txt", b"a", 0o644) for index in range(150)],
        )
        make_archive(
            many_second,
            [(f"package/{index:03}.txt", b"b", 0o644) for index in range(150)],
        )
        result = invoke(many_first, many_second)
        if (
            result.returncode != 1
            or "additional differences omitted after 100" not in result.stdout
            or len(result.stdout.encode("utf-8")) > 64 * 1024
        ):
            print("archive diagnostic selftest: report bound was not enforced")
            print(result.stdout + result.stderr)
            return 1

        too_many = root / "too-many.tar.gz"
        make_archive(
            too_many,
            [(f"package/{index:05}.txt", b"", 0o644)
             for index in range(10_001)],
        )
        result = invoke(too_many, too_many)
        if result.returncode != 2 or "10000-member limit" not in result.stdout:
            print("archive diagnostic selftest: identical member overflow passed")
            print(result.stdout + result.stderr)
            return 1

        logical = root / "logical-overflow.tar.gz"
        huge = tarfile.TarInfo("package/huge.bin")
        huge.size = 512 * 1024 * 1024 + 1
        make_header_only_archive(logical, huge)
        result = invoke(logical, logical)
        if result.returncode != 2 or "members exceed" not in result.stdout:
            print("archive diagnostic selftest: identical logical overflow passed")
            print(result.stdout + result.stderr)
            return 1

        long_name = root / "long-name.tar.gz"
        make_archive(long_name, [("x" * 5000, b"x", 0o644)])
        result = invoke(long_name, long_name)
        if result.returncode != 2 or "diagnostic limit" not in result.stdout:
            print("archive diagnostic selftest: identical oversized name passed")
            print(result.stdout + result.stderr)
            return 1

        pax = root / "pax-overflow.tar.gz"
        pax_member = tarfile.TarInfo("package/pax.txt")
        pax_member.pax_headers = {f"RS.key{index:02}": "x" for index in range(65)}
        make_header_only_archive(pax, pax_member)
        result = invoke(pax, pax)
        if result.returncode != 2 or "pax-header field limit" not in result.stdout:
            print("archive diagnostic selftest: identical pax overflow passed")
            print(result.stdout + result.stderr)
            return 1

    print(
        "archive diagnostic selftest: drift attribution, corruption, identical "
        "input limits and bounded reports enforced"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
