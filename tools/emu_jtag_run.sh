#!/usr/bin/env bash
# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# Run a UMD test binary against a Grendel emulation model over JTAG.
#
#   emu_jtag_run.sh <gtest-filter>
#
# There is no orchestration for this in UMD. chippy has run_validation_test.py, which brings a
# model up, starts OpenOCD, runs a chippy test and tears down; a gtest binary just expects a
# host:port to already exist. This is the missing middle, and it should be replaced by teaching
# chippy's orchestrator to launch a UMD binary -- see "Integrating into chippy" at the bottom.
#
#   emu run test_sival_jtag_server.py   publishes a jtag_vpi endpoint
#   openocd                             attaches to it, serves TCL RPC
#   api_tests                           chippy Jtag2AxiV2Transport -> openocd -> TAP -> chiplet
#
# Every guard below is here because its absence cost a run. Do not remove one without
# reproducing what it prevents.
#
#   * OpenOCD's jtag_vpi_set_address calls inet_addr(), which takes a dotted-quad only. Given a
#     hostname it exits with "inet_addr error occurred".
#   * The jtag_vpi server accepts one client at a time. Connecting to it to test liveness takes
#     the slot OpenOCD needs, and OpenOCD then fails with "Can't connect". Wait on the info file,
#     never on a connection.
#   * Killing the local `emu run` does not release the ZeBu session on the emulation host. It
#     strands the module for the rest of the -t budget and discards a server a retry could have
#     used. On a downstream failure, leave it up and say where it is.
#   * The scheduler can grant a module whose slices another session still holds. zServer refuses
#     with WRP0625E and the job dies in ~25s having consumed nothing. That is a retry.
#   * The JTAG model needs its own grendelemulation ref. mimir's setup_env.sh on main names a
#     module built against emu/tools 26ww33, under which emu/mimir/26ww25.jtag does not resolve.
#     chippy warns about the mismatch but does not fix it, and the run then fails several steps
#     later with a message that does not mention the ref.

set -uo pipefail

GRENDEL_EMU_DIR=${GRENDEL_EMU_DIR:?set it to a grendelemulation checkout on the ref below}
MODEL=${EMU_JTAG_MODEL:-emu/mimir/26ww25.jtag}
MODEL_DIR=${EMU_JTAG_MODEL_DIR:-mimir}
IDCODE=${EMU_JTAG_IDCODE:-0x00101f43}
API_TESTS=${API_TESTS_BIN:?set it to a built api_tests}
OPENOCD=${OPENOCD_BIN:-openocd}
OPENOCD_CFG=${OPENOCD_CFG_DIR:?set it to the chippy openocd_config_files directory}
TCL_PORT=${OPENOCD_TCL_PORT:-6666}
BUDGET=${EMU_JTAG_BUDGET:-900s}
ATTEMPTS=${EMU_JTAG_ATTEMPTS:-12}
FILTER=${1:-EmuTTDevice.*}

work=$(mktemp -d "${TMPDIR:-/tmp}/emu_jtag_run.XXXXXX")
info=$GRENDEL_EMU_DIR/tmp/silval_server_info.json
server_log=$work/emu_server.log
openocd_log=$work/openocd.log
echo "logs: $work"

# Read the endpoint out of the info file. Deliberately does not connect -- see the header.
endpoint() {
    python3 - "$info" <<'PY'
import json, sys
try:
    entries = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(0)
for value in entries.values():
    host, port = value.get("host"), value.get("port")
    if host and port:
        print(f"{host}:{port}")
        break
PY
}

