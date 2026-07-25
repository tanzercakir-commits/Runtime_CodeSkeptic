#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Re-fetch every corpus source and check it still says what the entry claims.

A citation is a claim, and it decays in two directions.

**Link rot** is the obvious one: a bug tracker moves, a mailing-list archive
disappears, a project is renamed. An entry citing a dead URL is worth less than
one citing nothing, because it looks checked.

**Fabrication** is the one that actually threatens this project. The 43 entries
added on 2026-07-25 were researched by several readers working in parallel and
written up by one. Every entry carries a quoted line and a claim that the page
was fetched. If any of those were reconstructed from memory rather than read,
the corpus would be exactly the thing `docs/PLAN.md` Phase 0 exit criterion 5
forbids - a claim resting on generated interpretation - and it would be
invisible, because a plausible citation looks like a real one.

So this fetches each `source:` and checks that the quoted line is present.

    tools/campaign/verify_corpus_sources.py            all sourced entries
    tools/campaign/verify_corpus_sources.py --sample 8 a random subset
    tools/campaign/verify_corpus_sources.py --id RSC-0018

DELIBERATELY NOT A CI GUARD. It needs network, it is slow, and several of these
hosts rate-limit or sit behind bot walls - WineHQ's bugzilla is unfetchable
entirely, which is recorded in the entries that would otherwise cite it. A
guard that fails for reasons unrelated to the repository gets switched off, and
this project has written down what that costs. Run it deliberately, and record
the result in `docs/PROGRESS.md`.

KNOWN LIMITATION, AND IT APPLIES WHERE THIS WAS WRITTEN. The authoring
environment reaches the network only through a proxy that permits its own
fetch tool and refuses plain HTTP clients: every one of these URLs returns
`403 Forbidden` to `urllib`. So this script has **never successfully verified
anything in that environment** - it exits 2 and says so, which is the correct
behaviour and the reason exit 2 exists. The corpus's verification there came
from the fetch tool instead, entry by entry at authoring time, plus six
independent re-fetches by a second reader. This script is for an environment
with ordinary network access, and until it runs somewhere that has one, the
sampling statement in the corpus README is the honest bound on what is
verified.

Exit codes: 0 all checked sources matched, 1 at least one did not, 2 nothing
was checkable (no network).
"""
import argparse
import random
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "corpus" / "runtime_failures"

FM = re.compile(r"^---\n(.*?)\n---", re.S)
QUOTE = re.compile(r"^> (.+)$", re.M)


def entries():
    for path in sorted(CORPUS.glob("RSC-*.md")):
        text = path.read_text()
        m = FM.match(text)
        if not m:
            continue
        fields = {}
        for line in m.group(1).splitlines():
            if ":" in line and not line.startswith(" "):
                k, _, v = line.partition(":")
                fields[k.strip()] = v.strip()
        if fields.get("status") not in ("sourced", "reproduced", "regression"):
            continue
        source = fields.get("source", "")
        if not source.startswith("http"):
            continue
        quotes = QUOTE.findall(text[m.end():])
        yield fields.get("id", path.name), source, quotes[0] if quotes else ""


def normalise(text):
    """Compare on words, not bytes.

    A fetched page is HTML; the entry quotes the sentence as it renders. Byte
    equality would fail on entities, smart quotes and wrapping while telling us
    nothing about whether the claim is true.
    """
    return re.sub(r"[^a-z0-9]+", " ", text.lower()).strip()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--id", default="")
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()

    items = list(entries())
    if args.id:
        items = [i for i in items if i[0] == args.id]
    if args.sample and args.sample < len(items):
        items = random.sample(items, args.sample)

    ok = bad = unreachable = 0
    for ident, url, quote in items:
        try:
            request = urllib.request.Request(
                url, headers={"User-Agent": "RuntimeSkeptic-corpus-verifier"})
            with urllib.request.urlopen(request, timeout=args.timeout) as r:
                body = r.read().decode("utf-8", "replace")
        except (urllib.error.URLError, OSError, ValueError) as exc:
            print(f"UNREACHABLE {ident}  {url}\n             {exc}")
            unreachable += 1
            continue

        if not quote:
            print(f"NO QUOTE    {ident}  fetched, but the entry quotes nothing")
            bad += 1
            continue

        needle = normalise(quote)
        # Long quotes may be split across markup; check a distinctive run of
        # words rather than the whole sentence.
        probe = " ".join(needle.split()[:8])
        if probe and probe in normalise(body):
            print(f"ok          {ident}")
            ok += 1
        else:
            print(f"NOT FOUND   {ident}  {url}\n             looked for: {probe!r}")
            bad += 1

    total = ok + bad + unreachable
    print(f"\n{total} source(s): {ok} matched, {bad} did not, "
          f"{unreachable} unreachable")
    if total and total == unreachable:
        print("nothing was checkable; this says nothing about the corpus",
              file=sys.stderr)
        return 2
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
