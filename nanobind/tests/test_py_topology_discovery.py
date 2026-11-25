# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import tempfile
import unittest
from pathlib import Path

import tt_umd
from test_py_simulation_connector import leave_stale_socket
from test_py_simulator_info import find_soc_descriptor

# The simulator only runs when UMD is built with TT_UMD_BUILD_SIMULATION.
SIMULATION_BUILT = hasattr(tt_umd, "SimulationConnector")


# One chip of each architecture, for an image of the other one to be declared with.
DECLARED_WORMHOLE_N150 = """arch:
  0: wormhole_b0
chips:
  0: [0, 0, 0, 0]
chip_unique_ids:
  0: 5550000
ethernet_connections: []
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 64}
boards:
  - - board_id: 1649267441665
    - board_type: n150
    - chips: [0]
"""

DECLARED_BLACKHOLE_P150 = """arch:
  0: blackhole
chips: {}
chip_unique_ids:
  0: 5550000
ethernet_connections: []
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 192, dram_harvesting_mask: 0, eth_harvesting_mask: 12288,
      pcie_harvesting_mask: 2}
boards:
  - - board_id: 4398046511105
    - board_type: p150
    - chips: [0]
"""


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
        options.discover_remote_devices = False

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


class TestSimulationDiscoveryOptions(unittest.TestCase):
    def test_host_mem_channels_default_to_sized_from_topology(self):
        self.assertIsNone(tt_umd.SimulationDiscoveryOptions().num_host_mem_channels)

    def test_host_mem_channels_accept_a_count_and_none(self):
        options = tt_umd.SimulationDiscoveryOptions()

        options.num_host_mem_channels = 2
        self.assertEqual(options.num_host_mem_channels, 2)
        options.num_host_mem_channels = None
        self.assertIsNone(options.num_host_mem_channels)

    def test_launch_simulator_defaults_to_true(self):
        self.assertTrue(tt_umd.SimulationDiscoveryOptions().launch_simulator)

    def test_launch_simulator_can_be_turned_off(self):
        options = tt_umd.SimulationDiscoveryOptions()

        options.launch_simulator = False
        self.assertFalse(options.launch_simulator)

    def test_target_devices_default_to_every_chip(self):
        self.assertEqual(tt_umd.SimulationDiscoveryOptions().target_devices, set())

    def test_target_devices_accept_a_set_of_chip_ids(self):
        options = tt_umd.SimulationDiscoveryOptions()

        options.target_devices = {0, 1}
        self.assertEqual(options.target_devices, {0, 1})


@unittest.skipUnless(SIMULATION_BUILT, "UMD was built without simulation support")
class TestSimulationDiscovery(unittest.TestCase):
    """Discovery of the Wormhole or Blackhole TTSim image TT_UMD_SIMULATOR names."""

    def setUp(self):
        self.simulator = os.environ.get("TT_UMD_SIMULATOR")
        if not self.simulator or not os.path.isfile(self.simulator):
            self.skipTest("TT_UMD_SIMULATOR does not name a TTSim library.")
        self.arch = tt_umd.get_simulator_arch(self.simulator)
        if self.arch not in (tt_umd.ARCH.WORMHOLE_B0, tt_umd.ARCH.BLACKHOLE):
            self.skipTest("TTSim models neither ARC nor Ethernet for Quasar.")

    def discovery_options(self, simulator_path):
        options = tt_umd.TopologyDiscoveryOptions()
        options.simulation = tt_umd.SimulationDiscoveryOptions()
        options.simulation.simulator_path = str(simulator_path)
        return options

    def test_discovers_a_device_for_every_chip(self):
        cluster_descriptor, devices = tt_umd.TopologyDiscovery.discover(
            self.discovery_options(self.simulator)
        )

        self.assertGreaterEqual(len(devices), 1)
        self.assertEqual(len(devices), len(cluster_descriptor.get_all_chips()))

    def test_unknown_target_device_raises(self):
        options = self.discovery_options(self.simulator)
        options.simulation.target_devices = {99}

        with self.assertRaises(tt_umd.error.UmdBaseException):
            tt_umd.TopologyDiscovery.discover(options)

    def test_declared_chip_of_another_arch_raises(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        library = Path(directory.name) / "libttsim.so"
        shutil.copyfile(self.simulator, library)
        # The copy ships its architecture's SoC descriptor, so discovery reads the architecture from that file
        # instead of running the image after other simulators are up in this process.
        if self.arch == tt_umd.ARCH.WORMHOLE_B0:
            soc_descriptor, other_arch = (
                "wormhole_b0_8x10.yaml",
                DECLARED_BLACKHOLE_P150,
            )
        else:
            soc_descriptor, other_arch = (
                "blackhole_140_arch.yaml",
                DECLARED_WORMHOLE_N150,
            )
        shutil.copyfile(
            find_soc_descriptor(soc_descriptor),
            Path(directory.name) / "soc_descriptor.yaml",
        )
        (Path(directory.name) / "cluster_descriptor.yaml").write_text(other_arch)

        with self.assertRaises(tt_umd.error.UmdBaseException):
            tt_umd.TopologyDiscovery.discover(self.discovery_options(library))


@unittest.skipUnless(SIMULATION_BUILT, "UMD was built without simulation support")
class TestSimulationServerDiscovery(unittest.TestCase):
    """Discovery attaching to a simulation server that serves the TTSim image TT_UMD_SIMULATOR names."""

    def setUp(self):
        simulator = os.environ.get("TT_UMD_SIMULATOR")
        if not simulator or not os.path.isfile(simulator):
            self.skipTest("TT_UMD_SIMULATOR does not name a TTSim library.")
        self.server_directory = tt_umd.SimulationConnector.allocate_server_directory()
        self.addCleanup(shutil.rmtree, self.server_directory, ignore_errors=True)
        host_options = tt_umd.SimulationConnectorOptions()
        host_options.simulator_directory = simulator
        host_options.serve_over_sockets = True
        host_options.server_directory = self.server_directory
        self.host_connection, self.host_devices, _ = (
            tt_umd.SimulationConnector.discover(host_options)
        )
        # Cleanups run last-registered first: the host stops serving before its directory is removed.
        self.addCleanup(delattr, self, "host_devices")

    def client_options(self):
        options = tt_umd.TopologyDiscoveryOptions()
        options.simulation = tt_umd.SimulationDiscoveryOptions()
        options.simulation.simulator_path = str(self.server_directory)
        # A chip the host reaches over Ethernet has no socket of its own to attach to.
        options.discover_remote_devices = False
        return options

    def test_attaches_a_device_to_every_served_socket(self):
        cluster_descriptor, devices = tt_umd.TopologyDiscovery.discover(
            self.client_options()
        )

        self.assertEqual(sorted(devices), sorted(self.host_connection.sockets))
        self.assertEqual(
            sorted(cluster_descriptor.get_all_chips()),
            sorted(self.host_connection.sockets),
        )

    def test_directory_without_a_live_socket_raises(self):
        for socket_path in self.host_connection.sockets.values():
            socket_path.unlink()
            leave_stale_socket(socket_path)

        with self.assertRaises(tt_umd.error.UmdBaseException):
            tt_umd.TopologyDiscovery.discover(self.client_options())
