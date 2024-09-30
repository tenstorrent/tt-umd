/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "chip.h"
#include "local_chip.h"

namespace tt::umd {

class RemoteChip: public Chip {
    
    RemoteChip(uint32_t device_id, LocalChip* local_chip);

    // Remote chip has associated LocalChip
    LocalChip* connected_local_chip;
};

}