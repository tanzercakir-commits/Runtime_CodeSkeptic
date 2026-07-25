#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Documents may not assert things about the code that have stopped being true.

Every document in this repository describes the implementation, and several of
them were found asserting the opposite of the current state:

    docs/architecture/determinism.md   "tests/unit/ and tests/conformance/ are
                                        empty"                 -> 13 suites exist
    docs/evidence_model.md            "Phase 1 probe (not implemented)"
                                                               -> implemented
    docs/findings/registry.md         "RS-VM-0018 is unreachable"
                                                               -> a rule emits it
    README.md                         "no extractor is bundled" -> rs-extract
    corpus/.../RSC-0008.md            "no rule exists"          -> it does

None of these was caught by anything, because prose is not compiled. This
guard compiles the checkable part of it.

THREE CHECKS.

1. PATH CLAIMS. A sentence naming a repository path and asserting absence
   ("does not exist", "is empty", "not implemented", "no ... exists") is
   verified against the filesystem. A doc that says a directory is empty when
   it has thirteen files fails here.

2. ABSENCE CLAIMS NEED A DATE. Any sentence asserting that something is not
   implemented must carry a `<!-- checked: YYYY-MM-DD -->` marker on the same
   line or the line before, or appear in KNOWN. The marker does not prove the
   claim; it proves somebody looked, and it makes an eight-month-old assertion
   visibly eight months old.

3. NAMED PATHS MUST EXIST. Checks 1 and 2 only ever look at claims of ABSENCE,
   which left the opposite direction wide open: a document naming a path that
   is not there passed silently. That is how `tools/rs-extract` survived its
   own deletion in a case study - and it survived carrying a fresh `checked:`
   marker, because the marker attaches to absence language and there was none.
   So every backticked repository path in every document is now resolved
   against the filesystem. 196 were named when this was written and 9 were
   missing; all but one were in the historical log, which is exempt.

   Two escape valves, because a document legitimately names paths that are not
   here: `<!-- external -->` for another project's tree (the shadPS4 case study
   cites that project's `src/common`, which collides with ours), and
   `<!-- planned -->` for something the ROADMAP specifies and nobody has built.
   Both must be on the same line or the line before, and both say out loud what
   was previously left to the reader to guess.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Deliberately NARROW. The first version also matched "an empty range is
# representable" and "that host does not exist on x86-64 Linux" - prose about
# concepts, not claims about this repository. A guard that cries wolf gets
# switched off, and a switched-off guard is worse than none, so this matches
# only phrases that assert something about the STATE OF THIS IMPLEMENTATION.
ABSENCE = re.compile(
    r"\bnot\s+implemented\b"
    r"|\bis\s+unreachable\b"
    r"|\bdoes\s+not\s+exist\s+yet\b|\bdo\s+not\s+exist\s+yet\b"
    r"|\bnone\s+of\s+(?:these|them)\s+exist(?:s)?\s+yet\b"
    r"|\bhas\s+never\s+run\b|\bhave\s+never\s+run\b"
    r"|\bnothing\s+is\s+emitted\b"
    r"|\bno\s+rule\s+(?:exists|reads)\b"
    r"|\bis\s+not\s+bundled\b|\bnone\s+is\s+bundled\b"
    r"|\bthe\s+probe\s+does\s+not\s+exist\b"
    r"|\bis\s+a\s+stub\b",
    re.IGNORECASE)

# Path claims are checked separately and are always mechanical, so this second
# pattern stays broad - it only ever fires when a real path is named next to it.
PATH_CLAIM = re.compile(
    r"\b(?:is|are)\s+empty\b|\bdoes\s+not\s+exist\b|\bnot\s+implemented\b"
    r"|\bhas\s+no\s+`?CMakeLists", re.IGNORECASE)

PATH = re.compile(r"`((?:src|tests|tools|include|docs|schemas|profiles|contracts|corpus)/[A-Za-z0-9_./*-]+)`")

