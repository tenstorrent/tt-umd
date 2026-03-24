# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

import unittest
import tt_umd


class TestBasicTypes(unittest.TestCase):
    def test_eth_coord(self):
        eth_coord = tt_umd.EthCoord(0, 1, 2, 3, 4)

    def test_tt_xy_pair(self):
        xy_pair = tt_umd.tt_xy_pair(1, 2)
        self.assertEqual(str(xy_pair), "(1, 2)")

    def test_arch(self):
        for arch in tt_umd.ARCH:
            self.assertEqual(arch, tt_umd.ARCH.from_str(str(arch)))
        self.assertEqual(str(tt_umd.ARCH.WORMHOLE_B0), "wormhole_b0")

    def test_risc_type_values(self):
        self.assertEqual(int(tt_umd.RiscType.NONE), 0)
        self.assertEqual(int(tt_umd.RiscType.ALL), 1)
        self.assertNotEqual(int(tt_umd.RiscType.BRISC), 0)
        self.assertNotEqual(int(tt_umd.RiscType.DM0), 0)

    def test_risc_type_str(self):
        s = str(tt_umd.RiscType.BRISC)
        self.assertIsInstance(s, str)
        self.assertTrue(len(s) > 0)

    def test_risc_type_bitwise(self):
        combined = tt_umd.RiscType.BRISC | tt_umd.RiscType.NCRISC
        self.assertEqual(
            int(combined), int(tt_umd.RiscType.BRISC) | int(tt_umd.RiscType.NCRISC)
        )
        masked = combined & tt_umd.RiscType.BRISC
        self.assertEqual(int(masked), int(tt_umd.RiscType.BRISC))

    def test_risc_type_invert(self):
        inverted = ~tt_umd.RiscType.BRISC
        # invert_selected_options masks to valid bits, so BRISC should not be set.
        self.assertEqual(int(inverted & tt_umd.RiscType.BRISC), 0)
        # But other valid bits should be set.
        self.assertNotEqual(int(inverted), 0)

    def test_tensix_soft_reset_options_values(self):
        self.assertEqual(int(tt_umd.TensixSoftResetOptions.NONE), 0)
        self.assertNotEqual(int(tt_umd.TensixSoftResetOptions.BRISC), 0)

    def test_tensix_soft_reset_options_bitwise(self):
        combined = (
            tt_umd.TensixSoftResetOptions.TRISC0
            | tt_umd.TensixSoftResetOptions.TRISC1
            | tt_umd.TensixSoftResetOptions.TRISC2
        )
        self.assertEqual(
            int(combined), int(tt_umd.TensixSoftResetOptions.ALL_TRISC_SOFT_RESET)
        )

    def test_tensix_soft_reset_options_invert(self):
        inverted = ~tt_umd.TensixSoftResetOptions.BRISC
        # invert_selected_options masks to valid bits, so BRISC should not be set.
        self.assertEqual(int(inverted & tt_umd.TensixSoftResetOptions.BRISC), 0)
        # But other valid bits should be set.
        self.assertNotEqual(int(inverted), 0)
