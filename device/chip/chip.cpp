/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/chip.h"

namespace tt::umd {

Chip::Chip(chip_id_t chip_id, tt_SocDescriptor soc_descriptor) : chip_id_(chip_id), soc_descriptor_(soc_descriptor) {}

chip_id_t Chip::get_chip_id() const { return chip_id_; }

tt_SocDescriptor& Chip::get_soc_descriptor() { return soc_descriptor_; }

}  // namespace tt::umd
