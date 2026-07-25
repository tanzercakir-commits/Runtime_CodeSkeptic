#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the false-positive rate against software that provably runs here.

ROADMAP Gate B and the Phase 3 exit criterion both turn on this number and
neither had ever been evaluated. The reason is not laziness - it is that the
obvious method is disqualified. The campaign's contracts were written by the
same author as the rules, so their agreement measures consistency, not
correctness.

`observe_requirements.py` removes the author. It runs real programs, records
the mapping requests they actually made, keeps only the ones that SUCCEEDED and
that appear in every run, and converts them mechanically. This scores the
result.

THE DEFINITION, stated precisely enough to argue with:

    A false positive is an UNSUPPORTED verdict on a request that this host
    was OBSERVED to perform successfully.

UNSUPPORTED means "no execution satisfying all constraints exists on this host
profile". The kernel performed it. Both cannot be true.

WHAT IS NOT A FALSE POSITIVE, and why each is reported anyway:

  CONDITIONALLY_SUPPORTED  the analyzer named a condition rather than a
                           refusal. Not wrong, but if 90% of real requests
                           trip a condition the tool is unusable in CI, so the
                           share is printed.
  UNKNOWN                  the analyzer declined to answer. NOT a pass. A tool
                           that answers UNKNOWN to everything has a perfect
                           false-positive rate and no value, which is why this
                           number is printed immediately next to the headline.
  a rejected requirement   a document the analyzer would not load. Counted
                           separately and loudly: 16 were rejected on the first
                           run of this harness, and they were the harness's
                           fault, not the analyzer's.

Two populations, scored separately, because they are different questions:

  SHAPE    size and protection, no address. "Does the analyzer refuse ordinary
           mappings that every process on this machine performs?"
  ADDRESS  MAP_FIXED requests carrying the concrete address the kernel
           accepted. "Does a profile measured by ONE process answer correctly
           about addresses accepted in ANOTHER?" ASLR means the honest answer
           may be no, and that is a finding about the tool.

USAGE
    tools/campaign/false_positive_rate.py --dir DIR --profile HOST.json \\
        [--rs-check PATH] [--json OUT.json]
"""
import argparse
import collections
import json
import subprocess
import sys
from pathlib import Path

VERDICTS = ["SUPPORTED", "CONDITIONALLY_SUPPORTED", "UNKNOWN", "UNSUPPORTED"]


def score_bundle(rs_check, bundle, profile):
    proc = subprocess.run(
        [rs_check, str(bundle), "--profile", str(profile), "--format", "json"],
        capture_output=True, text=True)
    if not proc.stdout.strip():
        return None, f"rs-check produced nothing: {proc.stderr.strip()[:200]}"
    try:
        return json.loads(proc.stdout), None
    except json.JSONDecodeError as exc:
        return None, f"rs-check output was not JSON: {exc}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--profile", required=True)
    ap.add_argument("--rs-check", default="rs-check")
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    root = Path(args.dir)
    population = collections.defaultdict(
        lambda: {"verdicts": collections.Counter(),
                 "findings": collections.Counter(),
                 "rejected": 0,
                 "programs": 0,
                 "false_positives": []})

    for kind in ("shape", "address"):
        for bundle in sorted(root.glob(f"*.{kind}.json")):
            label = bundle.name.split(".")[0]
            report, error = score_bundle(args.rs_check, bundle, args.profile)
            if report is None:
                print(f"{label} ({kind}): {error}", file=sys.stderr)
                return 70
            stats = population[kind]
            stats["programs"] += 1
            stats["rejected"] += len(report.get("rejected_requirements", []))

            requirements = json.loads(bundle.read_text())["requirements"]
            # rs-check drops rejected documents, so results and requirements
            # are not index-aligned. Match on the name, which the harness
            # makes unique per program.
            by_name = {r.get("name", ""): r for r in requirements}

            for result in report.get("results", []):
                verdict = result["overall"]
                stats["verdicts"][verdict] += 1
                for finding in result.get("findings", []):
                    stats["findings"][finding["id"]] += 1
                if verdict != "UNSUPPORTED":
                    continue
                name = result.get("requirement_name", "")
                source = by_name.get(name, {})
                stats["false_positives"].append({
                    "program": label,
                    "requirement": name,
                    "observed": source.get("x_observed", "(not recorded)"),
                    "size": source.get("request", {}).get("size"),
                    "address": source.get("request", {}).get("address"),
                    "findings": [f["id"] for f in result.get("findings", [])],
                    "confidence": [f["confidence"]
                                   for f in result.get("findings", [])],
                })

    if not population:
        print("no bundles found; run observe_requirements.py first",
              file=sys.stderr)
        return 70

    out = {"populations": {}}
    print("=" * 72)
    print("FALSE-POSITIVE RATE against observed, successful requests")
    print("=" * 72)

    for kind in ("shape", "address"):
        if kind not in population:
            continue
        s = population[kind]
        total = sum(s["verdicts"].values())
        unsupported = s["verdicts"]["UNSUPPORTED"]
        unknown = s["verdicts"]["UNKNOWN"]
        rate = (unsupported / total * 100) if total else 0.0
        unknown_share = (unknown / total * 100) if total else 0.0

        print(f"\n{kind.upper()}  ({s['programs']} program(s), "
              f"{total} requirement(s) evaluated, "
              f"{s['rejected']} rejected before evaluation)")
        for v in VERDICTS:
            n = s["verdicts"][v]
            print(f"   {v:26} {n:5}  {(n/total*100 if total else 0):5.1f}%")
        print(f"   {'-'*26} {'-'*5}")
        print(f"   FALSE POSITIVES            {unsupported:5}  {rate:5.1f}%"
              f"   <- every UNSUPPORTED here contradicts an observation")
        print(f"   answered nothing (UNKNOWN) {unknown:5}  {unknown_share:5.1f}%"
              f"   <- read this before believing the line above")

        if s["findings"]:
            print("   findings emitted:")
            for fid, n in s["findings"].most_common(8):
                print(f"      {fid}  x{n}")

        out["populations"][kind] = {
            "programs": s["programs"],
            "evaluated": total,
            "rejected": s["rejected"],
            "verdicts": dict(s["verdicts"]),
            "false_positive_count": unsupported,
            "false_positive_rate_percent": round(rate, 2),
            "unknown_share_percent": round(unknown_share, 2),
            "findings": dict(s["findings"]),
            "false_positives": s["false_positives"],
        }

    print("\n" + "=" * 72)
    for kind, s in out["populations"].items():
        for fp in s["false_positives"][:20]:
            print(f"FALSE POSITIVE [{kind}] {fp['program']}: "
                  f"{', '.join(fp['findings'])}")
            print(f"   requirement : {fp['requirement'][:88]}")
            print(f"   observed    : {fp['observed'][:88]}")

    if args.json:
        Path(args.json).write_text(json.dumps(out, indent=2) + "\n")
        print(f"\nwrote {args.json}")

    # Deliberately exits 0 whatever the number is. This measures; it does not
    # judge. The judgement belongs in docs/, signed and dated.
    return 0


if __name__ == "__main__":
    sys.exit(main())
