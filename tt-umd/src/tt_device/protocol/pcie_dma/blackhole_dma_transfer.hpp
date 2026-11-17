// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt_device/protocol/dma_interface.hpp"

namespace tt::umd {

class PCIDevice;

/**
 * @brief DmaInterface over Blackhole's PCIe DMA engine.
 *
 * Device-to-Host zero-copy transfers are not supported on Blackhole and throw.
 *
 * The PCIDevice is non-owning and must outlive this object.
 */
class BlackholeDmaTransfer : public DmaInterface {
public:
    explicit BlackholeDmaTransfer(PCIDevice* pci_device);

    [[nodiscard]] bool dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_multicast_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id)
        override;
    bool dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    bool dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    bool dma_multicast_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id)
        override;
    [[nodiscard]] bool dma_write_zero_copy_start(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_read_zero_copy_start(
        uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] DmaTransferState dma_write_zero_copy_check() override;
    [[nodiscard]] DmaTransferState dma_read_zero_copy_check() override;
    [[nodiscard]] size_t dma_zero_copy_max_size(uint64_t device_addr) const override;

private:
    PCIDevice* pci_device_ = nullptr;
};

}  // namespace tt::umd
