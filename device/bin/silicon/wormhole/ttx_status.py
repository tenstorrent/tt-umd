#!/usr/bin/env python3

# SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import time
import sys
from enum import Enum
from tenstorrent import utility

#PRINT = utility.PRINT

def get_endpoint_type(x, y):
    if x == 0:
        if y == 3:
            return "PCIE"
        elif y == 10:
            return "ARC"
        elif y in [2, 9, 4, 8]:
            return "Padding"
        else:
            return "GDDR"
    elif x == 5:
        return "GDDR"
    elif y in [0, 6]:
        return "Ethernet"
    else:
        return "Tensix"
    
def mmio_read_xy(chip, x, y, noc_id, reg_addr):
    #print(f" ~~ chip.pci_read_xy({x:d}, {y:d}, {noc_id:d}, 0x{reg_addr:08x})")
    return chip.pci_read_xy(x, y, noc_id, reg_addr)

class RegType(Enum):
    Cmd = 0
    Config = 1
    Status = 2

def read_print_noc_reg(chip, x, y, noc_id, reg_name, reg_index, reg_type=RegType.Status):
    if reg_type == RegType.Cmd:
        status_offset = 0
    elif reg_type == RegType.Config:
        status_offset = 0x100
    else:
        status_offset = 0x200
    endpoint_type = get_endpoint_type(x, y)
    if endpoint_type in ["Ethernet", "Tensix"]:
        reg_addr = 0xffb20000 + (noc_id*0x10000) + status_offset + (reg_index*4)
        val = mmio_read_xy(chip, x, y, 0, reg_addr)
    elif endpoint_type in ["GDDR", "PCIE", "ARC"]:
        reg_addr = 0xFFFB20000 +  status_offset + (reg_index*4)
        xr = x if noc_id==0 else 9-x
        yr = y if noc_id==0 else 11-y
        val = mmio_read_xy(chip, xr, yr, noc_id, reg_addr)
    elif endpoint_type in ["Padding"]:
        reg_addr = 0xFFB20000 + status_offset + (reg_index*4)
        xr = x if noc_id==0 else 9-x
        yr = y if noc_id==0 else 11-y
        val = mmio_read_xy(chip, xr, yr, noc_id, reg_addr)
    else:
        print (f"Unknown endpoint type {endpoint_type}")
        sys.exit()
    print(f"{endpoint_type} x={x:02d},y={y:02d} => NOC{noc_id:d} {reg_name:s} (0x{reg_addr:09x}) = 0x{val:08x} ({val:d})")
    return val

def _get_stream_reg_field(chip, x, y, stream_id, reg_index, start_bit, num_bits):
    reg_addr = 0xFFB40000 + (stream_id*0x1000) + (reg_index*4)
    val = mmio_read_xy(chip, x, y, 0, reg_addr)
    mask = (1 << num_bits) - 1
    val = (val >> start_bit) & mask
    return val

phase_state_map = {
    0: "PHASE_START",
    1: "PHASE_AUTO_CONFIG",
    2: "PHASE_AUTO_CONFIG_SENT",
    3: "PHASE_ADVANCE_WAIT",
    4: "PHASE_PREV_DATA_FLUSH_WAIT",
    5: "PHASE_FWD_DATA"
    }

dest_state_map = {
    0 : "DEST_IDLE",
    1 : "DEST_REMOTE",
    2 : "DEST_LOCAL_RDY_WAIT",
    3 : "DEST_LOCAL_HS",
    4 : "DEST_LOCAL",
    5 : "DEST_ENDPOINT",
    6 : "DEST_NO_FWD"
    }

dest_ready_state_map = {
    0 : "DEST_READY_IDLE",
    1 : "DEST_READY_SEND_FIRST",
    2 : "DEST_READY_WAIT_DATA",
    3 : "DEST_READY_SEND_SECOND",
    4 : "DEST_READY_FWD_DATA"
    }

