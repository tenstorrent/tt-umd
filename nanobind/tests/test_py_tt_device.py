# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import unittest
import tt_umd


class TestTTDevice(unittest.TestCase):
    def test_low_level_tt_device(self):
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", pci_ids)
        if len(pci_ids) == 0:
            print("No PCI devices found.")
            return

        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()
            print(
                f"TTDevice id {pci_id} has arch {dev.get_arch()} and board id {dev.get_board_id()}"
            )
            pci_dev = dev.get_pci_device()
            pci_info = pci_dev.get_device_info().pci_bdf
            print("pci bdf is ", pci_info)

            soc_descriptor = tt_umd.SocDescriptor(dev)
            tensix_core = soc_descriptor.get_cores(
                tt_umd.CoreType.TENSIX, tt_umd.CoordSystem.TRANSLATED
            )[0]

            # Test noc_read32
            val = dev.noc_read32(tensix_core.x, tensix_core.y, 0)
            print(
                f"Read value from device, core {tensix_core.x},{tensix_core.y} addr 0x0: {val}"
            )

            # Test noc_write32 and noc_read32
            original = dev.noc_read32(tensix_core.x, tensix_core.y, 0x100)
            test_val = (
                original + 0x12345678
            ) & 0xFFFFFFFF  # Add offset to ensure different value
            dev.noc_write32(tensix_core.x, tensix_core.y, 0x100, test_val)
            read_back = dev.noc_read32(tensix_core.x, tensix_core.y, 0x100)
            print(
                f"noc_write32/read32: original=0x{original:08x}, wrote 0x{test_val:08x}, read 0x{read_back:08x}"
            )
            self.assertEqual(
                read_back, test_val, "Read value should match written value"
            )
            dev.noc_write32(tensix_core.x, tensix_core.y, 0x100, original)  # Restore

            # Test noc_read and noc_write
            original_data = dev.noc_read(tensix_core.x, tensix_core.y, 0x200, 16)
            # Modify original data by XORing with a pattern to ensure it's different
            test_data = bytes([(b ^ 0xAA) for b in original_data])
            dev.noc_write(tensix_core.x, tensix_core.y, 0x200, test_data)
            read_data = dev.noc_read(tensix_core.x, tensix_core.y, 0x200, 16)
            print(f"noc_write/read: wrote {test_data.hex()}, read {read_data.hex()}")
            self.assertEqual(
                read_data, test_data, "Read data should match written data"
            )
            dev.noc_write(tensix_core.x, tensix_core.y, 0x200, original_data)  # Restore

            # Test noc_read with buffer parameter
            buffer_size = 32
            buffer = bytearray(buffer_size)
            dev.noc_read(0, tensix_core.x, tensix_core.y, 0x300, buffer)
            print(f"noc_read with buffer: read {buffer.hex()}")

            # Verify buffer version matches the original version
            data_via_original = dev.noc_read(
                tensix_core.x, tensix_core.y, 0x300, buffer_size
            )
            self.assertEqual(
                bytes(buffer),
                data_via_original,
                "Buffer-based noc_read should match original noc_read",
            )
            print(f"noc_read buffer version verified against original version")

    def test_dma_tt_device(self):
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", pci_ids)
        if len(pci_ids) == 0:
            print("No PCI devices found.")
            return

        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()
            if dev.is_remote():
                print(f"Skipping remote device {pci_id} for DMA test")
                continue

            soc_descriptor = tt_umd.SocDescriptor(dev)
            tensix_core = soc_descriptor.get_cores(
                tt_umd.CoreType.TENSIX, tt_umd.CoordSystem.TRANSLATED
            )[0]

            # Test noc_read32
            val = int.from_bytes(
                dev.dma_read_from_device(tensix_core.x, tensix_core.y, 0, 4),
                byteorder="little",
            )
            print(
                f"Read value from device, core {tensix_core.x},{tensix_core.y} addr 0x0: {val}"
            )

            # Test noc_write32 and noc_read32
            original = int.from_bytes(
                dev.dma_read_from_device(tensix_core.x, tensix_core.y, 0x100, 4),
                byteorder="little",
            )
            test_val = (
                original + 0x12345678
            ) & 0xFFFFFFFF  # Add offset to ensure different value
            dev.dma_write_to_device(
                tensix_core.x,
                tensix_core.y,
                0x100,
                test_val.to_bytes(4, byteorder="little"),
            )
            read_back = int.from_bytes(
                dev.dma_read_from_device(tensix_core.x, tensix_core.y, 0x100, 4),
                byteorder="little",
            )
            print(
                f"noc_write32/read32: original=0x{original:08x}, wrote 0x{test_val:08x}, read 0x{read_back:08x}"
            )
            self.assertEqual(
                read_back, test_val, "Read value should match written value"
            )
            dev.dma_write_to_device(
                tensix_core.x,
                tensix_core.y,
                0x100,
                original.to_bytes(4, byteorder="little"),
            )  # Restore

            # Test noc_read and noc_write
            original_data = dev.dma_read_from_device(
                tensix_core.x, tensix_core.y, 0x200, 16
            )
            # Modify original data by XORing with a pattern to ensure it's different
            test_data = bytes([(b ^ 0xAA) for b in original_data])
            dev.dma_write_to_device(tensix_core.x, tensix_core.y, 0x200, test_data)
            read_data = dev.dma_read_from_device(
                tensix_core.x, tensix_core.y, 0x200, 16
            )
            print(f"noc_write/read: wrote {test_data.hex()}, read {read_data.hex()}")
            self.assertEqual(
                read_data, test_data, "Read data should match written data"
            )
            dev.dma_write_to_device(
                tensix_core.x, tensix_core.y, 0x200, original_data
            )  # Restore

            # Test noc_read with buffer parameter
            buffer_size = 32
            buffer = bytearray(buffer_size)
            dev.dma_read_from_device(0, tensix_core.x, tensix_core.y, 0x300, buffer)
            print(f"noc_read with buffer: read {buffer.hex()}")

            # Verify buffer version matches the original version
            data_via_original = dev.dma_read_from_device(
                tensix_core.x, tensix_core.y, 0x300, buffer_size
            )
            self.assertEqual(
                bytes(buffer),
                data_via_original,
                "Buffer-based noc_read should match original noc_read",
            )
            print(f"noc_read buffer version verified against original version")

    def test_remote_tt_device(self):
        cluster_descriptor, umd_tt_devices = tt_umd.TopologyDiscovery.discover()
        for chip in cluster_descriptor.get_chips_local_first(
            cluster_descriptor.get_all_chips()
        ):
            if cluster_descriptor.is_chip_mmio_capable(chip):
                print(f"Chip MMIO capable: {chip}")
                # Verify that MMIO capable device is not remote
                self.assertFalse(
                    umd_tt_devices[chip].is_remote(),
                    f"MMIO capable device {chip} should not be remote",
                )
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")
                # Verify that remote device is actually remote
                self.assertTrue(
                    umd_tt_devices[chip].is_remote(),
                    f"Remote device {chip} should be remote",
                )

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
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", pci_ids)
        if len(pci_ids) == 0:
            print("No PCI devices found.")
            return

        for dev_id in pci_ids:
            dev = tt_umd.TTDevice.create(dev_id)
            dev.init_tt_device()
            arch = dev.get_arch()
            print(f"Testing arc_msg on device {dev_id} with arch {arch}")

            # Send TEST message with args 0x1234 and 0x5678
            exit_code, return_3, return_4 = dev.arc_msg(
                0x90, True, [0x1234, 0x5678], 1000
            )
            print(
                f"arc_msg result: exit_code={exit_code:#x}, return_3={return_3:#x}, return_4={return_4:#x}"
            )
            self.assertEqual(exit_code, 0, "arc_msg should succeed")
            exit_code, return_3, return_4 = dev.arc_msg(
                0x90, True, 0x1234, 0x5678, 1000
            )
            print(
                f"arc_msg result: exit_code={exit_code:#x}, return_3={return_3:#x}, return_4={return_4:#x}"
            )
            self.assertEqual(exit_code, 0, "arc_msg should succeed")

    def test_get_chip_info(self):
        """Test get_chip_info method."""
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if len(pci_ids) == 0:
            print("No PCI devices found. Skipping test.")
            return

        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()

            chip_info = dev.get_chip_info()
            print(f"\n=== ChipInfo for device {pci_id} ===")
            print(f"  noc_translation_enabled: {chip_info.noc_translation_enabled}")
            print(f"  board_type: {chip_info.board_type}")
            print(f"  board_id: {chip_info.board_id}")
            print(f"  asic_location: {chip_info.asic_location}")
            print(f"  harvesting_masks:")
            print(
                f"    tensix_harvesting_mask: {chip_info.harvesting_masks.tensix_harvesting_mask}"
            )
            print(
                f"    dram_harvesting_mask: {chip_info.harvesting_masks.dram_harvesting_mask}"
            )
            print(
                f"    eth_harvesting_mask: {chip_info.harvesting_masks.eth_harvesting_mask}"
            )
            print(
                f"    pcie_harvesting_mask: {chip_info.harvesting_masks.pcie_harvesting_mask}"
            )
            print(
                f"    l2cpu_harvesting_mask: {chip_info.harvesting_masks.l2cpu_harvesting_mask}"
            )

    def test_use_noc1(self):
        """Test use_noc1 static method."""
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if len(pci_ids) == 0:
            print("No PCI devices found. Skipping test.")
            return

        # Test setting use_noc1 to True
        tt_umd.set_thread_noc_id(tt_umd.NocId.NOC1)
        print("Set thread NocId to NOC1")

        # Perform basic read/write operations to verify use_noc1 works
        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()
            print(
                f"TTDevice id {pci_id} has arch {dev.get_arch()} and board id {dev.get_board_id()}"
            )
            pci_dev = dev.get_pci_device()
            pci_info = pci_dev.get_device_info().pci_bdf
            print("pci bdf is ", pci_info)

            soc_descriptor = tt_umd.SocDescriptor(dev)
            tensix_core = soc_descriptor.get_cores(
                tt_umd.CoreType.TENSIX, tt_umd.CoordSystem.TRANSLATED
            )[0]

            # Test noc_read and noc_write
            original_data = dev.noc_read(tensix_core.x, tensix_core.y, 0x200, 16)
            # Modify original data by XORing with a pattern to ensure it's different
            test_data = bytes([(b ^ 0x55) for b in original_data])
            dev.noc_write(tensix_core.x, tensix_core.y, 0x200, test_data)
            read_data = dev.noc_read(tensix_core.x, tensix_core.y, 0x200, 16)
            print(f"noc_write/read: wrote {test_data.hex()}, read {read_data.hex()}")
            self.assertEqual(
                read_data, test_data, "Read data should match written data"
            )
            dev.noc_write(tensix_core.x, tensix_core.y, 0x200, original_data)  # Restore

        tt_umd.set_thread_noc_id(tt_umd.NocId.NOC0)
        print("Set thread NocId back to NOC0")

    def test_sigbus_exception_type_binding(self):
        """
        Verifies that the C++ SigbusError is correctly mapped to a Python type
        and can be caught specifically.
        """
        # Verify that we can catch the specific type
        with self.assertRaises(tt_umd.SigbusError) as cm:
            tt_umd.raise_sigbus_error_for_testing()

        # Verify the message passed through
        self.assertIn("This is a test exception from C++", str(cm.exception))
