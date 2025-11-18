# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import tt_umd


class TestSocDescriptor(unittest.TestCase):
    def test_soc_descriptor_creation_and_get_cores(self):
        """Test creating SocDescriptor from TTDevice and listing cores per type."""
        # Enumerate available devices
        dev_ids = tt_umd.PCIDevice.enumerate_devices()
        print(f"Devices found: {dev_ids}")
        
        if len(dev_ids) == 0:
            print("No PCI devices found. Skipping test.")
            return
        
        # Test with the first available device
        dev_id = dev_ids[0]
        dev = tt_umd.TTDevice.create(dev_id)
        dev.init_tt_device()
        
        print(f"\n=== Testing SocDescriptor on device {dev_id} ===")
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


if __name__ == "__main__":
    unittest.main()

