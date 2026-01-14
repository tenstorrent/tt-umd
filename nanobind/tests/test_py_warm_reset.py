# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import unittest
import tt_umd

class TestWarmReset(unittest.TestCase):
    # @unittest.skip("Skipping warm reset test to avoid resetting cards during unit tests")
    def test_warm_reset(self):
        """Test warm reset functionality - SKIPPED to avoid resetting cards"""
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if len(pci_ids) == 0:
            print("No PCI devices found.")
            return
            
        # Create TTDevice for PCI ID 0
        dev = tt_umd.TTDevice.create(0)
        dev.init_tt_device()
        
        # Get board type and architecture
        board_type = dev.get_board_type()
        arch = dev.get_arch()
        print(f"Device board type: {board_type}")
        print(f"Device architecture: {arch}")
        
        # Check if it's UBB (Unified Board Bundle) and call appropriate warm reset
        if board_type == tt_umd.BoardType.UBB_WORMHOLE:
            print("UBB_WORMHOLE board detected, executing UBB warm reset...")
            tt_umd.WarmReset.ubb_warm_reset(timeout_s=60)  # Uncomment to actually reset
        else:
            print(f"Non-UBB board detected (type: {board_type}), executing standard warm reset...")
            tt_umd.WarmReset.warm_reset(pci_ids)  # Uncomment to actually reset
