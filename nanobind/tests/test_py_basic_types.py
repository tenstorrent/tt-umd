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
