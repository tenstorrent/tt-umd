# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
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
        cluster_descriptor, umd_tt_devices = tt_umd.TopologyDiscovery.discover()
        tag = int(tt_umd.TelemetryTag.ASIC_TEMPERATURE)
        for chip, dev in umd_tt_devices.items():
            tel_reader = umd_tt_devices[chip].get_arc_telemetry_reader()
            print(f"Telemetry reading for {'local' if dev.is_remote() else 'remote'} chip {chip} ASIC temperature: ", tel_reader.read_entry(tag))

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

