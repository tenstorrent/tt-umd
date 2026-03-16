# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import unittest
import tt_umd


class TestTelemetry(unittest.TestCase):
    def test_telemetry(self):
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", pci_ids)
        if len(pci_ids) == 0:
            print("No PCI devices found.")
            return

        # Test telemetry for all available devices
        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()
            tel_reader = dev.get_arc_telemetry_reader()
            tag = int(tt_umd.TelemetryTag.ASIC_TEMPERATURE)
            print(
                f"Device {pci_id} - Telemetry reading for asic temperature: ",
                tel_reader.read_entry(tag),
            )

    def test_remote_telemetry(self):
        cluster_descriptor, umd_tt_devices = tt_umd.TopologyDiscovery.discover()
        tag = int(tt_umd.TelemetryTag.ASIC_TEMPERATURE)
        for chip, dev in umd_tt_devices.items():
            tel_reader = umd_tt_devices[chip].get_arc_telemetry_reader()
            print(
                f"Telemetry reading for {'local' if dev.is_remote() else 'remote'} chip {chip} ASIC temperature: ",
                tel_reader.read_entry(tag),
            )

    def test_smbus_telemetry(self):
        """Test SMBUS telemetry reader on wormhole devices"""
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if len(pci_ids) == 0:
            print("No PCI devices found.")
            return

        # Test SMBUS telemetry for all available devices
        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()

            # Only test SMBUS telemetry on wormhole devices
            if dev.get_arch() == tt_umd.ARCH.WORMHOLE_B0:
                smbus_reader = tt_umd.SmBusArcTelemetryReader(dev)
                if smbus_reader.is_entry_available(
                    tt_umd.wormhole.TelemetryTag.ASIC_TEMPERATURE
                ):
                    temp = smbus_reader.read_entry(
                        tt_umd.wormhole.TelemetryTag.ASIC_TEMPERATURE
                    )
                    print(f"Device {pci_id} - SMBUS telemetry ASIC temperature: {temp}")

    def test_gddr_telemetry(self):
        """Test GDDR telemetry (temperatures, errors, status) for DRAM monitoring."""
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if len(pci_ids) == 0:
            print("No PCI devices found.")
            return

        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()

            fw_info = tt_umd.FirmwareInfoProvider.create_firmware_info_provider(dev)

            # Test DRAM speed (available on Blackhole and Wormhole >= 18.4)
            dram_speed = fw_info.get_dram_speed()
            if dram_speed is not None:
                print(f"Device {pci_id} - GDDR speed: {dram_speed} Mbps")

            # GDDR telemetry (temperatures, errors) is only available on Blackhole
            if dev.get_arch() != tt_umd.ARCH.BLACKHOLE:
                print(
                    f"Device {pci_id} - Skipping detailed GDDR telemetry (not Blackhole, arch={dev.get_arch()})"
                )
                continue

            # Test aggregated GDDR telemetry
            gddr_telemetry = fw_info.get_aggregated_dram_telemetry()
            if gddr_telemetry is None:
                print(f"Device {pci_id} - GDDR telemetry not available")
                continue

            # Test max DRAM temperature
            max_temp = fw_info.get_current_max_dram_temperature()
            if max_temp is not None:
                print(f"Device {pci_id} - Max GDDR temperature: {max_temp}C")

            # Print per-module telemetry from aggregated data
            print(f"Device {pci_id} - Per-module GDDR telemetry:")
            for gddr_index, module_telemetry in gddr_telemetry.modules.items():
                print(
                    f"  GDDR_{int(gddr_index)}: "
                    f"top={module_telemetry.dram_temperature_top}C "
                    f"bottom={module_telemetry.dram_temperature_bottom}C "
                    f"corr_rd={module_telemetry.corr_edc_rd_errors} "
                    f"corr_wr={module_telemetry.corr_edc_wr_errors} "
                    f"uncorr_rd={module_telemetry.uncorr_edc_rd_error} "
                    f"uncorr_wr={module_telemetry.uncorr_edc_wr_error}"
                )

            # Test individual module telemetry access
            print(f"Device {pci_id} - Testing individual module access:")
            for gddr_index in [
                tt_umd.GddrModule.GDDR_0,
                tt_umd.GddrModule.GDDR_1,
                tt_umd.GddrModule.GDDR_7,
            ]:
                module_telemetry = fw_info.get_dram_telemetry(gddr_index)
                if module_telemetry is not None:
                    print(
                        f"  GDDR_{int(gddr_index)}: "
                        f"top={module_telemetry.dram_temperature_top}C "
                        f"bottom={module_telemetry.dram_temperature_bottom}C"
                    )
