# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import tt_umd

class TestTopologyDiscovery(unittest.TestCase):
    def test_cluster_descriptor(self):
        cluster_descriptor = tt_umd.TopologyDiscovery.create_cluster_descriptor()
        print("Cluster descriptor:", cluster_descriptor)
        for chip in cluster_descriptor.get_all_chips():
            if cluster_descriptor.is_chip_mmio_capable(chip):
                print(f"Chip MMIO capable: {chip}")
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")
                
        print("All chips but local first: ", cluster_descriptor.get_chips_local_first(cluster_descriptor.get_all_chips()))
        
        for chip in cluster_descriptor.get_all_chips():
            print(f"Chip id {chip} has arch {cluster_descriptor.get_arch(chip)}")
    
    def test_cluster_descriptor_with_device_type(self):
        # Test with explicit PCIe device type (default)
        cluster_descriptor_pcie = tt_umd.TopologyDiscovery.create_cluster_descriptor(
            target_devices=set(),
            sdesc_path="",
            device_type=tt_umd.IODeviceType.PCIe
        )
        print("PCIe cluster descriptor created with", len(cluster_descriptor_pcie.get_all_chips()), "chips")
        
        # Test with JTAG device type
        # Note: This may not find devices if JTAG is not available
        cluster_descriptor_jtag = tt_umd.TopologyDiscovery.create_cluster_descriptor(
            target_devices=set(),
            sdesc_path="",
            device_type=tt_umd.IODeviceType.JTAG
        )
        print("JTAG cluster descriptor created with", len(cluster_descriptor_jtag.get_all_chips()), "chips")
