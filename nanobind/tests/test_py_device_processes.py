# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import os
import unittest
import tt_umd


class TestDeviceProcesses(unittest.TestCase):
    def test_returns_list(self):
        procs = tt_umd.PCIDevice.get_device_processes()
        self.assertIsInstance(procs, list)

    def test_own_pid_visible(self):
        pci_ids = tt_umd.PCIDevice.enumerate_devices()
        if not pci_ids:
            self.skipTest("No devices found")

        dev = tt_umd.PCIDevice(pci_ids[0])
        procs = tt_umd.PCIDevice.get_device_processes()
        pids = [p.pid for p in procs]
        self.assertIn(os.getpid(), pids)

    def test_no_duplicates(self):
        procs = tt_umd.PCIDevice.get_device_processes()
        seen = set()
        for p in procs:
            key = (p.pid, p.device)
            self.assertNotIn(key, seen, f"Duplicate: pid={p.pid} device={p.device}")
            seen.add(key)

    def test_sorted(self):
        procs = tt_umd.PCIDevice.get_device_processes()
        for i in range(1, len(procs)):
            self.assertLessEqual(
                (procs[i - 1].device, procs[i - 1].pid),
                (procs[i].device, procs[i].pid),
            )


if __name__ == "__main__":
    unittest.main()
