#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Derive requirements from what a program ACTUALLY DID, not from reading it.

This exists to make the false-positive rate honest. ROADMAP Gate B and the
Phase 3 exit criterion both turn on it, and the obvious way to measure it is
disqualified: the campaign's contracts and the analyzer's rules have the same
author, so agreement between them is a consistency check, not a measurement.

The way out is to stop writing the contracts.

    1. run a real program that is KNOWN TO WORK on this host - known because it
       just ran, and exited 0
    2. record the mapping requests it made, with strace
    3. keep only requests that SUCCEEDED, and only those that appear in EVERY
       run, so a one-off is not mistaken for a requirement
    4. convert each mechanically into a requirement document

Now the ground truth is not an opinion. The kernel performed these operations,
on this host, minutes ago. **Any UNSUPPORTED verdict on one of them is a false
positive**, with no argument available about whether the contract was fair -
nobody wrote it.

WHAT THIS ASSERTS, AND WHAT IT REFUSES TO.

It asserts only what the syscall carried. `exact_address_required` is set if
and only if MAP_FIXED was passed; a hint without MAP_FIXED is a preference and
is recorded as one. `guest_host_identity_required` is always FALSE, because
nothing in a trace reveals whether the program cared where the mapping landed -
inventing that assumption would manufacture the very finding the analyzer is
being tested for.

Evidence class is `observed_invariant`: docs/evidence_model.md defines it as
"held across a recorded trace set", which is exactly what surviving N runs
means, and it is honest about the limit - one trace of one program on one host
is not a specification.

TWO KINDS OF CONTRACT, COUNTED SEPARATELY.

  shape    the request with no address: size, protection, flags. Invariant
           across runs, because the kernel picks the address.
  address  a MAP_FIXED request WITH the concrete address, from one run.
           Emitted because that is what the address rules judge - and NOT
           called invariant, because ASLR moves it every run.

The second kind is where the interesting answer lives: those addresses were
accepted by the kernel in a process whose layout differs from the probe's, and
whether the analyzer still calls them available is a real question about the
tool rather than about the program.

USAGE
    tools/campaign/observe_requirements.py --out DIR --runs 3 -- python3 -c 'print(1)'
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# 14068 mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7ff924600000
MMAP = re.compile(
    r"^(?:\d+\s+)?mmap\(\s*(?P<addr>NULL|0x[0-9a-f]+)\s*,\s*(?P<len>\d+)\s*,"
    r"\s*(?P<prot>[A-Z_|0-9x]+)\s*,\s*(?P<flags>[A-Z_|0-9x]+)\s*,"
    r"\s*(?P<fd>-?\d+)\s*,\s*(?P<off>0x[0-9a-f]+|\d+)\s*\)\s*=\s*"
    r"(?P<ret>0x[0-9a-f]+)\s*$")

# 14068 mprotect(0x7ff9245f0000, 8192, PROT_READ) = 0
MPROTECT = re.compile(
    r"^(?:\d+\s+)?mprotect\(\s*(?P<addr>0x[0-9a-f]+)\s*,\s*(?P<len>\d+)\s*,"
    r"\s*(?P<prot>[A-Z_|0-9x]+)\s*\)\s*=\s*0\s*$")


def parse_prot(text):
    parts = set(text.split("|"))
    return {"read": "PROT_READ" in parts,
            "write": "PROT_WRITE" in parts,
            "execute": "PROT_EXEC" in parts}


def run_once(command, keep_trace=None):
    """Runs the command under strace. Returns (exit_code, [calls])."""
    with tempfile.NamedTemporaryFile("r", suffix=".strace",
                                     delete=False) as handle:
        trace_path = handle.name
    try:
        proc = subprocess.run(
            ["strace", "-f", "-e", "trace=mmap,mprotect", "-o", trace_path,
             *command],
            capture_output=True, text=True, timeout=120)
        calls = []
        for line in Path(trace_path).read_text(errors="replace").splitlines():
            m = MMAP.match(line.strip())
            if m:
                flags = set(m.group("flags").split("|"))
                calls.append({
                    "kind": "mmap",
                    "requested_address": (None if m.group("addr") == "NULL"
                                          else int(m.group("addr"), 16)),
                    "size": int(m.group("len")),
                    "protection": parse_prot(m.group("prot")),
                    "fixed": "MAP_FIXED" in flags or
                             "MAP_FIXED_NOREPLACE" in flags,
                    "anonymous": "MAP_ANONYMOUS" in flags,
                    "file_backed": int(m.group("fd")) >= 0,
                    "file_offset": int(m.group("off"), 0),
                    "returned_address": int(m.group("ret"), 16),
                })
                continue
            m = MPROTECT.match(line.strip())
            if m:
                calls.append({
                    "kind": "mprotect",
                    "requested_address": int(m.group("addr"), 16),
                    "size": int(m.group("len")),
                    "protection": parse_prot(m.group("prot")),
                    "fixed": False, "anonymous": False,
                    "file_backed": False, "file_offset": 0,
                    "returned_address": int(m.group("addr"), 16),
                })
        if keep_trace:
            Path(keep_trace).write_text(Path(trace_path).read_text(
                errors="replace"))
        return proc.returncode, calls
    finally:
        os.unlink(trace_path)


def shape_key(call):
    """What is invariant across runs: everything except the chosen address."""
    p = call["protection"]
    return (call["kind"], call["size"], p["read"], p["write"], p["execute"],
            call["fixed"], call["anonymous"], call["file_backed"])


