#!/usr/bin/env bash
# Run bin/lsm-sim over every trace under traces/, writing results to
# logs/<trace-set>/<trace-name>/<POLICY>_<config>.log
#
# The simulator reads ./config and writes its .log files into the current
# working directory, so each trace is run from inside its own log directory
# with a copy of the config next to it.
#
# Env:
#   JOBS=N   run N traces concurrently (default 8)

set -uo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BIN="$ROOT/bin/lsm-sim"
CONFIG="$ROOT/config"
JOBS="${JOBS:-8}"

[[ -x "$BIN" ]] || { echo "error: $BIN not built (run make)" >&2; exit 1; }
[[ -f "$CONFIG" ]] || { echo "error: missing $CONFIG" >&2; exit 1; }

cfg() { awk -v k="$1" '$1==k {print $2; found=1} END {if (!found) exit 1}' "$CONFIG"; }

SUFFIX="_cs$(cfg cache_size)_dbs$(cfg default_block_size)_sc$(cfg shard_count)"
SUFFIX+="_look$(cfg optimal_lookahead)_mlook$(cfg modular_lookahead)_bpk$(cfg bits_per_key).log"

case "$(cfg mode)" in
    0) PREFIX="filter_" ;;
    1) PREFIX="data_" ;;
    *) echo "error: unexpected mode '$(cfg mode)' in $CONFIG" >&2; exit 1 ;;
esac

run_trace() {
    local trace="$1"
    local set_name trace_name log_dir
    set_name="$(basename -- "$(dirname -- "$trace")")"
    trace_name="$(basename -- "$trace" .zst)"
    log_dir="$ROOT/logs/$set_name/$PREFIX$trace_name"

    mkdir -p "$log_dir" || return 1
    cp -- "$CONFIG" "$log_dir/config" || return 1

    echo "[run ] $set_name/$trace_name"
    local start=$SECONDS status=0
    ( cd "$log_dir" && "$BIN" "$trace" ) > "$log_dir/run.out" 2>&1 || status=$?
    rm -f -- "$log_dir/config"

    if (( status != 0 )); then
        echo "[FAIL] $set_name/$trace_name (exit $status, see $log_dir/run.out)"
        return 1
    fi
    rm -f -- "$log_dir/run.out"   # progress chatter, only kept on failure
    echo "[done] $set_name/$trace_name ($((SECONDS - start))s)"
}

mapfile -t TRACES < <(find "$ROOT/traces" -type f -name '*.zst' | sort)
(( ${#TRACES[@]} )) || { echo "error: no .zst traces found under $ROOT/traces" >&2; exit 1; }

echo "${#TRACES[@]} trace(s), $JOBS job(s), suffix $SUFFIX"

# `wait -n` yields the exit status of whichever job finished, so every simulator
# failure is counted here rather than being swallowed by a bare `wait`.
failures=0
running=0
reap() {
    wait -n || failures=$((failures + 1))
    running=$((running - 1))
}

for trace in "${TRACES[@]}"; do
    while (( running >= JOBS )); do reap; done
    run_trace "$trace" &
    running=$((running + 1))
done
while (( running > 0 )); do reap; done

if (( failures )); then
    echo "finished with $failures failed trace(s)"
    exit 1
fi
echo "all traces finished; ${#TRACES[@]} trace(s) complete"
