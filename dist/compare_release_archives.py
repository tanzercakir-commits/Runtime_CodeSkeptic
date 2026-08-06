#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explain which tar members make two release archives non-reproducible."""
from __future__ import annotations

import argparse
import hashlib
import tarfile
from pathlib import Path
from typing import Any

CHUNK_BYTES = 1024 * 1024
MAX_ARCHIVE_BYTES = 256 * 1024 * 1024
MAX_MEMBERS = 10_000
MAX_TOTAL_MEMBER_BYTES = 512 * 1024 * 1024
MAX_TEXT_FIELD_CHARS = 4096
MAX_PAX_FIELDS = 64
MAX_DIFFERENCES = 100
MAX_DIFFERENCE_CHARS = 512


class DifferenceReport:
    def __init__(self) -> None:
        self.items: list[str] = []
        self.truncated = False

    def add(self, message: str) -> None:
        if len(message) > MAX_DIFFERENCE_CHARS:
            message = message[: MAX_DIFFERENCE_CHARS - 3] + "..."
        if len(self.items) < MAX_DIFFERENCES:
            self.items.append(message)
        else:
            self.truncated = True


def checked_text(label: str, value: str) -> str:
    if len(value) > MAX_TEXT_FIELD_CHARS:
        raise ValueError(
            f"{label} exceeds {MAX_TEXT_FIELD_CHARS}-character diagnostic limit"
        )
    return value


def validate_archive(path: Path) -> None:
    size = path.stat().st_size
    if size > MAX_ARCHIVE_BYTES:
        raise ValueError(
            f"{path}: compressed archive exceeds {MAX_ARCHIVE_BYTES}-byte limit"
        )


def archives_are_identical(first: Path, second: Path) -> bool:
    validate_archive(first)
    validate_archive(second)
    if first.stat().st_size != second.stat().st_size:
        return False
    with first.open("rb") as left, second.open("rb") as right:
        while True:
            left_chunk = left.read(CHUNK_BYTES)
            right_chunk = right.read(CHUNK_BYTES)
            if left_chunk != right_chunk:
                return False
            if not left_chunk:
                return True


def path_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK_BYTES):
            digest.update(chunk)
    return digest.hexdigest()


def member_record(archive: tarfile.TarFile, member: tarfile.TarInfo) -> dict[str, Any]:
    text_fields = {
        "name": member.name,
        "uname": member.uname,
        "gname": member.gname,
        "linkname": member.linkname,
    }
    for label, value in text_fields.items():
        checked_text(f"{member.name!r} {label}", value)
    if len(member.pax_headers) > MAX_PAX_FIELDS:
        raise ValueError(
            f"{member.name!r}: exceeds {MAX_PAX_FIELDS} pax-header field limit"
        )
    pax_headers = []
    for key, value in sorted(member.pax_headers.items()):
        pax_headers.append((
            checked_text(f"{member.name!r} pax key", key),
            checked_text(f"{member.name!r} pax value", value),
        ))

    record: dict[str, Any] = {
        "type": bytes(member.type).hex(),
        "mode": member.mode,
        "uid": member.uid,
        "gid": member.gid,
        "uname": member.uname,
        "gname": member.gname,
        "size": member.size,
        "mtime": member.mtime,
        "linkname": member.linkname,
        "devmajor": member.devmajor,
        "devminor": member.devminor,
        "pax_headers": pax_headers,
    }
    if member.isfile():
        stream = archive.extractfile(member)
        if stream is None:
            raise ValueError(f"regular member has no readable payload: {member.name}")
        with stream:
            digest = hashlib.sha256()
            while chunk := stream.read(CHUNK_BYTES):
                digest.update(chunk)
            record["content_sha256"] = digest.hexdigest()
    return record


def inventory(path: Path) -> tuple[list[str], dict[str, dict[str, Any]]]:
    validate_archive(path)
    order: list[str] = []
    records: dict[str, dict[str, Any]] = {}
    logical_bytes = 0
    with tarfile.open(path, mode="r|*") as archive:
        for member in archive:
            if len(order) >= MAX_MEMBERS:
                raise ValueError(f"{path}: exceeds {MAX_MEMBERS}-member limit")
            checked_text("archive member name", member.name)
            if member.name in records:
                raise ValueError(f"{path}: duplicate archive member: {member.name}")
            if member.size < 0:
                raise ValueError(f"{path}: negative member size: {member.name}")
            logical_bytes += member.size
            if logical_bytes > MAX_TOTAL_MEMBER_BYTES:
                raise ValueError(
                    f"{path}: members exceed {MAX_TOTAL_MEMBER_BYTES}-byte limit"
                )
            order.append(member.name)
            records[member.name] = member_record(archive, member)
    return order, records


def compare(first: Path, second: Path) -> DifferenceReport:
    report = DifferenceReport()
    identical = archives_are_identical(first, second)

    # Byte identity is not permission to bypass structural limits. Validate one
    # complete inventory before returning success; if bytes differ, validate
    # both sides before reporting their member-level disagreement.
    first_order, first_records = inventory(first)
    if identical:
        return report
    second_order, second_records = inventory(second)
    first_names = set(first_records)
    second_names = set(second_records)

    for name in sorted(first_names - second_names):
        report.add(f"{name}: missing from second archive")
    for name in sorted(second_names - first_names):
        report.add(f"{name}: missing from first archive")
    if first_order != second_order:
        report.add("member traversal order differs")

    for name in sorted(first_names & second_names):
        left = first_records[name]
        right = second_records[name]
        for field in sorted(set(left) | set(right)):
            if left.get(field) != right.get(field):
                report.add(
                    f"{name}: {field}: {left.get(field)!r} != {right.get(field)!r}"
                )

    if not report.items:
        report.add("tar members are identical; only gzip/container bytes differ")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    args = parser.parse_args()

    try:
        report = compare(args.first, args.second)
    except (OSError, tarfile.TarError, ValueError) as error:
        print(f"archive comparison failed closed: {error}")
        return 2

    if not report.items:
        print(f"archives are byte-identical: sha256 {path_sha256(args.first)}")
        return 0

    qualifier = "at least " if report.truncated else ""
    count = len(report.items) + (1 if report.truncated else 0)
    print(f"archive mismatch: {qualifier}{count} member/container difference(s)")
    for difference in report.items:
        print(f"  - {difference}")
    if report.truncated:
        print(f"  - additional differences omitted after {MAX_DIFFERENCES}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
