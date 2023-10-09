/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once 

#include <tuple>

using chip_id_t = int;
using ethernet_channel_t = int;
using eth_coord_t = std::tuple<int, int, int, int>;  // x, y, rack, shelf

static inline int get_eth_coord_x(eth_coord_t const& coord) { return std::get<0>(coord); }
static inline int get_eth_coord_y(eth_coord_t const& coord) { return std::get<1>(coord); }
static inline int get_eth_coord_rack(eth_coord_t const& coord) { return std::get<2>(coord); }
static inline int get_eth_coord_shelf(eth_coord_t const& coord) { return std::get<3>(coord); }