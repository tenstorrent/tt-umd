// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "api/umd/device/warm_reset.hpp"

#include <fmt/color.h>
#include <glob.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>

#include "api/umd/device/arch/blackhole_implementation.hpp"
#include "api/umd/device/arch/grendel_implementation.hpp"
#include "api/umd/device/arch/wormhole_implementation.hpp"
#include "api/umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/timeouts.hpp"
#include "utils.hpp"

namespace tt::umd {

// TODO: Add more specific comments on what M3 reset does
// reset_m3 flag sends specific ARC message to do a M3 board level reset
void WarmReset::warm_reset(std::vector<int> pci_device_ids, bool reset_m3) {
    if constexpr (is_arm_platform()) {
        log_warning(tt::LogUMD, "Warm reset is disabled on ARM platforms due to instability. Skipping reset.");
        return;
    }
    // If pci_device_ids is empty, enumerate all devices.
    if (pci_device_ids.empty()) {
        pci_device_ids = PCIDevice::enumerate_devices();
    }

    if (PCIDevice::is_arch_agnostic_reset_supported()) {
        warm_reset_arch_agnostic(pci_device_ids, reset_m3);
        return;
    }

    auto enumerate_devices = PCIDevice::enumerate_devices_info();
    auto arch = enumerate_devices.begin()->second.get_arch();
    log_info(tt::LogUMD, "Starting reset for {} architecture.", arch_to_str(arch));
    switch (arch) {
        case ARCH::WORMHOLE_B0:
            warm_reset_wormhole_legacy(pci_device_ids, reset_m3);
            return;
        case ARCH::BLACKHOLE:
            if (reset_m3) {
                log_warning(tt::LogUMD, "Reset M3 flag doesn't influence Blackhole reset.");
            }
            warm_reset_blackhole_legacy(pci_device_ids);
            return;
        default:
            return;
    }
}

int wait_for_pci_bdf_to_reappear(
    const std::string& bdf, const std::chrono::milliseconds timeout_ms = timeout::WARM_RESET_DEVICES_REAPPEAR_TIMEOUT) {
    log_debug(tt::LogUMD, "Waiting for devices to reappear on pci bus.");

    auto deadline = std::chrono::steady_clock::now() + timeout_ms;
    bool device_reappeared = false;
    int interface_id = -1;

    while (std::chrono::steady_clock::now() < deadline) {
        // Use glob to find matching paths.
        glob_t glob_result;
        std::string pattern = fmt::format("/sys/bus/pci/devices/{}/tenstorrent/tenstorrent!*", bdf);

        int glob_status = glob(pattern.c_str(), GLOB_NOSORT, nullptr, &glob_result);

        if (glob_status == 0 && glob_result.gl_pathc > 0) {
            // Extract interface_id from the first match.
            std::string match_path = glob_result.gl_pathv[0];
            std::filesystem::path path(match_path);
            std::string filename = path.filename().string();

            // Remove "tenstorrent!" prefix using find().
            const std::string prefix = "tenstorrent!";
            if (filename.find(prefix) == 0) {
                std::string id_str = filename.substr(prefix.length());
                interface_id = std::stoi(id_str);

                std::string dev_path = fmt::format("/dev/tenstorrent/{}", interface_id);
                if (std::filesystem::exists(dev_path)) {
                    device_reappeared = true;
                }
            }
        }

        globfree(&glob_result);

        if (!device_reappeared) {
            std::this_thread::sleep_for(timeout::WARM_RESET_REAPPEAR_POLL_INTERVAL);
            continue;
        }
        break;
    }

    if (!device_reappeared) {
        log_warning(tt::LogUMD, "Timeout waiting for device at BDF {} to reappear.", bdf);
        return -1;
    }

    return interface_id;
}

void WarmReset::warm_reset_arch_agnostic(
    std::vector<int> pci_device_ids, bool reset_m3, std::chrono::milliseconds reset_m3_timeout) {
    std::unordered_set<int> pci_device_id_set(pci_device_ids.begin(), pci_device_ids.end());
    auto pci_devices_info = PCIDevice::enumerate_devices_info(pci_device_id_set);

    std::map<int, std::string> pci_bdfs;
    for (auto& pci_device_info : pci_devices_info) {
        pci_bdfs.insert({pci_device_info.first, pci_device_info.second.pci_bdf});
    }

    log_info(tt::LogUMD, "Starting reset on devices at PCI indices: {}", fmt::join(pci_device_id_set, ", "));
    PCIDevice::reset_device_ioctl(pci_device_id_set, TenstorrentResetDevice::RESET_PCIE_LINK);

    if (reset_m3) {
        PCIDevice::reset_device_ioctl(pci_device_id_set, TenstorrentResetDevice::ASIC_DMC_RESET);
    } else {
        PCIDevice::reset_device_ioctl(pci_device_id_set, TenstorrentResetDevice::ASIC_RESET);
    }

    // Calculate post-reset wait time: use provided M3 timeout if M3 reset, otherwise scale based on device count
    // (minimum 2 seconds, 0.4 seconds per device)
    auto post_reset_wait =
        reset_m3 ? reset_m3_timeout
                 : std::chrono::milliseconds(static_cast<int>(std::max(2.0, 0.4 * pci_devices_info.size()) * 1000));

    std::chrono::duration<double> post_reset_wait_seconds = post_reset_wait;

    log_debug(tt::LogUMD, "Waiting for {} seconds after reset execution.", post_reset_wait_seconds.count());
    std::this_thread::sleep_for(post_reset_wait);
    log_debug(tt::LogUMD, "{} seconds elapsed after reset execution.", post_reset_wait_seconds.count());

    for (auto& pci_bdf : pci_bdfs) {
        auto new_id = wait_for_pci_bdf_to_reappear(pci_bdf.second);
        if (new_id == -1) {
            log_error(tt::LogUMD, "Reset failed.");
            return;
        }
    }

    PCIDevice::reset_device_ioctl(pci_device_id_set, TenstorrentResetDevice::POST_RESET);
}

void WarmReset::warm_reset_blackhole_legacy(std::vector<int> pci_device_ids) {
    std::unordered_set<int> pci_device_ids_set(pci_device_ids.begin(), pci_device_ids.end());
    PCIDevice::reset_device_ioctl(pci_device_ids_set, TenstorrentResetDevice::CONFIG_WRITE);

    std::map<int, bool> reset_bits;

    for (const auto& pci_device_id : pci_device_ids) {
        reset_bits.emplace(pci_device_id, 0);
    }

    bool all_reset_bits_set{true};

    auto start = std::chrono::steady_clock::now();
    auto timeout_duration = timeout::BH_WARM_RESET_TIMEOUT;

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

    std::this_thread::sleep_for(POST_RESET_WAIT);

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

void WarmReset::warm_reset_wormhole_legacy(std::vector<int> pci_device_ids, bool reset_m3) {
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
        if (!tt_device->wait_arc_core_start(timeout::ARC_LONG_POST_RESET_TIMEOUT)) {
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
            MSG_TYPE_ARC_STATE3, arc_msg_return_values, {default_arg_value, default_arg_value});
        usleep(30'000);
        if (reset_m3) {
            tt_device->get_arc_messenger()->send_message(
                MSG_TYPE_TRIGGER_RESET, arc_msg_return_values, {3, default_arg_value});
        } else {
            tt_device->get_arc_messenger()->send_message(
                MSG_TYPE_TRIGGER_RESET, arc_msg_return_values, {default_arg_value, default_arg_value});
        }
    }

    std::this_thread::sleep_for(POST_RESET_WAIT);

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
        "Starting reset. Executing command: {}",
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
            // Success: Exit code is 0.
            log_info(tt::LogUMD, "Reset successfully completed. Exit code: {}", exit_code);
            return;
        }

        // Failure: Program exited normally but with a non-zero code.
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

void WarmReset::ubb_wait_for_driver_load(const std::chrono::milliseconds timeout_ms) {
    static constexpr size_t NUMBER_OF_PCIE_DEVICES = 32;
    auto pci_devices = PCIDevice::enumerate_devices();
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout_ms) {
        if (pci_devices.size() == NUMBER_OF_PCIE_DEVICES) {
            log_debug(tt::LogUMD, "Found all {} PCIe devices", NUMBER_OF_PCIE_DEVICES);
            return;
        }
        sleep(1);
        pci_devices = PCIDevice::enumerate_devices();
    }

    log_warning(
        tt::LogUMD, "Failed to find all {} PCIe devices, found: {}", NUMBER_OF_PCIE_DEVICES, pci_devices.size());
}

void WarmReset::ubb_warm_reset(const std::chrono::milliseconds timeout_ms) {
    static int constexpr UBB_NUM = 0xF;
    static int constexpr DEV_NUM = 0xFF;
    static int constexpr OP_MODE = 0x0;
    static int constexpr RESET_TIME = 0xF;

    wormhole_ubb_ipmi_reset(UBB_NUM, DEV_NUM, OP_MODE, RESET_TIME);
    log_debug(tt::LogUMD, "Waiting for 30 seconds after reset execution.");
    sleep(30);
    log_debug(tt::LogUMD, "30 seconds elapsed after reset execution.");
    ubb_wait_for_driver_load(timeout_ms);
}

}  // namespace tt::umd
