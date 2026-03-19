/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/pcie_dma/blackhole_dma_transfer.hpp"

#include <chrono>
#include <stdexcept>

#include "umd/device/pcie/pci_device.hpp"

namespace tt::umd {

void BlackholeDmaTransfer::d2h_transfer(
    volatile uint8_t* /*bar2*/, DmaBuffer& /*dma_buffer*/, uint64_t /*dst*/, uint32_t /*src*/, size_t /*size*/) {
    throw std::runtime_error("D2H DMA transfer is not supported on Blackhole.");
}

void BlackholeDmaTransfer::h2d_transfer(
    volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint32_t dst, uint64_t src, size_t size) {
    static constexpr uint32_t EN_OFF_RDCH_0 = 0x100;
    static constexpr uint32_t DOORBELL_OFF_RDCH_0 = 0x104;
    static constexpr uint32_t XFERSIZE_OFF_RDCH_0 = 0x11C;
    static constexpr uint32_t SAR_LOW_OFF_RDCH_0 = 0x120;
    static constexpr uint32_t SAR_HIGH_OFF_RDCH_0 = 0x124;
    static constexpr uint32_t DAR_LOW_OFF_RDCH_0 = 0x128;
    static constexpr uint32_t DAR_HIGH_OFF_RDCH_0 = 0x12C;
    static constexpr uint32_t INT_SETUP_OFF_RDCH_0 = 0x188;
    static constexpr uint32_t MSI_STOP_LOW_OFF_RDCH_0 = 0x190;
    static constexpr uint32_t MSI_STOP_HIGH_OFF_RDCH_0 = 0x194;
    static constexpr uint32_t MSI_ABORT_LOW_OFF_RDCH_0 = 0x1A0;
    static constexpr uint32_t MSI_ABORT_HIGH_OFF_RDCH_0 = 0x1A4;
    static constexpr uint32_t MSI_MSGD_OFF_RDCH_0 = 0x1A8;
    static constexpr uint32_t DMA_TIMEOUT_MS = 10000;

    auto write_reg = [&](uint32_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t*>(bar2 + offset) = value;
    };

    auto read_reg = [&](uint32_t offset) -> uint32_t { return *reinterpret_cast<volatile uint32_t*>(bar2 + offset); };

    // Configure interrupt setup: enable local interrupt (bit 3) and remote stop interrupt (bit 5).
    write_reg(INT_SETUP_OFF_RDCH_0, 0x28);
    // Set the MSI write address for the DMA "done" interrupt to the completion flag physical address.
    write_reg(MSI_STOP_LOW_OFF_RDCH_0, static_cast<uint32_t>(dma_buffer.completion_pa & 0xFFFFFFFF));
    write_reg(MSI_STOP_HIGH_OFF_RDCH_0, static_cast<uint32_t>((dma_buffer.completion_pa >> 32) & 0xFFFFFFFF));
    // Set the MSI write address for the DMA "abort" interrupt to the word after the completion flag.
    write_reg(
        MSI_ABORT_LOW_OFF_RDCH_0, static_cast<uint32_t>((dma_buffer.completion_pa + sizeof(uint32_t)) & 0xFFFFFFFF));
    write_reg(
        MSI_ABORT_HIGH_OFF_RDCH_0,
        static_cast<uint32_t>(((dma_buffer.completion_pa + sizeof(uint32_t)) >> 32) & 0xFFFFFFFF));
    // MSI message data written on completion (unused for polling, set to 0).
    write_reg(MSI_MSGD_OFF_RDCH_0, 0);
    // Enable the DMA read channel.
    write_reg(EN_OFF_RDCH_0, 0x1);
    // Set the source address (host physical address of the DMA buffer).
    write_reg(SAR_LOW_OFF_RDCH_0, static_cast<uint32_t>(src & 0xFFFFFFFF));
    write_reg(SAR_HIGH_OFF_RDCH_0, static_cast<uint32_t>((src >> 32) & 0xFFFFFFFF));
    // Set the destination address (device AXI address). BH uses a 32-bit device address space.
    write_reg(DAR_LOW_OFF_RDCH_0, dst);
    write_reg(DAR_HIGH_OFF_RDCH_0, 0);
    // Set transfer size and ring the doorbell to start the DMA.
    write_reg(XFERSIZE_OFF_RDCH_0, static_cast<uint32_t>(size));
    write_reg(DOORBELL_OFF_RDCH_0, 0x1);

    // WARNING: Busy-wait poll. Consider adding _mm_pause() or adaptive polling to reduce
    // CPU and memory bus contention.
    auto start = std::chrono::steady_clock::now();
    while (read_reg(XFERSIZE_OFF_RDCH_0) != 0) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        if (elapsed_ms > DMA_TIMEOUT_MS) {
            throw std::runtime_error("DMA timeout.");
        }
    }
}

}  // namespace tt::umd
