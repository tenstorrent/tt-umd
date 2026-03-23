// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "umd/device/types/tlb.hpp"

namespace tt::umd {

/**
 * Base class for TLB handles that provides a common interface
 * for both hardware (silicon) and simulation implementations.
 */
class TlbHandle {
public:
    virtual ~TlbHandle() noexcept = default;

    /**
     * Configures the TLB with the provided configuration.
     *
     * @param new_config The new configuration for the TLB.
     */
    virtual void configure(const tlb_data& new_config) = 0;

    /**
     * Returns the base mapped address of the TLB.
     */
    virtual uint8_t* get_base() = 0;

    /**
     * Returns the size of the TLB.
     */
    virtual size_t get_size() const = 0;

    /**
     * Returns the current configuration of the TLB.
     */
    virtual const tlb_data& get_config() const = 0;

    /**
     * Returns the TLB mapping type (UC or WC).
     */
    virtual TlbMapping get_tlb_mapping() const = 0;

    /**
     * Returns the TLB ID, representing index of TLB in BAR0.
     */
    virtual int get_tlb_id() const = 0;

protected:
    /**
     * Protected default constructor - only derived classes can construct.
     */
    TlbHandle() = default;

private:
    /**
     * Free any TLB resources. Called by destructor.
     * Implemented by derived classes.
     */
    virtual void free_tlb() noexcept = 0;
};

}  // namespace tt::umd
