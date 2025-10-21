# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import tt_umd

class TestTTDevice(unittest.TestCase):
    def test_low_level_tt_device(self):
        dev_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", dev_ids)
        if (len(dev_ids) == 0):
            print("No PCI devices found.")
            return

        for dev_id in dev_ids:
            dev = tt_umd.TTDevice.create(dev_id)
            dev.init_tt_device()
            print(f"TTDevice id {dev_id} has arch {dev.get_arch()} and board id {dev.get_board_id()}")
            pci_dev = dev.get_pci_device()
            pci_info = pci_dev.get_device_info().pci_bdf
            print("pci bdf is ", pci_info)
            val = dev.noc_read32(9, 0, 0)
            print("Read value from device, core 9,0 addr 0x0: ", val)

    def test_remote_tt_device(self):
        cluster_descriptor = tt_umd.TopologyDiscovery.create_cluster_descriptor()
        umd_tt_devices = {}
        chip_to_mmio_map = cluster_descriptor.get_chips_with_mmio()
        chip_eth_coords = cluster_descriptor.get_chip_locations()
        for chip in cluster_descriptor.get_chips_local_first(cluster_descriptor.get_all_chips()):
            if cluster_descriptor.is_chip_mmio_capable(chip):
                print(f"Chip MMIO capable: {chip}")
                umd_tt_devices[chip] = tt_umd.TTDevice.create(chip_to_mmio_map[chip])
                umd_tt_devices[chip].init_tt_device()
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")
                umd_tt_devices[chip] = tt_umd.create_remote_wormhole_tt_device(umd_tt_devices[closest_mmio], cluster_descriptor, chip)
                umd_tt_devices[chip].init_tt_device()
                
            val = umd_tt_devices[chip].noc_read32(9, 0, 0)
            print(f"Read value from device, core 9,0 addr 0x0: {val}")
        
