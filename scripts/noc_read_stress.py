#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

"""
Example usage:
- python scripts/noc_read_stress.py - would read everything for local chip
- python scripts/noc_read_stress.py --read-resp - Read one response slot (if any) and advance response rdptr.
- python scripts/noc_read_stress.py --remote --core-x 9 --core-y 0 - would read everything for remote chip, going through the local chip's core (9,0)
- python scripts/noc_read_stress.py --remote --core-x 9 --core-y --remote-rack 0 --remote-shelf 0 --remote-x 1 --remote-y 0 - would read everything for remote chip located at coords (1,0,0,0), going through the local chip's core (9,0)

Stress/analysis script: open local TTDevice, read NOC registers in a loop,
then report value histograms, training status, heartbeat, and queue pointers.

With --read-resp: read response from response queue (if any) and advance response rdptr.

With --remote: use remote communication to read from a remote chip. Requires --core-x and --core-y
(the local ethernet core to use for the tunnel). Reads heartbeat, postcodes, and training status
from the remote chip (same addresses as local).

- 1000 iterations: noc_read32(0xffb3010c) and noc_read32(0xffb2010c) on cores (9,6) and (1,6).
- Print per (core, address) how many times each distinct value was read.
- Read training status (address 0x1104 from codebase) for both cores once.
- Read heartbeat from eth cores at 0x1F80, 5 times consecutively per core, and print.
- Read wrptr/rdptr for request and response queues for both cores (layout from eth_interface.h).

Queue addresses are L1 local (0x11080, 0x110A0, 0x110B0, 0x11220, 0x11230). If another tool
shows different values, check whether it uses the same core coordinates (NOC0 vs NOC1) and
the same address space (L1 0x1xxxx vs RISC-local 0xffbxxxxx in eth_l1_address_map.h).
"""

import argparse
import sys

import tt_umd

# Addresses to stress-read (user-specified)
ADDR_0xffb3010c = 0xFFB3010C
ADDR_0xffb2010c = 0xFFB2010C
STRESS_ADDRESSES = [ADDR_0xffb3010c, ADDR_0xffb2010c]

# All wormhole ETH cores (NOC0), same order as device/api/umd/device/arch/wormhole_implementation.hpp ETH_CORES_NOC0
CORES = [
    (9, 0),
    (1, 0),
    (8, 0),
    (2, 0),
    (7, 0),
    (3, 0),
    (6, 0),
    (4, 0),
    (9, 6),
    (1, 6),
    (8, 6),
    (2, 6),
    (7, 6),
    (3, 6),
    (6, 6),
    (4, 6),
]

# Training status (from device/tt_device/wormhole_tt_device.cpp read_training_status)
TRAINING_STATUS_ADDR = 0x1104

# ETH command queue layout: must match src/firmware/riscv/wormhole/eth_interface.h (and
# device/tt_device/remote_communication_legacy_firmware.cpp). Layout:
#   ETH_ROUTING_STRUCT_ADDR = 0x11000; 128 bytes of latency counters then request queue.
#   cmd_q_t: cmd_counters_t (32B) | wrptr (16B) | rdptr (16B) | routing_cmd_t[4].
# So wrptr.ptr at base+32, rdptr.ptr at base+48. Same for response queue (after 2*CMD_Q_SIZE).
ETH_ROUTING_STRUCT_ADDR = 0x11000
REQUEST_CMD_QUEUE_BASE = ETH_ROUTING_STRUCT_ADDR + 128  # 0x11080
CMD_COUNTERS_SIZE_BYTES = 32
REMOTE_UPDATE_PTR_SIZE_BYTES = 16
CMD_BUF_SIZE = 4
CMD_SIZE_BYTES = 32
CMD_Q_SIZE_BYTES = (
    2 * REMOTE_UPDATE_PTR_SIZE_BYTES
    + CMD_COUNTERS_SIZE_BYTES
    + CMD_BUF_SIZE * CMD_SIZE_BYTES
)
RESPONSE_CMD_QUEUE_BASE = REQUEST_CMD_QUEUE_BASE + 2 * CMD_Q_SIZE_BYTES  # 0x11200

