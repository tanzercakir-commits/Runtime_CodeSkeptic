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
import shlex
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


# ---------------------------------------------------------------------------
# macOS: dtrace, NOT dtruss.
#
# The T-018 feasibility measurement (refs/measurements/3af0f9f/
# dtrace-feasibility) settled two questions by running things rather than
# reading about them: SIP is DISABLED on the macos-14 runners, so dtrace works
# without ceremony - and dtruss is unusable for this campaign, because its
# generic syscall clause prints only the first three arguments. For mmap that
# is (addr, len, prot); the flags argument, the one that carries MAP_FIXED and
# MAP_ANON - the two facts this whole file exists to observe - is argument
# FOUR. A tracer that silently drops the field under test is the mingw-flags
# mistake with a different vendor.
#
# So the D script below is the instrument: entry probes save all six
# arguments thread-locally, return probes print ONE canonical line per
# completed call, in a format this file defines and therefore can parse
# without guessing. `progenyof($target)` follows children (java and node both
# spawn them).
#
# THE RETURN-REGISTER QUESTION IS MEASURED, NOT ASSUMED. On this platform the
# syscall return value reaches the probe in arg0 or arg1 depending on
# conventions this project declines to trust documentation about (dtruss
# itself prints arg0). The line carries BOTH; the parser takes whichever
# looks like a mapping address (nonzero, page-aligned) and the two counts are
# published in observations.json. If both ever qualify and disagree, the run
# refuses loudly rather than guessing.
# ---------------------------------------------------------------------------
#
# AND THE FIRST ROUND WATCHED THE WRONG DOOR. python3 produced ONE mprotect
# and ZERO mmaps across an entire run (refs/measurements/3d47a41/
# fp-campaign-macos). The inventory round explains it (refs/measurements/
# e66cd64/mach-inventory): on macOS real allocation goes through MACH TRAPS,
# which the syscall:: provider never sees.
#
#     _kernelrpc_mach_vm_allocate_trap     fired   syscall::mmap    did not
#     _kernelrpc_mach_vm_map_trap          exists
#     _kernelrpc_mach_vm_protect_trap      fired   syscall::mprotect fired once
#
# BSD mmap is the minority path on this OS. So the script watches both doors,
# and the mach clauses carry a difficulty the BSD ones do not: allocate_trap
# and map_trap take the address as an IN/OUT POINTER (arg1), so the address
# the kernel chose only exists after the call, in the caller's memory. It is
# read with copyin() at :return - and both the requested value (copied in at
# :entry) and the returned one are printed, because the difference between
# them IS relocation, the thing several rules are about.
#
# THE ARGUMENT LAYOUT IS PRINTED RAW ALONGSIDE THE PARSED LINE. Every mach
# line carries its own arg0..arg5 in an RSRAW record. If the layout assumed
# here is wrong, the next measurement says so in its own output instead of
# producing plausible numbers - the failure mode this project keeps finding.
#
# WHAT THE MACH TRAPS DO NOT CARRY, recorded as a limitation rather than
# invented: allocate_trap has NO protection argument. Its result is the
# platform default and the protection a program ends up with comes from a
# later protect_trap. So an allocate-derived requirement asserts read+write
# and says in extraction_limitations that it was not observed.
# ---------------------------------------------------------------------------
D_SCRIPT = """\
syscall::mmap:entry
/progenyof($target)/
{
    self->a = arg0; self->l = arg1; self->p = arg2;
    self->f = arg3; self->fd = (int)arg4; self->o = arg5;
    self->in_mmap = 1;
}

syscall::mmap:return
/progenyof($target) && self->in_mmap/
{
    printf("RSOBS|mmap|%#llx|%llu|%llu|%llu|%lld|%llu|%#llx|%#llx|%d\\n",
        (unsigned long long)self->a, (unsigned long long)self->l,
        (unsigned long long)self->p, (unsigned long long)self->f,
        (long long)self->fd, (unsigned long long)self->o,
        (unsigned long long)arg0, (unsigned long long)arg1, errno);
    self->in_mmap = 0;
}

syscall::mprotect:entry
/progenyof($target)/
{
    self->pa = arg0; self->pl = arg1; self->pp = arg2;
    self->in_mprotect = 1;
}

syscall::mprotect:return
/progenyof($target) && self->in_mprotect/
{
    printf("RSOBS|mprotect|%#llx|%llu|%llu|%d\\n",
        (unsigned long long)self->pa, (unsigned long long)self->pl,
        (unsigned long long)self->pp, errno);
    self->in_mprotect = 0;
}

/*
 * The mach traps. arg1 is an IN/OUT pointer to the address on both allocate
 * and map, so the requested value is copied in at :entry and the granted one
 * at :return. Raw args travel with every line: if this layout is wrong, the
 * measurement says so instead of producing plausible numbers.
 */
mach_trap::*mach_vm_allocate_trap:entry
/progenyof($target)/
{
    self->va_p = arg1;
    self->va_req = *(uint64_t *)copyin(arg1, 8);
    self->va_size = arg2;
    self->va_flags = arg3;
    self->in_va = 1;
    printf("RSRAW|allocate|%#llx|%#llx|%#llx|%#llx|%#llx|%#llx\\n",
        (unsigned long long)arg0, (unsigned long long)arg1,
        (unsigned long long)arg2, (unsigned long long)arg3,
        (unsigned long long)arg4, (unsigned long long)arg5);
}

mach_trap::*mach_vm_allocate_trap:return
/progenyof($target) && self->in_va/
{
    printf("RSOBS|vm_allocate|%#llx|%llu|%llu|%#llx|%d\\n",
        (unsigned long long)self->va_req,
        (unsigned long long)self->va_size,
        (unsigned long long)self->va_flags,
        (unsigned long long)*(uint64_t *)copyin(self->va_p, 8),
        (int)arg1);
    self->in_va = 0;
}

mach_trap::*mach_vm_map_trap:entry
/progenyof($target)/
{
    self->vm_p = arg1;
    self->vm_req = *(uint64_t *)copyin(arg1, 8);
    self->vm_size = arg2;
    self->vm_mask = arg3;
    self->vm_flags = arg4;
    self->vm_prot = arg5;
    self->in_vm = 1;
    printf("RSRAW|map|%#llx|%#llx|%#llx|%#llx|%#llx|%#llx\\n",
        (unsigned long long)arg0, (unsigned long long)arg1,
        (unsigned long long)arg2, (unsigned long long)arg3,
        (unsigned long long)arg4, (unsigned long long)arg5);
}

mach_trap::*mach_vm_map_trap:return
/progenyof($target) && self->in_vm/
{
    printf("RSOBS|vm_map|%#llx|%llu|%llu|%llu|%llu|%#llx|%d\\n",
        (unsigned long long)self->vm_req,
        (unsigned long long)self->vm_size,
        (unsigned long long)self->vm_mask,
        (unsigned long long)self->vm_flags,
        (unsigned long long)self->vm_prot,
        (unsigned long long)*(uint64_t *)copyin(self->vm_p, 8),
        (int)arg1);
    self->in_vm = 0;
}

mach_trap::*mach_vm_protect_trap:entry
/progenyof($target)/
{
    self->vp_a = arg1; self->vp_l = arg2; self->vp_new = arg4;
    self->in_vp = 1;
    printf("RSRAW|protect|%#llx|%#llx|%#llx|%#llx|%#llx|%#llx\\n",
        (unsigned long long)arg0, (unsigned long long)arg1,
        (unsigned long long)arg2, (unsigned long long)arg3,
        (unsigned long long)arg4, (unsigned long long)arg5);
}

mach_trap::*mach_vm_protect_trap:return
/progenyof($target) && self->in_vp/
{
    printf("RSOBS|vm_protect|%#llx|%llu|%llu|%d\\n",
        (unsigned long long)self->vp_a, (unsigned long long)self->vp_l,
        (unsigned long long)self->vp_new, (int)arg1);
    self->in_vp = 0;
}
"""