src_ready_state_map = {
    0 : "SRC_READY_IDLE",
    1 : "SRC_READY_WAIT_CFG",
    2 : "SRC_READY_DEST_READY_TABLE_RD",
    3 : "SRC_READY_SEND_UPDATE",
    4 : "SRC_READY_WAIT_ALL_DESTS",
    5 : "SRC_READY_FWD_DATA"
    }

src_state_map = {
    0 : "SRC_IDLE",
    1 : "SRC_REMOTE",
    2 : "SRC_LOCAL",
    3 : "SRC_ENDPOINT"
    }

def get_stream_regs(chip, x, y, stream_id):
    reg = {}
    reg["STREAM_ID"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 224+5, 24, 6):d}"
    reg["PHASE_AUTO_CFG_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 12, 0, 24):x}"
    reg["CURR_PHASE"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 11, 0, 20):d}"
    reg["CURR_PHASE_NUM_MSGS_REMAINING"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 36, 12, 12):d}"
    reg["NUM_MSGS_RECEIVED"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 224+5, 0, 24):d}"
    reg["NEXT_MSG_ADDR"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 224+6, 0, 32):x}"
    reg["NEXT_MSG_SIZE"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 224+7, 0, 32):x}"
    reg["OUTGOING_DATA_NOC"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 1, 1):d}"
    local_sources_connected = _get_stream_reg_field(chip, x, y, stream_id, 10, 3, 1)
    reg["LOCAL_SOURCES_CONNECTED"] = f"{local_sources_connected:d}"
    reg["SOURCE_ENDPOINT"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 4, 1):d}"
    remote_source = _get_stream_reg_field(chip, x, y, stream_id, 10, 5, 1)
    reg["REMOTE_SOURCE"] = f"{remote_source:d}"
    reg["RECEIVER_ENDPOINT"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 6, 1):d}"
    reg["LOCAL_RECEIVER"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 7, 1):d}"
    remote_receiver = _get_stream_reg_field(chip, x, y, stream_id, 10, 8, 1)
    reg["REMOTE_RECEIVER"] = f"{remote_receiver:d}"
    reg["NEXT_PHASE_SRC_CHANGE"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 12, 1):d}"
    reg["NEXT_PHASE_DST_CHANGE"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 13, 1):d}"
    if remote_source == 1:
        reg["INCOMING_DATA_NOC"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 0, 1):d}"
        reg["REMOTE_SRC_X"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 0, 0, 6):d}"
        reg["REMOTE_SRC_Y"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 0, 6, 6):d}"
        reg["REMOTE_SRC_STREAM_ID"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 0, 12, 6):d}"
        reg["REMOTE_SRC_UPDATE_NOC"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 2, 1):d}"
        reg["REMOTE_SRC_PHASE"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 1, 0, 20):d}"
        reg["REMOTE_SRC_DEST_INDEX"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 0, 18, 6):d}"
        reg["REMOTE_SRC_IS_MCAST"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 16, 1):d}"
    if remote_receiver == 1:
        reg["OUTGOING_DATA_NOC"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 1, 1):d}"
        reg["REMOTE_DEST_STREAM_ID"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 2, 12, 6):d}"
        reg["REMOTE_DEST_X"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 2, 0, 6):d}"
        reg["REMOTE_DEST_Y"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 2, 6, 6):d}"
        reg["REMOTE_DEST_BUF_START (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 3, 0, 17):x}"
        reg["REMOTE_DEST_BUF_SIZE (words)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 4, 0, 17):x}"
        reg["REMOTE_DEST_BUF_WR_PTR"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 5, 0, 17):x}"
        reg["REMOTE_DEST_MSG_INFO_WR_PTR"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 9, 0, 17):x}"
        reg["DEST_DATA_BUF_NO_FLOW_CTRL"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 15, 1):d}"
        mcast_en = _get_stream_reg_field(chip, x, y, stream_id, 13, 12, 1)
        reg["MCAST_EN"] = f"{mcast_en:d}"
        if mcast_en == 1:
            reg["MCAST_END_X"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 0, 6):d}"
            reg["MCAST_END_Y"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 6, 6):d}"
            reg["MCAST_LINKED"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 13, 1):d}"
            reg["MCAST_VC"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 14, 1):d}"
            reg["MCAST_DEST_NUM"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 14, 0, 16):d}"
            for i in range(0, 31):
                reg["DEST_BUF_SPACE_AVAILABLE[{i:d}]"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 64+i, 0, 32):x}"
        else:
            reg["DEST_BUF_SPACE_AVAILABLE[0]"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 64, 0, 32):x}"

    if local_sources_connected == 1:
        local_src_mask_lo = _get_stream_reg_field(chip, x, y, stream_id, 48, 0, 32)
        local_src_mask_hi = _get_stream_reg_field(chip, x, y, stream_id, 49, 0, 32)
        local_src_mask = (local_src_mask_hi << 32) | local_src_mask_lo
        reg["LOCAL_SRC_MASK"] = f"0x{local_src_mask:x}"
        reg["MSG_ARB_GROUP_SIZE"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 15, 0, 3):d}"
        reg["MSG_SRC_IN_ORDER_FWD"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 15, 3, 1):d}"
        reg["STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSGS_REG_INDEX"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 16, 0, 24):d}"
    else:
        reg["BUF_START (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 6, 0, 17):x}"
        reg["BUF_SIZE (words)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 7, 0, 17):x}"
        reg["BUF_RD_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 24, 0, 17):x}"
        reg["BUF_WR_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 25, 0, 17):x}"
        reg["MSG_INFO_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 8, 0, 17):x}"
        reg["MSG_INFO_WR_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 26, 0, 17):x}"
        reg["STREAM_BUF_SPACE_AVAILABLE_REG_INDEX (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 28, 0, 17):x}"
        reg["DATA_BUF_NO_FLOW_CTRL"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 14, 1):d}"
        reg["UNICAST_VC_REG"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 18, 3):d}"
        reg["REG_UPDATE_VC_REG"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 21, 3):d}"
    reg["SCRATCH_REG0"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 248, 0, 32):08x}"
    reg["SCRATCH_REG1"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 249, 0, 32):08x}"
    reg["SCRATCH_REG2"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 250, 0, 32):08x}"
    reg["SCRATCH_REG3"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 251, 0, 32):08x}"
    reg["SCRATCH_REG4"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 252, 0, 32):08x}"
    reg["SCRATCH_REG5"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 253, 0, 32):08x}"
    for i in range(0, 10):
        reg[f"DEBUG_STATUS[{i:d}]"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 224+i, 0, 32):08x}"
        if i == 8:
            phase_state = _get_stream_reg_field(chip, x, y, stream_id, 224+i, 0, 4)
            src_ready_state = _get_stream_reg_field(chip, x, y, stream_id, 224+i, 4, 3)
            dest_ready_state = _get_stream_reg_field(chip, x, y, stream_id, 224+i, 7, 3)
            src_side_phase_complete = _get_stream_reg_field(chip, x, y, stream_id, 224+i, 10, 1)
            dest_side_phase_complete = _get_stream_reg_field(chip, x, y, stream_id, 224+i, 11, 1)
            src_state = _get_stream_reg_field(chip, x, y, stream_id, 224+i, 16, 4)
            dest_state = _get_stream_reg_field(chip, x, y, stream_id, 224+i, 20, 3)
            reg["PHASE_STATE"] = f"0x{phase_state:x} ({phase_state_map[phase_state]})"
            reg["SRC_READY_STATE"] = f"0x{src_ready_state:x} ({src_ready_state_map[src_ready_state]})"
            reg["DEST_READY_STATE"] = f"0x{dest_ready_state:x} ({dest_ready_state_map[dest_ready_state]})"
            reg["SRC_SIDE_PHASE_COMPLETE"] = f"{src_side_phase_complete:x}"
            reg["DEST_SIDE_PHASE_COMPLETE"] = f"{dest_side_phase_complete:x}"
            reg["SRC_STATE"] = f"0x{src_state:x} ({src_state_map[src_state]})"
            reg["DEST_STATE"] = f"0x{dest_state:x} ({dest_state_map[dest_state]})"
    return reg

