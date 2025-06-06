# SPDX-FileCopyrightText: 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0
import unittest
import libdevice  # Import the nanobind Python module

class TestCluster(unittest.TestCase):
    def test_cluster_creation(self):
        cluster = libdevice.Cluster()  # Create a Cluster instance
        self.assertIsNotNone(cluster)

    def test_cluster_get_ids(self):
        cluster = libdevice.Cluster()
        result = cluster.get_target_device_ids()
        print("Cluster device IDs:", result)

if __name__ == "__main__":
    unittest.main()