# <sys/mman.h> on Darwin. Numeric because the D script prints numbers.
DARWIN_PROT_READ = 0x1
DARWIN_PROT_WRITE = 0x2
DARWIN_PROT_EXEC = 0x4
DARWIN_MAP_FIXED = 0x10
DARWIN_MAP_ANON = 0x1000

# <mach/vm_statistics.h> / <mach/vm_prot.h>. The mach traps carry their own
# vocabulary: VM_FLAGS_ANYWHERE means "place it wherever", and its ABSENCE is
# what MAP_FIXED means here - the kernel will use the address in the in/out
# pointer and clobber whatever is there.
VM_FLAGS_ANYWHERE = 0x0001
VM_PROT_READ = 0x1
VM_PROT_WRITE = 0x2
VM_PROT_EXECUTE = 0x4

PAGE_MASK_4K = 0xFFF  # the loosest page size in play; alignment sanity only

RSOBS = re.compile(r"RSOBS\|(?P<rest>mmap|mprotect)\|")


def darwin_prot(bits):
    return {"read": bool(bits & DARWIN_PROT_READ),
            "write": bool(bits & DARWIN_PROT_WRITE),
            "execute": bool(bits & DARWIN_PROT_EXEC)}


def darwin_vm_prot(bits):
    """VM_PROT_*, which happen to share values with PROT_* but are a
    different vocabulary - kept separate so a divergence stays visible."""
    return {"read": bool(bits & VM_PROT_READ),
            "write": bool(bits & VM_PROT_WRITE),
            "execute": bool(bits & VM_PROT_EXECUTE)}