def full_dump(chip, args, x_coords, y_coords, streams):
    for x in x_coords:
        streams[x] = {}
        for y in y_coords:
            streams[x][y] = {}
            endpoint_type = get_endpoint_type(x, y)
            print("")
            print("")
            print(f"Next endpoint: x={x:02d},y={y:02d} => {endpoint_type}")
            if endpoint_type in ["Tensix", "Ethernet"]:
                num_streams = 64 if endpoint_type == "Tensix" else 32
                for stream_id in range (0, num_streams):
                    if args.full != 0:
                        print("")
                    streams[x][y][stream_id] = get_stream_regs(chip, x, y, stream_id)
                    if args.full != 0:
                        for reg, value in streams[x][y][stream_id].items():
                            print(f"{endpoint_type} x={x:02d},y={y:02d} => stream {stream_id:02d} {reg} = {value}")

            if args.full != 0:
                for noc_id in range (0, 2):
                    print("")
                    val = read_print_noc_reg(chip, x, y, noc_id, "ID", 0x2C>>2, RegType.Cmd)
                    rd_x = val & 0x3F
                    rd_y = (val >> 6) & 0x3F
                    (exp_x, exp_y) = (x, y) if noc_id == 0 else (9-x, 11-y)  
                    if rd_x == exp_x and rd_y == exp_y:
                        print ("  -> coordinate check OK")                  
                    else:
                        print(f"  -> ERROR: coordinate check FAILED: expected x={exp_x:02d},y={exp_y:02d}, got x={rd_x:02d},y={rd_y:02d}")
                        sys.exit()
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "NIU_CFG_0", 0x0, RegType.Config)
                    read_print_noc_reg(chip, x, y, noc_id, "ROUTER_CFG_0", 0x1, RegType.Config)
                    read_print_noc_reg(chip, x, y, noc_id, "ROUTER_CFG_1", 0x2, RegType.Config)  
                    read_print_noc_reg(chip, x, y, noc_id, "ROUTER_CFG_2", 0x3, RegType.Config)
                    read_print_noc_reg(chip, x, y, noc_id, "ROUTER_CFG_3", 0x4, RegType.Config)                 
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "nonposted write reqs sent", 0xA)
                    read_print_noc_reg(chip, x, y, noc_id, "posted write reqs sent", 0xB)
                    read_print_noc_reg(chip, x, y, noc_id, "nonposted write words sent", 0x8)
                    read_print_noc_reg(chip, x, y, noc_id, "posted write words sent", 0x9)
                    read_print_noc_reg(chip, x, y, noc_id, "write acks received", 0x1)
                    read_print_noc_reg(chip, x, y, noc_id, "read reqs sent", 0x5)
                    read_print_noc_reg(chip, x, y, noc_id, "read words received", 0x3)
                    read_print_noc_reg(chip, x, y, noc_id, "read resps received", 0x2)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "nonposted write reqs received", 0x3A)
                    read_print_noc_reg(chip, x, y, noc_id, "posted write reqs received", 0x3B)
                    read_print_noc_reg(chip, x, y, noc_id, "nonposted write words received", 0x38)
                    read_print_noc_reg(chip, x, y, noc_id, "posted write words received", 0x39)
                    read_print_noc_reg(chip, x, y, noc_id, "write acks sent", 0x31)
                    read_print_noc_reg(chip, x, y, noc_id, "read reqs received", 0x35)
                    read_print_noc_reg(chip, x, y, noc_id, "read words sent", 0x33)
                    read_print_noc_reg(chip, x, y, noc_id, "read resps sent", 0x32)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "router port x out vc full credit out vc stall", 0x44)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y out vc full credit out vc stall", 0x42)
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu out vc full credit out vc stall", 0x40)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC14 & VC15 dbg", 0x5d)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC12 & VC13 dbg", 0x5c)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC10 & VC11 dbg", 0x5b)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC8 & VC9 dbg", 0x5a)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC6 & VC7 dbg", 0x59)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC4 & VC5 dbg", 0x58)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC2 & VC3 dbg", 0x57)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC0 & VC1 dbg", 0x56)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC14 & VC15 dbg", 0x55)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC12 & VC13 dbg", 0x54)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC10 & VC11 dbg", 0x53)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC8 & VC9 dbg", 0x52)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC6 & VC7 dbg", 0x51)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC4 & VC5 dbg", 0x50)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC2 & VC3 dbg", 0x4f)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC0 & VC1 dbg", 0x4e)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu VC6 & VC7 dbg", 0x49)
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu VC4 & VC5 dbg", 0x48)
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu VC2 & VC3 dbg", 0x47)
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu VC0 & VC1 dbg", 0x46)

            if (args.full != 0) and (endpoint_type == "Tensix"):
                en = 1
                rd_sel = 0
                pc_mask = 0x7fffffff
                daisy_sel = 7

                sig_sel = 0xff
                rd_sel = 0
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                test_val1 = mmio_read_xy(chip, x, y, 0, 0xffb1205c)
                rd_sel = 1
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                test_val2 = mmio_read_xy(chip, x, y, 0, 0xffb1205c)

                rd_sel = 0
                sig_sel = 2*5
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                brisc_pc = mmio_read_xy(chip, x, y, 0, 0xffb1205c) & pc_mask

                # Doesn't work - looks like a bug for selecting inputs > 31 in daisy stop
                # rd_sel = 0
                # sig_sel = 2*16
                # chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                # nrisc_pc = mmio_read_xy(chip, x, y, 0, 0xffb1205c) & pc_mask

                rd_sel = 0
                sig_sel = 2*10
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                trisc0_pc = mmio_read_xy(chip, x, y, 0, 0xffb1205c) & pc_mask

                rd_sel = 0
                sig_sel = 2*11
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                trisc1_pc = mmio_read_xy(chip, x, y, 0, 0xffb1205c) & pc_mask

                rd_sel = 0
                sig_sel = 2*12
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                trisc2_pc = mmio_read_xy(chip, x, y, 0, 0xffb1205c) & pc_mask

                print("")
                print(f"Tensix x={x:02d},y={y:02d} => dbus_test_val1 (expect 7)={test_val1:x}, dbus_test_val2 (expect A5A5A5A5)={test_val2:x}")
                print(f"Tensix x={x:02d},y={y:02d} => brisc_pc=0x{brisc_pc:x}, trisc0_pc=0x{trisc0_pc:x}, trisc1_pc=0x{trisc1_pc:x}, trisc2_pc=0x{trisc2_pc:x}")

                chip.pci_write_xy(x, y, 0, 0xffb12054, 0)

