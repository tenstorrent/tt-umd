# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import tt_umd

class TestTTDevice(unittest.TestCase):
    def test_low_level_tt_device(self):
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", pci_ids)
        if (len(pci_ids) == 0):
            print("No PCI devices found.")
            return

        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()
            print(f"TTDevice id {pci_id} has arch {dev.get_arch()} and board id {dev.get_board_id()}")
            pci_dev = dev.get_pci_device()
            pci_info = pci_dev.get_device_info().pci_bdf
            print("pci bdf is ", pci_info)

            soc_descriptor = tt_umd.SocDescriptor(dev)
            tensix_core = soc_descriptor.get_cores(tt_umd.CoreType.TENSIX, tt_umd.CoordSystem.TRANSLATED)[0]

            # Test noc_read32
            val = dev.noc_read32(tensix_core.x, tensix_core.y, 0)
            print(f"Read value from device, core {tensix_core.x},{tensix_core.y} addr 0x0: {val}")

            # Test noc_write32 and noc_read32
            original = dev.noc_read32(tensix_core.x, tensix_core.y, 0x100)
            test_val = 0xABCD1234
            dev.noc_write32(tensix_core.x, tensix_core.y, 0x100, test_val)
            read_back = dev.noc_read32(tensix_core.x, tensix_core.y, 0x100)
            print(f"noc_write32/read32: wrote 0x{test_val:08x}, read 0x{read_back:08x}")
            dev.noc_write32(tensix_core.x, tensix_core.y, 0x100, original)  # Restore

            # Test noc_read and noc_write
            original_data = dev.noc_read(tensix_core.x, tensix_core.y, 0x200, 16)
            test_data = bytes([i for i in range(16)])
            dev.noc_write(tensix_core.x, tensix_core.y, 0x200, test_data)
            read_data = dev.noc_read(tensix_core.x, tensix_core.y, 0x200, 16)
            print(f"noc_write/read: wrote {test_data.hex()}, read {read_data.hex()}")
            dev.noc_write(tensix_core.x, tensix_core.y, 0x200, original_data)  # Restore

            # Test noc_read with buffer parameter
            buffer_size = 32
            buffer = bytearray(buffer_size)
            dev.noc_read(tensix_core.x, tensix_core.y, 0, 0x300, buffer)
            print(f"noc_read with buffer: read {buffer.hex()}")

            # Verify buffer version matches the original version
            data_via_original = dev.noc_read(tensix_core.x, tensix_core.y, 0x300, buffer_size)
            self.assertEqual(bytes(buffer), data_via_original, 
                           "Buffer-based noc_read should match original noc_read")
            print(f"noc_read buffer version verified against original version")

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
                # Verify that MMIO capable device is not remote
                self.assertFalse(umd_tt_devices[chip].is_remote(), f"MMIO capable device {chip} should not be remote")
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")
                umd_tt_devices[chip] = tt_umd.create_remote_wormhole_tt_device(umd_tt_devices[closest_mmio], cluster_descriptor, chip)
                umd_tt_devices[chip].init_tt_device()
                # Verify that remote device is actually remote
                self.assertTrue(umd_tt_devices[chip].is_remote(), f"Remote device {chip} should be remote")
                
            val = umd_tt_devices[chip].noc_read32(9, 0, 0)
            print(f"Read value from device, core 9,0 addr 0x0: {val}")

    def test_read_kmd_version(self):
        # Test reading KMD version
        kmd_version = tt_umd.PCIDevice.read_kmd_version()
        print(f"\nKMD version: {kmd_version.to_string()}")
        print(f"  Major: {kmd_version.major}")
        print(f"  Minor: {kmd_version.minor}")
        print(f"  Patch: {kmd_version.patch}")