def plausible_address(value):
    return value > 0xFFF and (value & PAGE_MASK_4K) == 0


def parse_rsobs_lines(text, convention_counts):
    """Parse the canonical lines the D script prints. Successes only."""
    calls = []
    for line in text.splitlines():
        idx = line.find("RSOBS|")
        if idx < 0:
            continue
        parts = line[idx:].strip().split("|")
        try:
            if parts[1] == "mmap" and len(parts) == 11:
                (addr, length, prot, flags,
                 fd, off, ret0, ret1, err) = parts[2:]
                if int(err, 0) != 0:
                    continue  # the campaign's ground truth is successes only
                r0, r1 = int(ret0, 16), int(ret1, 16)
                if plausible_address(r0) and plausible_address(r1) and r0 != r1:
                    raise SystemExit(
                        f"mmap return convention ambiguous: arg0={ret0} and "
                        f"arg1={ret1} are both plausible addresses and "
                        f"disagree. Refusing to guess which register this "
                        f"kernel returns in.")
                if plausible_address(r0):
                    returned, which = r0, "arg0"
                elif plausible_address(r1):
                    returned, which = r1, "arg1"
                else:
                    continue  # a zero-page or unaligned result: not a mapping
                convention_counts[which] = convention_counts.get(which, 0) + 1
                flag_bits = int(flags, 0)
                fd_val = int(fd, 0)
                requested = int(addr, 0)
                calls.append({
                    "kind": "mmap",
                    "requested_address": requested if requested else None,
                    "size": int(length, 0),
                    "protection": darwin_prot(int(prot, 0)),
                    "fixed": bool(flag_bits & DARWIN_MAP_FIXED),
                    "anonymous": bool(flag_bits & DARWIN_MAP_ANON),
                    "file_backed": fd_val >= 0,
                    "file_offset": int(off, 0),
                    "returned_address": returned,
                })
            elif parts[1] == "mprotect" and len(parts) == 6:
                addr, length, prot, err = parts[2:]
                if int(err, 0) != 0:
                    continue
                calls.append({
                    "kind": "mprotect",
                    "requested_address": int(addr, 0),
                    "size": int(length, 0),
                    "protection": darwin_prot(int(prot, 0)),
                    "fixed": False, "anonymous": False,
                    "file_backed": False, "file_offset": 0,
                    "returned_address": int(addr, 0),
                })
            # int(x, 0) throughout, not int(x, 16): the D script prints
            # some fields with %#llx and some with %llu, and reading a DECIMAL
            # flags word as hex is exactly the silent misinterpretation the
            # RSRAW records exist to expose. It did, on the first real run:
            # 1006632961 (0x3C000001 - VM_FLAGS_ANYWHERE plus a malloc memory
            # tag) parsed as hex became 0x1006632961, whose low bit happens to
            # be set as well, so the ANYWHERE test came out RIGHT BY ACCIDENT.
            # A wrong reading that agrees with the correct one on the data at
            # hand is the worst kind; only the raw record made it visible.
            elif parts[1] == "vm_allocate" and len(parts) == 7:
                requested, size, flags, granted, kr = parts[2:]
                if int(kr, 0) != 0:
                    continue  # KERN_SUCCESS only
                req_val, granted_val = int(requested, 0), int(granted, 0)
                flag_bits = int(flags, 0)
                calls.append({
                    "kind": "mmap",
                    "requested_address": req_val or None,
                    "size": int(size, 0),
                    # NOT OBSERVED. allocate_trap has no protection argument;
                    # the platform default is read+write and the protection a
                    # program ends up with comes from a later protect_trap.
                    # Recorded as the default and declared in the bundle's
                    # extraction_limitations rather than invented as a fact.
                    "protection": {"read": True, "write": True,
                                   "execute": False},
                    "protection_observed": False,
                    "fixed": not (flag_bits & VM_FLAGS_ANYWHERE),
                    "anonymous": True,
                    "file_backed": False,
                    "file_offset": 0,
                    "returned_address": granted_val,
                })
            elif parts[1] == "vm_map" and len(parts) == 9:
                requested, size, _mask, flags, prot, granted, kr = parts[2:]
                if int(kr, 0) != 0:
                    continue
                flag_bits = int(flags, 0)
                calls.append({
                    "kind": "mmap",
                    "requested_address": int(requested, 0) or None,
                    "size": int(size, 0),
                    "protection": darwin_vm_prot(int(prot, 0)),
                    "protection_observed": True,
                    "fixed": not (flag_bits & VM_FLAGS_ANYWHERE),
                    "anonymous": True,
                    "file_backed": False,
                    "file_offset": 0,
                    "returned_address": int(granted, 0),
                })
            elif parts[1] == "vm_protect" and len(parts) == 6:
                addr, size, prot, kr = parts[2:]
                if int(kr, 0) != 0:
                    continue
                calls.append({
                    "kind": "mprotect",
                    "requested_address": int(addr, 0),
                    "size": int(size, 0),
                    "protection": darwin_vm_prot(int(prot, 0)),
                    "protection_observed": True,
                    "fixed": False, "anonymous": False,
                    "file_backed": False, "file_offset": 0,
                    "returned_address": int(addr, 0),
                })
        except (ValueError, IndexError):
            continue  # a child's own stdout that happened to contain RSOBS|
    return calls