def summary(chip, args, x_coords, y_coords, streams):
    active_core = {}
    bad_streams = []
    gsync_hung = {}
    ncrisc_done = {}

    for x in x_coords:
        active_core[x] = {}
        gsync_hung[x] = {}
        ncrisc_done[x] = {}
        for y in y_coords:
            active_core[x][y] = []
            gsync_hung[x][y] = False
            ncrisc_done[x][y] = True
            if get_endpoint_type(x, y) != "Tensix":
                continue
            for stream_id in range (0, 64):
                if (int(streams[x][y][stream_id]["CURR_PHASE"]) != 0 and int(streams[x][y][stream_id]["NUM_MSGS_RECEIVED"]) > 0):
                    active_core[x][y].append(stream_id)
                if ((int(streams[x][y][stream_id]["DEBUG_STATUS[1]"], base=16) != 0 and int(streams[x][y][stream_id]["DEBUG_STATUS[1]"], base=16) != 64) or (int(streams[x][y][stream_id]["DEBUG_STATUS[2]"], base=16) & 0x7) == 0x4 or (int(streams[x][y][stream_id]["DEBUG_STATUS[2]"], base=16) & 0x7) == 0x2):
                    bad_streams.append([x,y,stream_id])

            if mmio_read_xy(chip, x, y, 0, 0xffb2010c) == 0xB0010000:
                gsync_hung[x][y] = True
            else:
                gsync_hung[x][y] = False

            if mmio_read_xy(chip, x, y, 0, 0xffb2010c) == 0x1FFFFFF1:
                ncrisc_done[x][y] = True
            else:
                ncrisc_done[x][y] = False


    print("")

    print_init_message = True
    all_streams_done = True
    for x in x_coords:
        for y in y_coords:
            if len(active_core[x][y]) != 0:
                if print_init_message:
                    print("Streams on the following cores have not gone idle: ")
                    print_init_message = False
                print(f"\t x={x:02d}, y={y:02d}")
                for i in range(len(active_core[x][y])):
                    print(f"\t\t stream_id={active_core[x][y][i]:02d}")
                all_streams_done = False
    if all_streams_done:
        print("No streams appear hung, but if the test hung maybe some of them havent gotten any tiles.")

    if len(bad_streams) != 0:
        print("")
        print("The following streams are in a bad state (have an assertion in DEBUG_STATUS[1], or DEBUG_STATUS[2] indicates a hang):")
        for i in range(len(bad_streams)):
            print(f"\t x={bad_streams[i][0]:02d}, y={bad_streams[i][1]:02d}, stream_id={bad_streams[i][2]:02d}")

    for x in x_coords:
        for y in y_coords:
            if gsync_hung[x][y]:
                print(f"Global sync hang: x={x:02d}, y={y:02d}")

    for x in x_coords:
        for y in y_coords:
            if not ncrisc_done[x][y]:
                print(f"NCRISC not idle: x={x:02d}, y={y:02d}")

