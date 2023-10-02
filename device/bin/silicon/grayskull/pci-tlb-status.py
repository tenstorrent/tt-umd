# SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from tenstorrent import utility
from tenstorrent import kmdif

def main(chip, args):
    args = utility.parse_args(args)
    chip.setup_interface()
    TLB_COUNT_16M = 20
    for tlb_16MB_index in range (TLB_COUNT_16M):
        path = f'PCI_TLBS.TLB_CFG_16MB[{tlb_16MB_index}]'
        addr, mask, shift, reg_info = chip.AXI.get_path_info (path)
        print (f"TLB {tlb_16MB_index} at 0x%x: " % addr, end='')
        fields = chip.AXI.read_fields(path)
        print (fields)
    return 0
