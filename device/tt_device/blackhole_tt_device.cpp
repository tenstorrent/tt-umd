// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/blackhole_tt_device.h"

#include <sys/mman.h>  // for MAP_FAILED

#include "logger.hpp"
#include "umd/device/blackhole_implementation.h"

namespace tt::umd {

BlackholeTTDevice::BlackholeTTDevice(std::unique_ptr<PCIDevice> pci_device) :
    TTDevice(std::move(pci_device), std::make_unique<blackhole_implementation>()) {}

BlackholeTTDevice::~BlackholeTTDevice() {
    if (pci_device_->bar2_uc != nullptr && pci_device_->bar2_uc != MAP_FAILED) {
        // Disable ATU index 0
        // TODO: Implement disabling for all indexes, once more host channels are enabled.

        // This is not going to happen if the application crashes, so if it's
        // essential for correctness then it needs to move to the driver.
        uint64_t iatu_index = 0;
        uint64_t iatu_base = UNROLL_ATU_OFFSET_BAR + iatu_index * 0x200;
        uint32_t region_ctrl_2 = 0 << 31;  // REGION_EN = 0

        volatile uint32_t *dest =
            reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(pci_device_->bar2_uc) + iatu_base + 0x04);
        const uint32_t *src = &region_ctrl_2;
        *dest = *src;
    }
}

void BlackholeTTDevice::configure_iatu_region(size_t region, uint64_t base, uint64_t target, size_t size) {
    static const uint64_t ATU_OFFSET_IN_BH_BAR2 = 0x1200;
    uint64_t iatu_base = ATU_OFFSET_IN_BH_BAR2 + (region * 0x200);
    auto *bar2 = static_cast<volatile uint8_t *>(pci_device_->bar2_uc);

    if (size % (1ULL << 30) != 0 || size > (1ULL << 32)) {
        // If you hit this, the suggestion is to stop using iATU: map your
        // buffer with the driver, and use the IOVA it gives you to access the
        // buffer from the device.
        throw std::runtime_error("Constraint: size % (1ULL << 30) == 0; size <= (1ULL <<32)");
    }

    if (bar2 == nullptr || bar2 == MAP_FAILED) {
        throw std::runtime_error("BAR2 not mapped");
    }

    auto write_iatu_reg = [bar2](uint64_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t *>(bar2 + offset) = value;
    };

    uint64_t limit = (base + (size - 1)) & 0xffff'ffff;
    ;
    uint32_t base_lo = (base >> 0x00) & 0xffff'ffff;
    uint32_t base_hi = (base >> 0x20) & 0xffff'ffff;
    uint32_t target_lo = (target >> 0x00) & 0xffff'ffff;
    uint32_t target_hi = (target >> 0x20) & 0xffff'ffff;

    uint32_t region_ctrl_1 = 0;
    uint32_t region_ctrl_2 = 1 << 31;  // REGION_EN
    uint32_t region_ctrl_3 = 0;
    uint32_t limit_hi = 0;

    log_info(
        LogSiliconDriver,
        "Device: {} Mapping iATU region {} from 0x{:x} to 0x{:x} to 0x{:x}",
        this->pci_device_->get_device_num(),
        region,
        base,
        limit,
        target);

    write_iatu_reg(iatu_base + 0x00, region_ctrl_1);
    write_iatu_reg(iatu_base + 0x04, region_ctrl_2);
    write_iatu_reg(iatu_base + 0x08, base_lo);
    write_iatu_reg(iatu_base + 0x0c, base_hi);
    write_iatu_reg(iatu_base + 0x10, limit);
    write_iatu_reg(iatu_base + 0x14, target_lo);
    write_iatu_reg(iatu_base + 0x18, target_hi);
    write_iatu_reg(iatu_base + 0x1c, limit_hi);
    write_iatu_reg(iatu_base + 0x20, region_ctrl_3);
}

}  // namespace tt::umd
