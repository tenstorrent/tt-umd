// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/tt_device/protocol/device_protocol.hpp"

namespace tt::umd {

class PCIDevice;

/**
 * @brief Device I/O performed by the kernel driver, one scalar access at a time.
 *
 * For architectures whose inbound apertures only the driver may program, so that nothing can be
 * mapped and dereferenced from user space. Every transfer is split into naturally aligned scalar
 * accesses the driver carries out on this process's behalf.
 *
 * The PCIDevice is non-owning and must outlive this object.
 */
class KmdProtocol : public DeviceProtocol {
public:
    explicit KmdProtocol(PCIDevice* pci_device);

    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(
        void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    void write_ctrl(
        const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;
    int get_mmio_id() override;

private:
    PCIDevice* pci_device_ = nullptr;
};

}  // namespace tt::umd
