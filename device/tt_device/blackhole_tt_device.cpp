// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/blackhole_tt_device.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/mman.h>  // for MAP_FAILED
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "noc_access.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/telemetry.hpp"
#include "utils.hpp"

namespace tt::umd {

BlackholeTTDevice::BlackholeTTDevice(std::shared_ptr<PCIDevice> pci_device) :
    TTDevice(std::move(pci_device), std::make_unique<blackhole_implementation>()) {
    arc_core = blackhole::get_arc_core(BlackholeTTDevice::get_noc_translation_enabled(), is_selected_noc1());
}

BlackholeTTDevice::BlackholeTTDevice(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id) :
    TTDevice(std::move(jtag_device), jlink_id, std::make_unique<blackhole_implementation>()) {
    arc_core = blackhole::get_arc_core(BlackholeTTDevice::get_noc_translation_enabled(), is_selected_noc1());
}

BlackholeTTDevice::~BlackholeTTDevice() {
    // Turn off iATU for the regions we programmed.  This won't happen if the
    // application crashes -- this is a good example of why userspace should not
    // be touching this hardware resource directly -- but it's a good idea to
    // clean up after ourselves.
    if (get_communication_device_type() != IODeviceType::PCIe) {
        return;
    }
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
        LogUMD,
        "Device: {} Mapped iATU region {} from 0x{:x} to 0x{:x} to 0x{:x}",
        this->pci_device_->get_device_num(),
        region,
        base,
        limit,
        target);
}

bool BlackholeTTDevice::get_noc_translation_enabled() {
    uint32_t niu_cfg;
    const uint64_t addr = blackhole::NIU_CFG_NOC0_BAR_ADDR;

    if (get_communication_device_type() == IODeviceType::JTAG) {
        // Target arc core.
        niu_cfg = get_jtag_device()->read32_axi(0, blackhole::NIU_CFG_NOC0_ARC_ADDR).value();
    } else {
        niu_cfg = bar_read32(addr);
    }
    return ((niu_cfg >> 14) & 0x1) != 0;
}

ChipInfo BlackholeTTDevice::get_chip_info() {
    ChipInfo chip_info = TTDevice::get_chip_info();
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

    chip_info.harvesting_masks.pcie_harvesting_mask = 0;
    if (telemetry->is_entry_available(TelemetryTag::PCIE_USAGE)) {
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
    }

    chip_info.harvesting_masks.l2cpu_harvesting_mask = 0;
    if (telemetry->is_entry_available(TelemetryTag::ENABLED_L2CPU)) {
        chip_info.harvesting_masks.l2cpu_harvesting_mask = CoordinateManager::shuffle_l2cpu_harvesting_mask(
            tt::ARCH::BLACKHOLE, telemetry->read_entry(TelemetryTag::ENABLED_L2CPU));
    }

    return chip_info;
}

