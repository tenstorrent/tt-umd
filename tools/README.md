# Tools

## Build flow

In general, see the common build instructions in the main [README](../README.md)

Short instructions for building tools:
```
cmake -B build -G Ninja
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

Example output:
```
=== UMD locks found in /dev/shm (8) ===
  [ARC_MSG                                      ]  FREE
  [ARC_MSG_0_PCIe                               ]  LOCKED  PID=12345 TID=12345
  [CHIP_IN_USE_0_PCIe                           ]  LOCKED  PID=12345 TID=12346
  [MEM_BARRIER_0_PCIe                           ]  FREE
  ...

=== PCIe devices found (1) ===
  device 0

=== Expected locks missing from /dev/shm (1) ===
  [MISSING]  NON_MMIO_0_PCIe
```
