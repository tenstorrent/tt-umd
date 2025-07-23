/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "api/umd/device/warm_reset.h"

#include <chrono>
#include <memory>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "api/umd/device/blackhole_implementation.h"
#include "api/umd/device/pci_device.hpp"
#include "api/umd/device/tt_soc_descriptor.h"
#include "api/umd/device/wormhole_implementation.h"
#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

void WarmReset::warm_reset(ARCH architecture, bool reset_m3) {
    switch (architecture) {
        case ARCH::WORMHOLE_B0:
            warm_reset_wormhole(reset_m3);
            return;
        case ARCH::BLACKHOLE:
            warm_reset_blackhole();
            return;
        default:
            return;
    }
}

void WarmReset::warm_reset_blackhole() {
    PCIDevice::reset_devices(tt::umd::TenstorrentResetDevice::CONFIG_WRITE);

    auto pci_device_ids = PCIDevice::enumerate_devices();

    std::vector<std::unique_ptr<TTDevice>> tt_devices;
    tt_devices.reserve(pci_device_ids.size());

    for (auto& i : pci_device_ids) {
        tt_devices.emplace_back(TTDevice::create(i));
    }

    std::map<int, bool> reset_bits;

    for (const auto& tt_device : tt_devices) {
        reset_bits.emplace(tt_device->get_pci_device()->get_device_num(), 0x0);
    }

    bool all_reset_bits_set{true};

    auto start = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::milliseconds(2000);  // 5 seconds

    while (std::chrono::steady_clock::now() - start < timeout_duration) {
        for (const auto& tt_device : tt_devices) {
            auto command_byte = tt_device->get_pci_device()->read_command_byte();
            bool reset_bit = (command_byte >> 1) & 1;
            reset_bits[tt_device->get_pci_device()->get_device_num()] = reset_bit;
        }

        for (auto& [pci_device_id, reset_bit] : reset_bits) {
            if (reset_bit != true) {
                all_reset_bits_set = false;
                break;
            }
        }

        if (all_reset_bits_set) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!all_reset_bits_set) {
        for (auto& [chip, reset_bit] : reset_bits) {
            if (!reset_bit) {
                log_warning(tt::LogSiliconDriver, "Config space reset not completed for chip_id : {}", chip);
            }
        }
    }

    PCIDevice::reset_devices(TenstorrentResetDevice::RESTORE_STATE);

    for (auto& tt_device : tt_devices) {
        reinitialize(tt_device.get());
    }
}

void WarmReset::warm_reset_wormhole(bool reset_m3) {
    static constexpr uint16_t default_arg_value = 0xFFFF;
    static constexpr uint32_t MSG_TYPE_ARC_STATE3 = 0xA3 | wormhole::ARC_MSG_COMMON_PREFIX;
    static constexpr uint32_t MSG_TYPE_TRIGGER_RESET = 0x56 | wormhole::ARC_MSG_COMMON_PREFIX;

    PCIDevice::reset_devices(TenstorrentResetDevice::RESET_PCIE_LINK);

    auto pci_device_ids = PCIDevice::enumerate_devices();

    std::vector<std::unique_ptr<TTDevice>> tt_devices;
    tt_devices.reserve(pci_device_ids.size());

    for (auto& i : pci_device_ids) {
        tt_devices.emplace_back(TTDevice::create(i));
    }

    std::vector<uint64_t> refclk_values_old;
    refclk_values_old.reserve(pci_device_ids.size());

    for (const auto& tt_device : tt_devices) {
        refclk_values_old.emplace_back(get_refclk_counter(tt_device.get()));
    }

    std::vector<uint32_t> arc_msg_return_values(1);
    for (const auto& tt_device : tt_devices) {
        tt_device->get_arc_messenger()->send_message(
            MSG_TYPE_ARC_STATE3, arc_msg_return_values, default_arg_value, default_arg_value);
        usleep(30'000);
        if (reset_m3) {
            tt_device->get_arc_messenger()->send_message(
                MSG_TYPE_TRIGGER_RESET, arc_msg_return_values, 3, default_arg_value);
        } else {
            tt_device->get_arc_messenger()->send_message(
                MSG_TYPE_TRIGGER_RESET, arc_msg_return_values, default_arg_value, default_arg_value);
        }
    }
    sleep(2);

    std::vector<uint64_t> refclk_current;
    refclk_current.reserve(pci_device_ids.size());

    PCIDevice::reset_devices(TenstorrentResetDevice::RESTORE_STATE);

    for (const auto& tt_device : tt_devices) {
        refclk_current.emplace_back(get_refclk_counter(tt_device.get()));
    }

    for (int i = 0; i < refclk_values_old.size(); i++) {
        if (refclk_values_old[i] < refclk_current[i]) {
            log_warning(
                LogSiliconDriver,
                "Reset for PCI: {} didn't go through! Refclk didn't reset. Value before: {}, value after: {}",
                i,
                refclk_values_old[i],
                refclk_current[i]);
        }
    }

    for (auto& tt_device : tt_devices) {
        reinitialize(tt_device.get());
    }
}

uint64_t WarmReset::get_refclk_counter(TTDevice* tt_device) {
    auto high1_addr =
        tt_device->bar_read32(wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + wormhole::ARC_RESET_REFCLK_HIGH_OFFSET);
    auto low_addr =
        tt_device->bar_read32(wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + wormhole::ARC_RESET_REFCLK_LOW_OFFSET);
    auto high2_addr =
        tt_device->bar_read32(wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + wormhole::ARC_RESET_REFCLK_HIGH_OFFSET);
    if (high1_addr != high2_addr) {
        low_addr =
            tt_device->bar_read32(wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + wormhole::ARC_RESET_REFCLK_LOW_OFFSET);
    }
    return (static_cast<uint64_t>(high2_addr) << 32) | low_addr;
}

void WarmReset::reinitialize(TTDevice* tt_device) {
    tt_device->wait_arc_core_start(tt_device->get_arc_core());
    tt_device->wait_dram_core_training();

    tt_SocDescriptor soc_descriptor{
        tt_device->get_arch(), tt_device->get_noc_translation_enabled(), tt_device->get_chip_info().harvesting_masks};
    auto eth_cores = soc_descriptor.get_cores(CoreType::ETH, CoordSystem::PHYSICAL);

    for (auto& eth_core : eth_cores) {
        tt_device->wait_eth_core_training(eth_core);
    }
}

}  // namespace tt::umd
