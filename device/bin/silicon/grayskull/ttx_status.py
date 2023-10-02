#!/usr/bin/env python3

# SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import time
from tenstorrent import utility

#PRINT = utility.PRINT

def read_print_noc_reg(chip, x, y, noc_id, reg_name, reg_index):
    reg_addr = 0xffb20000 + (noc_id*0x10000) + 0x200 + (reg_index*4)
    val = chip.pci_read_xy(x, y, 0, reg_addr)
    print(f"Tensix x={x:02d},y={y:02d} => NOC{noc_id:d} {reg_name:s} = 0x{val:08x} ({val:d})")    

def _get_stream_reg_field(chip, x, y, stream_id, reg_index, start_bit, num_bits):
    reg_addr = 0xFFB40000 + (stream_id*0x1000) + (reg_index*4)
    val = chip.pci_read_xy(x, y, 0, reg_addr)
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
    2 : "DEST_LOCAL",
    3 : "DEST_ENDPOINT",
    4 : "DEST_NO_FWD"
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
    reg["STREAM_ID"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 224+9, 0, 6):d}"
    reg["PHASE_AUTO_CFG_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 12, 0, 24):x}"
    reg["CURR_PHASE"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 11, 0, 20):d}"
    reg["CURR_PHASE_NUM_MSGS_REMAINING"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 35, 0, 12):d}"
    reg["NUM_MSGS_RECEIVED"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 224+5, 0, 16):d}"
    reg["NEXT_MSG_ADDR"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 224+6, 0, 16):x}"
    reg["NEXT_MSG_SIZE"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 224+6, 16, 16):x}"
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
        reg["REMOTE_DEST_BUF_START"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 3, 0, 16):d}"
        reg["REMOTE_DEST_BUF_SIZE"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 4, 0, 16):d}"
        reg["REMOTE_DEST_BUF_WR_PTR"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 5, 0, 16):d}"
        reg["REMOTE_DEST_MSG_INFO_WR_PTR"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 9, 0, 16):d}"
        reg["DEST_DATA_BUF_NO_FLOW_CTRL"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 10, 15, 1):d}"
        mcast_en = _get_stream_reg_field(chip, x, y, stream_id, 13, 12, 1)
        reg["MCAST_EN"] = f"{mcast_en:d}"
        if mcast_en == 1:
            reg["MCAST_END_X"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 0, 6):d}"
            reg["MCAST_END_Y"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 6, 6):d}"
            reg["MCAST_LINKED"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 13, 1):d}"
            reg["MCAST_VC"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 14, 1):d}"
            reg["MCAST_DEST_NUM"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 15, 0, 16):d}"
            for i in range(0, 31):
                reg[f"DEST_BUF_SPACE_AVAILABLE[{i:d}]"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 64+i, 0, 32):x}"
        else:
            reg["DEST_BUF_SPACE_AVAILABLE[0]"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 64, 0, 32):x}"
            
    if local_sources_connected == 1:
        local_src_mask_lo = _get_stream_reg_field(chip, x, y, stream_id, 48, 0, 32)
        local_src_mask_hi = _get_stream_reg_field(chip, x, y, stream_id, 49, 0, 32)
        local_src_mask = (local_src_mask_hi << 32) | local_src_mask_lo
        reg["LOCAL_SRC_MASK"] = f"0x{local_src_mask:x}"
        reg["MSG_ARB_GROUP_SIZE"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 16, 3):d}"
        reg["MSG_SRC_IN_ORDER_FWD"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 13, 19, 1):d}"
        reg["STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSGS_REG_INDEX"] = f"{_get_stream_reg_field(chip, x, y, stream_id, 14, 0, 24):d}"
    else:
        reg["BUF_START (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 6, 0, 16):x}"
        reg["BUF_SIZE (words)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 7, 0, 16):x}"
        reg["BUF_RD_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 23, 0, 16):x}"
        reg["BUF_WR_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 24, 0, 16):x}"
        reg["MSG_INFO_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 8, 0, 16):x}"
        reg["MSG_INFO_WR_PTR (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 25, 0, 16):x}"
        reg["STREAM_BUF_SPACE_AVAILABLE_REG_INDEX (word addr)"] = f"0x{_get_stream_reg_field(chip, x, y, stream_id, 27, 0, 16):x}"
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
        if i == 7:
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

            for stream_id in range (0, 64):
                if args.full != 0:
                    print("")
                streams[x][y][stream_id] = get_stream_regs(chip, x, y, stream_id)
                if args.full != 0:
                    for reg, value in streams[x][y][stream_id].items():
                        print(f"Tensix x={x:02d},y={y:02d} => stream {stream_id:02d} {reg} = {value}")

            if args.full != 0:
                for noc_id in range (0, 2):
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
                    read_print_noc_reg(chip, x, y, noc_id, "nonposted write reqs received", 0x1A)
                    read_print_noc_reg(chip, x, y, noc_id, "posted write reqs received", 0x1B)
                    read_print_noc_reg(chip, x, y, noc_id, "nonposted write words received", 0x18)
                    read_print_noc_reg(chip, x, y, noc_id, "posted write words received", 0x19)
                    read_print_noc_reg(chip, x, y, noc_id, "write acks sent", 0x10)
                    read_print_noc_reg(chip, x, y, noc_id, "read reqs received", 0x15)
                    read_print_noc_reg(chip, x, y, noc_id, "read words sent", 0x13)
                    read_print_noc_reg(chip, x, y, noc_id, "read resps sent", 0x12)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "router port x out vc full credit out vc stall", 0x24)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y out vc full credit out vc stall", 0x22)
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu out vc full credit out vc stall", 0x20)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC14 & VC15 dbg", 0x3d)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC12 & VC13 dbg", 0x3c)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC10 & VC11 dbg", 0x3b)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC8 & VC9 dbg", 0x3a)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC6 & VC7 dbg", 0x39)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC4 & VC5 dbg", 0x38)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC2 & VC3 dbg", 0x37)
                    read_print_noc_reg(chip, x, y, noc_id, "router port x VC0 & VC1 dbg", 0x36)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC14 & VC15 dbg", 0x35)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC12 & VC13 dbg", 0x34)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC10 & VC11 dbg", 0x33)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC8 & VC9 dbg", 0x32)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC6 & VC7 dbg", 0x31)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC4 & VC5 dbg", 0x30)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC2 & VC3 dbg", 0x2f)
                    read_print_noc_reg(chip, x, y, noc_id, "router port y VC0 & VC1 dbg", 0x2e)
                    print("")
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu VC6 & VC7 dbg", 0x29)
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu VC4 & VC5 dbg", 0x28)
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu VC2 & VC3 dbg", 0x27)
                    read_print_noc_reg(chip, x, y, noc_id, "router port niu VC0 & VC1 dbg", 0x26)

            if args.full != 0:
                en = 1
                rd_sel = 0
                pc_mask = 0x7fffffff
                daisy_sel = 7
                
                sig_sel = 0xff
                rd_sel = 0
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                test_val1 = chip.pci_read_xy(x, y, 0, 0xffb1205c) 
                rd_sel = 1
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                test_val2 = chip.pci_read_xy(x, y, 0, 0xffb1205c) 

                rd_sel = 0
                sig_sel = 2*9 
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                brisc_pc = chip.pci_read_xy(x, y, 0, 0xffb1205c) & pc_mask

                # Doesn't work - looks like a bug for selecting inputs > 31 in daisy stop
                # rd_sel = 0
                # sig_sel = 2*16
                # chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                # nrisc_pc = chip.pci_read_xy(x, y, 0, 0xffb1205c) & pc_mask
                
                rd_sel = 0
                sig_sel = 2*10
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                trisc0_pc = chip.pci_read_xy(x, y, 0, 0xffb1205c) & pc_mask
                
                rd_sel = 0
                sig_sel = 2*11
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                trisc1_pc = chip.pci_read_xy(x, y, 0, 0xffb1205c) & pc_mask
                
                rd_sel = 0
                sig_sel = 2*12
                chip.pci_write_xy(x, y, 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
                trisc2_pc = chip.pci_read_xy(x, y, 0, 0xffb1205c) & pc_mask
                
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
            for stream_id in range (0, 64):
                if (int(streams[x][y][stream_id]["CURR_PHASE"]) != 0 and int(streams[x][y][stream_id]["NUM_MSGS_RECEIVED"]) > 0):
                    active_core[x][y].append(stream_id)
                if (int(streams[x][y][stream_id]["DEBUG_STATUS[1]"], base=16) != 0 or (int(streams[x][y][stream_id]["DEBUG_STATUS[2]"], base=16) & 0x7) == 0x4 or (int(streams[x][y][stream_id]["DEBUG_STATUS[2]"], base=16) & 0x7) == 0x2):
                    bad_streams.append([x,y,stream_id])
            
            if chip.pci_read_xy(x, y, 0, 0xffb2010c) == 0xB0010000:
                gsync_hung[x][y] = True
            else:
                gsync_hung[x][y] = False

            if chip.pci_read_xy(x, y, 0, 0xffb2010c) == 0x1FFFFFF1:
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
    if "mmio_addr" not in args:
        args.mmio_addr = None
    else:
        args.mmio_addr = int(args.mmio_addr, base=16)
    if "data" not in args:
        args.data = None
    else:
        args.data = int(args.data, base=16)
    if "burst" not in args:
        args.burst = 0
    else:
        args.burst = int(args.burst)

    if args.mmio_addr is not None:
        if args.data is not None:
            if args.burst >= 1:
                for k in range(0, args.burst):
                    chip.AXI.write_function(args.mmio_addr + 4*k, args.data, path="N/A")
            else:
                chip.AXI.write_function(args.mmio_addr, args.data, path="N/A")
        else:
            if args.burst == 1:
                values = {}
                t_end = time.time() + 1
                while time.time() < t_end:
                    val = chip.AXI.read_function(args.mmio_addr, path="N/A")
                    values[val] = True
                for val in values.keys():
                    print(f"mmio 0x{args.mmio_addr:08x} => 0x{val:08x} ({val:d})")
            elif args.burst >= 2:
                for k in range(0, args.burst):
                    val = chip.AXI.read_function(args.mmio_addr + 4*k, path="N/A")
                    print(f"{k:04d}: mmio 0x{(args.mmio_addr + 4*k):08x} => 0x{val:08x} ({val:d})")
            else:
                val = chip.AXI.read_function(args.mmio_addr, path="N/A")
                print(f"mmio 0x{args.mmio_addr:08x} => 0x{val:08x} ({val:d})")       
    elif args.addr is not None:
        if args.x == -1:
            args.x = 0
        if args.y == -1:
            args.y = 0

        if args.data is not None:
            if args.burst >= 1:
                for k in range(0, args.burst):
                    chip.pci_write_xy(args.x, args.y, 0, args.addr + 4*k, args.data)
            else:
                chip.pci_write_xy(args.x, args.y, 0, args.addr, args.data)
        else:
            if args.burst == 1:
                values = {}
                t_end = time.time() + 1
                while time.time() < t_end:
                    val = chip.pci_read_xy(args.x, args.y, 0, args.addr)
                    values[val] = True
                for val in values.keys():
                    print(f"x={args.x:02d},y={args.y:02d} 0x{args.addr:08x} => 0x{val:08x} ({val:d})")
            elif args.burst >= 2:
                for k in range(0, args.burst):
                    val = chip.pci_read_xy(args.x, args.y, 0, args.addr + 4*k)
                    print(f"{k:04d}: x={args.x:02d},y={args.y:02d} 0x{(args.addr + 4*k):08x} => 0x{val:08x} ({val:d})")
            else:
                val = chip.pci_read_xy(args.x, args.y, 0, args.addr)
                print(f"x={args.x:02d},y={args.y:02d} 0x{args.addr:08x} => 0x{val:08x} ({val:d})")
    else:
        x_coords = []
        y_coords = []

        if args.x == -1:
            for x in range (1, 13):
                x_coords.append(x)
        else:
            x_coords.append(args.x)

        if args.y == -1:
            for y in range (1, 6):
                y_coords.append(y)
            for y in range (7, 12):
                y_coords.append(y)
        else:
            y_coords.append(args.y)

        streams = {}

        full_dump(chip, args, x_coords, y_coords, streams)

        summary(chip, args, x_coords, y_coords, streams)
            
        print("")

    return 0
                    
            
