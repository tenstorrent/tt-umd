// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt {
enum class CoreType;
}  // namespace tt

namespace tt::umd {

class ArchitectureImplementation {
public:
    virtual ~ArchitectureImplementation() = default;

    virtual tt::ARCH get_architecture() const = 0;
    virtual uint32_t get_arc_message_arc_go_busy() const = 0;
    virtual uint32_t get_arc_message_arc_go_long_idle() const = 0;
    virtual uint32_t get_arc_reset_unit_refclk_low_offset() const = 0;
    virtual uint32_t get_arc_reset_unit_refclk_high_offset() const = 0;
    virtual uint32_t get_dram_banks_number() const = 0;
    virtual uint32_t get_aiclk_busy_val() const = 0;
    virtual uint32_t get_num_eth_channels() const = 0;
    virtual uint32_t get_tensix_soft_reset_addr() const = 0;
    virtual uint32_t get_soft_reset_reg_value(RiscType risc_type) const = 0;
    virtual RiscType get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const = 0;
    virtual uint32_t get_soft_reset_staggered_start() const = 0;
    // Replace with std::span once we enable C++20.
    virtual const std::vector<uint32_t>& get_harvesting_noc_locations() const = 0;

    virtual DeviceL1AddressParams get_l1_address_params() const = 0;

    static std::unique_ptr<ArchitectureImplementation> create(tt::ARCH architecture);
};

}  // namespace tt::umd
