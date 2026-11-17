// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/types/io_window_config.hpp"

namespace tt::umd {

class PCIDevice;

/**
 * @brief IoWindow whose accesses the kernel driver performs, for architectures with no aperture
 * user space can map.
 *
 * Nothing is mapped, so the window has no size limit of its own beyond the one it was created with,
 * and every access is a scalar driver access. The PCIDevice is non-owning and must outlive this.
 */
class KmdNocWindow : public IoWindow {
public:
    KmdNocWindow(PCIDevice* pci_device, const TargetIoWindowConfig& target, const HostIoWindowConfig& host);

    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;
    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write64(uint64_t offset, uint64_t value) override;
    uint64_t read64(uint64_t offset) override;
    void write_aligned(uint64_t offset, const void* data, size_t size) override;
    void read_aligned(uint64_t offset, void* data, size_t size) override;
    void configure(const TargetIoWindowConfig& config) override;
    void configure(const TargetIoWindowConfig& config, IoOrdering ordering) override;
    TargetIoWindowConfig get_target_config() const override;
    IoOrdering get_io_ordering() const override;
    size_t get_size() const override;
    HostMemoryCaching get_memory_caching_type() const override;

private:
    PCIDevice* pci_device_ = nullptr;
    TargetIoWindowConfig target_;
    IoOrdering ordering_ = IoOrdering::Strict;
    HostIoWindowConfig host_;
};

}  // namespace tt::umd
