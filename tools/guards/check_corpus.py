#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The corpus's own rules, enforced, and its count computed rather than claimed.

`corpus/runtime_failures/README.md` is unusually strict about itself: exactly
ten category names, a fixed layer vocabulary, a fixed evidence vocabulary, five
status values, four provenance values, and a rule that only `sourced` and above
count toward the Phase 0 exit criteria. All of it was prose, and prose does not
compile.

It had already drifted. `RSC-0011` - the one entry that counted - carried
`provenance: reported_incident` and `evidence_available: [reported_measurement,
source_citation, log_excerpt]`, and **not one of those four values exists in
the README's own tables**. The entry that the exit criteria turned on was
malformed against the rules of the file that defines them, and the only reason
it mattered to nobody is that nobody was checking.

FIVE CHECKS.

1. FRONT MATTER IS COMPLETE AND USES THE DECLARED VOCABULARY. Every required
   field present; `category`, `layers_involved`, `evidence_available`, `status`
   and `provenance` drawn from the README's lists and nothing else.

2. STATUS AND PROVENANCE AGREE. `pattern_reconstruction` implies `draft`
   implies no `source:`. Anything `sourced` or better requires a `source:`.
   The README states this; nothing enforced it.

3. THE ID MATCHES THE FILENAME, and ids are unique. They are permanent, so a
   collision is not a typo, it is two entries claiming one history.

4. FINDING IDS EXIST. An entry may cite `[]` - that is a signal, not a defect -
   but a cited `RS-VM-nnnn` must be in `docs/findings/registry.md`.

5. THE COUNT IN THE README IS THE COUNT IN THE DIRECTORY. The exit criteria are
   "at least 30 classified real incidents" and "at least 10 in the
   virtual-memory category". Both numbers are recomputed here from the files.
   A README that says 0/30 when 34 entries count is as wrong as one that says
   34 when none do, and the second kind is the one that ends a project.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "corpus" / "runtime_failures"
README = CORPUS / "README.md"
REGISTRY = ROOT / "docs" / "findings" / "registry.md"

CATEGORIES = {
    "semantic success violation", "unsupported exact capability",
    "invalid fallback", "error-code laundering",
    "permanent error treated as retryable",
    "capability present but required property absent",
    "resource-topology contradiction", "temporal contract violation",
    "loader/ABI mismatch",
    "platform-observed behavior treated as guaranteed behavior",
}
LAYERS = {"application", "library", "language_runtime", "compatibility_layer",
          "syscall_interface", "operating_system", "kernel", "hardware",
          "analyzer"}
EVIDENCE = {"specified_guarantee", "measured_capability", "observed_invariant",
            "statically_inferred", "bounded_counterexample", "heuristic_risk",
            "unknown"}
STATUSES = {"draft", "sourced", "reproduced", "regression", "rejected"}
PROVENANCE = {"pattern_reconstruction", "public_report", "first_party",
              "vendor_documentation"}
COUNTING = {"sourced", "reproduced", "regression"}

# The categories that are virtual-memory contradictions rather than some other
# domain. Phase 0 wants at least ten of these specifically.
VM_CATEGORIES = {
    "unsupported exact capability", "resource-topology contradiction",
    "capability present but required property absent",
    "semantic success violation", "temporal contract violation",
}

REQUIRED = ["id", "title", "category", "layers_involved", "evidence_available",
            "finding_ids", "status", "provenance"]


def parse_front_matter(text):
    if not text.startswith("---"):
        return None
    end = text.find("\n---", 3)
    if end < 0:
        return None
    out = {}
    for line in text[3:end].splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("- "):          # block list continuation
            out.setdefault("_list", []).append(line[2:].strip())
            continue
        if ":" not in line:
            continue
        key, _, value = line.partition(":")
        out[key.strip()] = value.strip()
    return out


def as_list(value):
    if value is None:
        return []
    value = value.strip()
    if value.startswith("[") and value.endswith("]"):
        inner = value[1:-1].strip()
        return [v.strip() for v in inner.split(",") if v.strip()]
    return [value] if value else []


