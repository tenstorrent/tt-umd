# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import tt_umd

class TestTelemetry(unittest.TestCase):
    def test_telemetry(self):
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", pci_ids)
        if (len(pci_ids) == 0):
            print("No PCI devices found.")
            return
    
        # Test telemetry for all available devices
        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()
            tel_reader = dev.get_arc_telemetry_reader()
            tag = int(tt_umd.TelemetryTag.ASIC_TEMPERATURE)
            print(f"Device {pci_id} - Telemetry reading for asic temperature: ", tel_reader.read_entry(tag))
        
    def test_remote_telemetry(self):
        cluster_descriptor = tt_umd.TopologyDiscovery.create_cluster_descriptor()
        umd_tt_devices = {}
        tag = int(tt_umd.TelemetryTag.ASIC_TEMPERATURE)
        chip_to_mmio_map = cluster_descriptor.get_chips_with_mmio()
        chip_eth_coords = cluster_descriptor.get_chip_locations()
        for chip in cluster_descriptor.get_chips_local_first(cluster_descriptor.get_all_chips()):
            if cluster_descriptor.is_chip_mmio_capable(chip):
                print(f"Chip MMIO capable: {chip}")
                umd_tt_devices[chip] = tt_umd.TTDevice.create(chip_to_mmio_map[chip])
                umd_tt_devices[chip].init_tt_device()
                tel_reader = umd_tt_devices[chip].get_arc_telemetry_reader()
                print(f"Telemetry reading for chip {chip} ASIC temperature: ", tel_reader.read_entry(tag))
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")
                umd_tt_devices[chip] = tt_umd.create_remote_wormhole_tt_device(umd_tt_devices[closest_mmio], cluster_descriptor, chip)
                umd_tt_devices[chip].init_tt_device()
                tel_reader = umd_tt_devices[chip].get_arc_telemetry_reader()
                print(f"Telemetry reading for remote chip {chip} ASIC temperature: ", tel_reader.read_entry(tag))

    def test_smbus_telemetry(self):
        """Test SMBUS telemetry reader on wormhole devices"""
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if (len(pci_ids) == 0):
            print("No PCI devices found.")
            return

        # Test SMBUS telemetry for all available devices
        for pci_id in pci_ids:
            dev = tt_umd.TTDevice.create(pci_id)
            dev.init_tt_device()
            
            # Only test SMBUS telemetry on wormhole devices
            if dev.get_arch() == tt_umd.ARCH.WORMHOLE_B0:
                smbus_reader = tt_umd.SmBusArcTelemetryReader(dev)
                if smbus_reader.is_entry_available(tt_umd.wormhole.TelemetryTag.ASIC_TEMPERATURE):
                    temp = smbus_reader.read_entry(tt_umd.wormhole.TelemetryTag.ASIC_TEMPERATURE)
                    print(f"Device {pci_id} - SMBUS telemetry ASIC temperature: {temp}")

