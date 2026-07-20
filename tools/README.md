# Tools

## Build flow

In general, see the common build instructions in the main [README](../README.md)

Short instructions for building tools:
```
cmake -B build -G Ninja -DTT_UMD_BUILD_TOOLS=ON
cmake --build build --target umd_tools
```

## Topology tool

The topology tool can be used to generate cluster descriptor which describes system topology of tenstorrent devices.
It shows information such as pci connected chips, remote chips, ethernet connections, harvesting, etc.

You can run the following for more information:
```
./build/tools/umd/topology --help
```

Example output:
```
    ...
    ethernet_connections:
    -
       - chip: 5
          chan: 1
       - chip: 2
          chan: 9
    -
       - chip: 5
          chan: 0
       - chip: 2
          chan: 8
    ...
```

## Telemetry tool

The telemetry tool can be used to read telemetry from ARC. You can provide which pci chips should be polled, the frequency of polling and which telemetry to read.
It has a special mode where it can read some important factors for Wormhole device.

If you want to save the values, you can also pass an output file to write to.

You can run the following for more information:
```
./build/tools/umd/telemetry --help
```

Example output:
```
   ...
   Device id 0 - AICLK: 1350 VCore: 844 Power: 60 Temp: 64.12027
   Device id 0 - AICLK: 1350 VCore: 844 Power: 60 Temp: 64.632965
   Device id 0 - AICLK: 1350 VCore: 844 Power: 60 Temp: 64.632965
   ...
```

## Harvesting tool

The harvesting tool can be used to extract harvesting information for each chip.
It shows harvesting masks for Tensix, DRAM, ETH, and PCIE, and prints core coordinates in different coordinate systems.

You can run the following for more information:
```
./build/tools/umd/harvesting --help
```

## System Health tool

The system health tool can be used to report the health of the system by checking ethernet connections between chips.
It identifies board types, chip IDs, unique IDs, and shows the state of ethernet links.

You can run the following for more information:
```
./build/tools/umd/system_health --help
```

## TLB Virus tool

The TLB virus tool is a stress test tool that allocates TLBs of all available sizes until it fails.
It provides a summary of successful allocations versus total available TLBs per size per device.

You can run the following for more information:
```
./build/tools/umd/tlb_virus --help
```

## Warm Reset tool

The warm reset tool can be used to perform a warm reset on Tenstorrent devices.
It has a specific flag for 6U systems and runs topology discovery after the reset.

You can run the following for more information:
```
./build/tools/umd/warm_reset --help
```

## Lock Virus tool

The lock virus tool inspects all UMD shared-memory locks present in `/dev/shm` and
reports their state. For each lock it reports whether it is free or held, and if
held, the PID and TID of the owning thread. It also enumerates all PCIe devices
and cross-checks against the set of locks that should exist for each device,
reporting any that are missing (i.e. UMD has not yet been used with that device
since the last reboot).

You can run the following for more information:
```
./build/tools/umd/lock_virus --help
```

Lock names are printed with their full `/dev/shm` filename (including the `TT_UMD_LOCK.` prefix).

Example output:
```
=== UMD locks found in /dev/shm (8) ===
  [TT_UMD_LOCK.ARC_MSG                                      ]  FREE
  [TT_UMD_LOCK.ARC_MSG_0_PCIe                               ]  LOCKED  PID=12345 TID=12345
  [TT_UMD_LOCK.CHIP_IN_USE_0_PCIe                           ]  LOCKED  PID=12345 TID=12346
  [TT_UMD_LOCK.MEM_BARRIER_0_PCIe                           ]  FREE
  ...

=== PCIe devices found (1) ===
  device 0

=== Expected locks missing from /dev/shm (1) ===
  [MISSING]  TT_UMD_LOCK.NON_MMIO_0_PCIe
```

### Testing mode

The `--hold-lock <name>` flag acquires a single named mutex and holds it indefinitely, allowing
a second invocation of `lock_virus` to observe it as `LOCKED`:

```
# Terminal 1 – hold the lock
./build/tools/umd/lock_virus --hold-lock ARC_MSG

# Terminal 2 – observe it as LOCKED
./build/tools/umd/lock_virus
```

Press `Ctrl-C` in terminal 1 to release the lock.

## Sim Server tool

The sim server tool manages long-running simulation host processes, so one process can host a
simulation while other UMD processes attach to it as clients over its per-chip socket. It is only
built when the simulation backend is enabled (`-DTT_UMD_BUILD_SIMULATION=ON`).

Each host gets its own server directory (`<temp>/tt-umd-sim-server-<index>`), so two hosts on the
same machine never collide — even when they serve the same chip id — and a client attaches by
pointing at a specific server's directory. It has three subcommands:

- `start <simulator.so | rtl-dir>` — daemonizes a simulation host that serves the simulation in a
  fresh server directory and returns immediately, printing the host pid and that directory. Other
  UMD processes then attach to it as clients (e.g. a `Cluster` pointed at that directory).
- `list` — lists the currently-open simulation servers, showing each server's index, and for each
  of its chips the chip id, liveness, arch/backend, and socket path.
- `kill <server>` — asks a server (by its index from `list`) to shut down in-band over its socket
  (a `SHUTDOWN` request), which tears the host down gracefully; attached clients then fail their
  next request with a clear "server stopped" error. Shutdown goes over the socket rather than by
  PID/signal because the socket is world-writable and cross-user, while a signal would be same-uid
  only.

You can run the following for more information:
```
./build/tools/umd/sim_server --help
```

Example:
```
$ ./build/tools/umd/sim_server start /path/to/simulator.so
started simulation host pid 12345 (serving /path/to/simulator.so in /tmp/tt-umd-sim-server-0)

$ ./build/tools/umd/sim_server start /path/to/other_simulator.so
started simulation host pid 12346 (serving /path/to/other_simulator.so in /tmp/tt-umd-sim-server-1)

$ ./build/tools/umd/sim_server list
SERVER   CHIP   STATE        ARCH             SOCKET
0        0      live         blackhole/ttsim  /tmp/tt-umd-sim-server-0/tt-umd-sim-0.sock
1        0      live         blackhole/ttsim  /tmp/tt-umd-sim-server-1/tt-umd-sim-0.sock

$ ./build/tools/umd/sim_server kill 0
Requested shutdown of simulation server 0.
```
