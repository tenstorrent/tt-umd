# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import tempfile
import unittest
from pathlib import Path

import tt_umd

# The simulation bindings only exist when UMD is built with TT_UMD_BUILD_SIMULATION.
SIMULATION_BUILT = hasattr(tt_umd, "SimulationConnector")

# A host's per-chip sockets are named tt-umd-sim-<chip_id>.sock; a directory holding one is what
# makes UMD classify a path as a live server to attach to.
SOCKET_NAME = "tt-umd-sim-0.sock"


@unittest.skipUnless(SIMULATION_BUILT, "UMD was built without simulation support")
class TestSimulationConnector(unittest.TestCase):
    def test_role_for_directory_without_sockets_is_host(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(
                tt_umd.SimulationConnector.role_for(directory),
                tt_umd.SimulationConnector.Role.HOST,
            )

    def test_allocate_server_directory_claims_distinct_directories(self):
        first = tt_umd.SimulationConnector.allocate_server_directory()
        try:
            second = tt_umd.SimulationConnector.allocate_server_directory()
            try:
                self.assertTrue(first.is_dir())
                self.assertTrue(second.is_dir())
                self.assertNotEqual(first, second)
            finally:
                shutil.rmtree(second, ignore_errors=True)
        finally:
            shutil.rmtree(first, ignore_errors=True)

    def test_list_servers_reports_an_allocated_directory(self):
        directory = tt_umd.SimulationConnector.allocate_server_directory()
        try:
            servers = tt_umd.SimulationConnector.list_servers()
            listed = [server for server in servers if server.directory == directory]
            self.assertEqual(len(listed), 1, f"{directory} missing from {servers}")
            # Nothing serves in it yet, so it has no per-chip sockets.
            self.assertEqual(listed[0].sockets, {})
            self.assertGreaterEqual(listed[0].index, 0)
        finally:
            shutil.rmtree(directory, ignore_errors=True)


@unittest.skipUnless(SIMULATION_BUILT, "UMD was built without simulation support")
@unittest.skipUnless(os.environ.get("TT_UMD_SIMULATOR"), "TT_UMD_SIMULATOR is not set")
class TestSimulationConnectorAgainstSimulator(unittest.TestCase):
    """Mirrors tests/api/test_simulation_connector.cpp; needs a real simulator build."""

    def test_host_serves_a_socket_a_client_can_attach_to(self):
        server_directory = tt_umd.SimulationConnector.allocate_server_directory()
        try:
            host_options = tt_umd.SimulationConnectorOptions()
            host_options.simulator_directory = os.environ["TT_UMD_SIMULATOR"]
            host_options.serve_over_sockets = True
            host_options.server_directory = server_directory

            self.assertEqual(
                tt_umd.SimulationConnector.role_for(host_options.simulator_directory),
                tt_umd.SimulationConnector.Role.HOST,
            )

            host_connection, host_devices = tt_umd.SimulationConnector.discover(
                host_options
            )
            self.assertEqual(len(host_devices), 1)
            self.assertTrue((server_directory / SOCKET_NAME).exists())

            # The host reports what it opened and where it serves.
            self.assertEqual(host_connection.role, tt_umd.SimulationConnector.Role.HOST)
            self.assertEqual(
                host_connection.simulator, Path(os.environ["TT_UMD_SIMULATOR"])
            )
            self.assertEqual(
                host_connection.backend, tt_umd.SimulationBackendType.TTSIM
            )
            self.assertEqual(host_connection.server_directory, server_directory)
            self.assertEqual(
                host_connection.sockets, {0: server_directory / SOCKET_NAME}
            )

            # The server directory now holds a live socket, so pointing at it makes us a client.
            self.assertEqual(
                tt_umd.SimulationConnector.role_for(server_directory),
                tt_umd.SimulationConnector.Role.CLIENT,
            )

            client_options = tt_umd.SimulationConnectorOptions()
            client_options.simulator_directory = server_directory
            client_connection, client_devices = tt_umd.SimulationConnector.discover(
                client_options
            )
            self.assertEqual(sorted(client_devices), sorted(host_devices))

            # The client builds the device class matching the backend the host reports, so the
            # concrete type is visible from Python and not just the TTDevice base.
            expected_type = (
                tt_umd.TTSimTTDevice
                if host_connection.backend == tt_umd.SimulationBackendType.TTSIM
                else tt_umd.RtlSimulationTTDevice
            )
            self.assertIsInstance(host_devices[0], expected_type)
            self.assertIsInstance(client_devices[0], expected_type)

            # The client reports the directory it attached to, and the simulator the host
            # named over the wire.
            self.assertEqual(
                client_connection.role, tt_umd.SimulationConnector.Role.CLIENT
            )
            self.assertEqual(client_connection.server_directory, server_directory)
            self.assertEqual(client_connection.simulator, host_connection.simulator)
            self.assertEqual(client_connection.backend, host_connection.backend)
            self.assertEqual(client_connection.arch, host_connection.arch)

            # What the client writes over the socket, the host sees directly.
            client = client_devices[0]
            soc = client.get_soc_descriptor()
            tensix = soc.get_cores(tt_umd.CoreType.TENSIX)[0]
            core = soc.translate_chip_coord_to_translated(tensix)
            client.noc_write32(core.x, core.y, 0x1000, 0xDEADBEEF)
            self.assertEqual(client.noc_read32(core.x, core.y, 0x1000), 0xDEADBEEF)
            self.assertEqual(
                host_devices[0].noc_read32(core.x, core.y, 0x1000), 0xDEADBEEF
            )
        finally:
            shutil.rmtree(server_directory, ignore_errors=True)

    def test_host_learns_the_server_directory_umd_allocated(self):
        options = tt_umd.SimulationConnectorOptions()
        options.simulator_directory = os.environ["TT_UMD_SIMULATOR"]
        options.serve_over_sockets = True
        # server_directory left empty: UMD allocates one and must report it back.

        connection, _devices = tt_umd.SimulationConnector.discover(options)
        try:
            self.assertNotEqual(connection.server_directory, Path(""))
            self.assertTrue(connection.server_directory.is_dir())
            self.assertEqual(connection.sockets[0].parent, connection.server_directory)
        finally:
            shutil.rmtree(connection.server_directory, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
