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
    MockChip(tt_SocDescriptor soc_descriptor);
    bool is_mmio_capable() const override;
};
}  // namespace tt::umd