ep=""
for attempt in $(seq 1 "$ATTEMPTS"); do
    rm -f "$info"
    : > "$server_log"
    echo "=== starting jtag server (attempt $attempt) $(date +%T) ==="
    setsid bash -c "cd '$GRENDEL_EMU_DIR/models/$MODEL_DIR' \
        && source /tools_soc/tt/bin/bashrc >/dev/null 2>&1 \
        && source bin/setup_env.sh >/dev/null 2>&1 \
        && module load direnv >/dev/null 2>&1 \
        && emu switch $MODEL >/dev/null 2>&1 \
        && source /tools_soc/tt/bin/bashrc >/dev/null 2>&1 \
        && source bin/setup_env.sh >/dev/null 2>&1 \
        && exec emu run -t $BUDGET -- -sv tests/test_sival_jtag_server.py --disable-dpi" \
        > "$server_log" 2>&1 &
    leader=$!

    for i in $(seq 1 360); do
        ep=$(endpoint)
        [ -n "$ep" ] && break
        if grep -q WRP0625E "$server_log" 2>/dev/null; then
            echo "[retry] $(grep -m1 'is used by' "$server_log" | sed 's/.*WRP0625E : //' | cut -c1-80)"
            break
        fi
        sleep 5
    done

    [ -n "$ep" ] && break
    kill -TERM -"$leader" 2>/dev/null; sleep 3; kill -9 -"$leader" 2>/dev/null
    sleep 20
done

if [ -z "$ep" ]; then
    echo "no module after $ATTEMPTS attempts" >&2
    exit 1
fi

vpi_host=${ep%:*}
vpi_port=${ep##*:}
vpi_ip=$(python3 -c "import socket;print(socket.gethostbyname('$vpi_host'))")
echo "=== jtag_vpi at $vpi_host ($vpi_ip):$vpi_port ==="

# Retry OpenOCD: the server may not have re-armed its accept yet.
oocd=""
for try in 1 2 3; do
    "$OPENOCD" -c "bindto 127.0.0.1" -c "tcl_port $TCL_PORT" -c "telnet_port 4444" \
        -c "set VPI_ADDRESS $vpi_ip" -c "set VPI_PORT $vpi_port" \
        -f "$OPENOCD_CFG/adapter_jtag_vpi.cfg" -f "$OPENOCD_CFG/openocd_chiplet_config.cfg" \
        -c "init_chiplet_chain 1 $IDCODE" > "$openocd_log" 2>&1 &
    oocd=$!
    for i in $(seq 1 45); do
        python3 -c "
import socket, sys
s = socket.socket(); s.settimeout(1)
try:
    s.connect(('127.0.0.1', $TCL_PORT)); s.close()
except Exception:
    sys.exit(1)" 2>/dev/null && break
        sleep 1
    done
    kill -0 "$oocd" 2>/dev/null && break
    echo "[retry] openocd attempt $try failed: $(grep -m1 Error "$openocd_log" | cut -c1-70)"
    oocd=""
    sleep 5
done

if [ -z "$oocd" ]; then
    # The emu server is deliberately left running: see the header.
    echo "openocd would not attach; emu server still up at $vpi_host ($vpi_ip):$vpi_port" >&2
    tail -20 "$openocd_log" >&2
    exit 1
fi
grep -iE "tap/device found" "$openocd_log" | head -3

echo
TT_UMD_EMU_TRANSPORT=jtag TT_UMD_EMU_SERVER=127.0.0.1:$TCL_PORT \
    "$API_TESTS" --gtest_filter="$FILTER"
rc=$?

kill -TERM "$oocd" 2>/dev/null
echo "=== done; emu budget ends on its own, or QUIT its sideband server to release early ==="
exit $rc

# Integrating into chippy
# -----------------------
# chippy's run_validation_test.py already does the model bring-up, the OpenOCD lifecycle and the
# teardown, for its own tests. It injects --interface.type, --chip.package and
# --interface.parameters.server_ip/_port into the test it launches; a UMD gtest binary reads
# TT_UMD_EMU_SERVER instead and would skip. Teaching it to pass those as environment, or teaching
# the UMD test to accept the flags, makes a UMD binary a first-class orchestrated chippy test and
# retires this script. _resolve_under_chippy passes absolute paths through unchanged, so the
# binary can live anywhere.
