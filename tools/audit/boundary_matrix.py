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
import tempfile
import shutil

try:
    from jsonschema import Draft202012Validator
except ImportError:
    # Fail closed under CI (RS_MATRIX_REQUIRE=1): a missing oracle must not let
    # the contract check pass by default. Locally it stays a convenience skip.
    if os.environ.get("RS_MATRIX_REQUIRE") == "1":
        print("jsonschema not installed but RS_MATRIX_REQUIRE=1; the boundary "
              "matrix cannot run without its oracle", file=sys.stderr)
        raise SystemExit(2)
    print("jsonschema not installed; skipping boundary matrix", file=sys.stderr)
    raise SystemExit(0)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _host_native_binary(path):
    """Reject a PE executable when this guard is running under POSIX.

    WSL can launch Windows binaries, but a Windows process cannot open the
    guard's POSIX /tmp paths. Accepting that interop path made every
    schema-valid input look over-strict instead of reporting a setup error.
    """
    try:
        magic = open(path, "rb").read(4)
    except OSError:
        return False
    is_pe = magic[:2] == b"MZ" or path.lower().endswith(".exe")
    return is_pe if os.name == "nt" else not is_pe


def _bin(name):
    # Single-config generators put binaries in <build>/bin; multi-config ones
    # (MSVC, Xcode) nest them under the configuration. Search the canonical
    # build first, then named local build trees, but accept only the current
    # host's executable format.
    roots = []
    override = os.environ.get("RS_BOUNDARY_BUILD_DIR")
    if override:
        roots.extend((override, os.path.join(override, "bin")))

    build_roots = [os.path.join(REPO, "build")]
    build_roots.extend(
        os.path.join(REPO, leaf)
        for leaf in sorted(os.listdir(REPO))
        if leaf.startswith("build") and
        os.path.isdir(os.path.join(REPO, leaf)) and
        leaf != "build"
    )
    for build_root in build_roots:
        roots.append(os.path.join(build_root, "bin"))
        for cfg in ("Release", "RelWithDebInfo", "Debug", "MinSizeRel"):
            roots.append(os.path.join(build_root, "bin", cfg))

    seen = set()
    for root in roots:
        if root in seen:
            continue
        seen.add(root)
        for leaf in (name, name + ".exe"):
            candidate = os.path.join(root, leaf)
            if os.path.isfile(candidate) and _host_native_binary(candidate):
                return candidate
    return None


RS_CHECK = _bin("rs-check")
RS_PROFILE = _bin("rs-profile")
RS_REPLAY = _bin("rs-replay")
RS_VALIDATE = _bin("rs-validate")

# CI sets RS_MATRIX_REQUIRE=1: then a missing tool or missing jsonschema is a
# hard FAILURE, not a silent skip. Locally it stays unset so a dev without
# jsonschema can still build. The guard must fail closed exactly where it is
# meant to catch regressions - CI.
REQUIRE = os.environ.get("RS_MATRIX_REQUIRE") == "1"

def _schema(name):
    return json.load(open(os.path.join(REPO, "schemas", name)))


REQ_SCHEMA = _schema("application-requirements.v1.json")
BUNDLE_SCHEMA = _schema("application-requirements-bundle.v1.json")
PROF_SCHEMA = _schema("environment-profile.v1.json")
MANIFEST_SCHEMA = _schema("analysis-bundle.v1.json")

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

# Which schema file each `which` key validates against.
SCHEMA_OF = {
    "req": "application-requirements.v1.json",
    "prof": "environment-profile.v1.json",
    "bundle": "application-requirements-bundle.v1.json",
    "manifest": "analysis-bundle.v1.json",
}

