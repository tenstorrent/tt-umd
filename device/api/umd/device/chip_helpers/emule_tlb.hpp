// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

class SWEmuleChip;

// emule host-side device-memory access, modeled on the SimulationChip TLB stack
// but routed to SWEmuleChip's tt_emule::Core storage instead of a real (or
// simulated) MMIO window. Needed because host↔device paths that go through the
// static-TLB writer on Blackhole (e.g. H2DSocket::init_receiver_tlb) call
// get_tlb_manager()->get_tlb_window(core)->write_block(...), which the emule
// chip previously answered with nullptr.

// No real MMIO mapping. Carries a large power-of-two window size so TlbWindow's
// alignment math (local_offset & ~(size-1)) never truncates a device L1 address.
class EmuleTlbHandle : public TlbHandle {
public:
    EmuleTlbHandle(tt::ARCH arch, size_t size, int tlb_id);

    void configure(const tlb_data& new_config) override;
    tt::ARCH get_arch() const override;

private:
    void free_tlb() noexcept override {}

    tt::ARCH arch_;
};

// Routes all access to the SWEmuleChip's tt_emule::Core at the window's currently
// configured (x_end, y_end) TRANSLATED core, address get_base_address() + offset.
// This composes with the base-class *_reconfigure paths, which mutate the config
// (via configure()) and then call block(0, ...).
class EmuleTlbWindow : public TlbWindow {
public:
    EmuleTlbWindow(std::unique_ptr<TlbHandle> handle, SWEmuleChip* chip, const tlb_data config = {});

    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write_register(uint64_t offset, const void* data, size_t size) override;
    void read_register(uint64_t offset, void* data, size_t size) override;
    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;
    void safe_write16(uint64_t offset, uint16_t value) override;
    uint16_t safe_read16(uint64_t offset) override;

private:
    CoreCoord target_core() const;
    uint64_t target_addr(uint64_t offset) const;

    SWEmuleChip* chip_;
};

// Pre-populates one full-address-space window per TENSIX core (Blackhole
// statically maps the entire device address space, so get_tlb_window(core) is
// expected to already have a window). Built with a null TTDevice — only the
// map-backed get_tlb_window / is_tlb_mapped paths are used.
class EmuleTlbManager : public TLBManager {
public:
    explicit EmuleTlbManager(SWEmuleChip* chip);

private:
    SWEmuleChip* chip_;
    int next_tlb_id_ = 0;
};

}  // namespace tt::umd
