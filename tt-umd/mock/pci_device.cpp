// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/pci_device.hpp"

#include "tt-umd/pcie/pci_ids.h"

namespace tt::umd {

// TODO: enumerate_devices and enumerate_devices_info should be moved to a more
// generic TopologyDiscovery interface rather than living on PCIDevice.

std::vector<int> PCIDevice::enumerate_devices() { return {0}; }

std::map<int, PciDeviceInfo> PCIDevice::enumerate_devices_info() {
    PciDeviceInfo info{};
    info.vendor_id = 0x1e52;
    info.device_id = TT_WORMHOLE_PCI_DEVICE_ID;
    info.subsystem_vendor_id = 0;
    info.subsystem_id = 0;
    info.pci_domain = 0;
    info.pci_bus = 1;
    info.pci_device = 0;
    info.pci_function = 0;
    info.pci_bdf = "0000:01:00.0";
    info.physical_slot = std::nullopt;
    return {{0, info}};
}

// Should be renamed to read_kernel_version().
SemVer PCIDevice::read_kmd_version() { return SemVer(1, 0, 0); }

std::unique_ptr<TlbWindow> PCIDevice::allocate_tlb_window(const size_t, const TlbMapping) { return nullptr; }

tt::ARCH PciDeviceInfo::get_arch() const {
    if (device_id == TT_WORMHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (device_id == TT_BLACKHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::BLACKHOLE;
    }
    return tt::ARCH::Invalid;
}

tt::ARCH PCIDevice::get_pcie_arch() { return tt::ARCH::WORMHOLE_B0; }

}  // namespace tt::umd