CHECKED = re.compile(r"<!--\s*checked:\s*(\d{4}-\d{2}-\d{2})\s*-->")
# A named path that is not here must say why: it belongs to another project, or
# it is specified and unbuilt. Anything else is a document describing something
# that does not exist.
EXCUSED_PATH = re.compile(r"<!--\s*(?:external|planned)\s*-->")

# Claims that are TRUE and are expected to stay in the documents. Each needs a
# reason, so the list cannot quietly become a way to silence the guard.
KNOWN = {
    "docs/non_goals.md": "every entry is a commitment NOT to build something; "
                         "the absence language is the point",
    "docs/PLAN.md": "the plan's whole job is to record what is not done",
    "docs/PROGRESS.md": "an append-only log of what was found wrong; the "
                        "absence language is the record, not a claim about now",
    "tools/guards/check_docs.py": "this file quotes the claims it checks",
}


def main() -> int:
    problems = []
    scanned = 0
    for path in sorted(ROOT.rglob("*.md")):
        if ".git" in path.parts:
            continue
        rel = str(path.relative_to(ROOT))
        if rel in KNOWN:
            continue
        scanned += 1
        lines = path.read_text().splitlines()
        for i, line in enumerate(lines):
            has_path_claim = PATH_CLAIM.search(line)
            has_absence = ABSENCE.search(line)

            # Check 3: every named path must exist. Runs on EVERY line, before
            # the two absence checks bail out - a stale presence claim carries
            # none of the language they look for, which is exactly why one
            # survived. (docs/PLAN.md is in KNOWN and skipped here; its paths
            # are resolved by tools/guards/check_plan.py instead, which is
            # stricter about them.)
            window = "\n".join(lines[max(0, i - 1):i + 1])
            if not EXCUSED_PATH.search(window):
                for p in PATH.findall(line):
                    if "*" in p or "..." in p:
                        continue
                    if not (ROOT / p).exists():
                        problems.append(
                            f"{rel}:{i+1}: names `{p}`, which does not exist. "
                            f"Fix the path, or mark the line "
                            f"`<!-- external -->` (another project's tree) or "
                            f"`<!-- planned -->` (specified, unbuilt)")

            if not has_path_claim and not has_absence:
                continue

            # Check 1: a path named in the same sentence must match reality.
            for p in (PATH.findall(line) if has_path_claim else []):
                target = ROOT / p
                if "*" in p:
                    continue
                if "empty" in line.lower() and target.is_dir():
                    entries = [e for e in target.iterdir() if e.name != ".gitkeep"]
                    if entries:
                        problems.append(
                            f"{rel}:{i+1}: says `{p}` is empty; it has "
                            f"{len(entries)} entr{'y' if len(entries)==1 else 'ies'}")
                elif re.search(r"does not exist|not implemented", line, re.I) and target.exists():
                    problems.append(
                        f"{rel}:{i+1}: asserts `{p}` is absent or unimplemented; "
                        f"it exists")

            # Check 2: an implementation-state claim must be dated.
            if not has_absence:
                continue
            window = "\n".join(lines[max(0, i - 1):i + 1])
            if not CHECKED.search(window):
                problems.append(
                    f"{rel}:{i+1}: asserts something is absent or unimplemented "
                    f"with no `<!-- checked: YYYY-MM-DD -->` marker: "
                    f"\"{line.strip()[:88]}\"")

    print(f"doc guard: scanned {scanned} document(s)")
    if problems:
        print(f"\n{len(problems)} unverified or false claim(s) about the code:",
              file=sys.stderr)
        for p in problems[:40]:
            print(f"  - {p}", file=sys.stderr)
        if len(problems) > 40:
            print(f"  ... and {len(problems)-40} more", file=sys.stderr)
        print("\nEither fix the claim, or date it with <!-- checked: YYYY-MM-DD -->.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
