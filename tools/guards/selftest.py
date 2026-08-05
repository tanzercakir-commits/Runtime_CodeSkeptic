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
                        target.write_text(content, encoding="utf-8", newline="")
                    self._git(root, "add", "-A")
                    self._git(root, "commit", "-q", "-m", "selftest", when=when)

            for rel, content in self.files.items():
                target = root / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                if content is None:          # a directory, or an empty file
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.write_text(content, encoding="utf-8", newline="")
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


# One probe implementation per platform, spelled correctly. The real tree's
# guards are longer; only the `#if` matters to this check.
OK_PROBES = {
    "src/probe/vm_probe_linux.cpp": "#if defined(RS_PLATFORM_LINUX)\n",
    "src/probe/vm_probe_macos.cpp": "#if defined(RS_PLATFORM_MACOS)\n",
    "src/probe/vm_probe_windows.cpp": "#if defined(RS_PLATFORM_WINDOWS)\n",
    "src/probe/vm_probe_unimplemented.cpp":
        "#if !defined(RS_PLATFORM_LINUX) && !defined(RS_PLATFORM_MACOS) && \\\n"
        "    !defined(RS_PLATFORM_WINDOWS)\n",
}

# A workflow with everything `check_workflow_ctest.py` looks at and nothing else:
# a build type to compare against, and one ctest invocation that names it.
WORKFLOW_OK = """name: CI
jobs:
  build:
    steps:
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
      - name: Diagnostics
        run: |
          ctest --test-dir build --build-config RelWithDebInfo --rerun-failed --output-on-failure > /tmp/diag/ctest.txt
"""

# `check_windows_compiles.py` needs a real cross-compiler. Where there is none it
# reports SKIPPED and passes, so the cases below adapt rather than lie about what
# was checked - a case that "passes" because the guard declined to look is the
# vacuous pass this whole file exists to prevent.
SHADOW_IS_CHECKABLE = any(
    shutil.which(c) for c in ("x86_64-w64-mingw32-g++", "x86_64-w64-mingw32-c++"))

# Just the warning line the guard reads. Not the project's CMakeLists: a fixture
# that copied it would drift from it.
CMAKE_WARNINGS = """add_library(rs_warnings INTERFACE)
target_compile_options(rs_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
"""

# The C4456 shape, reduced: an inner declaration hiding an outer one. Clean under
# -Wall -Wextra, an error under -Wshadow, which is the entire incident.
WINDOWS_TU_SHADOWED = """#if defined(RS_PLATFORM_WINDOWS)
int probe() {
    int walk = 1;
    {
        int walk = 2;
        return walk;
    }
}
#endif
"""

WINDOWS_TU_OK = """#if defined(RS_PLATFORM_WINDOWS)
int probe() {
    int walk = 1;
    {
        int arena = 2;
        return walk + arena;
    }
}
#endif
"""

# check_roadmap needs a ROADMAP, its recorded hash, and a PLAN that mirrors the
# phases. Hashes are computed here rather than pasted, so the fixture cannot rot.
import hashlib as _hashlib


def _sha(text: str) -> str:
    return _hashlib.sha256(text.encode()).hexdigest()


