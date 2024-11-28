/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/tt_cluster_descriptor_types.h"
#include "umd/device/tt_soc_descriptor.h"

namespace tt::umd {

// An abstract class that represents a chip.
class Chip {
public:
    Chip(chip_id_t chip_id, tt_SocDescriptor soc_descriptor);

    virtual ~Chip() = default;

    chip_id_t get_chip_id() const;
    tt_SocDescriptor& get_soc_descriptor();

    virtual bool is_mmio_capable() const = 0;

private:
    chip_id_t chip_id_;
    tt_SocDescriptor soc_descriptor_;
};

}  // namespace tt::umd
