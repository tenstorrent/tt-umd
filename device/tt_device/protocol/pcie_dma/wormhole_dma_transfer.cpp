/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/pcie_dma/wormhole_dma_transfer.hpp"

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// TODO: This is a temporary implementation, and ought to be replaced with a
// driver-based technique that can take advantage of multiple channels and
// interrupts.  With a driver-based implementation we can also avoid the need to
// memcpy into/out of a buffer, although exposing zero-copy DMA functionality to
// the application will require IOMMU support.  One day...

namespace {

// Written by the engine to the completion address once the transfer is done. Both directions use the
// same value: only one transfer is ever in flight on this device.
constexpr uint32_t DMA_COMPLETION_VALUE = 0xfaca;

}  // namespace

void WormholeDmaTransfer::d2h_start(
    volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint64_t dst, uint32_t src, size_t size) {
    static constexpr uint64_t DMA_WRITE_ENGINE_EN_OFF = 0xc;
    static constexpr uint64_t DMA_WRITE_INT_MASK_OFF = 0x54;
    static constexpr uint64_t DMA_CH_CONTROL1_OFF_WRCH_0 = 0x200;
    static constexpr uint64_t DMA_WRITE_DONE_IMWR_LOW_OFF = 0x60;
    static constexpr uint64_t DMA_WRITE_CH01_IMWR_DATA_OFF = 0x70;
    static constexpr uint64_t DMA_WRITE_DONE_IMWR_HIGH_OFF = 0x64;
    static constexpr uint64_t DMA_WRITE_ABORT_IMWR_LOW_OFF = 0x68;
    static constexpr uint64_t DMA_WRITE_ABORT_IMWR_HIGH_OFF = 0x6c;
    static constexpr uint64_t DMA_TRANSFER_SIZE_OFF_WRCH_0 = 0x208;
    static constexpr uint64_t DMA_SAR_LOW_OFF_WRCH_0 = 0x20c;
    static constexpr uint64_t DMA_SAR_HIGH_OFF_WRCH_0 = 0x210;
    static constexpr uint64_t DMA_DAR_LOW_OFF_WRCH_0 = 0x214;
    static constexpr uint64_t DMA_DAR_HIGH_OFF_WRCH_0 = 0x218;
    static constexpr uint64_t DMA_WRITE_DOORBELL_OFF = 0x10;

    volatile uint32_t* completion = reinterpret_cast<volatile uint32_t*>(dma_buffer.completion);
    *completion = 0;

    auto write_reg = [&](uint64_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t*>(bar2 + offset) = value;
    };

    write_reg(DMA_WRITE_ENGINE_EN_OFF, 0x1);
    write_reg(DMA_WRITE_INT_MASK_OFF, 0);
    write_reg(DMA_CH_CONTROL1_OFF_WRCH_0, 0x00000010);
    write_reg(DMA_WRITE_DONE_IMWR_LOW_OFF, static_cast<uint32_t>(dma_buffer.completion_pa & 0xFFFFFFFF));
    write_reg(DMA_WRITE_DONE_IMWR_HIGH_OFF, static_cast<uint32_t>((dma_buffer.completion_pa >> 32) & 0xFFFFFFFF));
    write_reg(DMA_WRITE_CH01_IMWR_DATA_OFF, DMA_COMPLETION_VALUE);
    write_reg(DMA_WRITE_ABORT_IMWR_LOW_OFF, 0);
    write_reg(DMA_WRITE_ABORT_IMWR_HIGH_OFF, 0);
    write_reg(DMA_TRANSFER_SIZE_OFF_WRCH_0, size);
    write_reg(DMA_SAR_LOW_OFF_WRCH_0, src);
    write_reg(DMA_SAR_HIGH_OFF_WRCH_0, 0);
    write_reg(DMA_DAR_LOW_OFF_WRCH_0, static_cast<uint32_t>(dst & 0xFFFFFFFF));
    write_reg(DMA_DAR_HIGH_OFF_WRCH_0, static_cast<uint32_t>((dst >> 32) & 0xFFFFFFFF));
    write_reg(DMA_WRITE_DOORBELL_OFF, 0);
}

