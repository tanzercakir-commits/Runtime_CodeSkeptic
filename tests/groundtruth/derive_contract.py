#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Rewrite a ground-truth contract's address from a MEASURED fact.

WHY A CONTRACT MAY NOT NAME THE CEILING AS A CONSTANT.

`exact-mapping-above-user-space` asked for `0x800000000000` and its name asserts
that this is above the top of user space. On a 4-level x86-64 host it is:
`max_user_address` is `0x7ffffffff000`. On a host with 5-level paging it is
`0xfffffffffff000`, so `0x800000000000` sits 63 PiB BELOW the ceiling, in
ordinary mappable space - and the case's own name stops being true.

That is not hypothetical and it is not new. CI landed on LA57 hardware in July
2026 and `oversized-reservation-4pib` was CONTRADICTED there for the same reason:
a constant standing in for a measurement. `RSC-0049` in this repository's own
corpus documents the rule.

Renaming the case is not the fix. Deriving the address from the bound the probe
measured is.

WHAT IS DERIVED, AND WHAT IS NOT. Only the request address and the postcondition
that names it. Everything else - size, protections, assumptions, the failure sink
- stays exactly as committed, so the derived contract is the committed contract
with one measured number substituted, and `rs-check` sees a document that is still
schema-valid.

If the profile does not carry the fact, or carries it as unknown, NOTHING is
derived and the committed constant stands. A synthetic profile must not silently
become a measurement, and `tests/groundtruth/selftest.sh` drives this path with
one on purpose.
"""
import json
import sys


def main() -> int:
    if len(sys.argv) not in (5, 6):
        print("usage: derive_contract.py CONTRACT PROFILE FACT OUT "
              "[REQUEST_FIELD]", file=sys.stderr)
        return 64
    contract_path, profile_path, fact_name, out_path = sys.argv[1:5]
    request_field = sys.argv[5] if len(sys.argv) == 6 else "address"
    if request_field not in {"address", "address_max"}:
        print(f"unsupported derived request field: {request_field}",
              file=sys.stderr)
        return 64

    profile = json.load(open(profile_path))
    fact = (profile.get("virtual_memory") or {}).get(fact_name) or {}
    value = fact.get("value")
    if value is None or fact.get("evidence") in (None, "unknown", "absent"):
        return 1          # nothing measured; the caller keeps the constant

    if isinstance(value, str):
        address = int(value, 16 if value.startswith("0x") else 10)
    else:
        address = int(value)
    if address <= 0:
        return 1

    contract = json.load(open(contract_path))
    old = contract["request"][request_field]
    new = hex(address)
    contract["request"][request_field] = new
    contract["required_postconditions"] = [
        text.replace(old, new)
        for text in contract.get("required_postconditions", [])
    ]
    contract["name"] = contract["name"].replace(old, new)
    note = (f"DERIVED at run time: request.{request_field} was rewritten from "
            f"the committed constant {old} to {new}, the measured "
            f"`{fact_name}` of the host this ran on. The runtime case receives "
            f"the same derived value, so analysis and execution stay bound.")
    contract["x_derivation"] = note
    json.dump(contract, open(out_path, "w"), indent=2)
    print(new[2:])        # bare hex, which is what the case program takes
    return 0


if __name__ == "__main__":
    sys.exit(main())