# The three schemas the matrix exercises - requirement, profile, manifest - are
# self-contained (their only $ref is local #/$defs), so jsonschema resolves them
# with no registry and no `referencing` module. That keeps this guard runnable on
# a CI box whose jsonschema predates `referencing` (the 2026-08-02 push found one
# that did). The bundle schema is the sole one with a cross-file $ref; it is not
# in any run below (a bundle item IS a requirement doc, already proven by the
# `req` sweep, and the C++ Store is proven in tests/unit/test_schema.cpp), so its
# oracle is built lazily and only if `referencing` happens to be present.
_VALIDATORS = {
    "application-requirements.v1.json": Draft202012Validator(REQ_SCHEMA),
    "environment-profile.v1.json": Draft202012Validator(PROF_SCHEMA),
    "analysis-bundle.v1.json": Draft202012Validator(MANIFEST_SCHEMA),
}
try:
    from referencing import Registry, Resource
    from referencing.jsonschema import DRAFT202012
    _registry = Registry().with_resources([
        ("application-requirements.v1.json",
         Resource.from_contents(REQ_SCHEMA, default_specification=DRAFT202012)),
        (REQ_SCHEMA["$id"],
         Resource.from_contents(REQ_SCHEMA, default_specification=DRAFT202012)),
    ])
    _VALIDATORS["application-requirements-bundle.v1.json"] = \
        Draft202012Validator(BUNDLE_SCHEMA, registry=_registry)
except ImportError:
    pass  # bundle oracle unavailable here; no run needs it


def schema_valid(doc, which):
    """The oracle: does jsonschema accept `doc` against `which`'s schema?"""
    v = _VALIDATORS[SCHEMA_OF[which]]
    return next(v.iter_errors(doc), None) is None


_TMP_DIR = tempfile.TemporaryDirectory(prefix="runtime-skeptic-boundary-")
_TMP_INPUT = os.path.join(_TMP_DIR.name, "bm_input.json")


def _write(doc):
    with open(_TMP_INPUT, "w", encoding="utf-8") as stream:
        json.dump(doc, stream)
    return _TMP_INPUT


def cpp_valid(doc, which):
    """The C++ rs::schema validator's verdict, via the rs-validate dev tool.
    Comparing this to schema_valid() for every mutation is the direct proof the
    embedded validator that gates the tools agrees with jsonschema - independent
    of any tool's hand-parser. Raises on an unexpected exit so a crash in the
    validator cannot be mistaken for 'invalid'."""
    path = _write(doc)
    r = subprocess.run([RS_VALIDATE, SCHEMA_OF[which], path],
                       capture_output=True, text=True)
    if r.returncode == 0:
        return True
    if r.returncode == 65:
        return False
    raise RuntimeError(f"rs-validate {SCHEMA_OF[which]} exit {r.returncode}: "
                       f"{r.stderr.strip()}")


def tool_accepts(doc, which):
    """Returns (accepted, exit_code, crashed).

    ACCEPTED means the tool actually analysed the document: a verdict from
    rs-check (0/1/2/3), or a clean verify (0) from rs-profile. 64 (usage) and 65
    (input/schema-invalid) are a clean refusal. 70 (internal) or a signal crash
    is neither accept nor clean-refusal - it is a defect, returned as `crashed`
    so the caller fails on it rather than, as the old check did, folding exit 70
    into 'accepted'."""
    path = _write(doc)
    if which == "req":
        cmd = [RS_CHECK, path, "--profile", FIXED_PROFILE, "--quiet"]
        accept_codes = (0, 1, 2, 3)
    elif which == "prof":
        cmd = [RS_PROFILE, "verify", path]
        accept_codes = (0,)
    else:
        raise ValueError(f"no tool wired for {which}")
    r = subprocess.run(cmd, capture_output=True, text=True)
    code = r.returncode
    crashed = code == 70 or code < 0 or code >= 128
    return (code in accept_codes), code, crashed


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


# Applied to EVERY field path on top of its typed battery, so the null-and-
# container class the 2026-08-02 re-test found (36/36 accepted) is exercised on
# each field, not only the ones someone thought to list.
CONTAINER_VALUES = [
    ("sweep-null", None), ("sweep-object", {}),
    ("sweep-array", []), ("sweep-true", True),
]


def run(which, base, fields):
    """Row = (path, label, schema_valid, cpp_valid, tool_accepts, exit, crashed)."""
    rows = []
    for path, battery in fields:
        for label, value in list(battery) + CONTAINER_VALUES:
            doc = set_path(base, path, value)
            sv = schema_valid(doc, which)
            cv = cpp_valid(doc, which)
            ta, code, crashed = tool_accepts(doc, which)
            rows.append((".".join(path), label, sv, cv, ta, code, crashed))
    return rows


