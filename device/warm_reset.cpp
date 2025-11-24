/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "api/umd/device/warm_reset.hpp"

#include <fmt/color.h>
#include <glob.h>

#include <asio.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
// delete iostream
#include <iostream>
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

// -------------------------------------------------------
// START OF ASIO IMPLEMENTATION
// -------------------------------------------------------

const std::string LISTENER_DIR = "/tmp/tt_umd_listeners";

// Global keep-alive for the listener thread to prevent it from going out of scope
static std::unique_ptr<std::thread> monitor_thread;
static std::atomic<bool> keep_monitoring{true};

// Just a compile check, no logic needed yet
void check_asio_version() { std::cout << "Asio linked. Version: " << ASIO_VERSION << std::endl; }

bool WarmReset::start_monitoring(std::function<void()> on_cleanup_request) {
    namespace fs = std::filesystem;

    // Prevent starting monitoring twice in the same process
    if (monitor_thread) {
        log_warning(tt::LogUMD, "Reset monitoring is already running in this process.");
        return false;
    }

    monitor_thread = std::make_unique<std::thread>([on_cleanup_request]() {
        asio::io_context io;
        std::error_code ec;
        log_info(tt::LogUMD, "Made the thread!");
        // 1. Ensure Directory Exists
        if (!fs::exists(LISTENER_DIR)) {
            fs::create_directories(LISTENER_DIR, ec);
            fs::permissions(LISTENER_DIR, fs::perms::all, ec);  // Allow other users/groups
        }

        // 2. Create Unique Socket Name: client_<PID>.sock
        // We use the PID so the invoker knows who this is
        std::string socket_name = "client_" + std::to_string(getpid()) + ".sock";
        fs::path socket_path = fs::path(LISTENER_DIR) / socket_name;

        // Cleanup stale socket if we crashed previously
        ::unlink(socket_path.c_str());

        // 3. Setup Acceptor
        asio::local::stream_protocol::acceptor acceptor(
            io, asio::local::stream_protocol::endpoint(socket_path.string()));

        // Ensure socket is writable by others (if running as different users)
        fs::permissions(socket_path, fs::perms::all, ec);

        // 4. Accept Loop
        auto do_accept = std::make_shared<std::function<void()>>();
        *do_accept = [&, do_accept]() {
            auto sock = std::make_shared<asio::local::stream_protocol::socket>(io);
            acceptor.async_accept(*sock, [sock, &do_accept, on_cleanup_request](std::error_code ec) {
                if (!ec && keep_monitoring) {
                    // A Reset Tool connected to us!

                    auto buf = std::make_shared<std::vector<char>>(64);
                    sock->async_read_some(
                        asio::buffer(*buf), [sock, buf, on_cleanup_request](std::error_code ec, size_t len) {
                            if (!ec) {
                                std::string_view msg(buf->data(), len);
                                if (msg.find("PRE_RESET") != std::string::npos) {
                                    log_info(tt::LogUMD, "Received Pre-Reset Notification!");

                                    // --- EXECUTE USER CLEANUP CALLBACK ---
                                    if (on_cleanup_request) {
                                        on_cleanup_request();
                                    }
                                    // -------------------------------------

                                    // Send ACK
                                    asio::write(*sock, asio::buffer("READY"));
                                }
                            }
                        });

                    // Continue listening for future resets
                    (*do_accept)();
                }
            });
        };

        (*do_accept)();

        // Run until application exit
        while (keep_monitoring) {
            io.run_one();
        }

        // Cleanup on exit
        ::unlink(socket_path.c_str());
    });

    // Detach so it runs in background, or keep joinable if you want to manage lifecycle strictly
    monitor_thread->detach();
    return true;
}

// -------------------------------------------------------
// INVOKER IMPLEMENTATION (With Self-Exclusion)
// -------------------------------------------------------

int WarmReset::extract_pid_from_socket_name(const std::string& filename) {
    // Format: "client_12345.sock"
    try {
        size_t start = filename.find('_');
        size_t end = filename.find('.');
        if (start != std::string::npos && end != std::string::npos) {
            std::string pid_str = filename.substr(start + 1, end - start - 1);
            return std::stoi(pid_str);
        }
    } catch (...) {
    }
    return -1;
}