def main() -> int:
    if not CORPUS.exists():
        print("corpus/runtime_failures/ is missing", file=sys.stderr)
        return 1

    registry = REGISTRY.read_text() if REGISTRY.exists() else ""
    problems, counting, vm_counting, seen = [], 0, 0, {}

    for path in sorted(CORPUS.glob("RSC-*.md")):
        rel = str(path.relative_to(ROOT))
        fm = parse_front_matter(path.read_text())
        if fm is None:
            problems.append(f"{rel}: no YAML front matter")
            continue

        for field in REQUIRED:
            if field not in fm:
                problems.append(f"{rel}: missing required field `{field}`")
        if any(f not in fm for f in REQUIRED):
            continue

        ident = fm["id"]
        if not path.name.startswith(ident + "-"):
            problems.append(f"{rel}: front matter says `{ident}`, filename disagrees")
        if ident in seen:
            problems.append(f"{rel}: id `{ident}` already used by {seen[ident]}; "
                            f"ids are permanent")
        seen[ident] = rel

        if fm["category"] not in CATEGORIES:
            problems.append(f"{rel}: category `{fm['category']}` is not one of "
                            f"the ten in docs/failure_taxonomy.md")
        for layer in as_list(fm["layers_involved"]):
            if layer not in LAYERS:
                problems.append(f"{rel}: unknown layer `{layer}`")
        for ev in as_list(fm["evidence_available"]):
            if ev not in EVIDENCE:
                problems.append(f"{rel}: `{ev}` is not an evidence class in "
                                f"rs::EvidenceClass")
        if fm["status"] not in STATUSES:
            problems.append(f"{rel}: unknown status `{fm['status']}`")
        if fm["provenance"] not in PROVENANCE:
            problems.append(f"{rel}: `{fm['provenance']}` is not one of the "
                            f"four provenance values in the README")

        # Check 2: status and provenance must agree.
        if fm["provenance"] == "pattern_reconstruction" and fm["status"] != "draft":
            problems.append(f"{rel}: pattern_reconstruction must be `draft`; "
                            f"it is `{fm['status']}`")
        if fm["status"] in COUNTING and not fm.get("source"):
            problems.append(f"{rel}: status `{fm['status']}` counts toward the "
                            f"exit criteria and requires a `source:`")
        if fm["provenance"] == "pattern_reconstruction" and fm.get("source"):
            problems.append(f"{rel}: pattern_reconstruction with a `source:`; "
                            f"upgrade the provenance or drop the source")

        # Check 4: cited finding ids must exist.
        for fid in as_list(fm["finding_ids"]):
            if registry and fid not in registry:
                problems.append(f"{rel}: cites `{fid}`, which is not in "
                                f"docs/findings/registry.md")

        if fm["status"] in COUNTING:
            counting += 1
            if fm["category"] in VM_CATEGORIES:
                vm_counting += 1

    # Check 5: the README's numbers must be these numbers.
    text = README.read_text() if README.exists() else ""
    claim = re.search(r"<!--\s*counting:\s*(\d+)\s*/\s*30\s+vm:\s*(\d+)\s*/\s*10\s*-->",
                      text)
    if claim is None:
        problems.append(
            "corpus/runtime_failures/README.md has no "
            "`<!-- counting: N/30 vm: M/10 -->` marker. The exit criteria are "
            "numbers; they must be checked, not asserted in prose.")
    else:
        if int(claim.group(1)) != counting or int(claim.group(2)) != vm_counting:
            problems.append(
                f"README claims counting={claim.group(1)}/30 vm={claim.group(2)}/10; "
                f"the files say counting={counting}/30 vm={vm_counting}/10")

    print(f"corpus: {len(seen)} entries, {counting} counting toward 30, "
          f"{vm_counting} toward the virtual-memory 10")
    if problems:
        print(f"\n{len(problems)} corpus problem(s):", file=sys.stderr)
        for p in problems[:40]:
            print(f"  - {p}", file=sys.stderr)
        if len(problems) > 40:
            print(f"  ... and {len(problems)-40} more", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