bool WormholeDmaTransfer::d2h_is_complete(const volatile uint8_t* /*bar2*/, const DmaBuffer& dma_buffer) {
    // The completion flag lives in host memory, so this is a coherent load rather than a read across
    // PCIe: cheap enough to poll in a tight loop.
    return *reinterpret_cast<const volatile uint32_t*>(dma_buffer.completion) == DMA_COMPLETION_VALUE;
}

void WormholeDmaTransfer::h2d_start(
    volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint32_t dst, uint64_t src, size_t size) {
    static constexpr uint64_t DMA_READ_ENGINE_EN_OFF = 0x2c;
    static constexpr uint64_t DMA_READ_INT_MASK_OFF = 0xa8;
    static constexpr uint64_t DMA_CH_CONTROL1_OFF_RDCH_0 = 0x300;
    static constexpr uint64_t DMA_READ_DONE_IMWR_LOW_OFF = 0xcc;
    static constexpr uint64_t DMA_READ_CH01_IMWR_DATA_OFF = 0xdc;
    static constexpr uint64_t DMA_READ_DONE_IMWR_HIGH_OFF = 0xd0;
    static constexpr uint64_t DMA_READ_ABORT_IMWR_LOW_OFF = 0xd4;
    static constexpr uint64_t DMA_READ_ABORT_IMWR_HIGH_OFF = 0xd8;
    static constexpr uint64_t DMA_TRANSFER_SIZE_OFF_RDCH_0 = 0x308;
    static constexpr uint64_t DMA_SAR_LOW_OFF_RDCH_0 = 0x30c;
    static constexpr uint64_t DMA_SAR_HIGH_OFF_RDCH_0 = 0x310;
    static constexpr uint64_t DMA_DAR_LOW_OFF_RDCH_0 = 0x314;
    static constexpr uint64_t DMA_DAR_HIGH_OFF_RDCH_0 = 0x318;
    static constexpr uint64_t DMA_READ_DOORBELL_OFF = 0x30;

    volatile uint32_t* completion = reinterpret_cast<volatile uint32_t*>(dma_buffer.completion);
    *completion = 0;

    auto write_reg = [&](uint64_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t*>(bar2 + offset) = value;
    };

    write_reg(DMA_READ_ENGINE_EN_OFF, 0x1);
    write_reg(DMA_READ_INT_MASK_OFF, 0);
    write_reg(DMA_CH_CONTROL1_OFF_RDCH_0, 0x10);
    write_reg(DMA_READ_DONE_IMWR_LOW_OFF, static_cast<uint32_t>(dma_buffer.completion_pa & 0xFFFFFFFF));
    write_reg(DMA_READ_DONE_IMWR_HIGH_OFF, static_cast<uint32_t>((dma_buffer.completion_pa >> 32) & 0xFFFFFFFF));
    write_reg(DMA_READ_CH01_IMWR_DATA_OFF, DMA_COMPLETION_VALUE);
    write_reg(DMA_READ_ABORT_IMWR_LOW_OFF, 0);
    write_reg(DMA_READ_ABORT_IMWR_HIGH_OFF, 0);
    write_reg(DMA_TRANSFER_SIZE_OFF_RDCH_0, size);
    write_reg(DMA_SAR_LOW_OFF_RDCH_0, static_cast<uint32_t>(src & 0xFFFFFFFF));
    write_reg(DMA_SAR_HIGH_OFF_RDCH_0, static_cast<uint32_t>((src >> 32) & 0xFFFFFFFF));
    write_reg(DMA_DAR_LOW_OFF_RDCH_0, dst);
    write_reg(DMA_DAR_HIGH_OFF_RDCH_0, 0);
    write_reg(DMA_READ_DOORBELL_OFF, 0);
}

bool WormholeDmaTransfer::h2d_is_complete(const volatile uint8_t* /*bar2*/, const DmaBuffer& dma_buffer) {
    return *reinterpret_cast<const volatile uint32_t*>(dma_buffer.completion) == DMA_COMPLETION_VALUE;
}

}  // namespace tt::umd
