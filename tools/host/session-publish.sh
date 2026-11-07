#!/usr/bin/env bash
# Publish the complete source tree at the end of a task round.
#
# This is the EXIT-trap wrapper for workers and interactive sessions. It runs
# the given command and publishes afterwards even when the command fails. If no
# command is given it publishes immediately.
#
#   tools/host/session-publish.sh --task-id ID --result completed -- make test
#   tools/host/session-publish.sh --task-id ID --result failed --note "..." 
#
# Publication is never skipped because tests failed; the result is metadata.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
task_id=""
result="incomplete"
note=""
build_status="not-run"
test_status="not-run"
test_results=""
verify_full=()

usage() {
    echo "usage: $0 --task-id ID [--result completed|failed|incomplete] [--note TEXT] [--build-status S] [--test-status S] [--verify-full] [-- <command...>]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --task-id) shift; [ "$#" -gt 0 ] || usage; task_id="$1"; shift ;;
        --result) shift; [ "$#" -gt 0 ] || usage; result="$1"; shift ;;
        --note) shift; [ "$#" -gt 0 ] || usage; note="$1"; shift ;;
        --build-status) shift; [ "$#" -gt 0 ] || usage; build_status="$1"; shift ;;
        --test-status) shift; [ "$#" -gt 0 ] || usage; test_status="$1"; shift ;;
        --test-results) shift; [ "$#" -gt 0 ] || usage; test_results="$1"; shift ;;
        --verify-full) verify_full=(--verify-full); shift ;;
        --) shift; break ;;
        -h|--help) usage ;;
        *) break ;;
    esac
done

[ -n "$task_id" ] || usage
case "$result" in completed|failed|incomplete|unknown) ;; *) usage ;; esac

publish() {
    local args=(
        --task-id "$task_id"
        --result "$result"
        --note "$note"
        --build-status "$build_status"
        --test-status "$test_status"
    )
    if [ -n "$test_results" ]; then args+=(--test-results "$test_results"); fi
    if [ "${#verify_full[@]}" -gt 0 ]; then args+=("${verify_full[@]}"); fi
    ( cd "$ROOT" && python3 tools/host/mcp-publish.py "${args[@]}" )
}

if [ "$#" -eq 0 ]; then
    publish
    exit $?
fi

finish() {
    local rc=$?
    trap - EXIT INT TERM
    if [ "$rc" -ne 0 ] && [ "$result" = "completed" ]; then
        result="failed"
    elif [ "$result" = "incomplete" ]; then
        [ "$rc" -eq 0 ] && result="completed"
    fi
    publish
    local prc=$?
    if [ "$prc" -ne 0 ]; then
        echo "session-publish: source publication FAILED" >&2
    fi
    if [ "$rc" -eq 0 ] && [ "$prc" -ne 0 ]; then
        exit "$prc"
    fi
    exit "$rc"
}
trap finish EXIT
trap 'exit 130' INT TERM

"$@"
