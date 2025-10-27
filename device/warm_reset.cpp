/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "api/umd/device/warm_reset.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "api/umd/device/arch/blackhole_implementation.hpp"
#include "api/umd/device/arch/wormhole_implementation.hpp"
#include "api/umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "utils.hpp"

namespace tt::umd {

// TODO: Add more specific comments on what M3 reset does
// reset_m3 flag sends specific ARC message to do a M3 board level reset
void WarmReset::warm_reset(std::vector<int> pci_device_ids, bool reset_m3) {
    // If pci_device_ids is empty, enumerate all devices
    if (pci_device_ids.empty()) {
        pci_device_ids = PCIDevice::enumerate_devices();
    }

    auto enumerate_devices = PCIDevice::enumerate_devices_info();
    auto arch = enumerate_devices.begin()->second.get_arch();
    log_info(tt::LogUMD, "Starting reset for {} architecture.", arch_to_str(arch));
    switch (arch) {
        case ARCH::WORMHOLE_B0:
            warm_reset_wormhole(pci_device_ids, reset_m3);
            return;
        case ARCH::BLACKHOLE:
            if (reset_m3) {
                log_warning(tt::LogUMD, "Reset M3 flag doesn't influence Blackhole reset.");
            }
            warm_reset_blackhole(pci_device_ids);
            return;
        default:
            return;
    }
}

void WarmReset::warm_reset_blackhole(std::vector<int> pci_device_ids) {
    std::unordered_set<int> pci_device_ids_set(pci_device_ids.begin(), pci_device_ids.end());
    PCIDevice::reset_device_ioctl(pci_device_ids_set, tt::umd::TenstorrentResetDevice::CONFIG_WRITE);

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
                log_warning(tt::LogUMD, "Config space reset not completed for chip_id : {}", chip);
            }
        }
    }

    if (all_reset_bits_set) {
        log_info(tt::LogUMD, "Reset succesfully completed.");
    }
    PCIDevice::reset_device_ioctl(pci_device_ids_set, TenstorrentResetDevice::RESTORE_STATE);
}

void WarmReset::warm_reset_wormhole(std::vector<int> pci_device_ids, bool reset_m3) {
    bool reset_ok = true;
    static constexpr uint16_t default_arg_value = 0xFFFF;
    static constexpr uint32_t MSG_TYPE_ARC_STATE3 = 0xA3 | wormhole::ARC_MSG_COMMON_PREFIX;
    static constexpr uint32_t MSG_TYPE_TRIGGER_RESET = 0x56 | wormhole::ARC_MSG_COMMON_PREFIX;

    std::unordered_set<int> pci_device_ids_set(pci_device_ids.begin(), pci_device_ids.end());
    PCIDevice::reset_device_ioctl(pci_device_ids_set, TenstorrentResetDevice::RESET_PCIE_LINK);

    std::vector<std::unique_ptr<TTDevice>> tt_devices;
    tt_devices.reserve(pci_device_ids.size());

    for (auto& i : pci_device_ids) {
        auto tt_device = TTDevice::create(i);
        if (!tt_device->wait_arc_post_reset(300'000)) {
            log_warning(tt::LogUMD, "Reset failed for PCI id {} - ARC core init failed", i);
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

    PCIDevice::reset_device_ioctl(pci_device_ids_set, TenstorrentResetDevice::RESTORE_STATE);

    for (const auto& tt_device : tt_devices) {
        refclk_current.emplace_back(tt_device->get_refclk_counter());
    }

    for (int i = 0; i < refclk_values_old.size(); i++) {
        if (refclk_values_old[i] < refclk_current[i]) {
            reset_ok = false;
            log_warning(
                tt::LogUMD,
                "Reset for PCI: {} didn't go through! Refclk didn't reset. Value before: {}, value after: {}",
                i,
                refclk_values_old[i],
                refclk_current[i]);
        }
    }

    if (reset_ok) {
        log_info(tt::LogUMD, "Reset successfully completed.");
    }
}

void WarmReset::wormhole_ubb_ipmi_reset(int ubb_num, int dev_num, int op_mode, int reset_time) {
    const std::string ipmi_tool_command{"sudo ipmitool raw 0x30 0x8b"};
    log_info(
        tt::LogUMD,
        "Executing command: {}",
        utils::convert_to_space_separated_string(
            ipmi_tool_command,
            utils::to_hex_string(ubb_num),
            utils::to_hex_string(dev_num),
            utils::to_hex_string(op_mode),
            utils::to_hex_string(reset_time)));

    int status = system(utils::convert_to_space_separated_string(
                            ipmi_tool_command,
                            utils::to_hex_string(ubb_num),
                            utils::to_hex_string(dev_num),
                            utils::to_hex_string(op_mode),
                            utils::to_hex_string(reset_time))
                            .c_str());

    if (status == -1) {
        log_error(tt::LogUMD, "System call failed to execute: {}", strerror(errno));
        return;
    }

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);

        if (exit_code == 0) {
            // Success: Exit code is 0
            log_info(tt::LogUMD, "Reset successfully completed. Exit code: {}", exit_code);
            return;
        }

        // Failure: Program exited normally but with a non-zero code
        log_error(tt::LogUMD, "Reset error! Program exited with code: {}", exit_code);
        return;
    }

    if (WIFSIGNALED(status)) {
        int signal_num = WTERMSIG(status);
        log_error(tt::LogUMD, "Reset failed! Program terminated by signal: {} ({})", signal_num, strsignal(signal_num));
        return;
    }

    log_warning(tt::LogUMD, "Reset failed! Program terminated for an unknown reason (status: 0x{:x})", status);
}

void WarmReset::ubb_wait_for_driver_load(uint64_t timeout_s) {
    static constexpr size_t NUMBER_OF_PCIE_DEVICES = 32;
    auto pci_devices = PCIDevice::enumerate_devices();
    auto start = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::seconds(timeout_s);
    while (std::chrono::steady_clock::now() - start < timeout_duration) {
        if (pci_devices.size() == NUMBER_OF_PCIE_DEVICES) {
            log_info(tt::LogUMD, "Found all {} PCIe devices", NUMBER_OF_PCIE_DEVICES);
            return;
        }
        sleep(1);
        pci_devices = PCIDevice::enumerate_devices();
    }

    log_warning(
        tt::LogUMD, "Failed to find all {} PCIe devices, found: {}", NUMBER_OF_PCIE_DEVICES, pci_devices.size());
}

void WarmReset::ubb_warm_reset(uint64_t timeout_s) {
    static int constexpr UBB_NUM = 0xF;
    static int constexpr DEV_NUM = 0xFF;
    static int constexpr OP_MODE = 0x0;
    static int constexpr RESET_TIME = 0xF;

    wormhole_ubb_ipmi_reset(UBB_NUM, DEV_NUM, OP_MODE, RESET_TIME);
    log_info(tt::LogUMD, "Waiting for 30 seconds after reset execution.");
    sleep(30);
    log_info(tt::LogUMD, "30 seconds elapsed after reset execution.");
    ubb_wait_for_driver_load(timeout_s);
}

}  // namespace tt::umd
