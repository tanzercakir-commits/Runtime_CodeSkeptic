#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The guards are code, so the guards need tests.

Every guard here was written after a real drift incident, and every one of them
passes on the current repository. That proves nothing on its own: a guard whose
regex never matches also passes on everything, silently, forever. The project
has already been bitten twice by exactly that shape - the ground-truth harness
counted a crashing case as a confirmed refusal, and its comparison table was
green while discarding compiler warnings. Silence read as success both times.

So each check is run against a throwaway repository that is deliberately wrong,
and is required to FAIL, with the right message. Then against the corrected
version, and required to PASS. A guard that cannot be made to fail on demand is
not protecting anything.

    tools/guards/selftest.py            all cases
    tools/guards/selftest.py -v         print each case

The fixture is a real directory tree, not a mock: the guards resolve their root
from their own location, so each case copies the guard under test into
`<tmp>/tools/guards/` and lets it look at `<tmp>` as if it were the repository.
That is the same code path CI runs, with different contents underneath it.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv

# Minimal contents every fixture needs so that a guard fails for the ONE reason
# a case is about, rather than for a missing prerequisite.
BASE_NON_GOALS = """# Non-goals

These are commitments, not a description of current limitations.

## 18. We will not duplicate CodeSkeptic

Reserved for CodeSkeptic: contract extraction, fatal-sink identification.
"""

# A todo list that satisfies every shape rule, so a case about ONE rule fails
# for that rule and not for a missing prerequisite.
TODO_OK = """# TODO

## Now

### T-001 — do the thing `[now]`

**Done when:** `ctest` passes and prints the new suite.

## Deliberately not tracked

- **Phases 6-10** — scope risk, deferred deliberately.
"""


class Case:
    def __init__(self, guard, name, files, expect_fail, expect_text="",
                 commits=None):
        self.guard = guard
        self.name = name
        self.files = files
        self.expect_fail = expect_fail
        self.expect_text = expect_text
        # [(iso-date, {rel: content}), ...] - committed in order, each with
        # that author date. check_dates.py reads git, so testing it needs a
        # real repository with real (and deliberately wrong) history.
        self.commits = commits or []

    def _git(self, root, *args, when=None):
        env = {"PATH": "/usr/bin:/bin", "HOME": str(root),
               "GIT_CONFIG_GLOBAL": "/dev/null", "GIT_CONFIG_SYSTEM": "/dev/null"}
        if when:
            env["GIT_AUTHOR_DATE"] = when
            env["GIT_COMMITTER_DATE"] = when
        subprocess.run(["git", *args], cwd=root, env=env,
                       capture_output=True, text=True)

    def run(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "tools" / "guards").mkdir(parents=True)
            shutil.copy(HERE / self.guard, root / "tools" / "guards" / self.guard)

            if self.commits:
                self._git(root, "init", "-q", "-b", "main")
                self._git(root, "config", "user.email", "selftest@example.invalid")
                self._git(root, "config", "user.name", "selftest")
                for when, files in self.commits:
                    for rel, content in files.items():
                        target = root / rel
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_text(content)
                    self._git(root, "add", "-A")
                    self._git(root, "commit", "-q", "-m", "selftest", when=when)

            for rel, content in self.files.items():
                target = root / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                if content is None:          # a directory, or an empty file
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.write_text(content)
            proc = subprocess.run(
                [sys.executable, str(root / "tools" / "guards" / self.guard)],
                capture_output=True, text=True)
            out = proc.stdout + proc.stderr
            failed = proc.returncode != 0

            if failed != self.expect_fail:
                want = "fail" if self.expect_fail else "pass"
                got = "failed" if failed else "passed"
                return False, (f"expected the guard to {want}; it {got}\n"
                               f"        --- guard output ---\n"
                               f"        " + out.strip().replace("\n", "\n        "))
            if self.expect_text and self.expect_text not in out:
                return False, (f"guard behaved correctly but said the wrong "
                               f"thing: expected {self.expect_text!r} in\n"
                               f"        " + out.strip().replace("\n", "\n        "))
            return True, ""