def write_then_execute_pairs(calls):
    """Addresses mapped writable and later mprotect'ed executable.

    This is the W^X transition every JIT performs, and it cannot be seen in a
    single call - which is precisely why it is worth extracting. The set is
    what a runtime wrapper would report; here it comes out of the trace.
    """
    writable = {}
    transitions = []
    for c in calls:
        addr, prot = c["requested_address"], c["protection"]
        if c["kind"] == "mmap" and prot["write"] and not prot["execute"]:
            writable[c["returned_address"]] = c["size"]
        elif c["kind"] == "mprotect" and prot["execute"]:
            if addr in writable:
                transitions.append({"address": addr, "size": writable[addr]})
    return transitions


def requirement(name, component, call, with_address, observed):
    r = {
        "schema": "runtime-skeptic.application-requirements.v1",
        "name": name,
        "component": component,
        "operation": "virtual_memory_map",
        "request": {
            "size": call["size"],
            # MAP_FIXED and nothing else. A hint is a preference, and calling
            # it a requirement would manufacture the finding under test.
            #
            # And only when the address is carried: a shape contract that
            # claims an exact address is required while omitting the address
            # is internally inconsistent, and rs-check rejected 16 of them on
            # the first run rather than guessing - which is the behaviour the
            # unrecognized-field work was for. Exactness belongs to the
            # address contract; the shape contract asserts size and
            # protection and says so in its name.
            "exact_address_required": bool(call["fixed"] and with_address),
            "protection": call["protection"],
            "file_backed": bool(call["file_backed"]),
        },
        "assumptions": {
            # NOT observable in a trace. Left false on purpose.
            "guest_host_identity_required": False,
            "translation_layer_available": False,
            "retries_on_failure": False,
        },
        "failure_sink": {
            "kind": "error_return",
            "description": "not observable in a trace; recorded as the "
                           "weakest sink so severity is never inflated",
        },
        "assumption_evidence": "observed_invariant",
        "extraction_limitations": [
            "Derived mechanically from an strace of a program that ran to "
            "completion on this host. It asserts only what the syscall "
            "carried: no intent, no error handling, no guest/host identity "
            "requirement, and no failure sink.",
        ],
        "x_observed": observed,
    }
    if with_address and call["requested_address"] is not None:
        r["request"]["address"] = hex(call["requested_address"])
    if call["file_backed"]:
        r["request"]["file_offset"] = call["file_offset"]
    if call["protection"]["write"] and call["protection"]["execute"]:
        r["request"]["simultaneous_write_execute"] = True
    return r


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--label", default="")
    ap.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    command = [c for c in args.command if c != "--"]
    if not command:
        print("nothing to run", file=sys.stderr)
        return 64
    label = args.label or Path(command[0]).name

    outdir = Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)

    per_run, exit_codes = [], []
    for i in range(args.runs):
        rc, calls = run_once(
            command, keep_trace=outdir / f"{label}.run{i}.strace" if i == 0
            else None)
        exit_codes.append(rc)
        per_run.append(calls)

    # A program that did not run cannot serve as ground truth for "this works
    # here". Refusing is the whole point: the alternative is measuring the
    # false-positive rate against software that was already broken.
    if any(rc != 0 for rc in exit_codes):
        print(f"{label}: exited {exit_codes}; not usable as ground truth",
              file=sys.stderr)
        return 70

    # Shapes present in EVERY run. A one-off is not a requirement.
    common = set(shape_key(c) for c in per_run[0])
    for calls in per_run[1:]:
        common &= set(shape_key(c) for c in calls)

    shape_reqs, address_reqs = [], []
    seen = set()
    for call in per_run[0]:
        key = shape_key(call)
        if key not in common or key in seen:
            continue
        seen.add(key)
        kind = "mprotect" if call["kind"] == "mprotect" else "mmap"
        relocated = (call["requested_address"] is not None and
                     call["requested_address"] != call["returned_address"])
        observed = (f"{kind} succeeded on this host in {args.runs}/{args.runs} "
                    f"runs, returning "
                    f"{hex(call['returned_address'])}"
                    + (" (RELOCATED from the requested address)"
                       if relocated else ""))
        shape_reqs.append(requirement(
            f"{label}: {kind} {call['size']} bytes "
            f"{'RWX' if all(call['protection'].values()) else ''}".strip(),
            label, call, with_address=False, observed=observed))
        if call["fixed"] and call["requested_address"] is not None:
            address_reqs.append(requirement(
                f"{label}: MAP_FIXED at {hex(call['requested_address'])}, "
                f"{call['size']} bytes",
                label, call, with_address=True, observed=observed))

    transitions = write_then_execute_pairs(per_run[0])

    def bundle(reqs, kind):
        return {
            "schema": "runtime-skeptic.application-requirements-bundle.v1",
            "producer": {
                "tool": "observe_requirements.py",
                "version": "1",
                "rule": f"strace {kind}; {args.runs} runs; successes only",
            },
            "requirements": reqs,
        }

    (outdir / f"{label}.shape.json").write_text(
        json.dumps(bundle(shape_reqs, "shape"), indent=2) + "\n")
    if address_reqs:
        (outdir / f"{label}.address.json").write_text(
            json.dumps(bundle(address_reqs, "address"), indent=2) + "\n")

    (outdir / f"{label}.observations.json").write_text(json.dumps({
        "label": label,
        "command": command,
        "runs": args.runs,
        "exit_codes": exit_codes,
        "calls_first_run": len(per_run[0]),
        "shapes_in_every_run": len(common),
        "shape_requirements": len(shape_reqs),
        "address_requirements": len(address_reqs),
        "write_then_execute_transitions": len(transitions),
    }, indent=2) + "\n")

    print(f"{label}: exit {exit_codes[0]}, {len(per_run[0])} calls, "
          f"{len(shape_reqs)} shape + {len(address_reqs)} address "
          f"requirement(s), {len(transitions)} W->X transition(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
