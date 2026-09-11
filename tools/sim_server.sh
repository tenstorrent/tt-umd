#!/usr/bin/env bash
# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# Front door for the sim_server tool.
#
#   sim_server.sh start <simulator.so | rtl-dir>   start a host in the background, wait until it is
#                                                  actually serving, and report where its log went
#   sim_server.sh prune                            remove the directories of servers that are gone
#   sim_server.sh list | kill <server> | --help    forwarded to sim_server unchanged
#
# Only `start` and `prune` need process management -- detaching from the terminal, a unique log per
# server, checking that startup worked, and clearing up after a host that died without doing so
# itself. list and kill run and exit, so they go straight through, which keeps this script from
# restating an interface the binary already documents.

set -euo pipefail

# The binary sits beside this script once installed; override for a build tree elsewhere.
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
sim_server=${SIM_SERVER_BIN:-$here/sim_server}
if [ ! -x "$sim_server" ]; then
    echo "sim_server binary not found at $sim_server (set SIM_SERVER_BIN)" >&2
    exit 1
fi

tmp=${TMPDIR:-/tmp}

# A host removes its own directory when it shuts down gracefully, so anything left behind belongs to
# one that was killed or crashed. `list` already probes every socket, so it can say which: a server
# is gone when none of its chips answer -- reported per chip as `unreachable` (socket file, nobody
# home) or `empty` (directory, no socket at all).
#
# Note `empty` is also how a host that is still coming up looks, so do not prune while starting one.
prune() {
    local listing index path directory pruned=0
    listing=$("$sim_server" list)
    # Column 1 is the server index, 3 the state, 5 the socket path (or the directory, when `empty`).
    # Skip the header row, and keep an index only if no chip of it came back live.
    while read -r index path; do
        case $path in
            *.sock) directory=$(dirname "$path") ;;
            *) directory=$path ;;
        esac
        # Never rm -rf anything that is not a server directory, however the parse went.
        case $(basename "$directory") in
            tt-umd-sim-server-*) ;;
            *) continue ;;
        esac
        rm -rf "$directory"
        rm -f "$tmp/sim_server-$(basename "$directory").log"
        echo "pruned server $index ($directory)"
        pruned=$((pruned + 1))
    done < <(awk 'NR > 1 { seen[$1] = $5; if ($3 == "live") live[$1] = 1 }
                  END { for (i in seen) if (!(i in live)) print i, seen[i] }' <<< "$listing")

    if [ "$pruned" -eq 0 ]; then
        echo "Nothing to prune."
    fi
}

# Anything that is not handled here -- including no arguments at all, which prints usage -- is the
# binary's own command, forwarded untouched.
case ${1:-} in
    start) shift ;;
    prune) prune; exit 0 ;;
    *) exec "$sim_server" "$@" ;;
esac

if [ $# -ne 1 ]; then
    echo "usage: $(basename "$0") start <simulator.so | rtl-dir>" >&2
    exit 2
fi
simulator=$1

# Unique from the start, since the server's identity isn't known until it reports one below.
log=$(mktemp "$tmp/sim_server-XXXXXX.log")

# nohup makes the host ignore SIGHUP, so it survives the terminal closing.
nohup "$sim_server" start "$simulator" < /dev/null > "$log" 2>&1 &
pid=$!

# `sim_server start` prints "server directory: <dir>" on stdout once it has claimed one, and its
# sockets are serving by the time the Cluster is up right after. Parsing that line rather than a log
# line keeps this independent of the log format.
ticks=50 # 50 * 0.2s = 10s
for _ in $(seq $ticks); do
    directory=$(sed -n 's/^server directory: //p' "$log" | tail -n1)
    if [ -n "$directory" ]; then
        # Renaming is safe: the host holds an open fd, so it keeps writing to the same file.
        final_log="$tmp/sim_server-$(basename "$directory").log"
        mv "$log" "$final_log"
        echo "sim_server up: pid $pid, serving $directory, log $final_log"
        exit 0
    fi
    if ! kill -0 "$pid" 2> /dev/null; then
        echo "sim_server exited during startup:" >&2
        cat "$log" >&2
        exit 1
    fi
    sleep 0.2
done

echo "sim_server did not start serving within $((ticks / 5))s:" >&2
cat "$log" >&2
kill "$pid" 2> /dev/null || true
exit 1
