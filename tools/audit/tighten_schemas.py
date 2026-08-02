#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# One-shot: tighten the published schemas so they forbid exactly what the tool's
# data model cannot represent, making the two agree in the OVER-STRICT direction
# the boundary matrix found (the tool rejected values the loose schema allowed).
# Idempotent - re-running changes nothing. Run once, review the diff, delete or
# keep as the record of what was tightened and why.
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
U64 = 18446744073709551615  # the tool stores these as uint64; > this cannot load
HEXPAT = "^0x[0-9a-fA-F]+$"
LEVELS = ["SUPPORTED", "UNSUPPORTED", "CONDITIONALLY_SUPPORTED", "UNKNOWN"]


def uint(nullable=True):
    t = ["integer", "null"] if nullable else "integer"
    return {"type": t, "minimum": 0, "maximum": U64}


def addr(nullable=True):
    t = ["string", "integer", "null"] if nullable else ["string", "integer"]
    return {"type": t, "minimum": 0, "maximum": U64, "pattern": HEXPAT}


def enum_val(values):
    # A fact value that is a fixed vocabulary, or null when unknown.
    return {"enum": values + [None]}


# ---- application-requirements.v1.json -------------------------------------
req_path = os.path.join(ROOT, "schemas/application-requirements.v1.json")
req = json.load(open(req_path))

rp = req["properties"]["request"]["properties"]
# size stays minimum 1 (a zero-length mapping is meaningless) but gains a ceiling.
rp["size"] = {"type": "integer", "minimum": 1, "maximum": U64}
for k in ("required_alignment", "required_page_size", "file_length",
          "max_displacement_bytes"):
    rp[k] = uint()
rp["file_offset"] = uint()          # was a bare non-nullable integer
for k in ("address", "address_min", "address_max"):
    rp[k] = addr()

ap = req["properties"]["assumptions"]["properties"]
for k in ("max_retries", "pointer_storage_width_bits"):
    ap[k] = uint()

# operation "unknown" is not a checkable requirement; the tool refuses it, so the
# schema must too, or the two disagree. An operation the tool does not model at
# all is already rejected by not being in this enum.
req["properties"]["operation"]["enum"] = [
    "virtual_memory_map", "virtual_memory_protect",
    "virtual_memory_reserve", "virtual_memory_commit",
]
# permitted_fallbacks entries are a fixed vocabulary the parser enforces; the
# schema typed them as any string, so a bogus fallback was schema-valid but
# tool-rejected. Type them as the enum so the two agree.
req["properties"]["permitted_fallbacks"]["items"] = {
    "enum": ["relocate", "smaller_size", "weaker_protection", "non_executable",
             "none", "unknown"]}
# sourceLocation.line: a line number fits uint64 and is never negative.
req["$defs"]["sourceLocation"]["properties"]["line"] = {
    "type": "integer", "minimum": 0, "maximum": U64}

json.dump(req, open(req_path, "w"), indent=2, ensure_ascii=False)
open(req_path, "a").write("\n")

# ---- environment-profile.v1.json ------------------------------------------
prof_path = os.path.join(ROOT, "schemas/environment-profile.v1.json")
prof = json.load(open(prof_path))

# pointer_width_bits is derived and written as null when unknown; type it so.
prof["properties"]["platform"]["properties"]["pointer_width_bits"] = {
    "type": ["integer", "null"], "minimum": 0, "maximum": U64}

# A range's evidence may NOT be "unknown" (the reader forbids it: absence of
# observation is not evidence of impossibility). The fact evidence still may be.
prof["$defs"]["rangeEvidenceClass"] = {
    "enum": [e for e in prof["$defs"]["evidenceClass"]["enum"] if e != "unknown"]
}


def fix_range_items(items):
    p = items["properties"]
    p["start"]["pattern"] = HEXPAT
    p["end"]["pattern"] = HEXPAT
    p["evidence"] = {"$ref": "#/$defs/rangeEvidenceClass"}


vm = prof["properties"]["virtual_memory"]["properties"]
for rk in ("available_ranges", "unavailable_ranges"):
    fix_range_items(vm[rk]["items"])
if "range" in prof["$defs"]:
    fix_range_items(prof["$defs"]["range"])

# Typed fact values, so a wrong-typed value is a schema violation rather than
# something the loose `value: {}` waved through while the reader rejected it.
# A page size or allocation granularity of 0 is impossible - minimum 1.
POSITIVE_INT_FACTS = ["page_size", "allocation_granularity"]
INT_FACTS = ["max_single_reservation", "max_single_reservation_hinted"]
ADDR_FACTS = ["min_map_address", "max_user_address"]
BOOL_FACTS = ["anonymous_mapping_supported", "hinted_mapping_may_relocate",
              "fixed_noreplace_available"]
for k in POSITIVE_INT_FACTS:
    if k in vm:
        vm[k]["properties"]["value"] = {"type": ["integer", "null"],
                                        "minimum": 1, "maximum": U64}
for k in INT_FACTS:
    if k in vm:
        vm[k]["properties"]["value"] = {"type": ["integer", "null"],
                                        "minimum": 0, "maximum": U64}
for k in ADDR_FACTS:
    if k in vm:
        vm[k]["properties"]["value"] = {"type": ["string", "integer", "null"],
                                        "pattern": HEXPAT, "minimum": 0,
                                        "maximum": U64}
for k in BOOL_FACTS:
    if k in vm:
        vm[k]["properties"]["value"] = {"type": ["boolean", "null"]}
if "exact_mapping" in vm:
    vm["exact_mapping"]["properties"]["value"] = enum_val(LEVELS)
if "reserve_commit_model" in vm:
    vm["reserve_commit_model"]["properties"]["value"] = enum_val(
        ["posix_lazy", "windows_reserve_commit", "unknown"])
if "file_map_beyond_eof" in vm:
    vm["file_map_beyond_eof"]["properties"]["value"] = enum_val(
        ["sigbus", "error", "zero_fill", "unknown"])
# protection children are booleans; type their value the same way.
prot = vm.get("protection", {})
if "additionalProperties" in prot and isinstance(prot["additionalProperties"], dict):
    prot["additionalProperties"]["properties"]["value"] = {"type": ["boolean", "null"]}

json.dump(prof, open(prof_path, "w"), indent=2, ensure_ascii=False)
open(prof_path, "a").write("\n")
print("tightened both schemas")
