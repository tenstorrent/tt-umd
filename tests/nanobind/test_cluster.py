# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import tt_umd  # Import the nanobind Python module

class TestCluster(unittest.TestCase):
    def test_cluster_creation(self):
        cluster = tt_umd.Cluster()  # Create a Cluster instance
        self.assertIsNotNone(cluster)

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
