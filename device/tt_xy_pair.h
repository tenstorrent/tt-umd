/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <regex>

#include "device/xy_pair.h"

using tt_xy_pair = tt::umd::xy_pair;
using tt_cxy_pair = tt::umd::cxy_pair;

struct tt_physical_coords : public tt_xy_pair {
    tt_physical_coords() : tt_xy_pair() {}
    tt_physical_coords(std::size_t x, std::size_t y) : tt_xy_pair(x, y) {}
};

struct tt_chip_physical_coords : public tt_cxy_pair {
    tt_chip_physical_coords() : tt_cxy_pair() {}
    tt_chip_physical_coords(std::size_t ichip, xy_pair pair) : tt_cxy_pair(ichip, pair) {}
    tt_chip_physical_coords(std::size_t ichip, std::size_t x, std::size_t y) : tt_cxy_pair(ichip, x, y) {}
};

struct tt_logical_coords : public tt_xy_pair {
    tt_logical_coords() : tt_xy_pair() {}
    tt_logical_coords(std::size_t x, std::size_t y) : tt_xy_pair(x, y) {}
};

struct tt_chip_logical_coords : public tt_cxy_pair {
    tt_chip_logical_coords() : tt_cxy_pair() {}
    tt_chip_logical_coords(std::size_t ichip, xy_pair pair) : tt_cxy_pair(ichip, pair) {}
    tt_chip_logical_coords(std::size_t ichip, std::size_t x, std::size_t y) : tt_cxy_pair(ichip, x, y) {}
};

struct tt_virtual_coords : public tt_xy_pair {
    tt_virtual_coords() : tt_xy_pair() {}
    tt_virtual_coords(std::size_t x, std::size_t y) : tt_xy_pair(x, y) {}
};

struct tt_chip_virtual_coords : public tt_cxy_pair {
    tt_chip_virtual_coords() : tt_cxy_pair() {}
    tt_chip_virtual_coords(std::size_t ichip, xy_pair pair) : tt_cxy_pair(ichip, pair) {}
    tt_chip_virtual_coords(std::size_t ichip, std::size_t x, std::size_t y) : tt_cxy_pair(ichip, x, y) {}
};

struct tt_translated_coords : public tt_xy_pair {
    tt_translated_coords() : tt_xy_pair() {}
    tt_translated_coords(std::size_t x, std::size_t y) : tt_xy_pair(x, y) {}
};

struct tt_chip_translated_coords : public tt_cxy_pair {
    tt_chip_translated_coords() : tt_cxy_pair() {}
    tt_chip_translated_coords(std::size_t ichip, xy_pair pair) : tt_cxy_pair(ichip, pair) {}
    tt_chip_translated_coords(std::size_t ichip, std::size_t x, std::size_t y) : tt_cxy_pair(ichip, x, y) {}
};
