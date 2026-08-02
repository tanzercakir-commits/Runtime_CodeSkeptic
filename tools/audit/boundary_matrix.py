#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# The boundary matrix: does the tool accept exactly what the published schema
# accepts? An independent re-test on 2026-08-02 found it did not - wrong types
# were read as "field absent" and passed, and some schema-valid documents were
# refused. This harness reproduces that check mechanically so a fix can be
# measured instead of guessed.
#
# For every mutation of a base document it asks two independent questions:
#   schema-valid?  -> jsonschema against schemas/*.json  (the contract)
#   tool-accepts?  -> rs-check / rs-profile exit code     (the behaviour)
# and reports the disagreements. The two that matter:
#   FALSE-GREEN    schema-INVALID but tool ACCEPTED  (a bad doc analysed)
#   OVER-STRICT    schema-VALID   but tool REJECTED  (a good doc refused)
#
# Exit 0 when there are zero disagreements, 1 otherwise, so CI can gate on it.
import json
import subprocess
import sys
import copy
import os

try:
    from jsonschema import Draft202012Validator
except ImportError:
    print("jsonschema not installed; skipping boundary matrix", file=sys.stderr)
    raise SystemExit(0)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _bin(name):
    for c in (os.path.join(REPO, "build", "bin", name),
              os.path.join(REPO, "build", "bin", "RelWithDebInfo", name),
              os.path.join(REPO, "build", "bin", name + ".exe"),
              os.path.join(REPO, "build", "bin", "RelWithDebInfo", name + ".exe")):
        if os.path.exists(c):
            return c
    return None


RS_CHECK = _bin("rs-check")
RS_PROFILE = _bin("rs-profile")

REQ_SCHEMA = json.load(open(os.path.join(REPO, "schemas/application-requirements.v1.json")))
PROF_SCHEMA = json.load(open(os.path.join(REPO, "schemas/environment-profile.v1.json")))

# A known-good, schema-valid AND tool-valid pair to mutate from.
BASE_REQ = {
    "schema": "runtime-skeptic.application-requirements.v1",
    "name": "boundary-base",
    "operation": "virtual_memory_map",
    "assumption_evidence": "specified_guarantee",
    "request": {"size": 65536},
}
BASE_PROF = {
    "schema": "runtime-skeptic.environment-profile.v1",
    "origin": "measured",
    "platform": {"os": "linux", "host_arch": "x86_64", "process_arch": "x86_64"},
    "virtual_memory": {
        "page_size": {"value": 4096, "evidence": "measured_capability"},
    },
}
FIXED_PROFILE = os.path.join(REPO, "profiles/measured/macos-14-arm64-native.measured.json")

U64_MAX = (1 << 64) - 1
OVERFLOW = 1 << 64

_req_validator = Draft202012Validator(REQ_SCHEMA)
_prof_validator = Draft202012Validator(PROF_SCHEMA)


def schema_valid(doc, which):
    v = _req_validator if which == "req" else _prof_validator
    return next(v.iter_errors(doc), None) is None


def _write(doc):
    path = os.path.join("/tmp", "bm_input.json")
    json.dump(doc, open(path, "w"))
    return path