CASES = [
    # ---- check_docs: check 3, named paths must exist -------------------
    Case("check_docs.py", "a doc naming a path that is not there fails",
         {"docs/x.md": "The extractor lives in `tools/rs-extract` and works.\n",
          "docs/non_goals.md": BASE_NON_GOALS},
         expect_fail=True, expect_text="does not exist"),

    Case("check_docs.py", "the same path, once it exists, passes",
         {"docs/x.md": "The extractor lives in `tools/rs-extract` and works.\n",
          "tools/rs-extract/main.cpp": "int main(){}\n"},
         expect_fail=False),

    Case("check_docs.py", "<!-- external --> excuses another project's tree",
         {"docs/x.md": "Sparse checkout of that project's `src/common`. "
                       "<!-- external -->\n"},
         expect_fail=False),

    Case("check_docs.py", "<!-- planned --> excuses a specified, unbuilt path",
         {"docs/x.md": "<!-- planned -->\nROADMAP 17 specifies "
                       "`tools/rs-replay`.\n"},
         expect_fail=False),

    Case("check_docs.py", "a marker does not excuse the line after next",
         {"docs/x.md": "<!-- external -->\n\nWe also ship `tools/rs-ghost`.\n"},
         expect_fail=True, expect_text="rs-ghost"),

    Case("check_docs.py", "a glob is not resolved as a path",
         {"docs/x.md": "The guard globs `tools/*extract*` for this.\n"},
         expect_fail=False),

    Case("check_docs.py", "the scenarios may describe an unbuilt project",
         {"docs/scenarios/assessment.md":
              "A Windows probe does not exist yet, so S3 is dead.\n"},
         expect_fail=False),

    Case("check_docs.py", "but a scenario citing a missing path still fails",
         {"docs/scenarios/assessment.md":
              "Verified by `tests/groundtruth/contracts/gone.json`.\n"},
         expect_fail=True, expect_text="does not exist"),

    # THE ONE AN EXTERNAL REVIEWER FOUND. A cited path that exists on disk but
    # is not tracked passes for whoever generated it and fails for everyone
    # else. This needs a real repository, because the check falls back to the
    # filesystem when there is no git to ask.
    Case("check_docs.py", "an untracked file on disk does not satisfy a citation",
         {"profiles/generated/linux-x86_64.json": '{"generated": true}\n'},
         commits=[("2026-07-25T12:00:00+00:00",
                   {".gitignore": "profiles/generated/*.json\n",
                    "docs/x.md": "Measured into "
                                 "`profiles/generated/linux-x86_64.json`.\n"})],
         expect_fail=True, expect_text="not tracked"),

    Case("check_docs.py", "the same citation passes once the file is committed",
         {},
         commits=[("2026-07-25T12:00:00+00:00",
                   {"profiles/generated/linux-x86_64.json": '{"a":1}\n',
                    "docs/x.md": "Measured into "
                                 "`profiles/generated/linux-x86_64.json`.\n"})],
         expect_fail=False),

    # ---- check_docs: check 1, absence claims must match the filesystem --
    Case("check_docs.py", "\"is empty\" about a directory that is not fails",
         {"docs/x.md": "The suites in `tests/unit` are empty. "
                       "<!-- checked: 2026-07-25 -->\n",
          "tests/unit/test_a.cpp": "int main(){}\n"},
         expect_fail=True, expect_text="is empty; it has 1 entry"),

    Case("check_docs.py", "\"does not exist\" about something present fails",
         {"docs/x.md": "`src/probe` does not exist. <!-- checked: 2026-07-25 -->\n",
          "src/probe/vm_probe_linux.cpp": "int main(){}\n"},
         expect_fail=True, expect_text="it exists"),

    # ---- check_docs: check 2, absence claims need a date ----------------
    Case("check_docs.py", "an undated absence claim fails",
         {"docs/x.md": "The Windows probe is a stub.\n"},
         expect_fail=True, expect_text="no `<!-- checked"),

    Case("check_docs.py", "the same claim with a date passes",
         {"docs/x.md": "The Windows probe is a stub. <!-- checked: 2026-07-25 -->\n"},
         expect_fail=False),

    # ---- check_non_goals: a removed name may not survive as a claim -----
    Case("check_non_goals.py", "a removed component named in a schema fails",
         {"docs/non_goals.md": BASE_NON_GOALS,
          "schemas/a.v1.json": '{"description": "written by rs-extract"}\n'},
         expect_fail=True, expect_text="names `rs-extract`"),

    Case("check_non_goals.py", "the same name in the progress log is history",
         {"docs/non_goals.md": BASE_NON_GOALS,
          "docs/PROGRESS.md": "rs-extract was removed on 2026-07-25.\n"},
         expect_fail=False),

    Case("check_non_goals.py", "the underscore spelling is caught too",
         {"docs/non_goals.md": BASE_NON_GOALS,
          "src/a.cpp": "// see rs_extract for the recogniser\n"},
         expect_fail=True, expect_text="rs_extract"),

    # ---- check_non_goals: the capability may not grow back -------------
    Case("check_non_goals.py", "an extractor reappearing under a new name fails",
         {"docs/non_goals.md": BASE_NON_GOALS,
          "tools/rs-contract-extractor/main.cpp": "int main(){}\n"},
         expect_fail=True, expect_text="extractor growing back"),

    Case("check_non_goals.py", "a dated section-18 exception permits it",
         {"docs/non_goals.md": BASE_NON_GOALS +
                               "\nNON-GOAL-18-EXCEPTION: 2026-07-25\n",
          "tools/rs-contract-extractor/main.cpp": "int main(){}\n"},
         expect_fail=False, expect_text="reconciled"),

    Case("check_non_goals.py", "deleting section 18 is not a resolution",
         {"docs/non_goals.md": "# Non-goals\n\nNothing here.\n"},
         expect_fail=True, expect_text="no longer contains section 18"),

    # ---- check_plan: a [done] is a claim and needs evidence -------------
    Case("check_plan.py", "a [done] with no evidence fails",
         {"docs/PLAN.md": "# Plan and status\n\n- `[done]` the probe works\n"},
         expect_fail=True),

    Case("check_plan.py", "a [done] citing a path that is not there fails",
         {"docs/PLAN.md": "# Plan and status\n\n- `[done]` the probe works — "
                          "`tests/unit/test_nothing.cpp`\n"},
         expect_fail=True),

    Case("check_plan.py", "evidence on the next line still counts",
         {"docs/PLAN.md": "# Plan and status\n\n`[done]` the probe works,\n"
                          "measured on two lanes: `docs/PLAN.md`\n"},
         expect_fail=False),

    Case("check_plan.py", "the scenario assessment is checked too",
         {"docs/PLAN.md": "# Plan and status\n\n- `[open]` nothing yet\n",
          "docs/scenarios/assessment.md":
              "# Scenarios\n\n## S1\n\n`[done]` it works, honest\n"},
         expect_fail=True, expect_text="scenarios/assessment.md"),

    # ---- check_dates: git decides, not the author ----------------------
    Case("check_dates.py", "a progress entry dated before it was written fails",
         {},
         commits=[("2026-07-25T12:00:00+00:00",
                   {"docs/PROGRESS.md": "# Progress log\n\n"
                                        "## 2026-01-02 — backdated\n\nbody\n"})],
         expect_fail=True, expect_text="git says the line was written"),

    Case("check_dates.py", "the same entry dated when it was written passes",
         {},
         commits=[("2026-07-25T12:00:00+00:00",
                   {"docs/PROGRESS.md": "# Progress log\n\n"
                                        "## 2026-07-25 — honest\n\nbody\n"})],
         expect_fail=False),

    Case("check_dates.py", "a date after the newest commit fails",
         {},
         commits=[("2026-07-25T12:00:00+00:00",
                   {"docs/PROGRESS.md": "# Progress log\n\n"
                                        "## 2026-07-25 — honest\n\nbody\n",
                    "docs/x.md": "Verified. <!-- checked: 2027-03-01 -->\n"})],
         expect_fail=True, expect_text="after the newest commit"),

    Case("check_dates.py", "a marked future date is a scheduled event, not a bad clock",
         {},
         commits=[("2026-07-26T12:00:00+00:00",
                   {"docs/PROGRESS.md": "# Progress log\n\n"
                                        "## 2026-07-26 — honest\n\nbody\n",
                    "docs/x.md": "Quota resets 2026-08-01. <!-- future -->\n"})],
         expect_fail=False),

    Case("check_dates.py", "an unmarked future date still fails",
         {},
         commits=[("2026-07-26T12:00:00+00:00",
                   {"docs/PROGRESS.md": "# Progress log\n\n"
                                        "## 2026-07-26 — honest\n\nbody\n",
                    "docs/x.md": "Quota resets 2026-08-01.\n"})],
         expect_fail=True, expect_text="mark the line"),

    Case("check_dates.py", "a stale checked marker is reported, not failed",
         {},
         commits=[("2026-07-25T12:00:00+00:00",
                   {"docs/PROGRESS.md": "# Progress log\n\n"
                                        "## 2026-07-25 — honest\n\nbody\n",
                    "docs/x.md": "Still true. <!-- checked: 2024-01-01 -->\n"})],
         expect_fail=False, expect_text="stale:"),

    Case("check_dates.py", "one day either side is tolerated NEAR MIDNIGHT",
         {},
         commits=[("2026-07-25T00:30:00+00:00",
                   {"docs/PROGRESS.md": "# Progress log\n\n"
                                        "## 2026-07-24 — written before midnight\n\nbody\n"})],
         expect_fail=False),

    # The flat +/-1 day allowance swallowed a heading eleven and a half hours
    # wrong, in this project, hours after a reviewer flagged the class.
    Case("check_dates.py", "one day apart in the middle of the day fails",
         {},
         commits=[("2026-07-26T10:43:00+00:00",
                   {"docs/PROGRESS.md": "# Progress log\n\n"
                                        "## 2026-07-25 — stale sense of today\n\nbody\n"})],
         expect_fail=True, expect_text="too far for a timezone artifact"),

    # ---- check_includes: the MSVC class, caught without MSVC -----------
    Case("check_includes.py", "back_inserter without <iterator> fails",
         {"src/vm/impact.cpp": "#include <algorithm>\n#include <set>\n"
                               "void f(){ std::back_inserter(v); }\n"},
         expect_fail=True, expect_text="MSVC does not"),

    Case("check_includes.py", "the same file passes once <iterator> is there",
         {"src/vm/impact.cpp": "#include <algorithm>\n#include <iterator>\n"
                               "#include <set>\n"
                               "void f(){ std::back_inserter(v); }\n"},
         expect_fail=False),

    # The 42-hits-of-noise case: a .cpp getting <string> from its OWN header is
    # not relying on an accident, and a guard that says otherwise gets ignored.
    Case("check_includes.py", "a project header supplying the include is enough",
         {"include/runtimeskeptic/vm/thing.hpp": "#include <string>\n",
          "src/vm/thing.cpp": '#include "runtimeskeptic/vm/thing.hpp"\n'
                              "std::string f(){ return {}; }\n"},
         expect_fail=False),

    Case("check_includes.py", "a symbol named only in a comment is not a use",
         {"src/vm/thing.cpp": "// std::back_inserter is what broke on MSVC\n"
                              "int f(){ return 0; }\n"},
         expect_fail=False),

    # ---- check_shell_portability: macOS ships bash 3.2 -----------------
    Case("check_shell_portability.py", "declare -A fails, as it did on macOS",
         {"tests/x.sh": "#!/usr/bin/env bash\ndeclare -A want=( [a]=1 )\n"},
         expect_fail=True, expect_text="bash 3.2"),

    Case("check_shell_portability.py", "mapfile fails too - the queued next one",
         {"tests/x.sh": "#!/usr/bin/env bash\nmapfile -t a < <(echo x)\n"},
         expect_fail=True, expect_text="mapfile"),

    Case("check_shell_portability.py", "the portable rewrite passes",
         {"tests/x.sh": "#!/usr/bin/env bash\nwant_for(){ case \"$1\" in a) echo 1;; esac; }\n"},
         expect_fail=False),

    Case("check_shell_portability.py", "a construct named in a comment is not code",
         {"tests/x.sh": "#!/usr/bin/env bash\n# declare -A is what broke on macOS\ntrue\n"},
         expect_fail=False),

    # ---- check_shell_vars: the fix was in the file, unused --------------
    Case("check_shell_vars.py", "the historical shape: assigned, never read",
         {"tests/x.sh": '#!/usr/bin/env bash\n'
                        'PROFILE="$ROOT/profiles/fixtures/unknown-host.json"\n'
                        'echo done\n'},
         expect_fail=True, expect_text="`PROFILE` is assigned and never read"),

    Case("check_shell_vars.py", "the same variable, once used, passes",
         {"tests/x.sh": '#!/usr/bin/env bash\n'
                        'PROFILE="$ROOT/profiles/fixtures/unknown-host.json"\n'
                        'echo "$PROFILE"\n'},
         expect_fail=False),

    # The false positive that would have made this guard unusable: a prefix
    # assignment sets a CHILD's environment, and there is nothing to read in
    # this script.
    Case("check_shell_vars.py", "a FOO=bar prefix assignment is not a variable",
         {"tests/x.sh": '#!/usr/bin/env bash\n'
                        'GT_MANIFEST="$WORK/m.json" GT_CASES="$WORK/c" bash run.sh\n'},
         expect_fail=False),

    Case("check_shell_vars.py", "an exported variable may be read outside",
         {"tests/x.sh": '#!/usr/bin/env bash\nexport RS_PROFILE=/tmp/p.json\n'},
         expect_fail=False),

    Case("check_shell_vars.py", "a name mentioned only in a comment is not a use",
         {"tests/x.sh": '#!/usr/bin/env bash\n'
                        'PROFILE=/tmp/p.json\n'
                        '# PROFILE is where the synthetic host lives\n'
                        'echo done\n'},
         expect_fail=True, expect_text="never read"),

    Case("check_shell_vars.py", "a for-loop variable is bound, not assigned",
         {"tests/x.sh": '#!/usr/bin/env bash\nfor key in a b; do echo hi; done\n'},
         expect_fail=False),

    # ---- check_todo: the compass and the map must agree ----------------
    Case("check_todo.py", "an open plan criterion with no owner fails",
         {"docs/TODO.md": TODO_OK,
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[open]` the probe\n"},
         expect_fail=True, expect_text="with no owner"),

    Case("check_todo.py", "the same criterion, owned, passes",
         {"docs/TODO.md": TODO_OK,
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[open]` the probe (T-001)\n"},
         expect_fail=False),

    Case("check_todo.py", "a plan tag naming a missing item fails",
         {"docs/TODO.md": TODO_OK,
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[open]` the probe (T-404)\n"},
         expect_fail=True, expect_text="not an item in docs/TODO.md"),

    Case("check_todo.py", "a fourth item in Now fails",
         {"docs/TODO.md": TODO_OK + "".join(
             f"\n### T-1{n:02d} — filler `[now]`\n\n**Done when:** it runs\n"
             for n in range(3)),
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[done]` nothing\n"},
         expect_fail=True, expect_text="is not a compass"),

    Case("check_todo.py", "an item with no \"Done when\" fails",
         {"docs/TODO.md": "# TODO\n\n## Now\n\n### T-001 — vague `[now]`\n\n"
                          "We should probably improve things.\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[done]` nothing\n"},
         expect_fail=True, expect_text="is a mood"),

    Case("check_todo.py", "a blocked item with no blocker named fails",
         {"docs/TODO.md": "# TODO\n\n## Blocked\n\n### T-001 — waiting `[blocked]`\n\n"
                          "**Done when:** it runs\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[done]` nothing\n"},
         expect_fail=True, expect_text="is an excuse"),

    Case("check_todo.py", "a done item the progress log never mentions fails",
         {"docs/TODO.md": "# TODO\n\n## Done\n\n### T-001 — finished `[done]`\n\n"
                          "**Done when:** it ran\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[done]` nothing\n",
          "docs/PROGRESS.md": "# Progress\n\nNothing about it.\n"},
         expect_fail=True, expect_text="never mentions it"),

    Case("check_todo.py", "the same item, recorded in the log, passes",
         {"docs/TODO.md": "# TODO\n\n## Done\n\n### T-001 — finished `[done]`\n\n"
                          "**Done when:** it ran\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[done]` nothing\n",
          "docs/PROGRESS.md": "# Progress\n\nT-001 shipped, and here is what "
                              "it taught.\n"},
         expect_fail=False),

    Case("check_todo.py", "a [now] item under ## Next fails",
         {"docs/TODO.md": "# TODO\n\n## Now\n\n## Next\n\n"
                          "### T-001 — mislaid `[now]`\n\n"
                          "**Done when:** `ctest` passes.\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[done]` nothing\n"},
         expect_fail=True, expect_text="belongs under `## Now`"),

    Case("check_todo.py", "a recorded pending promotion makes it an open decision",
         {"docs/TODO.md": "# TODO\n\n## Now\n\n"
                          "<!-- pending-promotion: T-001 -->\n\n## Next\n\n"
                          "### T-001 — mislaid `[now]`\n\n"
                          "**Done when:** `ctest` passes.\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[done]` nothing\n"},
         expect_fail=False, expect_text="open decision"),

    Case("check_todo.py", "a marker in its right section passes",
         {"docs/TODO.md": "# TODO\n\n## Now\n\n"
                          "### T-001 — in place `[now]`\n\n"
                          "**Done when:** `ctest` passes.\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[done]` nothing\n"},
         expect_fail=False),

    Case("check_todo.py", "(untracked) needs a section that justifies it",
         {"docs/TODO.md": "# TODO\n\n## Now\n\n### T-001 — a thing `[now]`\n\n"
                          "**Done when:** `ctest` passes.\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[open]` later (untracked)\n"},
         expect_fail=True, expect_text="no \"Deliberately not tracked\""),
]


def plan_agrees() -> str:
    """`docs/PLAN.md` states this file's case count. Nothing was checking it.

    It said "25 cases" while there were 58. It had already survived 48 and 52,
    because a number in prose is a claim and prose is not compiled - the same
    reason `check_campaign.py` exists for measured numbers. The count is checked
    here rather than in a separate guard because the only thing that reliably
    knows it is this file.
    """
    plan = ROOT / "docs" / "PLAN.md"
    if not plan.exists():
        return ""
    m = re.search(r"tools/guards/selftest\.py`,\s*(\d+)\s*cases", plan.read_text())
    if not m:
        return ("docs/PLAN.md no longer states a case count for "
                "`tools/guards/selftest.py`; either restore it or drop this "
                "check deliberately")
    stated = int(m.group(1))
    if stated != len(CASES):
        return (f"docs/PLAN.md says `tools/guards/selftest.py`, {stated} cases; "
                f"there are {len(CASES)}. A number in prose is a claim like any "
                f"other.")
    return ""


def main() -> int:
    passed, failures = 0, []
    for case in CASES:
        ok, why = case.run()
        if ok:
            passed += 1
            if VERBOSE:
                print(f"  ok   [{case.guard}] {case.name}")
        else:
            failures.append(f"  FAIL [{case.guard}] {case.name}\n        {why}")

    drift = plan_agrees()
    if drift:
        failures.append(f"  FAIL [docs/PLAN.md] states this file's case count\n"
                        f"        {drift}")

    print(f"\nguard selftest: {passed}/{len(CASES)} cases")
    if failures:
        print(f"\n{len(failures)} guard(s) did not behave as specified:",
              file=sys.stderr)
        for f in failures:
            print(f, file=sys.stderr)
        print("\nA guard that cannot be made to fail on demand is not "
              "protecting anything.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
