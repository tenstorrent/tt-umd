# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import gc
import os
import shutil
import socket
import tempfile
import unittest
from pathlib import Path

import tt_umd

# The simulation bindings only exist when UMD is built with TT_UMD_BUILD_SIMULATION.
SIMULATION_BUILT = hasattr(tt_umd, "SimulationConnector")

# Setting TT_UMD_SIMULATOR means the simulator tests are expected to run.
# If tt-umd is built without simulation and TT_UMD_SIMULATOR is set, tests will fail.
# Otherwise tests will be skipped
if os.environ.get("TT_UMD_SIMULATOR") and not SIMULATION_BUILT:
    raise ImportError(
        "TT_UMD_SIMULATOR is set, but tt_umd was built without simulation support"
    )

# A host's per-chip sockets are named tt-umd-sim-<chip_id>.sock; a directory holding one is what
# makes UMD classify a path as a live server to attach to.
SOCKET_NAME = "tt-umd-sim-0.sock"


def leave_stale_socket(path):
    """Leave a socket file nothing is serving -- what a crashed host leaves on disk.

    Bound and closed, never listened on, so connect() gets ECONNREFUSED while stat() cannot tell
    it from a live host's socket. Mirrors leave_stale_socket in the C++ test utilities.
    """
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.bind(str(path))
    finally:
        sock.close()


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

    def test_listing_leaves_out_and_clears_up_a_server_whose_host_is_gone(self):
        directory = tt_umd.SimulationConnector.allocate_server_directory()
        try:
            leave_stale_socket(directory / SOCKET_NAME)
            servers = tt_umd.SimulationConnector.list_servers()
            self.assertEqual(
                [server for server in servers if server.directory == directory], []
            )
            # Enumerating is also what clears up, so nothing is left to prune by hand.
            self.assertFalse(directory.exists())
        finally:
            shutil.rmtree(directory, ignore_errors=True)

    def test_prune_dead_servers_reports_what_it_cleared(self):
        directory = tt_umd.SimulationConnector.allocate_server_directory()
        try:
            leave_stale_socket(directory / SOCKET_NAME)
            pruned = [
                server
                for server in tt_umd.SimulationConnector.prune_dead_servers()
                if server.directory == directory
            ]
            self.assertEqual(len(pruned), 1, f"{directory} was not reported as pruned")
            self.assertFalse(directory.exists())
            # Gone means gone: a second sweep has nothing left to report.
            self.assertEqual(
                [
                    server
                    for server in tt_umd.SimulationConnector.prune_dead_servers()
                    if server.directory == directory
                ],
                [],
            )
        finally:
            shutil.rmtree(directory, ignore_errors=True)

    def test_scan_servers_reports_both_halves_from_one_pass(self):
        gone = tt_umd.SimulationConnector.allocate_server_directory()
        coming_up = tt_umd.SimulationConnector.allocate_server_directory()
        try:
            leave_stale_socket(gone / SOCKET_NAME)
            live, removed = tt_umd.SimulationConnector.scan_servers()
            self.assertEqual(
                [s.directory for s in removed if s.directory == gone], [gone]
            )
            self.assertEqual([s.directory for s in live if s.directory == gone], [])
            # The socket-less directory is on the other side of the same pass.
            self.assertEqual(
                [s.directory for s in live if s.directory == coming_up], [coming_up]
            )
            self.assertFalse(gone.exists())
            self.assertTrue(coming_up.is_dir())
        finally:
            shutil.rmtree(gone, ignore_errors=True)
            shutil.rmtree(coming_up, ignore_errors=True)

    def test_prune_leaves_a_directory_with_no_socket_yet_alone(self):
        # Indistinguishable from a host still coming up, so the sweep must not take it.
        directory = tt_umd.SimulationConnector.allocate_server_directory()
        try:
            pruned = [
                server
                for server in tt_umd.SimulationConnector.prune_dead_servers()
                if server.directory == directory
            ]
            self.assertEqual(pruned, [])
            self.assertTrue(directory.is_dir())
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

            host_connection, host_devices, host_cluster = (
                tt_umd.SimulationConnector.discover(host_options)
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
            client_connection, client_devices, client_cluster = (
                tt_umd.SimulationConnector.discover(client_options)
            )
            self.assertEqual(sorted(client_devices), sorted(host_devices))

            # The point of reporting the topology: a client learns how the cluster looks without a
            # local simulator build, and sees the same chips the host does.
            self.assertIsNotNone(client_cluster)
            self.assertEqual(
                client_cluster.get_all_chips(), host_cluster.get_all_chips()
            )

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

        connection, _devices, _cluster = tt_umd.SimulationConnector.discover(options)
        try:
            self.assertNotEqual(connection.server_directory, Path(""))
            self.assertTrue(connection.server_directory.is_dir())
            self.assertEqual(connection.sockets[0].parent, connection.server_directory)
        finally:
            shutil.rmtree(connection.server_directory, ignore_errors=True)


def write_and_read_back(writer, reader):
    """Write a word through writer to its first Tensix core's L1, and read it back through reader."""
    soc = writer.get_soc_descriptor()
    tensix = soc.get_cores(tt_umd.CoreType.TENSIX)[0]
    core = soc.translate_chip_coord_to_translated(tensix)
    writer.noc_write32(core.x, core.y, 0x1000, 0xDEADBEEF)
    return reader.noc_read32(core.x, core.y, 0x1000)


def attach_client(server_directory):
    """The devices of a client attached to the server serving in server_directory."""
    options = tt_umd.SimulationConnectorOptions()
    options.simulator_directory = server_directory
    _connection, devices, _cluster = tt_umd.SimulationConnector.discover(options)
    return devices


class ServingTestCase(unittest.TestCase):
    """Keeps every client, server and device on self and drops them after each test, failed or not.

    A failing test's traceback would otherwise keep its devices alive into the next test's setUp.
    """

    def setUp(self):
        self.server_directory = tt_umd.SimulationConnector.allocate_server_directory()
        self.addCleanup(shutil.rmtree, self.server_directory, ignore_errors=True)
        self.client_devices = None
        self.server = None
        self.devices = None
        # Cleanups run last-registered first, so the directory is removed after everything is dropped.
        self.addCleanup(self.drop_clients_server_and_devices)

    def drop_clients_server_and_devices(self):
        # Clients before the server they attach to, the server before the devices it serves.
        self.client_devices = None
        self.server = None
        self.devices = None
        gc.collect()

    def attach_client(self):
        self.client_devices = attach_client(self.server_directory)
        return self.client_devices


@unittest.skipUnless(SIMULATION_BUILT, "UMD was built without simulation support")
@unittest.skipUnless(os.environ.get("TT_UMD_SIMULATOR"), "TT_UMD_SIMULATOR is not set")
class TestSimulationConnectorServe(ServingTestCase):
    """SimulationConnector.serve over the devices of a private simulator host."""

    def setUp(self):
        super().setUp()
        options = tt_umd.SimulationConnectorOptions()
        options.simulator_directory = os.environ["TT_UMD_SIMULATOR"]
        _connection, self.devices, self.cluster_descriptor = (
            tt_umd.SimulationConnector.discover(options)
        )

    def serve(self):
        self.server = tt_umd.SimulationConnector.serve(
            self.devices, self.cluster_descriptor, self.server_directory
        )
        return self.server

    def test_serves_a_socket_for_every_device_in_the_directory_given(self):
        server = self.serve()

        self.assertEqual(server.server_directory(), self.server_directory)
        self.assertEqual(sorted(server.sockets()), sorted(self.devices))
        self.assertEqual(server.sockets()[0], self.server_directory / SOCKET_NAME)

    def test_client_reads_what_the_served_device_wrote(self):
        self.serve()
        client_devices = self.attach_client()

        self.assertEqual(
            write_and_read_back(self.devices[0], client_devices[0]), 0xDEADBEEF
        )

    def test_serves_devices_removed_from_the_dict_it_was_given(self):
        self.serve()
        self.devices.clear()
        gc.collect()

        client_devices = self.attach_client()

        self.assertEqual(
            write_and_read_back(client_devices[0], client_devices[0]), 0xDEADBEEF
        )

    def test_dropping_the_server_removes_its_sockets_and_claimed_directory(self):
        socket_path = self.serve().sockets()[0]

        self.server = None
        gc.collect()

        self.assertFalse(socket_path.exists())
        # Claimed by allocate_server_directory and left empty, so it goes too.
        self.assertFalse(self.server_directory.exists())

    def test_empty_device_map_raises(self):
        with self.assertRaises(tt_umd.error.UmdBaseException):
            tt_umd.SimulationConnector.serve(
                {}, self.cluster_descriptor, self.server_directory
            )


@unittest.skipUnless(SIMULATION_BUILT, "UMD was built without simulation support")
@unittest.skipUnless(os.environ.get("TT_UMD_SIMULATOR"), "TT_UMD_SIMULATOR is not set")
class TestSimulationConnectorDiscoverServing(ServingTestCase):
    """discover(serve_over_sockets) serves until the last device it returned is dropped."""

    def setUp(self):
        super().setUp()
        options = tt_umd.SimulationConnectorOptions()
        options.simulator_directory = os.environ["TT_UMD_SIMULATOR"]
        options.serve_over_sockets = True
        options.server_directory = self.server_directory
        _connection, self.devices, _cluster = tt_umd.SimulationConnector.discover(
            options
        )
        self.socket_path = self.server_directory / SOCKET_NAME

    def test_serves_while_one_device_is_kept(self):
        self.devices = {0: self.devices[0]}
        gc.collect()

        client_devices = self.attach_client()
        self.assertEqual(
            write_and_read_back(self.devices[0], client_devices[0]), 0xDEADBEEF
        )

    def test_stops_serving_when_every_device_is_dropped(self):
        self.devices = None
        gc.collect()

        self.assertFalse(self.socket_path.exists())


@unittest.skipUnless(SIMULATION_BUILT, "UMD was built without simulation support")
@unittest.skipIf(
    os.environ.get("TT_UMD_SIMULATOR"), "TT_UMD_SIMULATOR selects a simulator"
)
class TestSimulationConnectorServeSilicon(ServingTestCase):
    """SimulationConnector.serve over silicon devices: any TTDevice can be served."""

    def setUp(self):
        if not tt_umd.PCIDevice.enumerate_devices():
            self.skipTest("No Tenstorrent card.")
        super().setUp()
        self.cluster_descriptor, self.devices = tt_umd.TopologyDiscovery.discover()
        self.server = tt_umd.SimulationConnector.serve(
            self.devices, self.cluster_descriptor, self.server_directory
        )

    def test_client_reads_what_the_served_device_wrote(self):
        client_devices = self.attach_client()

        self.assertEqual(
            write_and_read_back(self.devices[0], client_devices[0]), 0xDEADBEEF
        )

    def test_served_device_reads_what_the_client_wrote(self):
        client_devices = self.attach_client()

        self.assertEqual(
            write_and_read_back(client_devices[0], self.devices[0]), 0xDEADBEEF
        )

    def test_client_of_served_silicon_is_backed_by_ttsim(self):
        # The served device itself is silicon, which no simulator backs.
        self.assertIsNone(tt_umd.get_simulation_backend(self.devices[0]))

        client_devices = self.attach_client()

        # The server protocol has no other kind than the simulators', so served silicon answers TTSIM.
        self.assertEqual(
            tt_umd.get_simulation_backend(client_devices[0]),
            tt_umd.SimulationBackendType.TTSIM,
        )


if __name__ == "__main__":
    unittest.main()
