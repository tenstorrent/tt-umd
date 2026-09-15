// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// lock_virus: report the state (free / held, owner PID/TID) of every lock UMD can take - each mutex
// type, for every PCIe and JTAG device found. Locks are probed through LockManager, so what gets
// reported is the lock UMD would actually take, whichever mutex happens to back it.
//
// What this is for: finding out who is holding a lock when something is stuck, in particular when the
// holder is a process nobody knows about any more. That is also why it probes every lock rather than
// only the ones that already exist - a lock that was never taken is worth reporting as free.
//
// Note that probing a lock initializes it, and initializing a shared memory backed lock creates its
// /dev/shm file. So a run leaves the backing files of every lock it reported behind. They are empty
// locks, harmless to UMD, but it does mean "which lock files exist" says nothing after the first run.

#include <chrono>
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

namespace {

constexpr std::string_view DEVICE_TYPE_PCIE = "pcie";
constexpr std::string_view DEVICE_TYPE_JTAG = "jtag";

}  // namespace

// ── reporting ─────────────────────────────────────────────────────────────────

static void report_state(const std::string& mutex_name, const std::optional<std::pair<pid_t, pid_t>>& owner) {
    if (!owner.has_value()) {
        log_info(tt::LogUMD, "    [{:<16}]  FREE", mutex_name);
        return;
    }
    // A backend that cannot say who holds the lock reports {0, 0}, which is not a pid anyone has.
    if (owner->first == 0 && owner->second == 0) {
        log_info(tt::LogUMD, "    [{:<16}]  LOCKED  by an unknown holder", mutex_name);
        return;
    }
    log_info(tt::LogUMD, "    [{:<16}]  LOCKED  PID={} TID={}", mutex_name, owner->first, owner->second);
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
        const uint32_t device_count = jtag_device->get_device_cnt();
        std::vector<int> device_ids;
        device_ids.reserve(device_count);
        for (uint32_t index = 0; index < device_count; index++) {
            device_ids.push_back(static_cast<int>(index));
        }
        return device_ids;
    } catch (const std::exception& e) {
        log_debug(tt::LogUMD, "JTAG devices could not be enumerated: {}", e.what());
        return {};
    }
}

// ── testing mode ──────────────────────────────────────────────────────────────

static std::optional<MutexType> find_mutex_type(
    const std::vector<MutexType>& mutex_types, const std::string& mutex_name) {
    for (MutexType mutex_type : mutex_types) {
        if (to_string(mutex_type) == mutex_name) {
            return mutex_type;
        }
    }
    return std::nullopt;
}

static std::string join_mutex_type_names(const std::vector<MutexType>& mutex_types) {
    std::string names;
    for (MutexType mutex_type : mutex_types) {
        if (!names.empty()) {
            names += ", ";
        }
        names += to_string(mutex_type);
    }
    return names;
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
        "Meant for finding out who holds a lock when something is stuck.\n"
        "\n"
        "Probing a lock initializes it, so a run creates the /dev/shm file of every shared memory\n"
        "backed lock it reports and leaves it behind. The files are harmless.\n"
        "\n"
        "Testing mode: --hold-lock <MUTEX_TYPE> acquires that lock and spins forever, allowing\n"
        "lock_virus (run separately) to observe it as held. Without --device a system wide lock\n"
        "is taken, with it the chip specific lock of the given device.");

    // clang-format off
    options.add_options()
        ("h,help",        "Print usage")
        ("hold-lock",     "Acquire the lock of the given mutex type and hold it indefinitely (for testing)",
                          cxxopts::value<std::string>())
        ("device",        "Device to take the lock of, when holding a chip specific lock",
                          cxxopts::value<int>())
        ("device-type",   "Device type to take the lock of, lowercase: pcie or jtag",
                          cxxopts::value<std::string>()->default_value("pcie"));
    // clang-format on

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    try {
        const std::string device_type_name = result["device-type"].as<std::string>();
        if (device_type_name != DEVICE_TYPE_PCIE && device_type_name != DEVICE_TYPE_JTAG) {
            log_error(
                tt::LogUMD,
                "Unknown device type '{}', expected {} or {}.",
                device_type_name,
                DEVICE_TYPE_PCIE,
                DEVICE_TYPE_JTAG);
            return 1;
        }
        const IODeviceType device_type = device_type_name == DEVICE_TYPE_JTAG ? IODeviceType::JTAG : IODeviceType::PCIe;

        // ── Testing mode: hold a single lock and spin ─────────────────────
        if (result.count("hold-lock")) {
            const std::string mutex_name = result["hold-lock"].as<std::string>();
            std::optional<int> device_id;
            if (result.count("device")) {
                device_id = result["device"].as<int>();
            }

            // A mutex type is either system wide or chip specific, and --device is what picks between
            // the two. Holding the wrong one would take a lock UMD never takes, and which this tool
            // would never report either, so the type has to exist in the form being asked for.
            const std::vector<MutexType>& holdable_types =
                device_id.has_value() ? LockManager::CHIP_SPECIFIC_MUTEX_TYPES : LockManager::SYSTEM_WIDE_MUTEX_TYPES;
            std::optional<MutexType> mutex_type = find_mutex_type(holdable_types, mutex_name);
            if (!mutex_type.has_value()) {
                log_error(
                    tt::LogUMD,
                    "'{}' is not a {} mutex type. Expected one of: {}",
                    mutex_name,
                    device_id.has_value() ? "chip specific" : "system wide",
                    join_mutex_type_names(holdable_types));
                return 1;
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
