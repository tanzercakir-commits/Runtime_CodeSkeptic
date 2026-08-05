#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A standard-library symbol must come from a header this file actually includes.

The first time this project ran on a real Windows host, the build failed:

    src/vm/impact.cpp(89,47): error C2039: 'back_inserter': is not a member of
                              'std'                          MSVC 19.51.36248.0

`impact.cpp` included `<algorithm>` and `<set>` and used `std::back_inserter`,
which is declared in `<iterator>`. libstdc++ and libc++ hand it over
transitively; MSVC does not. Every local build, every mingw cross-build and both
macOS runners had been green for as long as the file existed.

That is a whole class of defect this project cannot see from where it is
written, and it costs a Windows runner minute to find. So it is checked here
instead, where it costs nothing.

WHAT MAKES THIS WORTH HAVING RATHER THAN NOISY.

A first attempt just grepped for symbols and reported any file that did not
include the declaring header. That produced 42 hits, and almost all of them were
correct code: a `.cpp` including its own `.hpp`, which includes `<string>`, is
not relying on an accident - it is relying on a header it owns.

So the include chain is RESOLVED. Every `#include "runtimeskeptic/..."` is
followed, recursively, and the standard headers those project headers pull in
count as available. On this repository the 42 collapse to ONE - and it is
exactly the one MSVC failed on. Zero false positives against a compiler's
verdict is the bar a guard like this has to clear, and this one clears it.

DELIBERATELY NARROW. The table below is not a model of the standard library. It
lists symbols whose declaring header is unambiguous and which are commonly
obtained by accident. A symbol that is not listed is not checked, and that is
the right trade: this guard exists to catch a known, expensive class, not to
reimplement include-what-you-use.

It also does NOT claim to predict MSVC. It reports a file using a symbol whose
header is nowhere in its include graph. That is worth fixing whether or not any
particular compiler currently complains.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STD_INCLUDE = re.compile(r'#include\s+<([a-z_]+)>')
OWN_INCLUDE = re.compile(r'#include\s+"(runtimeskeptic/[A-Za-z0-9_./]+)"')

# symbol pattern -> the header that declares it.
NEEDS = {
    "iterator":   [r"std::back_inserter", r"std::front_inserter", r"std::inserter",
                   r"std::distance\b", r"std::advance\b", r"std::istream_iterator",
                   r"std::ostream_iterator"],
    "numeric":    [r"std::accumulate", r"std::iota", r"std::reduce",
                   r"std::inner_product"],
    "algorithm":  [r"std::sort\b", r"std::stable_sort", r"std::any_of",
                   r"std::all_of", r"std::none_of", r"std::count_if",
                   r"std::set_difference", r"std::set_intersection",
                   r"std::max_element", r"std::min_element", r"std::transform\b",
                   r"std::unique\b", r"std::remove_if", r"std::find_if"],
    "functional": [r"std::function<", r"std::bind\(", r"std::hash<"],
    "memory":     [r"std::unique_ptr", r"std::shared_ptr", r"std::make_unique",
                   r"std::make_shared"],
    "cstdint":    [r"std::uint64_t", r"std::int64_t", r"std::uint32_t",
                   r"std::int32_t", r"std::uint8_t"],
    "optional":   [r"std::optional", r"std::nullopt"],
    "string":     [r"std::string\b", r"std::to_string", r"std::stoull"],
    "string_view": [r"std::string_view"],
    "vector":     [r"std::vector<"],
    "set":        [r"std::set<"],
    "map":        [r"std::map<", r"std::unordered_map<"],
    "sstream":    [r"std::ostringstream", r"std::istringstream"],
    "cstring":    [r"std::memcpy", r"std::memcmp", r"std::strlen"],
    "cmath":      [r"std::floor\(", r"std::ceil\(", r"std::fabs\("],
}

SEARCH = ["src/**/*.cpp", "include/**/*.hpp", "tools/**/*.cpp", "tools/**/*.hpp"]

# A file may name a symbol inside a comment explaining this very guard.
SKIP_COMMENT_ONLY = re.compile(r"^\s*(//|\*|/\*)")


def std_headers_reachable(path: Path, seen=None) -> set:
    """Standard headers this file gets, directly or through project headers."""
    if seen is None:
        seen = set()
    path = path.resolve()
    if path in seen or not path.exists():
        return set()
    seen.add(path)
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return set()
    out = set(STD_INCLUDE.findall(text))
    for own in OWN_INCLUDE.findall(text):
        out |= std_headers_reachable(ROOT / "include" / own, seen)
    return out


def uses(text: str, pattern: str):
    """First non-comment line using the pattern, or None."""
    rx = re.compile(pattern)
    for i, line in enumerate(text.splitlines(), 1):
        if SKIP_COMMENT_ONLY.match(line):
            continue
        m = rx.search(line)
        if m:
            return i, m.group(0)
    return None


def main() -> int:
    problems = []
    scanned = 0

    files = []
    for pattern in SEARCH:
        files.extend(ROOT.glob(pattern))

    for path in sorted(set(files)):
        rel = path.relative_to(ROOT).as_posix()
        available = std_headers_reachable(path)
        text = path.read_text(encoding="utf-8")
        scanned += 1
        for header, patterns in NEEDS.items():
            if header in available:
                continue
            for pattern in patterns:
                found = uses(text, pattern)
                if found is None:
                    continue
                line, symbol = found
                problems.append(
                    f"{rel}:{line}: uses `{symbol}` and no header in its "
                    f"include graph provides `<{header}>`. libstdc++ and libc++ "
                    f"often supply it transitively; MSVC does not, and this "
                    f"class of defect is invisible on the platforms this "
                    f"project builds on locally.")
                break

    print(f"includes: {scanned} translation unit(s) and header(s) checked "
          f"against {len(NEEDS)} standard headers")
    if problems:
        print(f"\n{len(problems)} symbol(s) whose header is not in the include "
              f"graph:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
