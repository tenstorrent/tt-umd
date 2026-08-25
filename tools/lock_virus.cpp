// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// lock_virus: report the state (free / held, owner PID/TID) of every lock UMD can take - each mutex
// type, for every PCIe and JTAG device found. Locks are probed through LockManager, so what gets
// reported is the lock UMD would actually take, whichever mutex happens to back it.

#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/lock_manager.hpp"

using namespace tt::umd;

// ── reporting ─────────────────────────────────────────────────────────────────

static void report_state(const std::string& mutex_name, const std::optional<std::pair<pid_t, pid_t>>& owner) {
    if (owner.has_value()) {
        log_info(tt::LogUMD, "    [{:<16}]  LOCKED  PID={} TID={}", mutex_name, owner->first, owner->second);
    } else {
        log_info(tt::LogUMD, "    [{:<16}]  FREE", mutex_name);
    }
}

static void report_device_locks(int device_id, IODeviceType device_type) {
    for (MutexType mutex_type : LockManager::CHIP_SPECIFIC_MUTEX_TYPES) {
        LockManager::initialize_mutex(mutex_type, device_id, device_type);
        report_state(to_string(mutex_type), LockManager::probe_mutex(mutex_type, device_id, device_type));
    }
}

// ── device enumeration ────────────────────────────────────────────────────────

// Returns the JTAG device indices, or an empty list if JTAG is not usable on this host. The J-Link
// library it needs is loaded at runtime and is absent on most systems.
static std::vector<int> enumerate_jtag_devices() {
    try {
        std::unique_ptr<JtagDevice> jtag_device = JtagDevice::create();
        std::vector<int> device_ids;
        device_ids.reserve(jtag_device->get_device_cnt());
        for (uint32_t index = 0; index < jtag_device->get_device_cnt(); index++) {
            device_ids.push_back(static_cast<int>(index));
        }
        return device_ids;
    } catch (const std::exception& e) {
        log_debug(tt::LogUMD, "JTAG devices could not be enumerated: {}", e.what());
        return {};
    }
}

// ── testing mode ──────────────────────────────────────────────────────────────

static std::optional<MutexType> find_mutex_type(const std::string& mutex_name) {
    for (MutexType mutex_type : LockManager::SYSTEM_WIDE_MUTEX_TYPES) {
        if (to_string(mutex_type) == mutex_name) {
            return mutex_type;
        }
    }
    for (MutexType mutex_type : LockManager::CHIP_SPECIFIC_MUTEX_TYPES) {
        if (to_string(mutex_type) == mutex_name) {
            return mutex_type;
        }
    }
    return std::nullopt;
}

static void spin_forever() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

static void hold_lock(MutexType mutex_type, std::optional<int> device_id, IODeviceType device_type) {
    if (device_id.has_value()) {
        LockManager::initialize_mutex(mutex_type, *device_id, device_type);
        auto lock = LockManager::acquire_mutex(mutex_type, *device_id, device_type);
        log_info(tt::LogUMD, "Holding lock — press Ctrl-C to release.");
        spin_forever();
    } else {
        LockManager::initialize_mutex(mutex_type);
        auto lock = LockManager::acquire_mutex(mutex_type);
        log_info(tt::LogUMD, "Holding lock — press Ctrl-C to release.");
        spin_forever();
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    cxxopts::Options options(
        "lock_virus",
        "Report the state of every lock UMD can take, for all PCIe and JTAG devices found.\n"
        "\n"
        "Testing mode: --hold-lock <MUTEX_TYPE> acquires that lock and spins forever, allowing\n"
        "lock_virus (run separately) to observe it as held. Without --device the system wide\n"
        "lock of that type is taken, with it the lock of the given device.");

    // clang-format off
    options.add_options()
        ("h,help",        "Print usage")
        ("hold-lock",     "Acquire the lock of the given mutex type and hold it indefinitely (for testing)",
                          cxxopts::value<std::string>())
        ("device",        "Device to take the lock of, when holding a chip specific lock",
                          cxxopts::value<int>())
        ("device-type",   "Device type to take the lock of: pcie or jtag",
                          cxxopts::value<std::string>()->default_value("pcie"));
    // clang-format on

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    try {
        const std::string device_type_name = result["device-type"].as<std::string>();
        if (device_type_name != "pcie" && device_type_name != "jtag") {
            log_error(tt::LogUMD, "Unknown device type '{}', expected pcie or jtag.", device_type_name);
            return 1;
        }
        const IODeviceType device_type = device_type_name == "jtag" ? IODeviceType::JTAG : IODeviceType::PCIe;

        // ── Testing mode: hold a single lock and spin ─────────────────────
        if (result.count("hold-lock")) {
            const std::string mutex_name = result["hold-lock"].as<std::string>();
            std::optional<MutexType> mutex_type = find_mutex_type(mutex_name);
            if (!mutex_type.has_value()) {
                log_error(tt::LogUMD, "Unknown mutex type '{}'.", mutex_name);
                return 1;
            }
            std::optional<int> device_id;
            if (result.count("device")) {
                device_id = result["device"].as<int>();
            }
            hold_lock(*mutex_type, device_id, device_type);
        }

        log_info(tt::LogUMD, "=== System wide locks ===");
        for (MutexType mutex_type : LockManager::SYSTEM_WIDE_MUTEX_TYPES) {
            LockManager::initialize_mutex(mutex_type);
            report_state(to_string(mutex_type), LockManager::probe_mutex(mutex_type));
        }

        std::vector<int> pcie_device_ids = PCIDevice::enumerate_devices();
        log_info(tt::LogUMD, "");
        log_info(tt::LogUMD, "=== PCIe devices found ({}) ===", pcie_device_ids.size());
        for (int device_id : pcie_device_ids) {
            log_info(tt::LogUMD, "  device {}", device_id);
            report_device_locks(device_id, IODeviceType::PCIe);
        }

        std::vector<int> jtag_device_ids = enumerate_jtag_devices();
        log_info(tt::LogUMD, "");
        log_info(tt::LogUMD, "=== JTAG devices found ({}) ===", jtag_device_ids.size());
        for (int device_id : jtag_device_ids) {
            log_info(tt::LogUMD, "  device {}", device_id);
            report_device_locks(device_id, IODeviceType::JTAG);
        }
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Error: {}", e.what());
        return 1;
    }

    return 0;
}
