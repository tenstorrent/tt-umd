// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/blackhole_tt_device.hpp"

#include <fmt/format.h>
#include <sys/mman.h>  // for MAP_FAILED

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "tracy.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/architecture_registers.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/tt_device/hang_detection/blackhole_hang_detector.hpp"
#include "umd/device/tt_device/hang_detection/hang_detector.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd {

BlackholeTTDevice::BlackholeTTDevice(std::unique_ptr<TTDeviceModel> model) : TTDevice(std::move(model)) {}

BlackholeTTDevice::~BlackholeTTDevice() {
    // Turn off iATU for the regions we programmed.  This won't happen if the
    // application crashes -- this is a good example of why userspace should not
    // be touching this hardware resource directly -- but it's a good idea to
    // clean up after ourselves.
    if (get_communication_device_type() != IODeviceType::PCIe) {
        return;
    }
    if (get_pci_device()->bar2_uc != nullptr && get_pci_device()->bar2_uc != MAP_FAILED) {
        auto *bar2 = static_cast<volatile uint8_t *>(get_pci_device()->bar2_uc);

        for (size_t region : iatu_regions_) {
            uint64_t iatu_base = ATU_OFFSET_IN_BH_BAR2 + (region * 0x200);
            uint64_t region_ctrl_2 = 0;
            *reinterpret_cast<volatile uint32_t *>(bar2 + iatu_base + 0x04) = region_ctrl_2;
        }
    }
}

void BlackholeTTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    uint64_t base = region * region_size;
    uint64_t iatu_base = ATU_OFFSET_IN_BH_BAR2 + (region * 0x200);
    auto *bar2 = static_cast<volatile uint8_t *>(get_pci_device()->bar2_uc);

    if (region_size % (1ULL << 30) != 0 || region_size > (1ULL << 32)) {
        // If you hit this, the suggestion is to not use iATU: map your buffer
        // with the driver, and use the IOVA it provides in your device code.
        UMD_THROW(
            error::RuntimeError, "Failed constraint: region_size % (1ULL << 30) == 0; region_size <= (1ULL <<32).");
    }

    if (bar2 == nullptr || bar2 == MAP_FAILED) {
        UMD_THROW(error::RuntimeError, "BAR2 not mapped.");
    }

    auto write_iatu_reg = [bar2](uint64_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t *>(bar2 + offset) = value;
    };

    uint64_t limit = (base + (region_size - 1)) & 0xffff'ffff;
    uint32_t base_lo = (base >> 0x00) & 0xffff'ffff;
    uint32_t base_hi = (base >> 0x20) & 0xffff'ffff;
    uint32_t target_lo = (target >> 0x00) & 0xffff'ffff;
    uint32_t target_hi = (target >> 0x20) & 0xffff'ffff;

    uint32_t region_ctrl_1 = 0;
    uint32_t region_ctrl_2 = 1 << 31;  // REGION_EN
    uint32_t region_ctrl_3 = 0;
    uint32_t limit_hi = 0;

    write_iatu_reg(iatu_base + 0x00, region_ctrl_1);
    write_iatu_reg(iatu_base + 0x04, region_ctrl_2);
    write_iatu_reg(iatu_base + 0x08, base_lo);
    write_iatu_reg(iatu_base + 0x0c, base_hi);
    write_iatu_reg(iatu_base + 0x10, limit);
    write_iatu_reg(iatu_base + 0x14, target_lo);
    write_iatu_reg(iatu_base + 0x18, target_hi);
    write_iatu_reg(iatu_base + 0x1c, limit_hi);
    write_iatu_reg(iatu_base + 0x20, region_ctrl_3);

    iatu_regions_.insert(region);

    log_debug(
        LogUMD,
        "Device: {} Mapped iATU region {} from 0x{:x} to 0x{:x} to 0x{:x}",
        this->get_pci_device()->get_device_num(),
        region,
        base,
        limit,
        target);
}

uint32_t BlackholeTTDevice::get_clock() {
    if (get_firmware_telemetry_reader()->is_entry_available(TelemetryTag::AICLK)) {
        return get_firmware_telemetry_reader()->read_entry(TelemetryTag::AICLK);
    }

    UMD_THROW(error::RuntimeError, "AICLK telemetry not available for Blackhole device.");
}

uint32_t BlackholeTTDevice::get_min_clock_freq() { return get_architecture_implementation()->get_min_clock_freq(); }

}  // namespace tt::umd
