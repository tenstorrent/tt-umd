# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import tt_umd  # Import the nanobind Python module

class TestCluster(unittest.TestCase):
    def test_cluster_creation(self):
        cluster = tt_umd.Cluster()  # Create a Cluster instance
        self.assertIsNotNone(cluster)
        
        cluster_descriptor = cluster.create_cluster_descriptor("", {})
        print("Cluster descriptor:", cluster_descriptor)
        for chip in cluster_descriptor.get_all_chips():
            if cluster_descriptor.is_chip_mmio_capable(chip):
                print(f"Chip MMIO capable: {chip}")
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")

    def test_cluster_functionality(self):
        cluster = tt_umd.Cluster()
        target_device_ids = cluster.get_target_device_ids()
        print("Cluster device IDs:", target_device_ids)
        clocks = cluster.get_clocks()
        print("Cluster clocks:", clocks)
        
class TestTelemetry(unittest.TestCase):
    def test_telemetry(self):
        dev_ids = tt_umd.PCIDevice.enumerate_devices()
        print("Devices found: ", dev_ids)
        if (len(dev_ids) == 0):
            print("No PCI devices found.")
            return
    
        dev = tt_umd.TTDevice.create(dev_ids[0])
        tel_reader = dev.get_arc_telemetry_reader()
        tag = int(tt_umd.wormhole.TelemetryTag.ASIC_TEMPERATURE)
        print("Telemetry reading for asic temperature: ", tel_reader.read_entry(tag))

if __name__ == "__main__":
    unittest.main()
