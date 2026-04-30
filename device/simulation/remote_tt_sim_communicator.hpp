/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "umd/device/simulation/tt_sim_communicator.hpp"

namespace tt::umd {

class ProcessManager;

/**
 * Parent-side stub communicator used by MultiProcessTTSimChip. Forwards the
 * three TLB-relevant calls (pci_mem_read/write_bytes, pci_config_read32) to
 * the child process via ProcessManager. All other methods inherited from
 * TTSimCommunicator throw — the multi-proc parent has no library handle, so
 * tile_*, advance_clock, etc. are not meaningful here (those go through
 * MultiProcessTTSimChip's own message types).
 */
class RemoteTTSimCommunicator : public TTSimCommunicator {
public:
    explicit RemoteTTSimCommunicator(ProcessManager* process_manager);
    ~RemoteTTSimCommunicator() override = default;

    void pci_mem_read_bytes(uint64_t paddr, void* data, uint32_t size) override;
    void pci_mem_write_bytes(uint64_t paddr, const void* data, uint32_t size) override;
    uint32_t pci_config_read32(uint32_t bus_device_function, uint32_t offset) override;

private:
    ProcessManager* process_manager_ = nullptr;
};

}  // namespace tt::umd
