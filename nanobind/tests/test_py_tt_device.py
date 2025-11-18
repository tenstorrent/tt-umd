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
            dev.noc_read(0, tensix_core.x, tensix_core.y, 0x300, buffer)
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

    def test_arc_msg(self):
        dev_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", dev_ids)
        if (len(dev_ids) == 0):
            print("No PCI devices found.")
            return

        for dev_id in dev_ids:
            dev = tt_umd.TTDevice.create(dev_id)
            dev.init_tt_device()
            arch = dev.get_arch()
            print(f"Testing arc_msg on device {dev_id} with arch {arch}")

            # Send TEST message with args 0x1234 and 0x5678
            exit_code, return_3, return_4 = dev.arc_msg(0x90, True, [0x1234, 0x5678], 1000)
            print(f"arc_msg result: exit_code={exit_code:#x}, return_3={return_3:#x}, return_4={return_4:#x}")
            self.assertEqual(exit_code, 0, "arc_msg should succeed")
            exit_code, return_3, return_4 = dev.arc_msg(0x90, True, 0x1234, 0x5678, 1000)
            print(f"arc_msg result: exit_code={exit_code:#x}, return_3={return_3:#x}, return_4={return_4:#x}")
            self.assertEqual(exit_code, 0, "arc_msg should succeed")

    # The testing algorithm is the same as the one in test_tt_device.cpp
    @unittest.skip("Disabled by default - potentially destructive SPI test. Remove this decorator to run.")
    def test_spi_read_write(self):
        """Test SPI read/write operations on discovered devices."""
        cluster_descriptor = tt_umd.TopologyDiscovery.create_cluster_descriptor()
        umd_tt_devices = {}
        chip_to_mmio_map = cluster_descriptor.get_chips_with_mmio()

        # Create TTDevice instances for all chips (local and remote)
        # Note that we have to enable SPI for the test to work.
        allow_spi = True;
        for chip in cluster_descriptor.get_chips_local_first(cluster_descriptor.get_all_chips()):
            if cluster_descriptor.is_chip_mmio_capable(chip):
                umd_tt_devices[chip] = tt_umd.TTDevice.create(chip_to_mmio_map[chip], allow_spi = allow_spi)
                umd_tt_devices[chip].init_tt_device()
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                umd_tt_devices[chip] = tt_umd.create_remote_wormhole_tt_device(
                    umd_tt_devices[closest_mmio], cluster_descriptor, chip, allow_spi)
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

            # Increment value again, but this time don't commit it to SPI.
            # This is to verify that the values from SPI are truly fetched.
            new_val[0] = (new_val[0] + 1) % 256
            if new_val[0] == 0:
                new_val[1] = (new_val[1] + 1) % 256

            # Performs write to the buffer, but doesn't commit it to SPI (skip_write_to_spi=True)
            print(f"SPI write (fake) to 0x{spare_addr:x}")
            tt_device.spi_write(spare_addr, bytes(new_val), True)

            # Read back to verify - should NOT match new_val since we didn't actually write to SPI
            verify2 = bytearray(2)
            tt_device.spi_read(spare_addr, verify2)
            print(f"Value after fake write at 0x{spare_addr:x}: {verify2[1]:02x}{verify2[0]:02x}")

            self.assertNotEqual(list(new_val), list(verify2),
                              f"SPI buffer update on read failed for device {chip_id} - fake write should not change SPI value")

            # Read wider area
            wide_read = bytearray(8)
            tt_device.spi_read(spare_addr, wide_read)
            wide_value = int.from_bytes(wide_read, byteorder='little')
            print(f"Wide read at 0x{spare_addr:x}: {wide_value:016x}")

            # Verify first 2 bytes match the verify2 value (not new_val)
            self.assertEqual(wide_read[0], verify2[0], 
                           f"First byte mismatch for device {chip_id}")
            self.assertEqual(wide_read[1], verify2[1], 
                           f"Second byte mismatch for device {chip_id}")
