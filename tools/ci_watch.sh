#!/usr/bin/env bash
# tools/ci_watch.sh <full commit sha> [--event push|pull_request] [--branch <name>]
#
# Follows the `ci` workflow's run for a commit until it is green or cannot
# be: one line whenever a job starts, ends or fails (with the step that
# failed), and the failed jobs re-run up to twice once the run has completed
# (GitHub re-runs failed jobs only then). Exit 0: the run is green. Exit 1:
# not green after two re-runs. One look every ICO_CI_WATCH_S seconds
# (default 300): `gh run watch` polls until GitHub's rate limit answers 403
# to every gh call for ten minutes.
set -u
R=nathanialf/ico-pc
SHA=${1:?usage: tools/ci_watch.sh <full commit sha> [--event E] [--branch B]}
shift
EVENT=push
BRANCH=main
while [ $# -gt 0 ]; do
    case $1 in
    --event) EVENT=$2; shift 2 ;;
    --branch) BRANCH=$2; shift 2 ;;
    *) echo "ci_watch: unknown argument $1" >&2; exit 2 ;;
    esac
done
EVERY=${ICO_CI_WATCH_S:-300}
case $SHA in
????????????????????????????????????????) ;;
*) echo "ci_watch: a full 40-character sha, not \"$SHA\"" >&2; exit 2 ;;
esac

now() { date -u +%H:%MZ; }
id=""
while [ -z "$id" ]; do
    id=$(gh -R $R run list --workflow ci.yml --event "$EVENT" --branch "$BRANCH" --limit 10 \
        --json databaseId,headSha -q ".[] | select(.headSha==\"$SHA\") | .databaseId" 2>/dev/null | head -1)
    [ -n "$id" ] || sleep 30
done
echo "$(now) run $id: https://github.com/$R/actions/runs/$id"

# the jobs as "name<TAB>state<TAB>failed steps", state = conclusion or status
jobs() {
    gh -R $R run view "$id" --json jobs -q '.jobs[] | [.name, (if .conclusion != "" and .conclusion != null then .conclusion else .status end), ([.steps[]? | select(.conclusion == "failure") | .name] | join(", "))] | @tsv' 2>/dev/null
}
prev=""
round=0
while :; do
    cur=$(jobs) || cur=""
    if [ -n "$cur" ]; then
        # what changed since the last look
        while IFS=$'\t' read -r name state steps; do
            [ -n "$name" ] || continue
            was=$(printf '%s\n' "$prev" | awk -F'\t' -v n="$name" '$1 == n { print $2 }')
            if [ "$state" != "$was" ]; then
                case $state in
                failure | cancelled | timed_out) echo "$(now) $name: $state${steps:+ in \"$steps\"}" ;;
                success | in_progress) echo "$(now) $name: $state" ;;
                esac
            fi
        done <<< "$cur"
        prev=$cur
    fi
    s=$(gh -R $R run view "$id" --json status,conclusion -q '.status + " " + (.conclusion // "")' 2>/dev/null) || s=""
    case $s in
    "completed success")
        echo "$(now) run $id: green${round:+ after $round re-run(s)}"
        exit 0
        ;;
    completed*)
        if [ "$round" -ge 2 ]; then
            echo "$(now) run $id: $s after two re-runs: not green"
            exit 1
        fi
        round=$((round + 1))
        echo "$(now) run $id: $s; re-running the failed jobs (re-run $round of 2)"
        gh -R $R run rerun "$id" --failed || { echo "$(now) the re-run was refused"; exit 1; }
        sleep 90
        ;;
    esac
    sleep "$EVERY"
done
