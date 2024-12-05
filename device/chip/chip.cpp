/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/chip.h"

namespace tt::umd {

Chip::Chip(tt_SocDescriptor soc_descriptor) : soc_descriptor_(soc_descriptor) {}

tt_SocDescriptor& Chip::get_soc_descriptor() { return soc_descriptor_; }

}  // namespace tt::umd
