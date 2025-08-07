/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/wormhole_jtag_tt_device.h"

#include "umd/device/pci_device.hpp"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/wormhole_implementation.h"

extern bool umd_use_noc1;

namespace tt::umd {

WormholeJtagTTDevice::WormholeJtagTTDevice(std::shared_ptr<PCIDevice> pci_device) : WormholeTTDevice(pci_device) {
    init_tt_device();
    wait_arc_core_start(
        umd_use_noc1 ? tt_xy_pair(
                           wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                           wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y])
                     : wormhole::ARC_CORES_NOC0[0],
        1000);

    std::filesystem::path temp_test_path(get_jtag_library_directory_path());
    init_jtag(temp_test_path);
}

WormholeJtagTTDevice::WormholeJtagTTDevice() : WormholeTTDevice(std::make_unique<wormhole_implementation>()) {
    std::filesystem::path temp_test_path(get_jtag_library_directory_path());
    init_jtag(temp_test_path);

    init_tt_device();
    wait_arc_core_start(
        umd_use_noc1 ? tt_xy_pair(
                           wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                           wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y])
                     : wormhole::ARC_CORES_NOC0[0],
        1000);
}

void WormholeJtagTTDevice::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (!pci_device_) {
        TTDevice::jtag_write_to_device(mem_ptr, core, addr, size);
    } else {
        TTDevice::write_to_device(mem_ptr, core, addr, size);
    }
}

void WormholeJtagTTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (!pci_device_) {
        TTDevice::jtag_read_from_device(mem_ptr, core, addr, size);
    } else {
        TTDevice::read_from_device(mem_ptr, core, addr, size);
    }
}
