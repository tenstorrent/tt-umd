# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import unittest
import tt_umd

# SPI Address Constants.
SPI_BOARD_INFO_ADDR = 0x20108


def get_spi_spare_addr_for_test(tt_device):
    """Return architecture-specific SPI spare/scratch address for read-modify-write tests."""
    arch = tt_device.get_arch()
    if arch == tt_umd.ARCH.WORMHOLE_B0:
        # This address is specified in the Wormhole SPI binaries as a reserved address for testing.
        return 0x20134
    if arch == tt_umd.ARCH.BLACKHOLE:
        # Blackhole doesn't have a reserved address for testing, so using a random high address.
        return 0x2800000
    raise ValueError(
        f"Unsupported architecture for SPI spare address calculation: {arch}"
    )


def setup_spi_test_devices():
    """Helper function to set up devices for SPI testing."""
    cluster_descriptor = tt_umd.TopologyDiscovery.create_cluster_descriptor()
    umd_tt_devices = {}
    chip_to_mmio_map = cluster_descriptor.get_chips_with_mmio()

    # Create TTDevice instances for all chips (local and remote)
    for chip in cluster_descriptor.get_chips_local_first(
        cluster_descriptor.get_all_chips()
    ):
        if cluster_descriptor.is_chip_mmio_capable(chip):
            umd_tt_devices[chip] = tt_umd.TTDevice.create(chip_to_mmio_map[chip])
            umd_tt_devices[chip].init_tt_device()
        else:
            closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
            umd_tt_devices[chip] = tt_umd.create_remote_wormhole_tt_device(
                umd_tt_devices[closest_mmio], cluster_descriptor, chip
            )
            umd_tt_devices[chip].init_tt_device()

    return cluster_descriptor, umd_tt_devices


class TestSPITTDevice(unittest.TestCase):
    @unittest.skip(
        "Disabled by default - potentially destructive SPI test. Remove this decorator to run."
    )
    def test_spi_read(self):
        """Test basic SPI read operations on discovered devices."""
        cluster_descriptor, umd_tt_devices = setup_spi_test_devices()

        # Test SPI read on each device
        for chip_id, tt_device in umd_tt_devices.items():
            print(
                f"\n=== Testing SPI read on device {chip_id} (remote: {cluster_descriptor.is_chip_remote(chip_id)}) ==="
            )

            # Create SPI implementation for this device
            spi_impl = tt_umd.SPITTDevice.create(tt_device)

            # Test SPI read - board info
            board_info = bytearray(8)
            spi_impl.read(SPI_BOARD_INFO_ADDR, board_info)
            print(f"Board info: {' '.join([f'{b:02x}' for b in board_info])}")

            # Verify board info is not all zeros
            self.assertTrue(
                any(b != 0 for b in board_info),
                f"Board info should not be all zeros for device {chip_id}",
            )

    @unittest.skip(
        "Disabled by default - potentially destructive SPI test. Remove this decorator to run."
    )
    def test_spi_read_modify_write(self):
        """Test SPI read-modify-write operations on discovered devices."""
        cluster_descriptor, umd_tt_devices = setup_spi_test_devices()

        # Test SPI read-modify-write on each device
        for chip_id, tt_device in umd_tt_devices.items():
            print(
                f"\n=== Testing SPI read-modify-write on device {chip_id} (remote: {cluster_descriptor.is_chip_remote(chip_id)}) ==="
            )
            spi_spare_area_addr = get_spi_spare_addr_for_test(tt_device)

            # Create SPI implementation for this device
            spi_impl = tt_umd.SPITTDevice.create(tt_device)

            # Test read-modify-write on spare area
            original = bytearray(2)
            spi_impl.read(spi_spare_area_addr, original)
            print(
                f"Original value at 0x{spi_spare_area_addr:x}: {original[1]:02x}{original[0]:02x}"
            )

            # Increment the value
            new_val = bytearray(original)
            new_val[0] = (new_val[0] + 1) % 256
            if new_val[0] == 0:
                new_val[1] = (new_val[1] + 1) % 256

            # Write back incremented value
            spi_impl.write(spi_spare_area_addr, bytes(new_val))

            # Verify the write
            verify = bytearray(2)
            spi_impl.read(spi_spare_area_addr, verify)
            print(
                f"Updated value at 0x{spi_spare_area_addr:x}: {verify[1]:02x}{verify[0]:02x}"
            )

            self.assertEqual(
                list(new_val),
                list(verify),
                f"SPI write verification failed for device {chip_id}",
            )

    @unittest.skip(
        "Disabled by default - potentially destructive SPI test. Remove this decorator to run."
    )
    def test_spi_uncommitted_write(self):
        """Test SPI uncommitted write operations on discovered devices."""
        cluster_descriptor, umd_tt_devices = setup_spi_test_devices()

        # Test SPI uncommitted write on each device
        for chip_id, tt_device in umd_tt_devices.items():
            print(
                f"\n=== Testing SPI uncommitted write on device {chip_id} (remote: {cluster_descriptor.is_chip_remote(chip_id)}) ==="
            )
            spi_spare_area_addr = get_spi_spare_addr_for_test(tt_device)

            # Create SPI implementation for this device
            spi_impl = tt_umd.SPITTDevice.create(tt_device)

            # Test uncommitted write on spare area
            original = bytearray(2)
            spi_impl.read(spi_spare_area_addr, original)
            print(
                f"Original value at 0x{spi_spare_area_addr:x}: {original[1]:02x}{original[0]:02x}"
            )

            # Increment value again, but this time don't commit it to SPI.
            # This is to verify that the values from SPI are truly fetched.
            new_val = bytearray(original)
            new_val[0] = (new_val[0] + 1) % 256
            if new_val[0] == 0:
                new_val[1] = (new_val[1] + 1) % 256

            # Performs write to the buffer, but doesn't commit it to SPI (skip_write_to_spi=True)
            print(f"SPI write (uncommitted) to 0x{spi_spare_area_addr:x}")
            spi_impl.write(spi_spare_area_addr, bytes(new_val), True)

            # Read back to verify - should NOT match new_val since we didn't actually write to SPI
            verify2 = bytearray(2)
            spi_impl.read(spi_spare_area_addr, verify2)
            print(
                f"Value after uncommitted write at 0x{spi_spare_area_addr:x}: {verify2[1]:02x}{verify2[0]:02x}"
            )

            self.assertNotEqual(
                list(new_val),
                list(verify2),
                f"SPI buffer update on read failed for device {chip_id} - uncommitted write should not change SPI value",
            )
            self.assertEqual(
                list(original),
                list(verify2),
                f"SPI read after uncommitted write should return original value for device {chip_id}",
            )

            # Read wider area
            wide_read = bytearray(8)
            spi_impl.read(spi_spare_area_addr, wide_read)
            wide_value = int.from_bytes(wide_read, byteorder="little")
            print(f"Wide read at 0x{spi_spare_area_addr:x}: {wide_value:016x}")

            # Verify first 2 bytes match the verify2 value (not new_val)
            self.assertEqual(
                wide_read[0], verify2[0], f"First byte mismatch for device {chip_id}"
            )
            self.assertEqual(
                wide_read[1], verify2[1], f"Second byte mismatch for device {chip_id}"
            )


if __name__ == "__main__":
    unittest.main()