bool BlackholeTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) noexcept {
    uint32_t arc_boot_status;
    const auto start = std::chrono::steady_clock::now();
    constexpr auto spin_limit = std::chrono::microseconds(1000);
    while (true) {
        read_from_arc_apb(&arc_boot_status, blackhole::SCRATCH_RAM_2, sizeof(arc_boot_status));

        // ARC started successfully.
        if ((arc_boot_status & 0x7) == 0x5) {
            return true;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;

        // If we are within the first 200us, busy-wait (continue).
        // This burns CPU, but guarantees we catch the status change instantly in this interval.
        if (elapsed < spin_limit) {
            // Optional: For 0ms timeouts, check manually here without strings.
            if (elapsed > timeout_ms) {
                return false;
            }
            continue;
        }

        if (utils::check_timeout(
                start,
                timeout_ms,
                fmt::format(
                    "Timed out after waiting {} ms for arc core ({}, {}) to start",
                    timeout_ms.count(),
                    arc_core.x,
                    arc_core.y),
                utils::TimeoutAction::Return)) {
            return false;
        }

        // If past 200us, avoid busy-waiting. Request a 10us sleep (minimum) -
        // actual duration will be longer due to OS scheduling and jitter.
        // This prevents 100% CPU usage during longer hardware initialization.
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

uint32_t BlackholeTTDevice::get_clock() {
    if (telemetry->is_entry_available(TelemetryTag::AICLK)) {
        return telemetry->read_entry(TelemetryTag::AICLK);
    }

    throw std::runtime_error("AICLK telemetry not available for Blackhole device.");
}

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

void BlackholeTTDevice::read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > blackhole::ARC_XBAR_ADDRESS_END) {
        throw std::runtime_error("Address is out of ARC XBAR address range.");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->read(
            communication_device_id_,
            mem_ptr,
            blackhole::ARC_CORES_NOC0[0].x,
            blackhole::ARC_CORES_NOC0[0].y,
            blackhole::ARC_NOC_XBAR_ADDRESS_START + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    if (!is_arc_available_over_axi()) {
        read_from_device(mem_ptr, arc_core, architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size);
        return;
    }
    auto result = bar_read32(blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset);
    *(reinterpret_cast<uint32_t *>(mem_ptr)) = result;
};

void BlackholeTTDevice::write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > blackhole::ARC_XBAR_ADDRESS_END) {
        throw std::runtime_error("Address is out of ARC XBAR address range.");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(
            communication_device_id_,
            mem_ptr,
            blackhole::ARC_CORES_NOC0[0].x,
            blackhole::ARC_CORES_NOC0[0].y,
            blackhole::ARC_NOC_XBAR_ADDRESS_START + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    if (!is_arc_available_over_axi()) {
        write_to_device(mem_ptr, arc_core, architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size);
        return;
    }
    bar_write32(
        blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset, *(reinterpret_cast<const uint32_t *>(mem_ptr)));
}

void BlackholeTTDevice::write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    throw std::runtime_error("CSM write not supported for Blackhole.");
}

void BlackholeTTDevice::read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    throw std::runtime_error("CSM read not supported for Blackhole.");
}

std::chrono::milliseconds BlackholeTTDevice::wait_eth_core_training(
    const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms) {
    auto time_taken = std::chrono::milliseconds(0);

    uint32_t port_status_addr = blackhole::BOOT_RESULTS_ADDR + offsetof(blackhole::eth_status_t, port_status);
    uint32_t port_status_val;
    read_from_device(&port_status_val, eth_core, port_status_addr, sizeof(port_status_val));

    // Port status should be last state to settle during the eth training sequence
    // PORT_UNKNOWN means that eth is still training.
    auto start = std::chrono::steady_clock::now();
    while (port_status_val == blackhole::port_status_e::PORT_UNKNOWN) {
        read_from_device(&port_status_val, eth_core, port_status_addr, sizeof(port_status_val));
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration > timeout_ms) {
            // TODO: Exception should be thrown here. ETH connections are very flaky
            // on Blackhole right now. When this is fixed we can throw the exception here.
            // Since we are not going to do any remote IO at the moment it is fine to just log the error.
            log_error(LogUMD, "ETH training timed out after {} ms", timeout_ms.count());
            break;
        }
    }
    return time_taken;
}

bool BlackholeTTDevice::is_hardware_hung() {
    // throw std::runtime_error("Hardware hang detection is not supported on Blackhole.");

    // TODO: I am commented that out because we end up in this code path if we
    // read 0xfffffff from a Blackhole. Although 0xffffffff can indicate a hang,
    // it doesn't necessarily mean the hardware is hung. It's possible to write
    // 0xffffffff to device memory and reading it back should not trigger an
    // exception. In my case, the hardware was not hung but the 0xffffffff was
    // related to a failure which was obscured by the exception. For now,
    // just return false.  -- @joelsmithTT, Oct 1 2025

    log_debug(LogUMD, "Hang detection is not supported (yet) on Blackhole.");
    return false;
}

int BlackholeTTDevice::get_pcie_x_coordinate() {
    // Extract the x-coordinate from the register using the lower 6 bits.
    return bar_read32(get_architecture_implementation()->get_read_checking_offset()) & 0x3F;
}

// ARC tile accessibility over AXI via PCIe depends on the PCIe tile's x-coordinate:
// x = 2: ARC not accessible, x = 11: ARC accessible
bool BlackholeTTDevice::is_arc_available_over_axi() { return (get_pcie_x_coordinate() == 11); }

void BlackholeTTDevice::dma_multicast_write(
    void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    throw std::runtime_error("DMA multicast write not supported for Blackhole devices.");
}

}  // namespace tt::umd
