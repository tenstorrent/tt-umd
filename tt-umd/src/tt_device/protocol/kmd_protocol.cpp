// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/kmd_protocol.hpp"

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

KmdProtocol::KmdProtocol(PCIDevice* pci_device) : pci_device_(pci_device) {}

int KmdProtocol::get_mmio_id() { return pci_device_->get_device_num(); }

void KmdProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "KmdProtocol::read_data is not implemented yet");
}

void KmdProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "KmdProtocol::write_data is not implemented yet");
}

void KmdProtocol::read_ctrl(
    void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    UMD_THROW(error::RuntimeError, "KmdProtocol::read_ctrl is not implemented yet");
}

void KmdProtocol::write_ctrl(
    const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    UMD_THROW(error::RuntimeError, "KmdProtocol::write_ctrl is not implemented yet");
}

bool KmdProtocol::write_to_core_range(
    const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "KmdProtocol::write_to_core_range is not implemented yet");
}

}  // namespace tt::umd
