// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/blackhole_tt_device.h"

#include <sys/mman.h>  // for MAP_FAILED

#include <tt-logger/tt-logger.hpp>

#include "umd/device/blackhole_implementation.h"
#include "umd/device/coordinate_manager.h"
#include "umd/device/types/blackhole_arc.h"
#include "umd/device/types/blackhole_eth.h"
#include "umd/device/types/cluster_descriptor_types.h"
#include "umd/device/types/telemetry.h"

namespace tt::umd {

BlackholeTTDevice::BlackholeTTDevice(std::shared_ptr<PCIDevice> pci_device) :
    TTDevice(pci_device, std::make_unique<blackhole_implementation>()) {
    arc_core = tt::umd::blackhole::get_arc_core(get_noc_translation_enabled(), umd_use_noc1);
}

BlackholeTTDevice::~BlackholeTTDevice() {
    // Turn off iATU for the regions we programmed.  This won't happen if the
    // application crashes -- this is a good example of why userspace should not
    // be touching this hardware resource directly -- but it's a good idea to
    // clean up after ourselves.
    if (pci_device_->bar2_uc != nullptr && pci_device_->bar2_uc != MAP_FAILED) {
        auto *bar2 = static_cast<volatile uint8_t *>(pci_device_->bar2_uc);

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
    auto *bar2 = static_cast<volatile uint8_t *>(pci_device_->bar2_uc);

    if (region_size % (1ULL << 30) != 0 || region_size > (1ULL << 32)) {
        // If you hit this, the suggestion is to not use iATU: map your buffer
        // with the driver, and use the IOVA it provides in your device code.
        throw std::runtime_error("Constraint: region_size % (1ULL << 30) == 0; region_size <= (1ULL <<32)");
    }

    if (bar2 == nullptr || bar2 == MAP_FAILED) {
        throw std::runtime_error("BAR2 not mapped");
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

    log_info(
        LogSiliconDriver,
        "Device: {} Mapped iATU region {} from 0x{:x} to 0x{:x} to 0x{:x}",
        this->pci_device_->get_device_num(),
        region,
        base,
        limit,
        target);
}

bool BlackholeTTDevice::get_noc_translation_enabled() {
    const uint64_t addr = blackhole::NIU_CFG_NOC0_BAR_ADDR;
    uint32_t niu_cfg;
    if (addr < get_pci_device()->bar0_uc_offset) {
        read_block(addr, sizeof(niu_cfg), reinterpret_cast<uint8_t *>(&niu_cfg));
    } else {
        read_regs(addr, 1, &niu_cfg);
    }

    return ((niu_cfg >> 14) & 0x1) != 0;
}

ChipInfo BlackholeTTDevice::get_chip_info() {
    ChipInfo chip_info;
    chip_info.harvesting_masks.tensix_harvesting_mask = CoordinateManager::shuffle_tensix_harvesting_mask(
        tt::ARCH::BLACKHOLE,
        telemetry->is_entry_available(TelemetryTag::ENABLED_TENSIX_COL)
            ? (~telemetry->read_entry(TelemetryTag::ENABLED_TENSIX_COL) & 0x3FFF)
            : 0);
    chip_info.harvesting_masks.dram_harvesting_mask = telemetry->is_entry_available(TelemetryTag::ENABLED_GDDR)
                                                          ? (~telemetry->read_entry(TelemetryTag::ENABLED_GDDR) & 0xFF)
                                                          : 0;

    chip_info.harvesting_masks.eth_harvesting_mask = telemetry->is_entry_available(TelemetryTag::ENABLED_ETH)
                                                         ? (~telemetry->read_entry(TelemetryTag::ENABLED_ETH) & 0x3FFF)
                                                         : 0;

    uint32_t pcie_usage = telemetry->read_entry(TelemetryTag::PCIE_USAGE);

    uint32_t pcie0_usage = pcie_usage & 0x3;
    uint32_t pcie1_usage = (pcie_usage >> 2) & 0x3;

    const uint32_t pcie_usage_endpoint = 1;
    chip_info.harvesting_masks.pcie_harvesting_mask = 0;
    if (pcie0_usage != pcie_usage_endpoint) {
        chip_info.harvesting_masks.pcie_harvesting_mask |= 0x1;
    }

    if (pcie1_usage != pcie_usage_endpoint) {
        chip_info.harvesting_masks.pcie_harvesting_mask |= (1 << 1);
    }

    // TODO: Read asic location of the chip from telemetry when it is available.
    // Until then we have to read it from ETH core, it happens during topology exploration.
    // chip_info.chip_uid.asic_location = telemetry->read_entry(TelemetryTag::ASIC_LOCATION);

    chip_info.noc_translation_enabled = get_noc_translation_enabled();

    // It is expected that these entries are always available.
    chip_info.chip_uid.board_id = get_board_id();

    chip_info.board_type = get_board_type_from_board_id(chip_info.chip_uid.board_id);

    // TODO: likely not needed anymore. Firware on P100 will give 0 for TAG_ENABLED_ETH
    if (chip_info.board_type == BoardType::P100) {
        chip_info.harvesting_masks.eth_harvesting_mask = 0x3FFF;
    }

    chip_info.asic_location = telemetry->read_entry(TelemetryTag::ASIC_LOCATION);

    return chip_info;
}

semver_t BlackholeTTDevice::get_firmware_version() {
    return telemetry->is_entry_available(TelemetryTag::FLASH_BUNDLE_VERSION)
               ? fw_version_from_telemetry(telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION))
               : semver_t(0, 0, 0);
}

void BlackholeTTDevice::wait_arc_core_start(const uint32_t timeout_ms) {
    auto start = std::chrono::system_clock::now();
    uint32_t arc_boot_status;
    while (true) {
        read_from_arc(&arc_boot_status, blackhole::SCRATCH_RAM_2, sizeof(arc_boot_status));

        // ARC started successfully.
        if ((arc_boot_status & 0x7) == 0x5) {
            return;
        }

        auto end = std::chrono::system_clock::now();  // End time
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms) {
            log_error(
                LogSiliconDriver,
                "Timed out after waiting {} ms for arc core ({}, {}) to start",
                timeout_ms,
                arc_core.x,
                arc_core.y);
        }
    }
}

uint32_t BlackholeTTDevice::get_clock() {
    if (telemetry->is_entry_available(TelemetryTag::AICLK)) {
        return telemetry->read_entry(TelemetryTag::AICLK);
    }

    throw std::runtime_error("AICLK telemetry not available for Blackhole device.");
}

// TODO: figure out if Blackhole has the information on maximum possible
// clock frequency. For now, we are using the maximum possible value.
uint32_t BlackholeTTDevice::get_max_clock_freq() { return blackhole::AICLK_BUSY_VAL; }

uint32_t BlackholeTTDevice::get_min_clock_freq() { return blackhole::AICLK_IDLE_VAL; }

void BlackholeTTDevice::dma_d2h(void *dst, uint32_t src, size_t size) {
    throw std::runtime_error("D2H DMA is not supported on Blackhole.");
}

void BlackholeTTDevice::dma_h2d(uint32_t dst, const void *src, size_t size) {
    throw std::runtime_error("H2D DMA is not supported on Blackhole.");
}

void BlackholeTTDevice::dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) {
    throw std::runtime_error("H2D DMA is not supported on Blackhole.");
}

