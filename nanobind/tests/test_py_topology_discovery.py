# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import unittest
import tt_umd


class TestTopologyDiscovery(unittest.TestCase):
    def test_cluster_descriptor(self):
        cluster_descriptor = tt_umd.TopologyDiscovery.create_cluster_descriptor()
        print("Cluster descriptor:", cluster_descriptor)
        for chip in cluster_descriptor.get_all_chips():
            if cluster_descriptor.is_chip_mmio_capable(chip):
                print(f"Chip MMIO capable: {chip}")
            else:
                closest_mmio = cluster_descriptor.get_closest_mmio_capable_chip(chip)
                print(f"Chip remote: {chip}, closest MMIO capable chip: {closest_mmio}")

        print(
            "All chips but local first: ",
            cluster_descriptor.get_chips_local_first(
                cluster_descriptor.get_all_chips()
            ),
        )

        for chip in cluster_descriptor.get_all_chips():
            print(f"Chip id {chip} has arch {cluster_descriptor.get_arch(chip)}")

        # Test get_board_type and get_board_id_for_chip
        for chip in cluster_descriptor.get_all_chips():
            board_type = cluster_descriptor.get_board_type(chip)
            board_id = cluster_descriptor.get_board_id_for_chip(chip)
            print(f"Chip {chip}: board_type={board_type}, board_id={board_id}")

        # Test get_chip_unique_ids
        print("\n=== Testing get_chip_unique_ids ===")
        chip_unique_ids = cluster_descriptor.get_chip_unique_ids()
        print(f"Number of chips with unique IDs: {len(chip_unique_ids)}")
        for chip_id, unique_id in chip_unique_ids.items():
            print(f"  Chip {chip_id}: unique_id={unique_id}")

    def test_no_remote_discovery_true(self):
        """Test that with no_remote_discovery=True, only local chips are discovered."""
        options = tt_umd.TopologyDiscoveryOptions()
        options.no_remote_discovery = True

        cluster_descriptor = tt_umd.TopologyDiscovery.create_cluster_descriptor(options)
        all_chips = cluster_descriptor.get_all_chips()

        print(f"Discovery with no_remote_discovery=True found {len(all_chips)} chips")

        # Count local and remote chips
        local_chips = [
            chip for chip in all_chips if cluster_descriptor.is_chip_mmio_capable(chip)
        ]
        remote_chips = [
            chip
            for chip in all_chips
            if not cluster_descriptor.is_chip_mmio_capable(chip)
        ]

        print(f"Local (MMIO-capable) chips: {len(local_chips)}")
        print(f"Remote chips: {len(remote_chips)}")

        # With no_remote_discovery=True, should only have local chips
        self.assertEqual(
            len(remote_chips),
            0,
            "With no_remote_discovery=True, should not discover any remote chips",
        )
