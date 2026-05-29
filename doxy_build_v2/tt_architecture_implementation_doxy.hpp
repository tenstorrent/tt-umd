// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "tt_enums_structs_constants_doxy.hpp"

namespace tt::umd {

/**
 * @defgroup tt_architecture_implementation ArchitectureImplementation
 * @{
 *
 * @brief Architecture-specific constants, register encodings, and address layouts.
 *
 * Provides the per-architecture values that differ between chip generations.
 * Concrete subclasses exist for each supported architecture.
 *
 */

/**
 * @brief Architecture-specific constants and register encodings.
 */
class ArchitectureImplementation {
public:
    virtual ~ArchitectureImplementation() = default;

    /** @name Identity */
    /** @{ */

    virtual ARCH get_architecture() const = 0;

    /** @} */

    /** @name Soft Reset */
    /** @{ */

    virtual uint64_t get_tensix_soft_reset_addr() const = 0;

    virtual uint32_t get_soft_reset_reg_value(RiscType risc_type) const = 0;

    virtual uint32_t get_soft_reset_staggered_start() const = 0;

    virtual RiscType get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const = 0;

    /** @} */

    /** @name Reference Clock */
    /** @{ */

    virtual uint64_t get_reset_unit_refclk_high_offset() const = 0;

    virtual uint64_t get_reset_unit_refclk_low_offset() const = 0;

    virtual uint32_t get_min_clock_freq() const = 0;

    /** @} */

    /** @name Firmware Command Codes */
    /** @{ */

    virtual uint32_t get_firmware_message_go_busy() const = 0;

    virtual uint32_t get_firmware_message_go_long_idle() const = 0;

    virtual uint32_t get_firmware_message_go_short_idle() const = 0;

    /** @} */

    /** @name Memory Layout */
    /** @{ */

    virtual DeviceL1AddressParams get_l1_address_params() const = 0;

    virtual uint32_t get_dram_banks_number() const = 0;

    /** @} */

    /** @name Hardware Topology */
    /** @{ */

    virtual uint32_t get_num_eth_channels() const = 0;

    virtual std::vector<tt_xy_pair> get_harvesting_noc_locations() const = 0;

    /** @} */
};

/** @} */  // end of tt_architecture_implementation group

}  // namespace tt::umd
