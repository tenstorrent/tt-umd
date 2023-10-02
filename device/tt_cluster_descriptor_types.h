/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once 

using chip_id_t = int;
using ethernet_channel_t = int;
using eth_coord_t = std::tuple<int, int, int, int>;  // x, y, rack, shelf