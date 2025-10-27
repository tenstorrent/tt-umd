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

    @unittest.skip("Disabled by default - potentially destructive SPI test. Run with: pytest -k test_spi_read_write --run-all")
    def test_spi_read_write(self):
        """Test SPI read/write operations on discovered devices."""
        cluster_descriptor = tt_umd.TopologyDiscovery.create_cluster_descriptor()
        umd_tt_devices = {}
        chip_to_mmio_map = cluster_descriptor.get_chips_with_mmio()
        
        # Create TTDevice instances for all chips (local and remote)
        for chip in cluster_descriptor.get_chips_local_first(cluster_descriptor.get_all_chips()):
            if cluster_descriptor.is_chip_mmio_capable(chip):
                umd_tt_devices[chip] = tt_umd.TTDevice.create(chip_to_mmio_map[chip])
                umd_tt_devices[chip].init_tt_device()
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                umd_tt_devices[chip] = tt_umd.create_remote_wormhole_tt_device(
                    umd_tt_devices[closest_mmio], cluster_descriptor, chip)
                umd_tt_devices[chip].init_tt_device()
        
        # Test SPI operations on each device
        for chip_id, tt_device in umd_tt_devices.items():
            print(f"\n=== Testing SPI on device {chip_id} (remote: {cluster_descriptor.is_chip_remote(chip_id)}) ===")
            
            # Test SPI read - board info
            board_info_addr = 0x20108
            board_info = bytearray(8)
            tt_device.spi_read(board_info_addr, board_info)
            print(f"Board info: {' '.join([f'{b:02x}' for b in board_info])}")
            
            # Verify board info is not all zeros
            self.assertTrue(any(b != 0 for b in board_info), 
                          f"Board info should not be all zeros for device {chip_id}")
            
            # Test read-modify-write on spare area
            spare_addr = 0x20134
            original = bytearray(2)
            tt_device.spi_read(spare_addr, original)
            print(f"Original value at 0x{spare_addr:x}: {original[1]:02x}{original[0]:02x}")
            
            # Increment the value
            new_val = bytearray(original)
            new_val[0] = (new_val[0] + 1) % 256
            if new_val[0] == 0:
                new_val[1] = (new_val[1] + 1) % 256
            
            # Write back incremented value
            tt_device.spi_write(spare_addr, bytes(new_val))
            
            # Verify the write
            verify = bytearray(2)
            tt_device.spi_read(spare_addr, verify)
            print(f"Updated value at 0x{spare_addr:x}: {verify[1]:02x}{verify[0]:02x}")
            
            self.assertEqual(list(new_val), list(verify), 
                           f"SPI write verification failed for device {chip_id}")
            
            # Read wider area
            wide_read = bytearray(8)
            tt_device.spi_read(spare_addr, wide_read)
            wide_value = int.from_bytes(wide_read, byteorder='little')
            print(f"Wide read at 0x{spare_addr:x}: {wide_value:016x}")
            
            # Verify first 2 bytes match
            self.assertEqual(wide_read[0], new_val[0], 
                           f"First byte mismatch for device {chip_id}")
            self.assertEqual(wide_read[1], new_val[1], 
                           f"Second byte mismatch for device {chip_id}")
        