def tool_accepts(doc, which):
    """True if the tool ANALYSED the doc (any verdict), False if it refused it
    as invalid input. Exit 65 = input/schema-invalid; 64 = usage. Anything else
    (0/1/2/3 verdict, or 0 valid for verify) means the doc was accepted."""
    path = _write(doc)
    if which == "req":
        cmd = [RS_CHECK, path, "--profile", FIXED_PROFILE, "--quiet"]
    else:
        cmd = [RS_PROFILE, "verify", path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode not in (64, 65), r.returncode


def set_path(doc, path, value):
    """path like ('request','size'); a trailing '+DELETE' sentinel deletes."""
    d = copy.deepcopy(doc)
    cur = d
    for k in path[:-1]:
        cur = cur.setdefault(k, {})
    if value is _DELETE:
        cur.pop(path[-1], None)
    else:
        cur[path[-1]] = value
    return d


class _Del:
    pass


_DELETE = _Del()

# Mutation batteries keyed by the field's intended JSON type. Each entry is
# (label, value). jsonschema decides which are valid against the real schema;
# this harness never hard-codes the expected answer.
INT_BATTERY = [
    ("string-int", "16384"), ("float", 1.5), ("bool", True),
    ("negative", -1), ("zero", 0), ("overflow", OVERFLOW),
    ("u64max", U64_MAX), ("object", {}), ("array", []), ("null", None),
]
STR_BATTERY = [
    ("number", 123), ("bool", True), ("object", {}), ("array", []), ("null", None),
]
BOOL_BATTERY = [
    ("string", "true"), ("number", 1), ("object", {}), ("null", None),
]
ENUM_BADS = [("bad-string", "frobnicate"), ("number", 5), ("bool", True)]
ADDR_BATTERY = [
    ("hex", "0x1000"), ("int", 4096), ("bad-hex", "nothex"),
    ("negative", -1), ("bool", True), ("object", {}), ("overflow", OVERFLOW),
]

# (path, battery) for requirement fields, driven by the schema's declared types.
REQ_FIELDS = [
    (("request", "size"), INT_BATTERY),
    (("request", "required_page_size"), INT_BATTERY),
    (("request", "required_alignment"), INT_BATTERY),
    (("request", "file_length"), INT_BATTERY),
    (("request", "file_offset"), INT_BATTERY),
    (("request", "max_displacement_bytes"), INT_BATTERY),
    (("request", "address"), ADDR_BATTERY),
    (("request", "address_min"), ADDR_BATTERY),
    (("request", "address_max"), ADDR_BATTERY),
    (("request", "exact_address_required"), BOOL_BATTERY),
    (("request", "file_backed"), BOOL_BATTERY),
    (("request", "simultaneous_write_execute"), BOOL_BATTERY),
    (("request", "required_page_size_relation"), ENUM_BADS + [("valid", "at_least")]),
    (("request", "eof_access_extent"), ENUM_BADS + [("valid", "whole_page_past_end")]),
    (("name",), STR_BATTERY + [("valid", "ok")]),
    (("component",), STR_BATTERY + [("valid", "ok")]),
    (("operation",), ENUM_BADS + [("unknown", "unknown"), ("valid", "virtual_memory_reserve")]),
    (("assumption_evidence",), ENUM_BADS + [("unknown", "unknown")]),
    (("assumptions", "max_retries"), INT_BATTERY),
    (("assumptions", "pointer_storage_width_bits"), INT_BATTERY),
    (("assumptions", "retries_on_failure"), BOOL_BATTERY),
]

# Profile fields.
FACT = {"value": 4096, "evidence": "measured_capability"}
FACT_BATTERY = [
    ("string", "x"), ("number", 5), ("array", []),
    ("no-evidence", {"value": 4096}),
    ("no-value", {"evidence": "measured_capability"}),
    ("bad-evidence", {"value": 4096, "evidence": "nope"}),
    ("extra-key", {"value": 4096, "evidence": "measured_capability", "valeu": 1}),
    ("source-number", {"value": 4096, "evidence": "measured_capability", "source": 5}),
    ("value-string", {"value": "notanumber", "evidence": "measured_capability"}),
    ("value-bool", {"value": True, "evidence": "measured_capability"}),
    ("value-negative", {"value": -1, "evidence": "measured_capability"}),
    ("value-zero", {"value": 0, "evidence": "measured_capability"}),
    ("value-overflow", {"value": OVERFLOW, "evidence": "measured_capability"}),
    ("value-null-unknown", {"value": None, "evidence": "unknown"}),
    ("valid", FACT),
]
BASE_RANGE = {"start": "0x1000", "end": "0x2000", "evidence": "measured_capability"}


def rng(**over):
    r = dict(BASE_RANGE)
    r.update(over)
    return [r]


RANGE_CASES = [
    ("valid", rng()),
    ("evidence-unknown", rng(evidence="unknown")),
    ("evidence-bad", rng(evidence="nope")),
    ("start-badhex", rng(start="nothex")),
    ("start-number", rng(start=4096)),
    ("start-uppercase", rng(start="0x1A00")),  # < end 0x2000, valid uppercase hex
    ("missing-end", [{"start": "0x1000", "evidence": "measured_capability"}]),
    ("extra-key", rng(reigon="typo")),
    ("note-number", rng(note=5)),
    ("not-array", {"start": "0x1000"}),
]
PROF_FIELDS = [
    (("profile_name",), STR_BATTERY + [("valid", "p")]),
    (("platform", "os_version"), STR_BATTERY + [("valid", "1.0")]),
    (("platform", "kernel_version"), STR_BATTERY + [("valid", "6.0")]),
    (("platform", "pointer_width_bits"), INT_BATTERY),
    (("platform", "os"), [("number", 5), ("bool", True), ("object", {}),
                          ("unmodeled", "freebsd"), ("valid", "linux")]),
    (("platform", "process_arch"), [("number", 5), ("object", {}),
                                    ("unmodeled", "riscv64"), ("valid", "aarch64")]),
    (("platform", "translation_mode"), ENUM_BADS + [("valid", "rosetta2")]),
    (("virtual_memory", "page_size"), FACT_BATTERY),
    (("virtual_memory", "max_user_address"), FACT_BATTERY),
    (("virtual_memory", "exact_mapping"),
     [("valid-level", {"value": "SUPPORTED", "evidence": "measured_capability"}),
      ("bad-level", {"value": "MAYBE", "evidence": "measured_capability"}),
      ("string", "x")]),
    (("virtual_memory", "unavailable_ranges"), RANGE_CASES),
    (("virtual_memory", "available_ranges"), RANGE_CASES),
    (("notes",), [("string", "x"), ("number", 5), ("array-of-int", [1, 2]),
                  ("valid", ["a note"])]),
    (("probe_run",), [("string", "x"), ("number", 5), ("valid", {"run_id": "abc"})]),
    (("origin",), ENUM_BADS + [("valid", "synthetic")]),
]


# --- nested / container fields: the re-test found 26 disagreements here that
# the top-level sweep never reached. A valid failure_sink is added to the base
# so its sub-field mutations sit on an otherwise-valid document. ---
BASE_REQ["failure_sink"] = {"kind": "fatal_assert"}

SINK_LOC_CASES = [
    ("valid", {"file": "x.c", "line": 5}),
    ("line-string", {"file": "x.c", "line": "5"}),
    ("line-negative", {"file": "x.c", "line": -1}),
    ("file-number", {"file": 5, "line": 5}),
    ("symbol-number", {"file": "x.c", "symbol": 5}),
    ("null", None),
]
SRCLOC_CASES = [
    ("valid", [{"file": "a.c", "line": 1}]),
    ("item-not-object", ["notanobject"]),
    ("line-string", [{"file": "a.c", "line": "1"}]),
    ("file-number", [{"file": 5}]),
    ("not-array", {"file": "a.c"}),
]
STR_ARRAY_CASES = [
    ("valid", ["a", "b"]),
    ("item-number", ["a", 5]),
    ("not-array", "a"),
    ("null", None),
]
FACT_ENUM = lambda v: [  # noqa: E731
    ("valid", {"value": v, "evidence": "measured_capability"}),
    ("bad-value", {"value": "nope", "evidence": "measured_capability"}),
    ("value-number", {"value": 5, "evidence": "measured_capability"}),
]
REQ_FIELDS += [
    (("failure_sink", "kind"), ENUM_BADS + [("valid", "process_exit")]),
    (("failure_sink", "description"), STR_BATTERY + [("valid", "d")]),
    (("failure_sink", "location"), SINK_LOC_CASES),
    (("source_locations",), SRCLOC_CASES),
    (("required_postconditions",), STR_ARRAY_CASES),
    (("permitted_fallbacks",), [("valid", ["relocate"]), ("bad", ["nope"]),
                                ("number", [5]), ("not-array", "relocate")]),
    (("extraction_limitations",), STR_ARRAY_CASES),
    (("assumptions", "guest_host_identity_required"), BOOL_BATTERY),
    (("assumptions", "translation_layer_available"), BOOL_BATTERY),
]
PROF_FIELDS += [
    (("virtual_memory", "reserve_commit_model"), FACT_ENUM("posix_lazy")),
    (("virtual_memory", "file_map_beyond_eof"), FACT_ENUM("sigbus")),
    (("virtual_memory", "anonymous_mapping_supported"),
     [("valid", {"value": True, "evidence": "measured_capability"}),
      ("value-string", {"value": "yes", "evidence": "measured_capability"}),
      ("value-number", {"value": 1, "evidence": "measured_capability"})]),
    (("virtual_memory", "exact_mapping_failure_codes"), STR_ARRAY_CASES),
    (("virtual_memory", "protection"),
     [("valid", {"write_execute_simultaneous":
                 {"value": True, "evidence": "measured_capability"}}),
      ("subfact-string", {"write_execute_simultaneous": "x"}),
      ("subfact-no-value",
       {"write_execute_simultaneous": {"evidence": "measured_capability"}}),
      ("subfact-bad-evidence",
       {"write_execute_simultaneous": {"value": True, "evidence": "nope"}}),
      ("not-object", "rwx")]),
]


def run(which, base, fields):
    rows = []
    for path, battery in fields:
        for label, value in battery:
            doc = set_path(base, path, value)
            sv = schema_valid(doc, which)
            ta, code = tool_accepts(doc, which)
            rows.append((".".join(path), label, sv, ta, code))
    return rows


def report(which, rows):
    fg = [r for r in rows if not r[2] and r[3]]      # schema-invalid, accepted
    os_ = [r for r in rows if r[2] and not r[3]]     # schema-valid, rejected
    print(f"\n=== {which.upper()} : {len(rows)} cases ===")
    print(f"  schema-invalid ACCEPTED (false-green): {len(fg)}")
    print(f"  schema-valid   REJECTED (over-strict): {len(os_)}")
    for tag, group in (("FALSE-GREEN", fg), ("OVER-STRICT", os_)):
        for path, label, sv, ta, code in group:
            print(f"    {tag:11} {path}={label:14} schema_valid={sv} tool_exit={code}")
    return len(fg), len(os_)


# ---------------------------------------------------------------------------
# Golden verdicts. The matrix above only asks "accepted or rejected?"; these ask
# "is the ANSWER right?". The re-test found file_offset overflow coming out
# SUPPORTED where offset 0 was UNSUPPORTED - a verdict bug no accept/reject check
# can see. Each case is a document the tool ACCEPTS, with the exit it must give
# (0 SUPPORTED, 1 UNSUPPORTED, 2 CONDITIONALLY, 3 UNKNOWN, 65 refused input).
def _req(**request):
    return {"schema": "runtime-skeptic.application-requirements.v1",
            "operation": "virtual_memory_map",
            "assumption_evidence": "specified_guarantee",
            "request": request, "failure_sink": {"kind": "fatal_assert"}}


PROF_EOF = {"schema": "runtime-skeptic.environment-profile.v1",
            "origin": "measured",
            "platform": {"os": "linux", "process_arch": "x86_64"},
            "virtual_memory": {"file_map_beyond_eof":
                               {"value": "sigbus",
                                "evidence": "measured_capability"}}}
PROF_4K = {"schema": "runtime-skeptic.environment-profile.v1",
           "origin": "measured",
           "platform": {"os": "linux", "process_arch": "x86_64"},
           "virtual_memory": {"page_size":
                              {"value": 4096, "evidence": "measured_capability"}}}

GOLDEN = [
    ("eof-offset-0",
     _req(size=4096, file_backed=True, file_length=1, accesses_beyond_eof=True,
          file_offset=0), PROF_EOF, 1),
    ("eof-offset-UINT64_MAX",
     _req(size=4096, file_backed=True, file_length=1, accesses_beyond_eof=True,
          file_offset=U64_MAX), PROF_EOF, 1),
    ("address+size overflow",
     _req(address="0xffffffffffff0000", size=131072, exact_address_required=True),
     BASE_PROF, 1),
    ("required 16K page on a 4K host",
     _req(size=65536, required_page_size=16384, required_page_size_relation="equal"),
     PROF_4K, 1),
    ("size 0 refused", _req(size=0), PROF_4K, 65),
    ("string page size refused",
     _req(size=65536, required_page_size="16384"), PROF_4K, 65),
]


def run_golden():
    print("\n=== GOLDEN VERDICTS ===")
    bad = 0
    for label, req, prof, want in GOLDEN:
        rp = _write(req)
        pp = os.path.join("/tmp", "bm_golden_prof.json")
        json.dump(prof, open(pp, "w"))
        r = subprocess.run([RS_CHECK, rp, "--profile", pp, "--quiet"],
                           capture_output=True, text=True)
        ok = r.returncode == want
        if not ok:
            bad += 1
            print(f"  WRONG  {label:34} want exit {want}, got {r.returncode}")
        else:
            print(f"  ok     {label:34} exit {r.returncode}")
    print(f"  golden verdict mismatches: {bad}")
    return bad


def main():
    if RS_CHECK is None or RS_PROFILE is None:
        print("rs-check / rs-profile not built; skipping boundary matrix "
              "(build the tools to run it)", file=sys.stderr)
        return 0
    total = 0
    for which, base, fields in (("req", BASE_REQ, REQ_FIELDS),
                                ("prof", BASE_PROF, PROF_FIELDS)):
        rows = run(which, base, fields)
        fg, os_ = report(which, rows)
        total += fg + os_
    total += run_golden()
    print(f"\nTOTAL disagreements + verdict mismatches: {total}")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
