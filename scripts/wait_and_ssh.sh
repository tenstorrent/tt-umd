#!/usr/bin/env bash
# Wait for a dedicated lab machine to be free, then open an interactive
# SSH session.
#
# Usage: wait_and_ssh.sh [host]
#   host defaults to bgd-lab-06; must resolve via ~/.ssh/config.
#
# A single SSH probe per poll reports three signals:
#   * Runner.Worker count   -> active GitHub Actions jobs (wait for these)
#   * who, minus self       -> other interactive users (abort, do not wait)
#   * docker ps             -> any running container       (abort, do not wait)
#
# Runner.Listener is the long-lived idle daemon and is intentionally not
# matched; only the per-job Runner.Worker counts as "CI busy".

set -euo pipefail

HOST="${1:-bgd-lab-06}"
TIMEOUT_SEC=$((60 * 60))
POLL_SEC=60
SSH_OPTS=(-T -o BatchMode=yes -o ConnectTimeout=10)

remote_status() {
    ssh "${SSH_OPTS[@]}" "$HOST" bash -s <<'REMOTE'
me=$(whoami)
echo '---WORKERS---'
pgrep -fc 'Runner\.Worker' || echo 0
echo '---OTHERS---'
who | awk -v me="$me" '$1 != me'
echo '---CONTAINERS---'
docker ps --format '{{.Names}} ({{.Image}}) [{{.Status}}]' 2>/dev/null
REMOTE
}

extract() {
    awk -v sec="---$1---" '
        $0 == sec { f = 1; next }
        /^---[A-Z]+---$/ { f = 0 }
        f
    ' <<<"$2"
}

report_occupied() {
    local others="$1" containers="$2"
    {
        echo "$HOST is occupied; not connecting."
        if [ -n "$others" ]; then
            echo "Other users logged in:"
            sed 's/^/  /' <<<"$others"
        fi
        if [ -n "$containers" ]; then
            echo "Running docker containers:"
            sed 's/^/  /' <<<"$containers"
        fi
    } >&2
}

start=$(date +%s)
while :; do
    if ! status=$(remote_status); then
        echo "[$(date +%H:%M:%S)] $HOST: SSH probe failed; retrying in ${POLL_SEC}s..." >&2
        sleep "$POLL_SEC"
        continue
    fi

    workers=$(extract WORKERS "$status" | tr -d '[:space:]')
    workers=${workers:-0}
    others=$(extract OTHERS "$status" | sed '/^$/d')
    containers=$(extract CONTAINERS "$status" | sed '/^$/d')

    if [ -n "$others" ] || [ -n "$containers" ]; then
        report_occupied "$others" "$containers"
        exit 2
    fi

    if [ "$workers" -eq 0 ]; then
        break
    fi

    elapsed=$(( $(date +%s) - start ))
    if [ "$elapsed" -ge "$TIMEOUT_SEC" ]; then
        echo "$HOST: still running CI after $((TIMEOUT_SEC / 60))m; giving up." >&2
        exit 1
    fi
    remaining=$(( (TIMEOUT_SEC - elapsed) / 60 ))
    printf '[%s] %s busy (%s CI worker(s)); %dm left, sleeping %ds...\n' \
        "$(date +%H:%M:%S)" "$HOST" "$workers" "$remaining" "$POLL_SEC"
    sleep "$POLL_SEC"
done

echo "$HOST is idle and unoccupied; connecting..."
exec ssh "$HOST"
