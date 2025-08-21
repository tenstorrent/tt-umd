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
#include "api/umd/device/wormhole_implementation.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/arch.h"

namespace tt::umd {

// TODO: Add more specific comments on what M3 reset does
// reset_m3 flag sends specific ARC message to do a M3 board level reset
void WarmReset::warm_reset(bool reset_m3) {
    auto enumerate_devices = PCIDevice::enumerate_devices_info();
    auto arch = enumerate_devices.begin()->second.get_arch();
    log_info(tt::LogSiliconDriver, "Starting reset for {} architecture.", arch_to_str(arch));
    switch (arch) {
        case ARCH::WORMHOLE_B0:
            warm_reset_wormhole(reset_m3);
            return;
        case ARCH::BLACKHOLE:
            if (reset_m3) {
                log_warning(tt::LogSiliconDriver, "Reset M3 flag doesn't influence Blackhole reset.");
            }
            warm_reset_blackhole();
            return;
        default:
            return;
    }
}

void WarmReset::warm_reset_blackhole() {
    PCIDevice::reset_devices(tt::umd::TenstorrentResetDevice::CONFIG_WRITE);

    auto pci_device_ids = PCIDevice::enumerate_devices();

    std::map<int, bool> reset_bits;

    for (const auto& pci_device_id : pci_device_ids) {
        reset_bits.emplace(pci_device_id, 0);
    }

    bool all_reset_bits_set{true};

    auto start = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::milliseconds(2000);

    while (std::chrono::steady_clock::now() - start < timeout_duration) {
        for (const auto& pci_device_id : pci_device_ids) {
            auto command_byte = PCIDevice::read_command_byte(pci_device_id);
            bool reset_bit = (command_byte >> 1) & 1;
            reset_bits[pci_device_id] = reset_bit;
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

    sleep(POST_RESET_WAIT);

    if (!all_reset_bits_set) {
        for (auto& [chip, reset_bit] : reset_bits) {
            if (!reset_bit) {
                log_warning(tt::LogSiliconDriver, "Config space reset not completed for chip_id : {}", chip);
            }
        }
    }

    if (all_reset_bits_set) {
        log_info(tt::LogSiliconDriver, "Reset succesfully completed.");
    }
    PCIDevice::reset_devices(TenstorrentResetDevice::RESTORE_STATE);
}

void WarmReset::warm_reset_wormhole(bool reset_m3) {
    bool reset_ok = true;
    static constexpr uint16_t default_arg_value = 0xFFFF;
    static constexpr uint32_t MSG_TYPE_ARC_STATE3 = 0xA3 | wormhole::ARC_MSG_COMMON_PREFIX;
    static constexpr uint32_t MSG_TYPE_TRIGGER_RESET = 0x56 | wormhole::ARC_MSG_COMMON_PREFIX;

    PCIDevice::reset_devices(TenstorrentResetDevice::RESET_PCIE_LINK);

    auto pci_device_ids = PCIDevice::enumerate_devices();

    std::vector<std::unique_ptr<TTDevice>> tt_devices;
    tt_devices.reserve(pci_device_ids.size());

    for (auto& i : pci_device_ids) {
        auto tt_device = TTDevice::create(i);
        if (!tt_device->wait_arc_post_reset(300'000)) {
            log_warning(tt::LogSiliconDriver, "Reset failed for pci id {} - ARC core init failed", i);
            continue;
        }
        tt_devices.emplace_back(std::move(tt_device));
    }

    for (auto& tt_device : tt_devices) {
        tt_device->init_tt_device();
    }

    std::vector<uint64_t> refclk_values_old;
    refclk_values_old.reserve(pci_device_ids.size());

    for (const auto& tt_device : tt_devices) {
        refclk_values_old.emplace_back(tt_device->get_refclk_counter());
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

    sleep(POST_RESET_WAIT);

    std::vector<uint64_t> refclk_current;
    refclk_current.reserve(pci_device_ids.size());

    PCIDevice::reset_devices(TenstorrentResetDevice::RESTORE_STATE);

    for (const auto& tt_device : tt_devices) {
        refclk_current.emplace_back(tt_device->get_refclk_counter());
    }

    for (int i = 0; i < refclk_values_old.size(); i++) {
        if (refclk_values_old[i] < refclk_current[i]) {
            reset_ok = false;
            log_warning(
                LogSiliconDriver,
                "Reset for PCI: {} didn't go through! Refclk didn't reset. Value before: {}, value after: {}",
                i,
                refclk_values_old[i],
                refclk_current[i]);
        }
    }

    if (reset_ok) {
        log_info(tt::LogSiliconDriver, "Reset successfully completed.");
    }
}

}  // namespace tt::umd
