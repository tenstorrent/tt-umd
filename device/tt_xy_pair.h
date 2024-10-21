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

using tt_physical_coords = tt::umd::xy_pair;
using tt_chip_physical_coords = tt::umd::cxy_pair;

using tt_logical_coords = tt::umd::xy_pair;
using tt_chip_logical_coords = tt::umd::cxy_pair;

using tt_translated_coords = tt::umd::xy_pair;
using tt_chip_translated_coords = tt::umd::cxy_pair;