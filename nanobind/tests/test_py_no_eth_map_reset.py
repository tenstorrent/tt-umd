# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import unittest
import time

# Add build directory to path to use the newly built module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../build/nanobind'))
import tt_umd

class TestNoEthMapReset(unittest.TestCase):
    
    def profile_topology_discovery(self, options, label):
        print(f"\n{'='*60}")
        print(f"Profiling: {label}")
        print(f"{'='*60}")
        
        total_start = time.time()
        
        # Discover topology
        cluster_desc = tt_umd.TopologyDiscovery.create_cluster_descriptor(options)
        
        total_time = time.time() - total_start
        
        print(f"\nTotal topology discovery time: {total_time:.3f} seconds")
        print(f"Number of chips discovered: {len(cluster_desc.get_all_chips())}")
        
        # Get device info
        mmio_chips = cluster_desc.get_chips_with_mmio()
        print(f"MMIO-capable chips: {len(mmio_chips)}")
        for chip_id, pci_id in mmio_chips.items():
            print(f"  Chip {chip_id} -> PCI device {pci_id}")
        
        return cluster_desc, total_time

    def perform_warm_reset(self):
        # Perform warm reset
        print("\n" + "="*60)
        print("PERFORMING WARM RESET")
        print("="*60)

        reset_start = time.time()
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if len(pci_ids) == 0:
            print("No PCI devices found. Skipping warm reset.")
            return 0.0
        
        print(f"Found {len(pci_ids)} PCI device(s): {pci_ids}")
        
        # Get PCI device info to determine reset method
        pci_devices_info = tt_umd.PCIDevice.enumerate_devices_info()
        arch = pci_devices_info[0].get_arch()
        print(f"Device architecture: {arch}")
        
        # Check if the first device is a WH UBB (0x0035 subsystem id)
        is_wormhole_ubb = arch == tt_umd.ARCH.WORMHOLE_B0 and pci_devices_info[0].subsystem_id == 0x0035
        kmd_supports_reset = tt_umd.PCIDevice.is_arch_agnostic_reset_supported()
        print(f"KMD supports arch agnostic reset: {kmd_supports_reset}")
        print(f"Is Wormhole UBB: {is_wormhole_ubb}")
        
        # Perform appropriate warm reset
        if is_wormhole_ubb and not kmd_supports_reset:
            print("Executing UBB warm reset...")
            tt_umd.WarmReset.ubb_warm_reset()
        else:
            should_perform_secondary_bus_reset = not is_wormhole_ubb
            print(f"Executing standard warm reset, with secondary bus reset: {should_perform_secondary_bus_reset}")
            tt_umd.WarmReset.warm_reset(pci_ids, secondary_bus_reset=should_perform_secondary_bus_reset)
        
        reset_time = time.time() - reset_start
        print(f"Warm reset completed in {reset_time:.3f} seconds")
        print("\nWaiting a moment for devices to stabilize...")
        time.sleep(2)  # Give devices a moment to stabilize
        return reset_time

    # @unittest.skip("Comment this out to profile warm reset and topology discovery.")
    def test_profiling_and_reset(self):
        reset_time_1 = self.perform_warm_reset()
        options_1 = tt_umd.TopologyDiscoveryOptions()
        options_1.no_remote_discovery = False
        options_1.no_wait_for_eth_training = False
        cluster_desc, time_1 = self.profile_topology_discovery(options_1, "Full discovery (no_remote_discovery=False, no_wait_for_eth_training=False)")

        reset_time_2 = self.perform_warm_reset()
        options_2 = tt_umd.TopologyDiscoveryOptions()
        options_1.no_remote_discovery = True
        options_1.no_wait_for_eth_training = False
        _, time_2 = self.profile_topology_discovery(options_2, "Local-only, full initialization (no_remote_discovery=True, no_wait_for_eth_training=False)")

        reset_time_3 = self.perform_warm_reset()
        options_3 = tt_umd.TopologyDiscoveryOptions()
        options_3.no_remote_discovery = True
        options_3.no_wait_for_eth_training = True
        _, time_3 = self.profile_topology_discovery(options_3, "Local-only, no waiting on eth (no_remote_discovery=True, no_wait_for_eth_training=True)")

        # Build comprehensive device map
        print("\n" + "="*60)
        print("BUILDING DEVICE MAP")
        print("="*60)

        map_start = time.time()
        device_map = {}

        for chip_id in cluster_desc.get_all_chips():
            device_map[chip_id] = {
                # Basic info
                'chip_id': chip_id,
                'unique_id': cluster_desc.get_chip_unique_ids().get(chip_id),
                'arch': cluster_desc.get_arch(chip_id),
                
                # Connectivity
                'is_mmio_capable': cluster_desc.is_chip_mmio_capable(chip_id),
                'is_remote': cluster_desc.is_chip_remote(chip_id),
                'pci_device_num': cluster_desc.get_chips_with_mmio().get(chip_id),
                'closest_mmio_chip': cluster_desc.get_closest_mmio_capable_chip(chip_id) if not cluster_desc.is_chip_mmio_capable(chip_id) else chip_id,
                
                # Location
                'location': cluster_desc.get_chip_locations().get(chip_id),  # EthCoord with cluster_id, x, y, rack, shelf
                
                # Board info
                'board_type': cluster_desc.get_board_type(chip_id),
                'board_id': cluster_desc.get_board_id_for_chip(chip_id) if chip_id in cluster_desc.get_chips_with_mmio() else None,
                
                # Ethernet
                'active_eth_channels': list(cluster_desc.get_active_eth_channels(chip_id)),
                'ethernet_connections': cluster_desc.get_ethernet_connections().get(chip_id, {}),
            }

        map_time = time.time() - map_start
        print(f"Device map building time: {map_time:.3f} seconds")

        # Print summary
        print(f"\n{'='*60}")
        print("DEVICE MAP SUMMARY")
        print(f"{'='*60}")
        print(f"Total devices: {len(device_map)}")
        for chip_id, info in device_map.items():
            print(f"\nChip {chip_id}:")
            print(f"  Architecture: {info['arch']}")
            print(f"  MMIO-capable: {info['is_mmio_capable']}")
            print(f"  PCI device: {info['pci_device_num']}")
            print(f"  Board type: {info['board_type']}")
            if info['board_id'] is not None:
                print(f"  Board ID: {info['board_id']:#x}")
            print(f"  Active ETH channels: {len(info['active_eth_channels'])}")
            if info['location']:
                loc = info['location']
                print(f"  Location: cluster={loc.cluster_id}, rack={loc.rack}, shelf={loc.shelf}, x={loc.x}, y={loc.y}")

        print(f"\n{'='*60}")
        print(f"TOTAL TIME BREAKDOWN:")
        print(f"  Test 1 (Full discovery):                   Reset: {reset_time_1:.3f}s, Discovery: {time_1:.3f}s")
        print(f"  Test 2 (Local-only, full initialization):  Reset: {reset_time_2:.3f}s, Discovery: {time_2:.3f}s")
        print(f"  Test 3 (Local-only, no waiting on eth):    Reset: {reset_time_3:.3f}s, Discovery: {time_3:.3f}s")
        print(f"  Device map building:                       {map_time:.3f}s")
        total_time = reset_time_1 + time_1 + reset_time_2 + time_2 + reset_time_3 + time_3 + map_time
        print(f"  Total: {total_time:.3f}s")
        print(f"{'='*60}")

if __name__ == '__main__':
    unittest.main()
