# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import tempfile
import unittest
from pathlib import Path

import tt_umd


def find_soc_descriptor(name):
    """A SoC descriptor UMD ships, for a simulator to ship as its soc_descriptor.yaml.

    Looked up like the C++ tests do (UMD_DESCRIPTORS_PATH, else descriptors/ in the working directory), and else in
    the checkout these tests sit in.
    """
    roots = [Path(os.environ.get("UMD_DESCRIPTORS_PATH", "descriptors"))]
    roots += [parent / "descriptors" for parent in Path(__file__).resolve().parents]
    for root in roots:
        if (root / "soc_descs" / name).is_file():
            return root / "soc_descs" / name
    raise FileNotFoundError(f"no descriptors/soc_descs/{name} found")


QUASAR_SOC_DESCRIPTOR = find_soc_descriptor("quasar_32_arch.yaml")


# Every simulation call is bound in every build; the simulation TTDevice classes are bound only in a build that can run
# a simulator. Gating on them keeps the probe off the calls the gated tests check.
SIMULATION_SUPPORTED = hasattr(tt_umd, "RtlSimulationTTDevice")


def simulator_under_test():
    """The simulator TT_UMD_SIMULATOR names, or None when the tests run without one."""
    return os.environ.get("TT_UMD_SIMULATOR")


class TestSimulatorSocDescriptorPath(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.library = self.directory / "libttsim.so"
        self.library.touch()

    def test_finds_soc_descriptor_beside_ttsim_library(self):
        shutil.copyfile(QUASAR_SOC_DESCRIPTOR, self.directory / "soc_descriptor.yaml")

        found = tt_umd.find_simulator_soc_descriptor_path(str(self.library))

        self.assertTrue(Path(found).samefile(self.directory / "soc_descriptor.yaml"))

    def test_ttsim_library_without_soc_descriptor_ships_none(self):
        self.assertEqual(
            tt_umd.find_simulator_soc_descriptor_path(str(self.library)), ""
        )


class TestSimulatorArch(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)

    @unittest.skipUnless(
        SIMULATION_SUPPORTED, "UMD was built without simulation support"
    )
    def test_rtl_directory_reports_the_shipped_arch(self):
        shutil.copyfile(QUASAR_SOC_DESCRIPTOR, self.directory / "soc_descriptor.yaml")

        self.assertEqual(
            tt_umd.get_simulator_arch(str(self.directory)), tt_umd.ARCH.QUASAR
        )

    @unittest.skipUnless(
        SIMULATION_SUPPORTED, "UMD was built without simulation support"
    )
    def test_rtl_directory_without_soc_descriptor_raises(self):
        with self.assertRaises(tt_umd.error.UmdBaseException):
            tt_umd.get_simulator_arch(str(self.directory))

    @unittest.skipIf(SIMULATION_SUPPORTED, "UMD was built with simulation support")
    def test_raises_without_simulation_support(self):
        shutil.copyfile(QUASAR_SOC_DESCRIPTOR, self.directory / "soc_descriptor.yaml")

        with self.assertRaises(tt_umd.error.UmdBaseException):
            tt_umd.get_simulator_arch(str(self.directory))


class TestSimulationBackend(unittest.TestCase):
    def test_backend_types_are_bound_in_every_build(self):
        self.assertNotEqual(
            tt_umd.SimulationBackendType.TTSIM, tt_umd.SimulationBackendType.RTL
        )

    def test_device_reached_through_kmd_has_no_simulation_backend(self):
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if simulator_under_test() or not pci_ids:
            self.skipTest("Needs a device reached through the KMD.")

        device = tt_umd.TTDevice.create(pci_ids[0])

        self.assertIsNone(device.get_simulation_backend())

    def test_ttsim_device_is_backed_by_ttsim(self):
        simulator = simulator_under_test()
        if not simulator or not os.path.isfile(simulator):
            self.skipTest("TT_UMD_SIMULATOR does not name a TTSim library.")
        options = tt_umd.SimulationConnectorOptions()
        options.simulator_directory = simulator

        _connection, devices, _cluster = tt_umd.SimulationConnector.discover(options)

        self.assertEqual(
            devices[0].get_simulation_backend(),
            tt_umd.SimulationBackendType.TTSIM,
        )


if __name__ == "__main__":
    unittest.main()
