// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/io_window/io_window.hpp"
#include "umd/device/tt_device/protocol/scalar_noc_access.hpp"
#include "umd/device/types/io_window_config.hpp"

namespace tt::umd {

/**
 * An IoWindow over Quasar's kernel-mediated scalar access.
 *
 * Nothing is mapped. On Wormhole and Blackhole a window is a region of host address space and an
 * access through it is a load or a store; here the driver owns the only inbound apertures, so an
 * access is a round trip through it at the window's base plus the offset. The interface is the
 * same so that callers above need not know which of the two they hold, but the cost is not: a
 * block transfer is one round trip per word.
 *
 * The window has no size of its own. Its base is whatever it was last configured with, and an
 * offset past the target's slot is caught by the translation rather than by this class.
 */
class KmdNocWindow : public IoWindow {
public:
    KmdNocWindow(std::unique_ptr<ScalarNocAccess> access, const TargetIoWindowConfig& config);
    ~KmdNocWindow() override;

    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;

    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;

    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;

    void write_aligned(uint64_t offset, const void* data, size_t size) override;
    void read_aligned(uint64_t offset, void* data, size_t size) override;

    void configure(const TargetIoWindowConfig& config) override;
    void configure(const TargetIoWindowConfig& config, IoOrdering ordering) override;

    TargetIoWindowConfig get_target_config() const override;
    IoOrdering get_io_ordering() const override;
    size_t get_size() const override;
    HostMemoryCaching get_memory_caching_type() const override;

private:
    /** Aperture flags implied by the configured target. */
    uint32_t access_flags() const;

    std::unique_ptr<ScalarNocAccess> access_;
    TargetIoWindowConfig config_;
};

}  // namespace tt::umd
