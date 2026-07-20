# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import unittest
import tt_umd


class TestWarmReset(unittest.TestCase):
    def test_warm_reset(self):
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

        # Get board type and architecture
        arch = pci_devices_info[0].get_arch()
        print(f"Device architecture: {arch}")

        # Check if the first device is UBB and execute warm reset with secondary bus reset disabled
        print(f"First device subsystem ID: 0x{pci_devices_info[0].subsystem_id:04X}")

        is_wh_ubb = pci_devices_info[0].subsystem_id == 0x0035
        is_bh_ubb = pci_devices_info[0].subsystem_id == 0x0047
        kmd_supports_reset = tt_umd.PCIDevice.is_arch_agnostic_reset_supported()
        print(f"KMD supports arch agnostic reset: {kmd_supports_reset}")
        print(f"Is WH UBB: {is_wh_ubb}, Is BH UBB: {is_bh_ubb}")

        if is_bh_ubb:
            self.skipTest("Skipping warm reset test on BH UBB.")

        # In case KMD still doesn't support arch agnostic reset, and in case of UBB, we have to call special UBB warm reset
        if is_wh_ubb and not kmd_supports_reset:
            print("Executing UBB warm reset with recovery...")
            tt_umd.WarmResetWithRecovery.ubb_warm_reset()
        else:
            should_perform_secondary_bus_reset = not is_wh_ubb
            print(
                f"Executing standard warm reset with recovery, with secondary bus reset: {should_perform_secondary_bus_reset}"
            )
            tt_umd.WarmResetWithRecovery.warm_reset(
                secondary_bus_reset=should_perform_secondary_bus_reset
            )

        # Verify that the device is back online
        options = tt_umd.TopologyDiscoveryOptions()
        # Our 6U has unstable eth links, so this will ensure having full links after this test.
        options.perform_6u_eth_retrain = True
        tt_umd.TopologyDiscovery.discover(options)