void BlackholeTTDevice::dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) {
    throw std::runtime_error("D2H DMA is not supported on Blackhole.");
}

void BlackholeTTDevice::read_from_arc(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    read_from_device(mem_ptr, arc_core, get_arc_noc_base_address() + arc_addr_offset, size);
};

void BlackholeTTDevice::write_to_arc(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    write_to_device(mem_ptr, arc_core, get_arc_noc_base_address() + arc_addr_offset, size);
}

void BlackholeTTDevice::wait_eth_core_training(const tt_xy_pair eth_core, const uint32_t timeout_ms) {
    uint32_t port_status_addr = blackhole::BOOT_RESULTS_ADDR + offsetof(blackhole::eth_status_t, port_status);
    uint32_t port_status_val;
    read_from_device(&port_status_val, eth_core, port_status_addr, sizeof(port_status_val));

    // Port status should be last state to settle during the eth training sequence
    // PORT_UNKNOWN means that eth is still training
    auto start = std::chrono::system_clock::now();
    while (port_status_val == blackhole::port_status_e::PORT_UNKNOWN) {
        read_from_device(&port_status_val, eth_core, port_status_addr, sizeof(port_status_val));
        auto end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms) {
            // TODO: Exception should be thrown here. ETH connections are very flaky
            // on Blackhole right now. When this is fixed we can throw the exception here.
            // Since we are not going to do any remote IO at the moment it is fine to just log the error.
            log_error(LogSiliconDriver, "ETH training timed out after {} ms", timeout_ms);
            break;
        }
    }
}

double BlackholeTTDevice::get_asic_temperature() {
    // Data stored in telemetry has temperature of ASIC stored in a way that high 16 bits
    // have integer part and lower 16 bits have fractional part.
    // It needs to be divided by 65536 to get temperature in Celsius.
    return (double)telemetry->read_entry(TelemetryTag::ASIC_TEMPERATURE) / 65536.0f;
}

uint64_t BlackholeTTDevice::get_arc_noc_base_address() const { return blackhole::ARC_NOC_XBAR_ADDRESS_START; }

bool BlackholeTTDevice::wait_arc_post_reset(const uint32_t timeout_ms) { return true; }

}  // namespace tt::umd