# Offsets from eth_interface.h cmd_q_t: cmd_counters at +0, wrptr at +32, rdptr at +48
REQ_WRPTR_ADDR = REQUEST_CMD_QUEUE_BASE + CMD_COUNTERS_SIZE_BYTES + 0  # 0x110A0
REQ_RDPTR_ADDR = (
    REQUEST_CMD_QUEUE_BASE + CMD_COUNTERS_SIZE_BYTES + REMOTE_UPDATE_PTR_SIZE_BYTES + 0
)  # 0x110B0
RESP_WRPTR_ADDR = RESPONSE_CMD_QUEUE_BASE + CMD_COUNTERS_SIZE_BYTES + 0  # 0x11220
RESP_RDPTR_ADDR = (
    RESPONSE_CMD_QUEUE_BASE
    + CMD_COUNTERS_SIZE_BYTES
    + REMOTE_UPDATE_PTR_SIZE_BYTES
    + 0  # 0x11230
)
# Request command slots (routing_cmd_t each); after counters + wrptr + rdptr
REQUEST_ROUTING_CMD_QUEUE_BASE = (
    REQUEST_CMD_QUEUE_BASE + REMOTE_UPDATE_PTR_SIZE_BYTES * 2 + CMD_COUNTERS_SIZE_BYTES
)
RESPONSE_ROUTING_CMD_QUEUE_BASE = (
    RESPONSE_CMD_QUEUE_BASE + REMOTE_UPDATE_PTR_SIZE_BYTES * 2 + CMD_COUNTERS_SIZE_BYTES
)
CMD_BUF_PTR_MASK = (CMD_BUF_SIZE << 1) - 1  # 7 for ptr wrap

# All routing queues (same map as debug_eth_non_mmio.py / eth_interface.h)
# 4-slot queues (192 bytes each)
REQ_CMD_Q0_BASE = 0x11080  # HOST request (= REQUEST_CMD_QUEUE_BASE)
REQ_CMD_Q1_BASE = 0x11140  # ETH request
RESP_CMD_Q_BASE = 0x11200  # response (= RESPONSE_CMD_QUEUE_BASE)
ETH_OUT_REQ_CMD_Q_BASE = 0x112C0
# 8-slot queues (320 bytes each)
NODE_CMD_BUF_SIZE = 8
NODE_REQ_CMD_Q0_BASE = 0x11380
NODE_REQ_CMD_Q1_BASE = 0x114C0
NODE_RESP_CMD_Q0_BASE = 0x11600
NODE_RESP_CMD_Q1_BASE = 0x11740


def _wrptr_addr(base):
    return base + CMD_COUNTERS_SIZE_BYTES + 0


def _rdptr_addr(base):
    return base + CMD_COUNTERS_SIZE_BYTES + REMOTE_UPDATE_PTR_SIZE_BYTES + 0


def _is_full_4slot(wr, rd):
    return (wr != rd) and ((wr & 3) == (rd & 3))


def _is_full_8slot(wr, rd):
    return (wr != rd) and ((wr & 7) == (rd & 7))


def _pending_4slot(wr, rd):
    """Number of requests in queue (4-slot); handles wrap."""
    return (wr - rd) if wr >= rd else (CMD_BUF_SIZE + wr - rd)


def _pending_8slot(wr, rd):
    """Number of requests in queue (8-slot); handles wrap."""
    return (wr - rd) if wr >= rd else (NODE_CMD_BUF_SIZE + wr - rd)


# Heartbeat register on eth core (noc_read32 from address 0x1F80)
HEARTBEAT_ADDR = 0x1F80

# Postcode: eth_status_t postcode at byte 0 on some layouts; use 0 as placeholder
POSTCODE_ADDR = 0x0

# Optional readout range (single-line hex dump per core)
READOUT_RANGE_START = 0x9000
READOUT_RANGE_END = 0x9050  # exclusive; (0x9050 - 0x9000) / 4 = 20 words

# Single addresses to read (one line per address in a section)
SINGLE_READ_ADDRESSES = [0x10E8]

NUM_ITERATIONS = 10
HEARTBEAT_READS = 5

# NOC address layout (topology_utils.hpp get_sys_addr): 36 local bits, 6 bits per node id
NOC_ADDR_LOCAL_BITS = 36
NOC_ADDR_NODE_ID_BITS = 6


