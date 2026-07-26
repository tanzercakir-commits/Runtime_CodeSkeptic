#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Publish measurement artifacts to a git ref, as a commit containing ONLY them.
#
#   tools/ci/publish_measurement.sh <namespace> <slug> <sha> <file>...
#
#     -> refs/<namespace>/<sha>/<slug>
#
# WHY THIS EXISTS.
#
# On 2026-07-26 the macOS runner went green for the first time. Inside that green
# job, `file-map-beyond-eof` and `file-map-partial-page` produced output on macOS
# for the first time in the project's history - whether native arm64 raises SIGBUS
# where Rosetta 2 hands back zeroes is the question those cases exist to answer,
# and nobody had ever measured it.
#
# It was not readable. `refs/ci-logs/*` publishes `if: failure()`, so the run that
# finally produced the observation was the run that discarded it. The channel was
# built to diagnose failures and does that well; a first-ever platform measurement
# arriving inside a SUCCESSFUL job is the case the design missed.
#
# WHY NOT REUSE THE ci-logs STEP. That one commits on top of the checked-out tree,
# so every diagnostics ref carries a full copy of the repository - acceptable for a
# rare failure, wasteful for something published on every green push. This builds
# the commit with a temporary index and `git commit-tree`, so the ref holds exactly
# the files named on the command line and nothing else. The working tree is never
# touched, which also means it cannot disturb a build that is still running.
#
# Reading one back:
#
#   git ls-remote origin "refs/measurements/<sha>/*"
#   git fetch origin "refs/measurements/<sha>/<slug>:refs/tmp/m"
#   git show refs/tmp/m:gt_run.txt
#
# Set RS_PUBLISH_DRY_RUN=1 to build the commit and print the tree without pushing.
set -uo pipefail

if [ "$#" -lt 4 ]; then
    echo "usage: $0 <namespace> <slug> <sha> <file>..." >&2
    exit 64
fi

namespace="$1"; shift
slug="$1"; shift
sha="$1"; shift

# A ref path component must not contain the characters git forbids, and `slug`
# comes from a workflow matrix name.
slug="$(printf '%s' "$slug" | tr -c 'A-Za-z0-9._-' '-')"
case "$namespace" in
    */*|""|.*) echo "$0: bad namespace '$namespace'" >&2; exit 64 ;;
esac

index="$(mktemp -u)"
trap 'rm -f "$index"' EXIT
GIT_INDEX_FILE="$index"
export GIT_INDEX_FILE

added=0
for path in "$@"; do
    if [ ! -f "$path" ]; then
        echo "$0: no $path, skipping" >&2
        continue
    fi
    blob="$(git hash-object -w "$path")" || exit 70
    # Basename only: the ref is a flat bag of artifacts, not a source tree, and a
    # /tmp path in a published ref would be noise at best.
    git update-index --add --cacheinfo "100644,$blob,$(basename "$path")" || exit 70
    added=$((added + 1))
done

if [ "$added" -eq 0 ]; then
    echo "$0: nothing to publish" >&2
    exit 65
fi

tree="$(git write-tree)" || exit 70

# commit-tree needs an identity and will not take one from the empty config a
# runner may have. Passed in the environment so nothing in the repository's own
# git config is modified.
GIT_AUTHOR_NAME="${GIT_AUTHOR_NAME:-ci-measurements}"
GIT_AUTHOR_EMAIL="${GIT_AUTHOR_EMAIL:-ci-measurements@localhost}"
GIT_COMMITTER_NAME="${GIT_COMMITTER_NAME:-ci-measurements}"
GIT_COMMITTER_EMAIL="${GIT_COMMITTER_EMAIL:-ci-measurements@localhost}"
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL

commit="$(git commit-tree "$tree" -m "${slug} measurements @ ${sha}")" || exit 70

echo "$added artifact(s) -> refs/${namespace}/${sha}/${slug}"
git ls-tree --name-only "$tree" | sed 's/^/  /'

if [ -n "${RS_PUBLISH_DRY_RUN:-}" ]; then
    echo "(dry run: not pushed)"
    exit 0
fi

git push --force origin "${commit}:refs/${namespace}/${sha}/${slug}"
