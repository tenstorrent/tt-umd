#!/usr/bin/env python3

# SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
import os
import yaml

from tenstorrent.chip import Chip
from tenstorrent import utility

DEBUG_MAILBOX_BUF_BASE  = 112
DEBUG_MAILBOX_BUF_SIZE  = 64
ARCH = "wormhole"
THREADS = ["T0", "T1", "T2", "Ncrisc"]

# This allows you to use tt-script <script name> -- <args> 
# Instead of tt-script <script name> --args <args>
post_parse = True

def get_x_coords(arch = ARCH):
    x_coords = []
    if arch == "grayskull":
        for x in range (1, 13):
            x_coords.append(x)
    elif arch == "wormhole":
        for x in range (1, 5):
            x_coords.append(x)
        for x in range (6, 10):
            x_coords.append(x)
    else:
        raise ValueError("Invalid arch")
    
    return x_coords

def get_y_coords(arch = ARCH):
    y_coords = []
    if arch == "grayskull":
        for y in range (1, 6):
            y_coords.append(y)
        for y in range (7, 12):
            y_coords.append(y)
    elif arch == "wormhole":
        for y in range (1, 6):
            y_coords.append(y)
        for y in range (7, 12):
            y_coords.append(y)
    else:
        raise ValueError("Invalid arch")
    
    return y_coords

def dump_debug_mailbox(state):

    mailbox_dump = {}
    for x in get_x_coords(state.arch):
        for y in get_y_coords(state.arch):
            mailbox_core_vals = {}
            for thread_idx, thread in enumerate(THREADS):
                mailbox_threads_vals = []
                for idx in range(DEBUG_MAILBOX_BUF_SIZE // 4):
                    reg_addr = DEBUG_MAILBOX_BUF_BASE + thread_idx * DEBUG_MAILBOX_BUF_SIZE + idx * 4
                    val = state.chip.pci_read_xy(x, y, 0, reg_addr)
                    mailbox_threads_vals.append(val & 0xffff)
                    mailbox_threads_vals.append((val >> 16) & 0xffff)
                    mailbox_core_vals[thread] = mailbox_threads_vals
            mailbox_dump[f"{x}-{y}"] = mailbox_core_vals
    
    with open(state.mailbox_output_path, 'w') as mailbox_file:
        print(f"Writing the debug mailbox registers to {state.mailbox_output_path}")
        yaml.dump(mailbox_dump, mailbox_file)

# if __name__ == "__main__":
def main(chip: Chip, args):
    # script_args = sys.argv[1:idx]
    print("script_args = ", args)

    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--timeout", type=int, default=None, help="timeout in seconds to wait for command to finish")
    parser.add_argument("--mailbox-output-path", type=str, default="debug_mailbox.yaml", help="The output file path for mailbox dump")
    # parser.add_argument("--arch", type=str, default="grayskull", help="target arch")

    utility.register_argparse_actions(parser)
    state = parser.parse_args(args)
    state.chip = chip
    state.arch = ARCH

    dump_debug_mailbox(state)
