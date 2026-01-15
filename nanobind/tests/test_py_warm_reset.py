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
            
        # Get PCI device info to access subsystem_vendor_id
        pci_devices_info = tt_umd.PCIDevice.enumerate_devices_info()
        for pci_id, device_info in pci_devices_info.items():
            print(f"PCI Device {pci_id}:")
            print(f"  Vendor ID: 0x{device_info.vendor_id:04X}")
            print(f"  Device ID: 0x{device_info.device_id:04X}")
            print(f"  Subsystem Vendor ID: 0x{device_info.subsystem_vendor_id:04X}")
            print(f"  Subsystem ID: 0x{device_info.subsystem_id:04X}")
            print(f"  PCI BDF: {device_info.pci_bdf}")    
        
        # Create TTDevice for PCI ID 0
        dev = tt_umd.TTDevice.create(0)
        dev.init_tt_device()
        
        # Get board type and architecture
        board_type = dev.get_board_type()
        arch = dev.get_arch()
        print(f"Device board type: {board_type}")
        print(f"Device architecture: {arch}")
        
        # Check if the first device is a WH UBB (0x0035 subsystem id) and execute warm reset with secondary bus reset disabled
        print(f"First device subsystem ID: 0x{pci_devices_info[0].subsystem_id:04X}")
        
        is_wormhole_ubb = arch == tt_umd.ARCH.WORMHOLE_B0 and pci_devices_info[0].subsystem_id == 0x0035
        kmd_supports_reset = tt_umd.PCIDevice.is_arch_agnostic_reset_supported()
        print(f"KMD supports arch agnostic reset: {kmd_supports_reset}")
        print(f"Is Wormhole UBB: {is_wormhole_ubb}")

        # In case KMD still doesn't support arch agnostic reset, and in case of UBB, we have to call special UBB warm reset
        if is_wormhole_ubb and not kmd_supports_reset:
            print("Executing UBB warm reset...")
            tt_umd.WarmReset.ubb_warm_reset(timeout_s=300)
        else:
            should_perform_secondary_bus_reset = not is_wormhole_ubb
            print("Executing standard warm reset, with secondary bus reset: {should_perform_secondary_bus_reset}")
            tt_umd.WarmReset.warm_reset(pci_ids, secondary_bus_reset=should_perform_secondary_bus_reset)