def report(which, rows):
    div = [r for r in rows if r[2] != r[3]]           # jsonschema vs C++ validator
    fg = [r for r in rows if not r[2] and r[4]]       # schema-invalid, tool accepted
    os_ = [r for r in rows if r[2] and not r[4]]      # schema-valid, tool rejected
    cr = [r for r in rows if r[6]]                     # tool crashed (exit 70/signal)
    print(f"\n=== {which.upper()} : {len(rows)} cases ===")
    print(f"  validator != jsonschema (divergence):  {len(div)}")
    print(f"  schema-invalid ACCEPTED (false-green):  {len(fg)}")
    print(f"  schema-valid   REJECTED (over-strict):  {len(os_)}")
    print(f"  tool crashed on input:                  {len(cr)}")
    for tag, group in (("DIVERGENCE", div), ("FALSE-GREEN", fg),
                       ("OVER-STRICT", os_), ("CRASH", cr)):
        for path, label, sv, cv, ta, code, crashed in group:
            print(f"    {tag:11} {path}={label:16} "
                  f"jsonschema={sv} cpp={cv} tool_exit={code}")
    return len(div) + len(fg) + len(os_) + len(cr)


def run_validator_only(which, base, fields):
    """Prove the C++ validator == jsonschema for a schema whose tool path is
    driven elsewhere (the manifest: rs-replay also recomputes hashes, so a
    mutated manifest cannot isolate a schema rejection here). Compares cpp_valid
    to schema_valid for each mutation; reports the divergences."""
    rows = []
    for path, battery in fields:
        for label, value in list(battery) + CONTAINER_VALUES:
            doc = set_path(base, path, value)
            sv = schema_valid(doc, which)
            cv = cpp_valid(doc, which)
            rows.append((".".join(path), label, sv, cv))
    div = [r for r in rows if r[2] != r[3]]
    print(f"\n=== {which.upper()} (validator vs jsonschema) : {len(rows)} cases ===")
    print(f"  validator != jsonschema (divergence):  {len(div)}")
    for path, label, sv, cv in div:
        print(f"    DIVERGENCE  {path}={label:16} jsonschema={sv} cpp={cv}")
    return len(div)


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
        pp = os.path.join(_TMP_DIR.name, "bm_golden_prof.json")
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


# --- manifest (analysis-bundle.v1): the document rs-replay reads back. The
# re-test found 28/111 nested-field mutations accepted (host.os, schema_versions
# .*, analysis_options.report_unknowns, replay.*). A real, valid manifest is
# generated from the tool and mutated field by field; the C++ validator must
# match jsonschema on every one. ---
def make_base_manifest():
    bdir = os.path.join(_TMP_DIR.name, "bm_manifest_bundle")
    shutil.rmtree(bdir, ignore_errors=True)
    report_path = os.path.join(_TMP_DIR.name, "bm_rep.txt")
    req = _write(BASE_REQ)
    r = subprocess.run([RS_CHECK, req, "--profile", FIXED_PROFILE,
                        "--bundle", bdir, "--quiet", "-o", report_path],
                       capture_output=True, text=True)
    mpath = os.path.join(bdir, "manifest.json")
    if r.returncode not in (0, 1, 2, 3) or not os.path.exists(mpath):
        raise RuntimeError(f"could not generate a base manifest (exit "
                           f"{r.returncode}): {r.stderr.strip()}")
    return json.load(open(mpath))


MANIFEST_FIELDS = [
    (("tool_version",), STR_BATTERY + [("valid", "1.2.3")]),
    (("overall",), ENUM_BADS + [("valid", "SUPPORTED")]),
    (("host", "os"), STR_BATTERY + [("valid", "linux")]),
    (("host", "process_arch"), STR_BATTERY + [("valid", "x86_64")]),
    (("host", "profile_id"),
     [("no-prefix", "abc"), ("number", 5), ("null", None),
      ("valid", "sha256:" + "0" * 64)]),
    (("host", "translation_mode"), STR_BATTERY + [("valid", "none")]),
    (("schema_versions", "profile"), STR_BATTERY + [("valid", "x")]),
    (("schema_versions", "requirement"), STR_BATTERY + [("valid", "x")]),
    (("schema_versions", "result"), STR_BATTERY + [("valid", "x")]),
    (("analysis_options", "report_unknowns"), BOOL_BATTERY + [("valid", True)]),
    (("replay", "status"),
     [("bad", "frobnicate"), ("number", 5), ("valid", "reproduced")]),
    (("replay", "finding_ids_match"), BOOL_BATTERY + [("valid", True)]),
    (("replay", "recorded_overall"), STR_BATTERY + [("valid", "SUPPORTED")]),
    (("finding_ids",),
     [("valid", ["RS-VM-0001"]), ("bad-pattern", ["nope"]),
      ("item-number", [5]), ("not-array", "RS-VM-0001")]),
]


