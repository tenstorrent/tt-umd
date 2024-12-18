/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/tt_soc_descriptor.h"
#include "umd/device/types/cluster_descriptor_types.h"

namespace tt::umd {

class TTDevice;
class TLBManager;

// An abstract class that represents a chip.
class Chip {
public:
    Chip(tt_SocDescriptor soc_descriptor);

    virtual ~Chip() = default;

    tt_SocDescriptor& get_soc_descriptor();

    virtual TTDevice* get_tt_device();
    virtual TLBManager* get_tlb_manager();

    virtual bool is_mmio_capable() const = 0;

private:
    tt_SocDescriptor soc_descriptor_;
};

}  // namespace tt::umd
