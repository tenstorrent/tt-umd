// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "umd/device/types/arch.hpp"
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
    uint8_t* get_base() const { return tlb_base_; }

    /**
     * Returns the size of the TLB.
     */
    size_t get_size() const { return tlb_size_; }

    /**
     * Returns the current configuration of the TLB.
     */
    const tlb_data& get_config() const { return tlb_config_; }

    /**
     * Returns the TLB mapping type (UC or WC).
     */
    TlbMapping get_tlb_mapping() const { return tlb_mapping_; }

    /**
     * Returns the TLB ID, representing index of TLB in BAR0.
     */
    int get_tlb_id() const { return tlb_id_; }

    /**
     * Whether a window was laid out behind this handle, i.e. whether it has an index with a base
     * address (get_base()) and configuration registers to program. Always true on silicon. False for
     * a simulation backend that models no TLB windows at all: there is nothing to program, and the
     * window object names its target core explicitly instead of deriving an address from the handle.
     */
    virtual bool maps_window() const { return true; }

    virtual tt::ARCH get_arch() const = 0;

protected:
    /**
     * Protected default constructor - only derived classes can construct.
     */
    TlbHandle() = default;

    /**
     * Protected constructor letting derived classes fix verify_config_ for the lifetime of the handle.
     *
     * @param verify_config Require every subsequent configure() to confirm the new configuration has
     *                       reached the device before returning, at the cost of a PCIe round trip per
     *                       reconfigure. False is correct whenever the host itself is the next user of
     *                       the window: the host's access to the window travels behind the config
     *                       write, into the same block, so it cannot be translated by the old
     *                       configuration. Pass true for a window whose next user is on-chip --
     *                       notably the PCIe DMA engine, which is a separate requester started by a
     *                       doorbell to a different register block, and so is not ordered behind the
     *                       config write at all.
     */
    explicit TlbHandle(const bool verify_config) : verify_config_(verify_config) {}

    /**
     * Whether configure() must confirm the new configuration reached the device before returning.
     */
    bool get_verify_config() const { return verify_config_; }

    int tlb_id_ = 0;
    uint8_t* tlb_base_ = nullptr;
    size_t tlb_size_ = 0;
    tlb_data tlb_config_{};
    TlbMapping tlb_mapping_ = TlbMapping::UC;

private:
    /**
     * Free any TLB resources. Called by destructor.
     * Implemented by derived classes.
     */
    virtual void free_tlb() noexcept = 0;

    bool verify_config_ = false;
};

}  // namespace tt::umd