def run_verify_integrity():
    """Profile-level semantic rejections the schema cannot state: a contradictory
    address space, and a stored profile_id that no longer matches the facts. Both
    verified with exit 0 on the 2026-08-02 re-test; both must now refuse (exit 65).
    A real, self-consistent profile must still verify (exit 0)."""
    print("\n=== VERIFY INTEGRITY ===")
    base = json.load(open(FIXED_PROFILE))
    real_id = base.get("profile_id")

    contradictory = copy.deepcopy(base)
    contradictory.setdefault("virtual_memory", {})
    contradictory["virtual_memory"]["min_map_address"] = {
        "value": "0xffff000000000000", "evidence": "measured_capability"}
    contradictory["virtual_memory"]["max_user_address"] = {
        "value": "0x1000", "evidence": "measured_capability"}
    contradictory.pop("profile_id", None)  # id would also mismatch; isolate the bound

    tampered = copy.deepcopy(base)
    tampered["profile_id"] = "sha256:" + "0" * 64

    cases = [
        ("contradictory min>=max refused", contradictory, 65),
        ("tampered profile_id refused", tampered, 65),
        ("a self-consistent profile still verifies", base, 0),
    ]
    bad = 0
    for label, prof, want in cases:
        pp = os.path.join(_TMP_DIR.name, "bm_verify.json")
        json.dump(prof, open(pp, "w"))
        r = subprocess.run([RS_PROFILE, "verify", pp], capture_output=True, text=True)
        ok = r.returncode == want
        if not ok:
            bad += 1
            print(f"  WRONG  {label:44} want exit {want}, got {r.returncode}")
        else:
            print(f"  ok     {label:44} exit {r.returncode}")
    if real_id is None:
        print("  note: fixed profile carries no profile_id; tamper case still valid")
    print(f"  verify integrity mismatches: {bad}")
    return bad


def _require_binaries():
    """Under RS_MATRIX_REQUIRE=1 a missing tool is a hard failure; the whole
    point of the guard is to catch a regression in CI, where the tools are
    always built. Returns an exit code to fail with, or None to proceed."""
    missing = [n for n, b in (("rs-check", RS_CHECK), ("rs-profile", RS_PROFILE),
                              ("rs-replay", RS_REPLAY), ("rs-validate", RS_VALIDATE))
               if b is None]
    if not missing:
        return None
    msg = "boundary matrix: missing built binaries: " + ", ".join(missing)
    if REQUIRE:
        print(msg + " (RS_MATRIX_REQUIRE=1 -> failing, not skipping)",
              file=sys.stderr)
        return 2
    print(msg + " (build the tools to run it; skipping)", file=sys.stderr)
    return 0


def main():
    guard = _require_binaries()
    if guard is not None:
        return guard

    total = 0
    for which, base, fields in (("req", BASE_REQ, REQ_FIELDS),
                                ("prof", BASE_PROF, PROF_FIELDS)):
        total += report(which, run(which, base, fields))

    # Manifest: prove the validator matches jsonschema across the nested fields.
    try:
        base_manifest = make_base_manifest()
        total += run_validator_only("manifest", base_manifest, MANIFEST_FIELDS)
    except RuntimeError as e:
        print(f"\n=== MANIFEST : could not run ({e}) ===", file=sys.stderr)
        total += 1  # fail closed: an un-runnable manifest check is not a pass

    total += run_golden()
    total += run_verify_integrity()
    print(f"\nTOTAL disagreements + divergences + verdict mismatches: {total}")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