ROADMAP_OK = "# Spec\n\n## Phase 0 — begin\n\n## Phase 1 — continue\n"
PLAN_MIRROR_OK = "# Plan\n\nPhase 0  begin  DONE\nPhase 1  continue  OPEN\n"
PROGRESS_OK = "# Progress\n\nAppend-only. Newest first.\n\n---\n\n## 2026-08-01 - old\n\nKept.\n"
PROCESS_PLAN_OK = "# Stable project plan\n"
PROCESS_PIN_OK = _sha(PROCESS_PLAN_OK) + "  plan.md\n"
PROCESS_PLAN_V2 = PROCESS_PLAN_OK + "changed together with its pin\n"
PROCESS_PIN_V2 = _sha(PROCESS_PLAN_V2) + "  plan.md\n"
TODO_WITH_ITEM = "# TODO\n\n## Now\n\n### T-001 - ship\n"
TODO_EMPTY = "# TODO\n\n## Now\n"

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

    # THE GUARD FIRING ON A CORRECT TREE, which is the failure this whole
    # directory exists to prevent, committed by a guard rather than caught by
    # one. These documents describe their own markers, and every prose mention
    # was being counted as a criterion and made to carry evidence. Four real
    # instances, three of which predated the commit that noticed.
    Case("check_plan.py", "a marker mentioned mid-sentence is prose, not a claim",
         {"docs/PLAN.md": "# Plan and status\n\n## Phase 1\n\n"
                          "- `[open]` the probe\n\n"
                          "This line read `[done]` yesterday and was wrong.\n"},
         expect_fail=False),

    # ...and the rule it must not weaken.
    Case("check_plan.py", "a leading [done] with no evidence still fails",
         {"docs/PLAN.md": "# Plan and status\n\n## Phase 1\n\n"
                          "- `[done]` the probe works\n"},
         expect_fail=True, expect_text="no evidence"),

    # The continuation loop keyed on the same wrong pattern, so evidence sitting
    # after a prose mention was invisible and the entry looked unsupported.
    Case("check_plan.py", "evidence after a prose mention is still found",
         {"docs/PLAN.md": "# Plan and status\n\n## Phase 1\n\n"
                          "- `[done]` the probe works, and although this was\n"
                          "  `[partial]` last week it is now measured by\n"
                          "  `tools/guards/check_plan.py`\n"},
         expect_fail=False),

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

    # ---- check_profiles_fresh: a measurement older than its instrument -
    # An external reviewer found a committed macOS profile carrying a ceiling
    # bug two later commits had already fixed - the profile was never
    # regenerated, and no guard asked whether a measurement still matches the
    # probe that made it.
    Case("check_profiles_fresh.py",
         "a profile older than the probe that makes it is stale",
         {},
         commits=[("2026-07-25T12:00:00+00:00",
                   {"profiles/measured/x.measured.json":
                        '{"platform":{"os":"macos"},"virtual_memory":{}}',
                    "src/probe/vm_probe_macos.cpp": "// probe v1\n"}),
                  ("2026-07-27T12:00:00+00:00",
                   {"src/probe/vm_probe_macos.cpp":
                        "// probe v2 - ceiling measured correctly now\n"})],
         expect_fail=True, expect_text="STALE"),

    Case("check_profiles_fresh.py",
         "a profile regenerated after the probe change passes",
         {},
         commits=[("2026-07-25T12:00:00+00:00",
                   {"profiles/measured/x.measured.json":
                        '{"platform":{"os":"macos"},"virtual_memory":{}}',
                    "src/probe/vm_probe_macos.cpp": "// probe v1\n"}),
                  ("2026-07-27T12:00:00+00:00",
                   {"src/probe/vm_probe_macos.cpp": "// probe v2\n"}),
                  ("2026-07-28T12:00:00+00:00",
                   {"profiles/measured/x.measured.json":
                        '{"platform":{"os":"macos"},'
                        '"virtual_memory":{"note":"regenerated"}}'})],
         expect_fail=False),

    # An exempt profile (no CI job regenerates it) is stale by date and still
    # passes - the guard prints its reason instead of failing. The Wine profile
    # is the real instance; the guard's first CI run flagged it with Windows.
    Case("check_profiles_fresh.py",
         "an exempt profile with no CI regeneration path is not failed",
         {},
         commits=[("2026-07-25T12:00:00+00:00",
                   {"profiles/measured/wine-9.0-on-linux-x86_64.measured.json":
                        '{"platform":{"os":"windows"},"virtual_memory":{}}',
                    "src/probe/vm_probe_windows.cpp": "// probe v1\n"}),
                  ("2026-07-27T12:00:00+00:00",
                   {"src/probe/vm_probe_windows.cpp": "// probe v2\n"})],
         expect_fail=False, expect_text="exempt"),

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

    # An empty array's expansion is unbound in bash 3.2 under `set -u`. This is
    # the third instance of the class and the first one that is not a bash-4
    # FEATURE, so scanning for features could never have found it.
    Case("check_shell_portability.py", "an empty array expanded under set -u fails",
         {"tests/x.sh": '#!/usr/bin/env bash\nset -uo pipefail\nargs=()\n'
                        'args+=("x")\necho "${args[@]}"\n'},
         expect_fail=True, expect_text="bash 3.2 semantics"),

    Case("check_shell_portability.py", "the ${arr[@]+...} idiom passes",
         {"tests/x.sh": '#!/usr/bin/env bash\nset -uo pipefail\nargs=()\n'
                        'echo ${args[@]+"${args[@]}"}\n'},
         expect_fail=False),

    Case("check_shell_portability.py", "asking an empty array its length fails too",
         {"tests/x.sh": '#!/usr/bin/env bash\nset -u\nnames=()\n'
                        'if [ "${#names[@]}" -gt 0 ]; then echo hi; fi\n'},
         expect_fail=True, expect_text="bash 3.2 semantics"),

    # The false positive that would make the rule unusable: a literal table
    # cannot be empty, so its expansion is safe and must not be flagged.
    Case("check_shell_portability.py", "a non-empty literal array is not flagged",
         {"tests/x.sh": '#!/usr/bin/env bash\nset -uo pipefail\n'
                        'ROWS=("a|b" "c|d")\nfor r in "${ROWS[@]}"; do echo "$r"; done\n'},
         expect_fail=False),

    # Without `set -u` the expansion is merely empty, not an error.
    Case("check_shell_portability.py", "no set -u means no unbound-variable death",
         {"tests/x.sh": '#!/usr/bin/env bash\nargs=()\necho "${args[@]}"\n'},
         expect_fail=False),

    # ---- check_probe_platforms: exactly one implementation per platform --
    Case("check_probe_platforms.py", "the real bug: the stub forgot Windows",
         {**OK_PROBES,
          "src/probe/vm_probe_unimplemented.cpp":
              "#if !defined(RS_PLATFORM_LINUX) && !defined(RS_PLATFORM_MACOS)\n"},
         expect_fail=True, expect_text="RS_PLATFORM_WINDOWS: 2 implementation"),

    Case("check_probe_platforms.py", "the corrected set passes",
         OK_PROBES, expect_fail=False),

    Case("check_probe_platforms.py", "a platform with NO implementation fails",
         {**OK_PROBES,
          "src/probe/vm_probe_unimplemented.cpp":
              "#if defined(RS_PLATFORM_LINUX) && !defined(RS_PLATFORM_LINUX)\n"},
         expect_fail=True, expect_text="NONE"),

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

    # A here-doc body is data, often another language. A Python kwarg
    # `file=sys.stderr` inside one is not a shell assignment; scanning it flagged
    # the first script in this repo to embed a Python heredoc.
    Case("check_shell_vars.py", "a kwarg inside a heredoc body is not an assignment",
         {"tests/x.sh": '#!/usr/bin/env bash\n'
                        "python3 - <<'PY'\n"
                        'import sys\n'
                        'print("x", file=sys.stderr)\n'
                        'PY\n'
                        'echo done\n'},
         expect_fail=False),

    # ...but masking the body must not hide a real dead variable beside it, nor
    # swallow the whole file: DEAD is still caught, the heredoc's `val=1` is not.
    Case("check_shell_vars.py", "a dead var is still caught alongside a heredoc",
         {"tests/x.sh": '#!/usr/bin/env bash\n'
                        'DEAD="$ROOT/x"\n'
                        "python3 - <<'PY'\n"
                        'val=1\n'
                        'print(val)\n'
                        'PY\n'
                        'echo done\n'},
         expect_fail=True, expect_text="`DEAD` is assigned and never read"),

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

    # [partial] was the one unfinished state the guard never read, and it is
    # exactly the state this project reaches for when something is half-true.
    # Gate B sat [partial] on two grounds and one of them had no item on the
    # compass at all - nothing objected for as long as the gate existed.
    Case("check_todo.py", "a partial plan criterion with no owner fails",
         {"docs/TODO.md": TODO_OK,
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[partial]` the probe\n"},
         expect_fail=True, expect_text="with no owner"),

    Case("check_todo.py", "the same partial criterion, owned, passes",
         {"docs/TODO.md": TODO_OK,
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n"
                          "- `[partial]` the probe (T-001)\n"},
         expect_fail=False),

    # Found the same day, six lines below: Gate B was [partial] and cited a
    # [done] item. Read literally that says the work holding the gate open was
    # completed - so nobody owns it and nothing objects.
    Case("check_todo.py", "an unfinished criterion owned by a done item fails",
         {"docs/TODO.md": "# TODO\n\n## Done\n\n### T-001 — finished `[done]`\n\n"
                          "**Done when:** it ran\n",
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n- `[partial]` still open "
                          "(T-001)\n",
          "docs/PROGRESS.md": "# Progress\n\nT-001 shipped.\n"},
         expect_fail=True, expect_text="which is `[done]`"),

    # And the false positive that fix introduced, kept as a case: a status
    # QUOTED IN PROSE inside another criterion's body was read as a criterion
    # of its own. check_plan.py had the identical bug against `[done]`, so this
    # is the second time the same mistake was made in this directory.
    Case("check_todo.py", "a marker quoted in prose is not a criterion",
         {"docs/TODO.md": TODO_OK,
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n"
                          "- `[done]` the probe — it stays `[partial]` only in "
                          "the sense that\n  the prose below is not "
                          "machine-checked and never will be\n"},
         expect_fail=False),

    # The second false positive, five minutes after the first: a WRAPPED line
    # that happens to begin with a status. The criterion above lost its own tag
    # to the split and was reported as unowned - the guard breaking a document
    # that was correct.
    Case("check_todo.py", "a wrapped line beginning with a status is not one",
         {"docs/TODO.md": TODO_OK,
          "docs/PLAN.md": "# Plan\n\n## Phase 1\n\n"
                          "- `[partial]` the probe — measured every push, and\n"
                          "  `[partial]` only while the last bucket is a "
                          "backlog (T-001)\n"},
         expect_fail=False),

    # ---- check_workflow_ctest: the diagnostics channel's own blind spot --
    Case("check_workflow_ctest.py", "the real bug: diagnostics ctest with no -C",
         {".github/workflows/ci.yml": WORKFLOW_OK.replace(
             "ctest --test-dir build --build-config RelWithDebInfo "
             "--rerun-failed", "ctest --test-dir build --rerun-failed")},
         expect_fail=True, expect_text="no configuration"),

    Case("check_workflow_ctest.py", "the corrected workflow passes",
         {".github/workflows/ci.yml": WORKFLOW_OK}, expect_fail=False),

    # THE FALSE POSITIVE THAT THIS GUARD ACTUALLY HAD. The first version matched
    # `\bctest\b`, and `.` is a word boundary, so `tail /tmp/diag/ctest.txt`
    # counted as an invocation and five correct lines in this repository's own
    # workflows were reported as defects. A guard that fires on the fixed tree is
    # worse than no guard, because the next person turns it off.
    Case("check_workflow_ctest.py", "a FILE named ctest.txt is not an invocation",
         {".github/workflows/ci.yml": WORKFLOW_OK.replace(
             "ctest --test-dir build --build-config RelWithDebInfo "
             "--rerun-failed --output-on-failure > /tmp/diag/ctest.txt\n",
             "tail -c 100 /tmp/diag/ctest.txt > /tmp/diag/ctest_tail.txt\n")},
         expect_fail=False),

    # Prose about a command is not the command. Every guard in this directory
    # explains itself in comments, and several of those comments name ctest.
    Case("check_workflow_ctest.py", "a comment mentioning ctest is not a call",
         {".github/workflows/ci.yml": WORKFLOW_OK +
             "          # ctest without -C is the defect this rule is about\n"},
         expect_fail=False),

    # Naming a config nobody builds reports exactly as much as naming none.
    Case("check_workflow_ctest.py", "a config no CMAKE_BUILD_TYPE builds fails",
         {".github/workflows/ci.yml":
             WORKFLOW_OK.replace("--build-config RelWithDebInfo",
                                 "--build-config Debug")},
         expect_fail=True, expect_text="which no -DCMAKE_BUILD_TYPE="),

    # ---- check_process_contract: stable plan, live working records -------
    Case("check_process_contract.py", "the three-record contract passes",
         {"plan.md": PROCESS_PLAN_OK,
          "tools/guards/plan.sha256": PROCESS_PIN_OK,
          "docs/TODO.md": TODO_OK,
          "docs/PROGRESS.md": PROGRESS_OK},
         expect_fail=False),

    Case("check_process_contract.py", "editing the immutable plan fails",
         {"plan.md": PROCESS_PLAN_OK + "mutable status\n",
          "tools/guards/plan.sha256": PROCESS_PIN_OK,
          "docs/TODO.md": TODO_OK,
          "docs/PROGRESS.md": PROGRESS_OK},
         expect_fail=True, expect_text="immutable project plan"),

    Case("check_process_contract.py", "missing working records fail",
         {"plan.md": PROCESS_PLAN_OK,
          "tools/guards/plan.sha256": PROCESS_PIN_OK},
         expect_fail=True, expect_text="missing consumable TODO"),

    Case("check_process_contract.py", "plan and pin cannot move together",
         {"plan.md": PROCESS_PLAN_V2,
          "tools/guards/plan.sha256": PROCESS_PIN_V2,
          "docs/TODO.md": TODO_OK,
          "docs/PROGRESS.md": PROGRESS_OK},
         expect_fail=True, expect_text="committed baseline",
         commits=[("2026-08-01T12:00:00+00:00",
                   {"plan.md": PROCESS_PLAN_OK,
                    "tools/guards/plan.sha256": PROCESS_PIN_OK,
                    "docs/TODO.md": TODO_OK,
                    "docs/PROGRESS.md": PROGRESS_OK})]),

    # ---- check_roadmap: the spec is frozen, the map mirrors it ----------
    Case("check_roadmap.py", "the real risk: ROADMAP edited under a stale hash",
         {"ROADMAP.md": ROADMAP_OK + "a quiet extra promise\n",
          "tools/guards/roadmap.sha256": _sha(ROADMAP_OK) + "  ROADMAP.md\n",
          "docs/PLAN.md": PLAN_MIRROR_OK},
         expect_fail=True, expect_text="ROADMAP.md has changed"),

    Case("check_roadmap.py", "a matching hash and a mirroring plan pass",
         {"ROADMAP.md": ROADMAP_OK,
          "tools/guards/roadmap.sha256": _sha(ROADMAP_OK) + "  ROADMAP.md\n",
          "docs/PLAN.md": PLAN_MIRROR_OK},
         expect_fail=False),

    # No recorded hash means nothing pins the spec - that is a failure with
    # instructions, not a silent pass on first run.
    Case("check_roadmap.py", "a missing recorded hash fails loudly",
         {"ROADMAP.md": ROADMAP_OK, "docs/PLAN.md": PLAN_MIRROR_OK},
         expect_fail=True, expect_text="nothing pins the specification"),

    # A phase the plan stops mentioning is a criterion nobody grades any more.
    Case("check_roadmap.py", "a plan that lost a phase fails",
         {"ROADMAP.md": ROADMAP_OK,
          "tools/guards/roadmap.sha256": _sha(ROADMAP_OK) + "  ROADMAP.md\n",
          "docs/PLAN.md": "# Plan\n\nPhase 0  begin  DONE\n"},
         expect_fail=True, expect_text="Phase 1"),

    # ---- check_progress_history: additions survive, history is immutable --
    Case("check_progress_history.py", "editing an old progress entry fails",
         {"docs/PROGRESS.md": PROGRESS_OK.replace("Kept.", "Rewritten.")},
         expect_fail=True, expect_text="old PROGRESS content was edited",
         commits=[("2026-08-01T12:00:00+00:00",
                   {"docs/PROGRESS.md": PROGRESS_OK})]),

    Case("check_progress_history.py", "a newest-first session prepend passes",
         {"docs/PROGRESS.md": PROGRESS_OK.replace(
             "---\n", "---\n\n## 2026-08-02 - new\n\nAdded only.\n", 1)},
         expect_fail=False,
         commits=[("2026-08-01T12:00:00+00:00",
                   {"docs/PROGRESS.md": PROGRESS_OK})]),

    Case("check_progress_history.py", "consumed TODO needs new progress evidence",
         {"docs/TODO.md": TODO_EMPTY,
          "docs/PROGRESS.md": PROGRESS_OK},
         expect_fail=True, expect_text="consumed without a new PROGRESS block",
         commits=[("2026-08-01T12:00:00+00:00",
                   {"docs/TODO.md": TODO_WITH_ITEM,
                    "docs/PROGRESS.md": PROGRESS_OK})]),

    Case("check_progress_history.py", "consumed TODO named in new progress passes",
         {"docs/TODO.md": TODO_EMPTY,
          "docs/PROGRESS.md": PROGRESS_OK.replace(
              "---\n", "---\n\n## 2026-08-02 - consume T-001\n\nCompleted T-001.\n", 1)},
         expect_fail=False,
         commits=[("2026-08-01T12:00:00+00:00",
                   {"docs/TODO.md": TODO_WITH_ITEM,
                    "docs/PROGRESS.md": PROGRESS_OK})]),

    Case("check_progress_history.py", "shallow history fails closed",
         {"docs/PROGRESS.md": PROGRESS_OK,
          ".git/shallow": "0000000000000000000000000000000000000000\n"},
         expect_fail=True, expect_text="shallow checkout"),

    # The line continuation matters: the flag is usually on the NEXT line.
    Case("check_workflow_ctest.py", "a flag on a continuation line still counts",
         {".github/workflows/ci.yml":
             "name: CI\njobs:\n  t:\n    steps:\n      - run: |\n"
             "          cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo\n"
             "          ctest --test-dir build \\\n"
             "            --build-config RelWithDebInfo --output-on-failure\n"},
         expect_fail=False),

    # ---- check_windows_compiles: the flags must be READ, not restated -----
    #
    # These cases pass trivially where no mingw is installed, which is honest:
    # the guard says SKIPPED and returns 0 there. On CI, where it is installed,
    # they are the real thing.
    Case("check_windows_compiles.py", "the real bug: a shadowed local, C4456",
         {"CMakeLists.txt": CMAKE_WARNINGS,
          "src/probe/vm_probe_windows.cpp": WINDOWS_TU_SHADOWED},
         expect_fail=SHADOW_IS_CHECKABLE,
         expect_text="shadows" if SHADOW_IS_CHECKABLE else "windows cross-compile"),

    Case("check_windows_compiles.py", "the corrected translation unit passes",
         {"CMakeLists.txt": CMAKE_WARNINGS,
          "src/probe/vm_probe_windows.cpp": WINDOWS_TU_OK},
         expect_fail=False),

    # THE POINT OF THE GUARD, not an incidental feature. The push that provoked
    # it was cross-compiled by hand with a flag list typed from memory; the one
    # flag left out was the one that mattered. A guard holding its own copy of
    # the list would fail the same way, one release later.
    Case("check_windows_compiles.py", "flags it cannot read are not guessed",
         {"CMakeLists.txt": "add_library(rs_warnings INTERFACE)\n",
          "src/probe/vm_probe_windows.cpp": WINDOWS_TU_OK},
         expect_fail=SHADOW_IS_CHECKABLE,
         expect_text="guesses them" if SHADOW_IS_CHECKABLE
                     else "windows cross-compile"),
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
    m = re.search(r"tools/guards/selftest\.py`,\s*(\d+)\s*cases", plan.read_text(encoding="utf-8"))
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
