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

if __name__ == "__main__":
    unittest.main()