bool WarmReset::notify_all_listeners_with_handshake(std::chrono::milliseconds timeout_ms) {
    namespace fs = std::filesystem;

    if (!fs::exists(LISTENER_DIR)) {
        return true;
    }

    asio::io_context io;
    std::vector<std::shared_ptr<asio::local::stream_protocol::socket>> active_sockets;
    int my_pid = getpid();

    for (const auto& entry : fs::directory_iterator(LISTENER_DIR)) {
        if (!entry.is_socket()) {
            continue;
        }

        // --- SAFETY CHECK: IGNORE SELF ---
        std::string filename = entry.path().filename().string();
        int target_pid = extract_pid_from_socket_name(filename);

        if (target_pid == my_pid) {
            log_info(tt::LogUMD, "Skipping my own listener socket ({})", filename);
            continue;
        }
        // ---------------------------------

        auto sock = std::make_shared<asio::local::stream_protocol::socket>(io);
        try {
            sock->connect(asio::local::stream_protocol::endpoint(entry.path().string()));
            log_info(tt::LogUMD, "Connected to socket!");
            active_sockets.push_back(sock);
        } catch (...) {
            // Stale socket
        }
    }

    if (active_sockets.empty()) {
        return true;
    }

    // ... (Rest of the broadcast/handshake logic logic from previous answer) ...
    // Send PRE_RESET, wait for READY, handle timeout...
    log_info(tt::LogUMD, "Handshaking with {} active clients...", active_sockets.size());

    for (auto& sock : active_sockets) {
        // Async write to avoid blocking if a client hangs on read
        asio::async_write(*sock, asio::buffer("PRE_RESET"), [](std::error_code, size_t) { /* Ignore write errors */ });
    }

    // 4. Wait for Handshakes (ACKs)
    std::atomic<int> pending_acks = active_sockets.size();
    asio::steady_timer timer(io, timeout_ms);

    // Timeout Handler
    timer.async_wait([&](std::error_code ec) {
        if (!ec) {  // Timer expired
            log_warning(tt::LogUMD, "Reset Handshake Timeout! Proceeding anyway.");
            io.stop();
        }
    });

    // Read Handler for each client
    for (auto& sock : active_sockets) {
        auto buf = std::make_shared<std::vector<char>>(64);  // Keep alive via lambda capture
        sock->async_read_some(asio::buffer(*buf), [&io, &pending_acks, &timer, buf](std::error_code ec, size_t len) {
            if (!ec) {
                std::string_view msg(buf->data(), len);
                // Check for "READY" string
                if (msg.find("READY") != std::string::npos) {
                    if (--pending_acks == 0) {
                        timer.cancel();  // All clients ready -> Cancel timeout
                        io.stop();       // Stop loop -> Proceed to reset
                    }
                }
            }
        });
    }

    io.run();  // Blocks until Timeout OR All Acks received

    // 5. Final Cleanup
    // Explicitly close sockets to be polite
    for (auto& s : active_sockets) {
        if (s->is_open()) {
            s->close();
        }
    }

    return true;
}

// -------------------------------------------------------
// END OF ASIO IMPLEMENTATION
// -------------------------------------------------------

// TODO: Add more specific comments on what M3 reset does
// reset_m3 flag sends specific ARC message to do a M3 board level reset
void WarmReset::warm_reset(std::vector<int> pci_device_ids, bool reset_m3) {
    if constexpr (is_arm_platform()) {
        log_warning(tt::LogUMD, "Warm reset is disabled on ARM platforms due to instability. Skipping reset.");
        return;
    }
    // If pci_device_ids is empty, enumerate all devices
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
    PCIDevice::reset_device_ioctl(pci_device_id_set, tt::umd::TenstorrentResetDevice::RESET_PCIE_LINK);

    if (reset_m3) {
        PCIDevice::reset_device_ioctl(pci_device_id_set, tt::umd::TenstorrentResetDevice::ASIC_DMC_RESET);
    } else {
        PCIDevice::reset_device_ioctl(pci_device_id_set, tt::umd::TenstorrentResetDevice::ASIC_RESET);
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

    PCIDevice::reset_device_ioctl(pci_device_id_set, tt::umd::TenstorrentResetDevice::POST_RESET);
}

void WarmReset::warm_reset_blackhole_legacy(std::vector<int> pci_device_ids) {
    std::unordered_set<int> pci_device_ids_set(pci_device_ids.begin(), pci_device_ids.end());
    PCIDevice::reset_device_ioctl(pci_device_ids_set, tt::umd::TenstorrentResetDevice::CONFIG_WRITE);

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