def run_once_dtrace(command, keep_trace, convention_counts):
    """One traced run under dtrace. Returns (exit_code, [calls]).

    dtrace -c splits its command on whitespace without shell quoting, and the
    workloads carry arguments with spaces (python3 -c '...'). So the command
    goes into a wrapper script and -c gets a path with no spaces in it.
    """
    with tempfile.NamedTemporaryFile("w", suffix=".d", delete=False) as h:
        h.write(D_SCRIPT)
        script = h.name
    with tempfile.NamedTemporaryFile("w", suffix=".sh", delete=False) as h:
        h.write("#!/bin/sh\nexec " +
                " ".join(shlex.quote(c) for c in command) + "\n")
        wrapper = h.name
    os.chmod(wrapper, 0o755)
    try:
        proc = subprocess.run(
            ["sudo", "dtrace", "-q", "-b", "8m", "-s", script,
             "-c", f"/bin/sh {wrapper}"],
            capture_output=True, text=True, timeout=180)
        calls = parse_rsobs_lines(proc.stdout, convention_counts)
        if keep_trace:
            Path(keep_trace).write_text(proc.stdout)
        # dtrace's own exit status is not a reliable copy of the child's, so
        # the caller verifies the workload untraced before believing any of
        # this. Here: a run that produced no calls AND a nonzero dtrace exit
        # is a broken trace, not a quiet program.
        rc = 0 if (calls or proc.returncode == 0) else proc.returncode
        return rc, calls
    finally:
        os.unlink(script)
        os.unlink(wrapper)


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
            "Derived mechanically from a syscall trace (strace on Linux, "
            "dtrace on macOS) of a program that ran to completion on this "
            "host. It asserts only what the syscall carried: no intent, no "
            "error handling, no guest/host identity requirement, and no "
            "failure sink.",
        ],
        "x_observed": observed,
    }
    if with_address and call["requested_address"] is not None:
        r["request"]["address"] = hex(call["requested_address"])
    if call["file_backed"]:
        r["request"]["file_offset"] = call["file_offset"]
    if call["protection"]["write"] and call["protection"]["execute"]:
        r["request"]["simultaneous_write_execute"] = True
    # mach_vm_allocate_trap carries no protection argument at all. Saying so
    # in the document is the difference between a default and an observation,
    # and this project's whole method is that the difference is written down.
    if not call.get("protection_observed", True):
        r["extraction_limitations"].append(
            "Protection was NOT observed. This call came from "
            "_kernelrpc_mach_vm_allocate_trap, which takes no protection "
            "argument; read+write is the platform default, and the "
            "protection this mapping ends up with is set by a later "
            "mach_vm_protect_trap that this document does not model.")
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

    tracer = "dtrace" if sys.platform == "darwin" else "strace"
    convention_counts = {}

    if tracer == "dtrace":
        # dtrace's exit status is not a trustworthy copy of the child's, so
        # "this program runs to completion on this host" - the fact the whole
        # campaign rests on - is established by running it once WITHOUT the
        # tracer. A program that only exits 0 while untraced would still be
        # honest ground truth for its untraced behaviour; a program that
        # cannot exit 0 at all must not become a contract.
        verify = subprocess.run(command, capture_output=True, timeout=180)
        if verify.returncode != 0:
            print(f"{label}: exited {verify.returncode} untraced; not usable "
                  f"as ground truth", file=sys.stderr)
            return 70

    per_run, exit_codes = [], []
    for i in range(args.runs):
        keep = outdir / f"{label}.run{i}.{tracer}" if i == 0 else None
        if tracer == "dtrace":
            rc, calls = run_once_dtrace(command, keep, convention_counts)
        else:
            rc, calls = run_once(command, keep_trace=keep)
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
                "rule": f"{tracer} {kind}; {args.runs} runs; successes only",
            },
            "requirements": reqs,
        }

    (outdir / f"{label}.shape.json").write_text(
        json.dumps(bundle(shape_reqs, "shape"), indent=2) + "\n")
    if address_reqs:
        (outdir / f"{label}.address.json").write_text(
            json.dumps(bundle(address_reqs, "address"), indent=2) + "\n")

    observations = {
        "label": label,
        "command": command,
        "tracer": tracer,
        "runs": args.runs,
        "exit_codes": exit_codes,
        "calls_first_run": len(per_run[0]),
        "shapes_in_every_run": len(common),
        "shape_requirements": len(shape_reqs),
        "address_requirements": len(address_reqs),
        "write_then_execute_transitions": len(transitions),
    }
    if tracer == "dtrace":
        # Which register the return value arrived in, measured per call.
        # parse_rsobs_lines refuses outright if the two ever disagree on one
        # line; this records the tally so a drifting kernel shows up in data.
        observations["return_register_counts"] = convention_counts
    (outdir / f"{label}.observations.json").write_text(
        json.dumps(observations, indent=2) + "\n")

    print(f"{label}: exit {exit_codes[0]}, {len(per_run[0])} calls, "
          f"{len(shape_reqs)} shape + {len(address_reqs)} address "
          f"requirement(s), {len(transitions)} W->X transition(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
