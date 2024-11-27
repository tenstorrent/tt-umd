/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"

namespace tt::umd {
class MockChip : public Chip {
public:
    MockChip(chip_id_t chip_id, tt_SocDescriptor soc_descriptor);
};
}  // namespace tt::umd
