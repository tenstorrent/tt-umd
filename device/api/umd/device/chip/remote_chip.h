/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"

namespace tt::umd {
class RemoteChip : public Chip {
public:
    RemoteChip(tt_SocDescriptor soc_descriptor);
    RemoteChip(tt_SocDescriptor soc_descriptor, ChipInfo chip_info);
    bool is_mmio_capable() const override;
};
}  // namespace tt::umd
