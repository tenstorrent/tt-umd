// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_manager.hpp"

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/types/arch.hpp"

namespace tt::umd {

std::unique_ptr<SysmemBuffer> SysmemManager::allocate_buffer(size_t size, bool bind_to_noc) {
    return allocate_sysmem_buffer(size, bind_to_noc);
}

std::unique_ptr<SysmemBuffer> SysmemManager::map_user_buffer(
    void *user_ptr, size_t size, bool bind_to_noc, DeviceBufferAccess device_access) {
    return map_sysmem_buffer(user_ptr, size, bind_to_noc, device_access);
}

uint64_t SysmemManager::get_pcie_base_for_arch(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return 0x800000000;
        case tt::ARCH::BLACKHOLE:
            return 4ULL << 58;
        default:
            return 0;
    }
}

bool SysmemManager::is_read_only_page_pinning_supported() const {
    // Managers without a PCI device behind them (simulation) cannot pin host pages at all.
    return pci_device_ != nullptr && pci_device_->is_read_only_page_pinning_supported();
}

}  // namespace tt::umd
