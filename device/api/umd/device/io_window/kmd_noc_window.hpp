// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/io_window/io_window.hpp"
#include "umd/device/tt_device/protocol/kmd_scalar_noc_access.hpp"
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
 * Its base is whatever it was last configured with. Nothing is allocated to bound it, so the size
 * is the span the window accepts offsets within: whatever the caller asked for, or unbounded when
 * the caller asked for nothing and left the choice here. A mapped window cannot offer the latter.
 */
class KmdNocWindow : public IoWindow {
public:
    /**
     * @param access The scalar access every transfer through this window is built from. Shared,
     * because the protocol on the same device issues its own accesses over the same driver handle.
     * @param config The target this window starts out pointing at.
     * @param size The span to accept offsets within, or 0 for unbounded.
     */
    KmdNocWindow(std::shared_ptr<KmdScalarNocAccess> access, const TargetIoWindowConfig& config, size_t size = 0);
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

    /** Refuses an access that is misaligned, or that runs past a window with a size. */
    void validate(uint64_t offset, size_t size, uint32_t width) const;

    std::shared_ptr<KmdScalarNocAccess> access_;
    TargetIoWindowConfig config_;
    size_t size_;
};

}  // namespace tt::umd
