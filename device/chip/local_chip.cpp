/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/local_chip.h"

namespace tt::umd {

LocalChip::LocalChip(tt_SocDescriptor soc_descriptor) : Chip(soc_descriptor) {}

bool LocalChip::is_mmio_capable() const { return true; }
}  // namespace tt::umd
