# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import glob
import os
import unittest

import tt_umd

# The simulation bindings only exist when UMD is built with TT_UMD_BUILD_SIMULATION.
SIMULATION_BUILT = hasattr(tt_umd, "create_simulation_tt_device")

if os.environ.get("TT_UMD_SIMULATOR") and not SIMULATION_BUILT:
    raise ImportError(
        "TT_UMD_SIMULATOR is set, but tt_umd was built without simulation support"
    )


def shm_lock_files():
    return set(glob.glob(f"/dev/shm/{tt_umd.RobustMutex.SHM_FILE_PREFIX}*"))


@unittest.skipUnless(SIMULATION_BUILT, "UMD was built without simulation support")
@unittest.skipUnless(os.environ.get("TT_UMD_SIMULATOR"), "TT_UMD_SIMULATOR is not set")
class TestSimulationRemoteDevice(unittest.TestCase):
    """Needs a multichip image with an Ethernet-only chip 1 at (x=1, y=0), such as ttsim's wh_x2."""

    def test_remote_device_declared_private_leaves_no_lock_file(self):
        # A simulated gateway lives in this process only, so its creator declares the remote chip's
        # locks private, and nothing may outlive the process in /dev/shm.
        gateway = tt_umd.create_simulation_tt_device(os.environ["TT_UMD_SIMULATOR"])
        # Taken after the gateway exists: its own host-wide locks are not what this test is about.
        lock_files_before = shm_lock_files()

        remote = tt_umd.create_remote_tt_device_from_coord(
            gateway, rack=0, shelf=0, x=1, y=0, shared_across_processes=False
        )

        self.assertTrue(remote.is_remote())
        self.assertEqual(shm_lock_files() - lock_files_before, set())


if __name__ == "__main__":
    unittest.main()
