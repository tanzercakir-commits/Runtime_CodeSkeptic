#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail-closed structural guard for the Phase 4 runtime boundary.

Conformance tests prove outcomes on real hosts. This guard protects properties
that a normal test can accidentally stop exercising: one native call per
wrapper, captured/restored native error state, an allocation-free recorder, a
bounded static buffer, link-free compile-disable aliases, and replay code that
cannot issue the operating-system calls it claims only to model.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def stripped(text: str) -> str:
    """Remove comments and literals while preserving tokens and line shape."""
    out = []
    i = 0
    state = "code"
    quote = ""
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if c == "/" and n == "/":
                out.extend("  "); i += 2; state = "line"; continue
            if c == "/" and n == "*":
                out.extend("  "); i += 2; state = "block"; continue
            if c in ('"', "'"):
                quote = c; out.append(" "); i += 1; state = "string"; continue
            out.append(c); i += 1; continue
        if state == "line":
            if c == "\n": out.append("\n"); state = "code"
            else: out.append(" ")
            i += 1; continue
        if state == "block":
            if c == "*" and n == "/":
                out.extend("  "); i += 2; state = "code"; continue
            out.append("\n" if c == "\n" else " "); i += 1; continue
        if c == "\\" and n:
            out.extend("  "); i += 2; continue
        if c == quote:
            out.append(" "); i += 1; state = "code"; continue
        out.append("\n" if c == "\n" else " "); i += 1
    return "".join(out)


def body(code: str, function: str):
    match = re.search(r"\b" + re.escape(function) + r"\s*\([^;]*?\)\s*(?:noexcept\s*)?\{", code, re.S)
    if not match:
        return None
    start = code.find("{", match.start())
    depth = 0
    for index in range(start, len(code)):
        if code[index] == "{": depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0: return code[start + 1:index]
    return None


def read(rel: str, problems: list[str]) -> str:
    path = ROOT / rel
    if not path.exists():
        problems.append(f"missing runtime boundary file: {rel}")
        return ""
    return path.read_text(encoding="utf-8")


def main() -> int:
    problems = []
    common = read("include/runtimeskeptic/runtime/runtime.h", problems)
    posix_h = read("include/runtimeskeptic/runtime/runtime_posix.h", problems)
    windows_h = read("include/runtimeskeptic/runtime/runtime_windows.h", problems)
    recorder = stripped(read("src/runtime/runtime.cpp", problems))
    posix = stripped(read("src/runtime/posix.cpp", problems))
    windows = stripped(read("src/runtime/windows.cpp", problems))
    replay = stripped(read("src/runtime/trace.cpp", problems))

    required_api = (
        "rs_runtime_initialize_v1", "rs_runtime_set_enabled_v1",
        "rs_runtime_snapshot_v1", "rs_runtime_flush_trace_v1",
        "rs_runtime_after_fork_child_v1", "rs_runtime_shutdown_v1")
    for symbol in required_api:
        if symbol not in common: problems.append(f"C ABI lost {symbol}")
    for header, aliases in ((posix_h, ("rs_mmap_v1 mmap", "rs_mprotect_v1 mprotect", "rs_munmap_v1 munmap")),
                            (windows_h, ("rs_virtual_alloc_v1 VirtualAlloc", "rs_virtual_protect_v1 VirtualProtect", "rs_virtual_free_v1 VirtualFree"))):
        if "RS_MONITOR_COMPILE_DISABLED" not in header:
            problems.append("platform header lost compile-time disable mode")
        for alias in aliases:
            if alias not in header: problems.append(f"compile-disable alias missing: {alias}")

    record = body(recorder, "record_event")
    if record is None:
        problems.append("cannot locate record_event body")
    else:
        forbidden = r"\b(?:new|malloc|calloc|realloc|free|throw)\b|std::(?:string|vector|mutex|lock_guard|filesystem|fstream|ofstream)"
        found = re.search(forbidden, record)
        if found: problems.append(f"record_event contains allocation/locking token: {found.group(0)}")
        if "g_inside_recorder" not in record:
            problems.append("record_event lost its TLS recursion guard")
        if "fetch_add" not in record or "published.store" not in record:
            problems.append("record_event lost atomic claim/publish ordering")
    if "std::array<Slot, RS_RUNTIME_EVENT_CAPACITY_MAX_V1>" not in recorder:
        problems.append("event storage is no longer a fixed-capacity static array")

    wrappers = (
        (posix, "mmap_once", "mmap", "errno = native_error"),
        (posix, "rs_mprotect_v1", "mprotect", "errno = native_error"),
        (posix, "rs_munmap_v1", "munmap", "errno = native_error"),
        (windows, "virtual_alloc_once", "VirtualAlloc", "SetLastError(native_error)"),
        (windows, "rs_virtual_protect_v1", "VirtualProtect", "SetLastError(native_error)"),
        (windows, "rs_virtual_free_v1", "VirtualFree", "SetLastError(native_error)"),
    )
    for code, function, native, restore in wrappers:
        function_body = body(code, function)
        if function_body is None:
            problems.append(f"cannot locate wrapper body: {function}")
            continue
        calls = len(re.findall(r"\b" + re.escape(native) + r"\s*\(", function_body))
        if calls != 1:
            problems.append(f"{function} must call {native} exactly once, found {calls}")
        native_at = function_body.find(native + "(")
        capture_at = function_body.find("native_error", native_at + 1)
        record_at = function_body.find("record_event", capture_at + 1)
        restore_at = function_body.find(restore, record_at + 1)
        return_at = function_body.rfind("return")
        if not (0 <= native_at < capture_at < record_at < restore_at < return_at):
            problems.append(f"{function} no longer captures, records, restores, then returns in order")

    forbidden_replay_calls = re.findall(
        r"\b(?:mmap|mprotect|munmap|VirtualAlloc|VirtualProtect|VirtualFree)\s*\(", replay)
    if forbidden_replay_calls:
        problems.append("trace replay reissues an OS call: " + forbidden_replay_calls[0].strip())
    for phrase in ("missing its header or footer", "sequence gap or reordering",
                   "digest mismatch", "incomplete runtime trace"):
        if phrase not in read("src/runtime/trace.cpp", problems):
            problems.append(f"trace reader lost fail-closed rejection: {phrase}")

    if problems:
        print(f"runtime safety: {len(problems)} violation(s)", file=sys.stderr)
        for problem in problems: print(f"  - {problem}", file=sys.stderr)
        return 1
    print("runtime safety: one-call wrappers, error restoration, bounded recorder, pure replay")
    return 0


if __name__ == "__main__":
    sys.exit(main())
