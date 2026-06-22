// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "umd/device/pcie/tlb_window.hpp"

namespace tt::umd {

class RtlSimCommunicator;
class TlbHandle;
struct tlb_data;

/**
 * RTL simulation TlbWindow implementation that translates TLB-based memory access
 * into tile_read_bytes/tile_write_bytes calls on RtlSimCommunicator.
 * Since RTL sim has no PCIe BAR0, the TLB config (core coordinates + address)
 * is used to reconstruct the target core and address for each access.
 */
class RtlSimTlbWindow : public TlbWindow {
public:
    RtlSimTlbWindow(std::unique_ptr<TlbHandle> handle, RtlSimCommunicator* communicator, const tlb_data config = {});

    void write16(uint64_t offset, uint16_t value, const std::function<bool()>& on_timeout = {}) override;
    uint16_t read16(uint64_t offset, const std::function<bool()>& on_timeout = {}) override;
    void write32(uint64_t offset, uint32_t value, const std::function<bool()>& on_timeout = {}) override;
    uint32_t read32(uint64_t offset, const std::function<bool()>& on_timeout = {}) override;
    void write_register(
        uint64_t offset, const void* data, size_t size, const std::function<bool()>& on_timeout = {}) override;
    void read_register(uint64_t offset, void* data, size_t size, const std::function<bool()>& on_timeout = {}) override;
    void write_block(
        uint64_t offset, const void* data, size_t size, const std::function<bool()>& on_timeout = {}) override;
    void read_block(uint64_t offset, void* data, size_t size, const std::function<bool()>& on_timeout = {}) override;

    void safe_write16(uint64_t offset, uint16_t value, const std::function<bool()>& on_timeout = {}) override;
    uint16_t safe_read16(uint64_t offset, const std::function<bool()>& on_timeout = {}) override;

private:
    /**
     * Translate a TLB window offset to (core, address) and perform a write via the communicator.
     */
    void translate_and_write(uint64_t offset, const void* data, size_t size);

    /**
     * Translate a TLB window offset to (core, address) and perform a read via the communicator.
     */
    void translate_and_read(uint64_t offset, void* data, size_t size);

    RtlSimCommunicator* communicator_;
};

}  // namespace tt::umd