def decode_sys_addr(addr):
    """Decode sys_addr into (chip_y, chip_x, noc_y, noc_x, offset). Matches topology_utils.hpp get_sys_addr.
    Packed order is chip_y | chip_x | noc_y | noc_x | offset (MSB to LSB), so upper 24 bits: chip_y(6), chip_x(6), noc_y(6), noc_x(6).
    """
    node_mask = (1 << NOC_ADDR_NODE_ID_BITS) - 1
    local_mask = (1 << NOC_ADDR_LOCAL_BITS) - 1
    offset = addr & local_mask
    noc_x = (addr >> NOC_ADDR_LOCAL_BITS) & node_mask
    noc_y = (addr >> (NOC_ADDR_LOCAL_BITS + NOC_ADDR_NODE_ID_BITS)) & node_mask
    chip_x = (addr >> (NOC_ADDR_LOCAL_BITS + 2 * NOC_ADDR_NODE_ID_BITS)) & node_mask
    chip_y = (addr >> (NOC_ADDR_LOCAL_BITS + 3 * NOC_ADDR_NODE_ID_BITS)) & node_mask
    return chip_y, chip_x, noc_y, noc_x, offset


def main():
    parser = argparse.ArgumentParser(description="NOC read stress and queue inspection")
    parser.add_argument(
        "--read-resp",
        action="store_true",
        help="Read response from response queue (if any) and advance response rdptr",
    )
    parser.add_argument(
        "--remote",
        action="store_true",
        help="Use remote communication to read from remote chip (heartbeat, postcodes, training status)",
    )
    parser.add_argument(
        "--core-x",
        type=int,
        default=None,
        help="Ethernet core X for remote transfer (required with --remote)",
    )
    parser.add_argument(
        "--core-y",
        type=int,
        default=None,
        help="Ethernet core Y for remote transfer (required with --remote)",
    )
    parser.add_argument(
        "--remote-rack",
        type=int,
        default=0,
        help="Remote chip rack (default 0, used with --remote)",
    )
    parser.add_argument(
        "--remote-shelf",
        type=int,
        default=0,
        help="Remote chip shelf (default 0, used with --remote)",
    )
    parser.add_argument(
        "--remote-x",
        type=int,
        default=1,
        help="Remote chip x (default 1, used with --remote)",
    )
    parser.add_argument(
        "--remote-y",
        type=int,
        default=0,
        help="Remote chip y (default 0, used with --remote)",
    )
    args = parser.parse_args()

    pci_ids = tt_umd.PCIDevice.enumerate_devices()
    if not pci_ids:
        print("No PCI devices found. Exiting.")
        return 1
    device_id = pci_ids[0]
    print("Enumerated devices:", pci_ids, "-> using device", device_id)

    if args.remote:
        if args.core_x is None or args.core_y is None:
            parser.error(
                "--remote requires --core-x and --core-y (local ethernet core for transfer)"
            )
        print("Opening local TTDevice (no init)...")
        local_dev = tt_umd.TTDevice.create(device_id)
        print(
            f"Creating remote device at coord rack={args.remote_rack} shelf={args.remote_shelf} "
            f"x={args.remote_x} y={args.remote_y} (no init)..."
        )
        dev = tt_umd.create_remote_wormhole_tt_device_from_coord(
            local_dev, args.remote_rack, args.remote_shelf, args.remote_x, args.remote_y
        )
        dev.get_remote_communication().set_remote_transfer_ethernet_cores(
            [(args.core_x, args.core_y)]
        )
        print(f"Remote transfer ethernet core: ({args.core_x}, {args.core_y})\n")
    else:
        print("Opening TTDevice (no init)...")
        dev = tt_umd.TTDevice.create(device_id)
        print("TTDevice opened.\n")

    # Stress loop: collect value counts per (core, address) for local or remote
    hist = {}
    for core_x, core_y in CORES:
        for addr in STRESS_ADDRESSES:
            hist[(core_x, core_y, addr)] = {}
    for i in range(NUM_ITERATIONS):
        for core_x, core_y in CORES:
            for addr in STRESS_ADDRESSES:
                key = (core_x, core_y, addr)
                val = dev.noc_read32(core_x, core_y, addr)
                hist[key][val] = hist[key].get(val, 0) + 1

    # Same per-core block for local or remote: check training first; if not 1, one line and skip rest.
    for core_x, core_y in CORES:
        ts = dev.noc_read32(core_x, core_y, TRAINING_STATUS_ADDR)
        if ts != 1:
            print(
                f"Core ({core_x}, {core_y}): training_status={ts} (0x{ts:08x}), skipping."
            )
            continue

        print("\n" + "=" * 60)
        print(f"Core ({core_x}, {core_y})")
        print("=" * 60)

        # Value histograms (stress read)
        print("\n--- Value histograms ---")
        for addr in STRESS_ADDRESSES:
            key = (core_x, core_y, addr)
            print(f"  addr 0x{addr:08X}:")
            for val, count in sorted(hist[key].items(), key=lambda x: x[0]):
                print(f"    {hex(val)}: {count} times")

        # Training status (already read above), postcode, heartbeat
        print("\n--- Training status (0x1104) ---")
        print(f"  {ts} (0x{ts:08x})")
        print("\n--- Postcode (0x0) ---")
        pc = dev.noc_read32(core_x, core_y, POSTCODE_ADDR)
        print(f"  0x{pc:08x}")
        print("\n--- Heartbeat (0x1F80, 5 reads) ---")
        for i in range(HEARTBEAT_READS):
            hb = dev.noc_read32(core_x, core_y, HEARTBEAT_ADDR)
            print(f"  heartbeat[{i}]: 0x{hb:08x}")

        # Read 0x9000-0x9050 (20 words), single line
        words = []
        for off in range(READOUT_RANGE_START, READOUT_RANGE_END, 4):
            words.append(dev.noc_read32(core_x, core_y, off))
        print(f"\n--- 0x{READOUT_RANGE_START:x}-0x{READOUT_RANGE_END:x} ---")
        print("  " + " ".join(f"0x{w:08x}" for w in words))

        # Single-address readouts (one line per address)
        print("\n--- Single addresses ---")
        for addr in SINGLE_READ_ADDRESSES:
            val = dev.noc_read32(core_x, core_y, addr)
            print(f"  0x{addr:04x}: 0x{val:08x} ({val})")

        # Request/response queue wrptr and rdptr (L1 addresses; see eth_interface.h)
        print("\n--- Request/response queue wrptr and rdptr ---")
        print(
            f"  Addrs: req wr 0x{REQ_WRPTR_ADDR:08x}, req rd 0x{REQ_RDPTR_ADDR:08x}, "
            f"resp wr 0x{RESP_WRPTR_ADDR:08x}, resp rd 0x{RESP_RDPTR_ADDR:08x}"
        )
        req_wrptr = dev.noc_read32(core_x, core_y, REQ_WRPTR_ADDR)
        req_rdptr = dev.noc_read32(core_x, core_y, REQ_RDPTR_ADDR)
        resp_wrptr = dev.noc_read32(core_x, core_y, RESP_WRPTR_ADDR)
        resp_rdptr = dev.noc_read32(core_x, core_y, RESP_RDPTR_ADDR)
        print(f"  Request  queue: wrptr = {req_wrptr}, rdptr = {req_rdptr}")
        print(f"  Response queue: wrptr = {resp_wrptr}, rdptr = {resp_rdptr}")

        # All routing queues: wrptr, rdptr, full?, pending count
        print("\n--- All routing queues (wrptr, rdptr, full?, pending) ---")
        queues_4slot = [
            ("req_cmd_q[0] HOST", REQ_CMD_Q0_BASE),
            ("req_cmd_q[1] ETH", REQ_CMD_Q1_BASE),
            ("resp_cmd_q", RESP_CMD_Q_BASE),
            ("eth_out_req_cmd_q", ETH_OUT_REQ_CMD_Q_BASE),
        ]
        for name, base in queues_4slot:
            w = dev.noc_read32(core_x, core_y, _wrptr_addr(base))
            r = dev.noc_read32(core_x, core_y, _rdptr_addr(base))
            full = _is_full_4slot(w, r)
            pending = _pending_4slot(w, r)
            print(
                f"  {name:24} base=0x{base:05x}  wrptr={w}  rdptr={r}  full={full}  pending={pending}"
            )
        queues_8slot = [
            ("node_req_cmd_q[0]", NODE_REQ_CMD_Q0_BASE),
            ("node_req_cmd_q[1]", NODE_REQ_CMD_Q1_BASE),
            ("node_resp_cmd_q[0]", NODE_RESP_CMD_Q0_BASE),
            ("node_resp_cmd_q[1]", NODE_RESP_CMD_Q1_BASE),
        ]
        for name, base in queues_8slot:
            w = dev.noc_read32(core_x, core_y, _wrptr_addr(base))
            r = dev.noc_read32(core_x, core_y, _rdptr_addr(base))
            full = _is_full_8slot(w, r)
            pending = _pending_8slot(w, r)
            print(
                f"  {name:24} base=0x{base:05x}  wrptr={w}  rdptr={r}  full={full}  pending={pending}"
            )

        # --read-resp: read one response slot (if available) and advance response rdptr
        if getattr(args, "read_resp", False) and resp_wrptr != resp_rdptr:
            slot_idx = resp_rdptr & (CMD_BUF_SIZE - 1)
            slot_base = RESPONSE_ROUTING_CMD_QUEUE_BASE + slot_idx * CMD_SIZE_BYTES
            sys_lo = dev.noc_read32(core_x, core_y, slot_base + 0)
            sys_hi = dev.noc_read32(core_x, core_y, slot_base + 4)
            sys_addr = (sys_hi << 32) | sys_lo
            data = dev.noc_read32(core_x, core_y, slot_base + 8)
            flags = dev.noc_read32(core_x, core_y, slot_base + 12)
            print(f"\n--- Response read (--read-resp): slot={slot_idx} ---")
            print(f"  sys_addr=0x{sys_addr:012x} data={data} flags=0x{flags:08x}")
            new_rdptr = (resp_rdptr + 1) & CMD_BUF_PTR_MASK
            dev.noc_write32(core_x, core_y, RESP_RDPTR_ADDR, new_rdptr)
            print(f"  Advanced response rdptr: {resp_rdptr} -> {new_rdptr}")
        elif getattr(args, "read_resp", False):
            print(
                "\n--- Response read (--read-resp): no response pending (wrptr == rdptr) ---"
            )

        # If request queue has pending commands (wrptr advanced past rdptr), read and print them
        if req_wrptr != req_rdptr:
            num_pending = (req_wrptr - req_rdptr) % (CMD_BUF_PTR_MASK + 1)
            if num_pending > CMD_BUF_SIZE:
                num_pending = CMD_BUF_SIZE  # cap for safety
            print(f"\n--- Request queue pending commands ({num_pending}) ---")
            for i in range(num_pending):
                slot_idx = (req_rdptr + i) & (CMD_BUF_SIZE - 1)
                slot_base = REQUEST_ROUTING_CMD_QUEUE_BASE + slot_idx * CMD_SIZE_BYTES
                sys_lo = dev.noc_read32(core_x, core_y, slot_base + 0)
                sys_hi = dev.noc_read32(core_x, core_y, slot_base + 4)
                sys_addr = (sys_hi << 32) | sys_lo
                data = dev.noc_read32(core_x, core_y, slot_base + 8)
                flags = dev.noc_read32(core_x, core_y, slot_base + 12)
                rack = dev.noc_read32(core_x, core_y, slot_base + 16) & 0xFFFF
                src_resp_buf = (
                    dev.noc_read32(core_x, core_y, slot_base + 16) >> 16
                ) & 0xFFFF
                local_buf = dev.noc_read32(core_x, core_y, slot_base + 20)
                src_addr_tag = dev.noc_read32(core_x, core_y, slot_base + 28)
                cy, cx, ny, nx, off = decode_sys_addr(sys_addr)
                print(
                    f"  [{i}] slot={slot_idx} sys_addr=0x{sys_addr:012x} data={data} flags=0x{flags:08x} "
                    f"rack={rack} src_resp_buf={src_resp_buf} local_buf={local_buf} src_addr_tag=0x{src_addr_tag:08x}"
                )
                print(
                    f"       -> decode: chip=({cx},{cy}) noc=({nx},{ny}) offset=0x{off:09x}"
                )
        else:
            print("\n--- Request queue pending commands: 0 (wrptr == rdptr) ---")

        print("=============================================================\n")

    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
