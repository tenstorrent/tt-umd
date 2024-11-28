/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/mock_chip.h"

namespace tt::umd {

MockChip::MockChip(chip_id_t chip_id, tt_SocDescriptor soc_descriptor) : Chip(chip_id, soc_descriptor) {}

bool MockChip::is_mmio_capable() const { return true; }
}  // namespace tt::umd