def main(chip, args):
    args = utility.parse_args(args)

    if "x" not in args:
        args.x = -1
    else:
        args.x = int(args.x)
    if "y" not in args:
        args.y = -1
    else:
        args.y = int(args.y)
    if "full" not in args:
        args.full = 0
    else:
        args.full = int(args.full)
    if "addr" not in args:
        args.addr = None
    else:
        args.addr = int(args.addr, base=16)
    if "data" not in args:
        args.data = None
    else:
        args.data = int(args.data, base=16)
    if "burst" not in args:
        args.burst = 0
    else:
        args.burst = int(args.burst)

    if args.addr is not None:
        if args.x == -1:
            args.x = 0
        if args.y == -1:
            args.y = 0

        if args.data is not None:
            chip.pci_write_xy(args.x, args.y, 0, args.addr, args.data)
        else:
            if args.burst == 1:
                values = {}
                t_end = time.time() + 1
                while time.time() < t_end:
                    val = mmio_read_xy(chip, args.x, args.y, 0, args.addr)
                    values[val] = True
                for val in values.keys():
                    print(f"x={args.x:02d},y={args.y:02d} 0x{args.addr:08x} => 0x{val:08x} ({val:d})")
            elif args.burst >= 2:
                for k in range(0, args.burst):
                    val = mmio_read_xy(chip, args.x, args.y, 0, args.addr + 4*k)
                    print(f"x={args.x:02d},y={args.y:02d} 0x{(args.addr + 4*k):08x} => 0x{val:08x} ({val:d})")
            else:
                val = mmio_read_xy(chip, args.x, args.y, 0, args.addr)
                print(f"x={args.x:02d},y={args.y:02d} 0x{args.addr:08x} => 0x{val:08x} ({val:d})")
    else:
        x_coords = []
        y_coords = []

        if args.x == -1:
            for x in range (0, 10):
                x_coords.append(x)
        else:
            x_coords.append(args.x)

        if args.y == -1:
            for y in range (12):
                y_coords.append(y)
        else:
            y_coords.append(args.y)

        streams = {}

        full_dump(chip, args, x_coords, y_coords, streams)

        summary(chip, args, x_coords, y_coords, streams)

        print("")

    return 0


