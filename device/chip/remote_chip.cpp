/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/remote_chip.h"

namespace tt::umd {

RemoteChip::RemoteChip(tt_SocDescriptor soc_descriptor) : Chip(soc_descriptor) {}

bool RemoteChip::is_mmio_capable() const { return false; }
}  // namespace tt::umd
