#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep the accepted standalone product boundary executable.

The guard is intentionally about dependency edges, not one spelling. It rejects
all submodules, all vendored dependency trees, tracked prebuilt libraries or
archives, and any build/runtime control file that fetches, links, imports or
invokes CodeSkeptic. The accepted ADR is hash-pinned so deleting one of its
normative clauses cannot leave the guard green.
"""
import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ADR_REL = "docs/decisions/0001-standalone-product-boundary.md"
ADR = ROOT / ADR_REL
PIN = ROOT / "tools" / "guards" / "standalone-boundary.sha256"

DEPENDENCY_DIRS = {
    "deps", "dependencies", "external", "externals", "third_party",
    "third-party", "vendor", "vendors",
}
PREBUILT_DIRS = DEPENDENCY_DIRS | {"bin", "lib", "libs"}
PREBUILT_FILE = re.compile(
    r"(?:\.a|\.dll|\.dylib|\.exe|\.lib|\.so(?:\.\d+)*|\.tar(?:\.gz)?|"
    r"\.tgz|\.zip|\.gz)$", re.IGNORECASE)
DOC_SURFACES = {
    "README.md", "docs/integrations.md", "dist/QUICKSTART.md",
}
DOCUMENT_SUFFIXES = {".adoc", ".md", ".rst", ".txt"}
DOCUMENT_NAMES = {"LICENSE", "NOTICE"}
CONTROL_SUFFIXES = {
    ".bat", ".c", ".cc", ".cmake", ".cmd", ".cpp", ".cxx", ".h", ".hh",
    ".hpp", ".hxx", ".json", ".mk", ".ps1", ".py", ".sh", ".toml",
    ".yaml", ".yml",
}
CONTROL_NAMES = {
    "CMakeLists.txt", "CMakePresets.json", "Dockerfile", "Makefile",
    "conanfile.py", "meson.build", "vcpkg.json",
}
SKIP = {".git", "build", "node_modules"}
NAME_TEXT = r"(?<![A-Za-z0-9])code[_-]?skeptic(?![A-Za-z0-9])"
NAME = re.compile(NAME_TEXT, re.IGNORECASE)
ACTION_TEXT = (
    r"(?:FetchContent\w*|ExternalProject\w*|add_subdirectory|"
    r"target_link_libraries|find_package|git\s+clone|git\s+submodule|"
    r"download|Invoke-WebRequest|curl|wget|#\s*include|"
    r"subprocess\.(?:run|Popen|call)|Start-Process|"
    r"CreateProcess\w*|ShellExecute\w*|system\s*\(|exec\w*\s*\(|"
    r"popen\s*\(|spawn\w*\s*\(|run\s*:|command\s*:)"
)
EDGE = re.compile(
    rf"(?:{ACTION_TEXT}).{{0,240}}{NAME_TEXT}|"
    rf"{NAME_TEXT}.{{0,240}}(?:{ACTION_TEXT})",
    re.IGNORECASE | re.DOTALL)
IMPORT_EDGE = re.compile(
    rf"\b(?:from|import)\s+{NAME_TEXT}", re.IGNORECASE)
DIRECT_CLI = re.compile(
    rf"(?:^|[\n;&|])\s*(?:-\s*)?"
    rf"(?:(?:run|command)\s*:\s*)?(?:[$>]\s*)?(?:&\s*)?"
    rf"[\"']?(?:(?:\.{{1,2}}|[A-Za-z]:|[\w.-]+)"
    rf"(?:[/\\][\w.-]+)*[/\\])?"
    rf"{NAME_TEXT}(?:\.exe)?[\"']?(?:\s|$)",
    re.IGNORECASE)
PYTHON_MODULE = re.compile(
    rf"(?:^|[\n;&|])\s*(?:[$>]\s*)?"
    rf"python(?:3(?:\.\d+)?)?\s+-m\s+{NAME_TEXT}(?:\s|$)",
    re.IGNORECASE)


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        return ""


def project_files() -> list[Path]:
    """Tracked plus non-ignored untracked files; filesystem fallback for tests."""
    proc = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "--cached", "--others",
         "--exclude-standard", "-z"],
        capture_output=True, check=False)
    if proc.returncode == 0 and proc.stdout:
        return sorted({ROOT / raw.decode("utf-8")
                       for raw in proc.stdout.split(b"\0") if raw})
    return sorted(p for p in ROOT.rglob("*") if p.is_file()
                  and not (SKIP & set(p.relative_to(ROOT).parts)))


def executable_text(value: str) -> str:
    """Drop comment-only lines so historical/budget comments are not edges."""
    kept = []
    for line in value.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("//"):
            continue
        if stripped.startswith("#") and not re.match(
                r"#\s*include\b", stripped, re.IGNORECASE):
            continue
        kept.append(line)
    return "\n".join(kept)


def main() -> int:
    problems = []
    files = project_files()

    if not ADR.exists():
        problems.append(f"accepted standalone ADR is missing: {ADR_REL}")
    if not PIN.exists():
        problems.append("standalone ADR hash pin is missing")
    if ADR.exists() and PIN.exists():
        pin_fields = read(PIN).strip().split()
        actual = hashlib.sha256(
            read(ADR).replace("\r\n", "\n").encode("utf-8")).hexdigest()
        if len(pin_fields) != 2 or pin_fields[1] != ADR_REL:
            problems.append("standalone ADR hash pin has an invalid format/path")
        elif pin_fields[0] != actual:
            problems.append(
                "standalone ADR changed without an explicit pin update: "
                f"expected {pin_fields[0]}, got {actual}")

    gitmodules = ROOT / ".gitmodules"
    if gitmodules.exists():
        live = executable_text(read(gitmodules)).strip()
        if live:
            problems.append(
                ".gitmodules is non-empty; the standalone repository permits "
                "no source submodule, including renamed forks")

    for path in files:
        try:
            rel = path.relative_to(ROOT)
        except ValueError:
            continue
        parts = {part.lower() for part in rel.parts}
        is_document = (path.suffix.lower() in DOCUMENT_SUFFIXES
                       or path.name in DOCUMENT_NAMES)
        if DEPENDENCY_DIRS & parts:
            problems.append(
                f"{rel.as_posix()}: vendored dependency content is forbidden, "
                "including renamed source or archives")
        if not is_document and NAME.search(path.name):
            problems.append(
                f"{rel.as_posix()}: CodeSkeptic-named content is forbidden "
                "outside documentation; consume only producer-neutral artifacts")
        in_binary_dir = bool(PREBUILT_DIRS & parts)
        extensionless_binary = "bin" in parts and path.suffix == ""
        if in_binary_dir and (
                PREBUILT_FILE.search(path.name) or extensionless_binary):
            problems.append(
                f"{rel.as_posix()}: tracked prebuilt library/archive is "
                "forbidden in dependency or binary directories")

    exempt = {
        ROOT / "tools" / "guards" / "check_standalone_boundary.py",
        ROOT / "tools" / "guards" / "selftest.py",
        ROOT / "tools" / "guards" / "check_non_goals.py",
    }
    for path in files:
        if path in exempt or not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        is_doc_surface = rel.as_posix() in DOC_SURFACES
        if is_doc_surface:
            if (DIRECT_CLI.search(read(path))
                    or PYTHON_MODULE.search(read(path))):
                problems.append(
                    f"{rel.as_posix()}: copy-pastable CodeSkeptic invocation "
                    "violates the standalone documentation boundary")
            continue
        is_document = (path.suffix.lower() in DOCUMENT_SUFFIXES
                       or path.name in DOCUMENT_NAMES)
        if ("docs" in {part.lower() for part in rel.parts}
                and is_document):
            continue
        if (path.name not in CONTROL_NAMES
                and path.suffix.lower() not in CONTROL_SUFFIXES):
            continue
        body = executable_text(read(path))
        if NAME.search(body) and (
                EDGE.search(body) or IMPORT_EDGE.search(body)
                or DIRECT_CLI.search(body) or PYTHON_MODULE.search(body)):
            problems.append(
                f"{rel.as_posix()}: build/runtime control fetches, links, imports "
                "or invokes CodeSkeptic")

    if problems:
        print("standalone boundary guard failed:", file=sys.stderr)
        for problem in sorted(set(problems)):
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print("standalone boundary: pinned ADR; no submodule, vendoring, dependency "
          "binary or CodeSkeptic edge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
