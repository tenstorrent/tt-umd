# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import unittest
import tt_umd


class TestSocDescriptor(unittest.TestCase):
    def test_soc_descriptor_creation_and_get_cores(self):
        """Test creating SocDescriptor from TTDevice and listing cores per type."""
        # Enumerate available devices
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        print(f"Devices found: {pci_ids}")

        if len(pci_ids) == 0:
            print("No PCI devices found. Skipping test.")
            return

        # Test with all available devices
        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()

            print(f"\n=== Testing SocDescriptor on device {pci_id} ===")
            print(f"Device arch: {dev.get_arch()}")
            print(f"Device board type: {dev.get_board_type()}")

            # Create SocDescriptor from TTDevice
            soc_descriptor = tt_umd.SocDescriptor(dev)
            print("\nSocDescriptor created successfully!")

            # Define all core types we want to test
            core_types = [
                tt_umd.CoreType.ARC,
                tt_umd.CoreType.DRAM,
                tt_umd.CoreType.PCIE,
                tt_umd.CoreType.TENSIX,
                tt_umd.CoreType.ROUTER_ONLY,
                tt_umd.CoreType.SECURITY,
                tt_umd.CoreType.L2CPU,
                tt_umd.CoreType.ETH,
            ]

            # Test get_cores for each core type
            print("\n=== Listing cores per core type (NOC0 coordinates) ===")
            for core_type in core_types:
                cores = soc_descriptor.get_cores(core_type)
                print(f"\n{core_type} cores (count: {len(cores)}):")
                if len(cores) > 0:
                    # Print first few cores as examples
                    for i, core in enumerate(cores[:5]):
                        print(f"  [{i}] {core}")
                    if len(cores) > 5:
                        print(f"  ... and {len(cores) - 5} more")
                else:
                    print("  (none)")

            # Test with different coordinate systems
            print("\n=== Testing TENSIX cores in different coordinate systems ===")
            coord_systems = [
                tt_umd.CoordSystem.LOGICAL,
                tt_umd.CoordSystem.NOC0,
                tt_umd.CoordSystem.TRANSLATED,
                tt_umd.CoordSystem.NOC1,
            ]

            for coord_sys in coord_systems:
                tensix_cores = soc_descriptor.get_cores(tt_umd.CoreType.TENSIX, coord_sys)
                print(f"\n{coord_sys} - TENSIX cores (count: {len(tensix_cores)}):")
                if len(tensix_cores) > 0:
                    # Show first 3 cores
                    for i, core in enumerate(tensix_cores[:3]):
                        print(f"  {core}")
                    if len(tensix_cores) > 3:
                        print(f"  ... and {len(tensix_cores) - 3} more")

            # Test harvested cores
            print("\n=== Harvested cores ===")
            for core_type in core_types:
                harvested_cores = soc_descriptor.get_harvested_cores(core_type)
                if len(harvested_cores) > 0:
                    print(f"\n{core_type} harvested cores (count: {len(harvested_cores)}):")
                    for i, core in enumerate(harvested_cores[:3]):
                        print(f"  {core}")
                    if len(harvested_cores) > 3:
                        print(f"  ... and {len(harvested_cores) - 3} more")

            # Test get_all_cores
            print("\n=== All cores (any type) ===")
            all_cores = soc_descriptor.get_all_cores()
            print(f"Total cores: {len(all_cores)}")
            print(f"First 5 cores: {[str(c) for c in all_cores[:5]]}")

    def test_translate_coord_to(self):
        """Test translate_coord_to method with both overloads."""
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if len(pci_ids) == 0:
            print("No PCI devices found. Skipping test.")
            return

        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()

            print(f"\n=== Testing translate_coord_to on device {pci_id} ===")
            soc_descriptor = tt_umd.SocDescriptor(dev)

            # Get some cores to test with
            tensix_cores = soc_descriptor.get_cores(tt_umd.CoreType.TENSIX, tt_umd.CoordSystem.NOC0)
            if len(tensix_cores) == 0:
                print("No TENSIX cores found. Skipping translate_coord_to test.")
                continue

            # Test first overload: translate_coord_to(CoreCoord, CoordSystem)
            print("\n=== Testing translate_coord_to with CoreCoord ===")
            test_core = tensix_cores[0]
            print(f"Original core (NOC0): {test_core}")
            
            # Translate to different coordinate systems
            for target_coord_sys in [tt_umd.CoordSystem.LOGICAL, tt_umd.CoordSystem.NOC1, tt_umd.CoordSystem.TRANSLATED]:
                translated = soc_descriptor.translate_coord_to(test_core, target_coord_sys)
                print(f"  Translated to {target_coord_sys}: {translated}")
                # Verify the translated core has the correct coordinate system
                self.assertEqual(translated.coord_system, target_coord_sys,
                               f"Translated core should have coord_system {target_coord_sys}")

            # Test second overload: translate_coord_to(tt_xy_pair, CoordSystem, CoordSystem)
            print("\n=== Testing translate_coord_to with tt_xy_pair ===")
            # Use the x, y coordinates from the test core
            xy_pair = tt_umd.tt_xy_pair(test_core.x, test_core.y)
            print(f"Original xy_pair: {xy_pair}")
            
            # Translate from NOC0 to different coordinate systems
            for target_coord_sys in [tt_umd.CoordSystem.LOGICAL, tt_umd.CoordSystem.NOC1]:
                translated = soc_descriptor.translate_coord_to(
                    xy_pair, 
                    tt_umd.CoordSystem.NOC0, 
                    target_coord_sys
                )
                print(f"  Translated from NOC0 to {target_coord_sys}: {translated}")
                # Verify the translated core has the correct coordinate system
                self.assertEqual(translated.coord_system, target_coord_sys,
                               f"Translated core should have coord_system {target_coord_sys}")


if __name__ == "__main__":
    unittest.main()
