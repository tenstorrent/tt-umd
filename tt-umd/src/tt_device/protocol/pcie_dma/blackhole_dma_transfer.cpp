// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/pcie_dma/blackhole_dma_transfer.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

BlackholeDmaTransfer::BlackholeDmaTransfer(PCIDevice* pci_device) : pci_device_(pci_device) {}

bool BlackholeDmaTransfer::dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_read is not implemented yet");
}

bool BlackholeDmaTransfer::dma_write(const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_write is not implemented yet");
}

bool BlackholeDmaTransfer::dma_multicast_write(
    const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_multicast_write is not implemented yet");
}

bool BlackholeDmaTransfer::dma_read_zero_copy(
    uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_read_zero_copy is not implemented yet");
}

bool BlackholeDmaTransfer::dma_write_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_write_zero_copy is not implemented yet");
}

bool BlackholeDmaTransfer::dma_multicast_write_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_multicast_write_zero_copy is not implemented yet");
}

bool BlackholeDmaTransfer::dma_write_zero_copy_start(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_write_zero_copy_start is not implemented yet");
}

bool BlackholeDmaTransfer::dma_read_zero_copy_start(
    uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_read_zero_copy_start is not implemented yet");
}

DmaTransferState BlackholeDmaTransfer::dma_write_zero_copy_check() {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_write_zero_copy_check is not implemented yet");
}

DmaTransferState BlackholeDmaTransfer::dma_read_zero_copy_check() {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_read_zero_copy_check is not implemented yet");
}

size_t BlackholeDmaTransfer::dma_zero_copy_max_size(uint64_t device_addr) const {
    UMD_THROW(error::RuntimeError, "BlackholeDmaTransfer::dma_zero_copy_max_size is not implemented yet");
}

}  // namespace tt::umd
