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
     * Require every subsequent configure() to confirm the new configuration has reached the device
     * before returning, at the cost of a PCIe round trip per reconfigure.
     *
     * Off by default, which is correct whenever the host itself is the next user of the window: the
     * host's access to the window travels behind the config write, into the same block, so it
     * cannot be translated by the old configuration. Turn it on for a window whose next user is
     * on-chip -- notably the PCIe DMA engine, which is a separate requester started by a doorbell to
     * a different register block, and so is not ordered behind the config write at all.
     */
    void set_verify_config(const bool verify) { verify_config_ = verify; }

    virtual tt::ARCH get_arch() const = 0;

protected:
    /**
     * Protected default constructor - only derived classes can construct.
     */
    TlbHandle() = default;

    int tlb_id_ = 0;
    uint8_t* tlb_base_ = nullptr;
    size_t tlb_size_ = 0;
    tlb_data tlb_config_{};
    TlbMapping tlb_mapping_ = TlbMapping::UC;
    bool verify_config_ = false;

private:
    /**
     * Free any TLB resources. Called by destructor.
     * Implemented by derived classes.
     */
    virtual void free_tlb() noexcept = 0;
};

}  // namespace tt::umd
