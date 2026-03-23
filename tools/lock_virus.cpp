// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// lock_virus: inspect all UMD shared-memory locks present in /dev/shm, report
// their state (free / held, owner PID/TID), and cross-check against the locks
// expected for every enumerated PCIe device.

#include <dirent.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cxxopts.hpp>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/robust_mutex.hpp"

using namespace tt::umd;

// ── lock-name helpers ─────────────────────────────────────────────────────────

static std::vector<std::string> chip_specific_mutex_names(int device_id, const std::string& device_type = "PCIe") {
    std::vector<std::string> names;
    names.reserve(LockManager::CHIP_SPECIFIC_MUTEX_TYPES.size());
    for (MutexType type : LockManager::CHIP_SPECIFIC_MUTEX_TYPES) {
        std::string name = LockManager::MUTEX_TYPE_TO_STRING.at(type);
        name.append("_").append(std::to_string(device_id)).append("_").append(device_type);
        names.push_back(std::move(name));
    }
    return names;
}

// ── /dev/shm scan ─────────────────────────────────────────────────────────────

static std::vector<std::string> scan_umd_locks() {
    static constexpr std::string_view prefix = RobustMutex::SHM_FILE_PREFIX;

    std::vector<std::string> found;
    DIR* dir = opendir("/dev/shm");
    if (!dir) {
        log_warning(tt::LogUMD, "Could not open /dev/shm: {}", std::to_string(errno));
        return found;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.rfind(std::string(prefix), 0) == 0) {
            found.push_back(name);
        }
    }
    closedir(dir);
    std::sort(found.begin(), found.end());
    return found;
}

// ── reporting ─────────────────────────────────────────────────────────────────

static void report_lock(const std::string& shm_name) {
    // shm_name includes the TT_UMD_LOCK. prefix; RobustMutex wants the bare name.
    const std::string mutex_name = shm_name.substr(RobustMutex::SHM_FILE_PREFIX.size());
    RobustMutex m(mutex_name);
    try {
        m.initialize();
    } catch (const std::exception& e) {
        log_info(tt::LogUMD, "  [{:<55}]  ERROR initializing: {}", shm_name, e.what());
        return;
    }

    if (auto owner = m.probe_lock(std::chrono::seconds(0))) {
        // Optional has a value: lock is held by another thread/process.
        log_info(tt::LogUMD, "  [{:<55}]  LOCKED  PID={} TID={}", shm_name, owner->first, owner->second);
    } else {
        // nullopt: we acquired the lock — it was free.
        m.unlock();
        log_info(tt::LogUMD, "  [{:<55}]  FREE", shm_name);
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    cxxopts::Options options(
        "lock_virus",
        "Inspect all UMD shared-memory locks in /dev/shm and report their state.\n"
        "Also enumerates PCIe devices and reports expected locks that are missing.\n"
        "\n"
        "Testing mode: --hold-lock <name> acquires the named mutex and spins forever,\n"
        "allowing lock_virus (run separately) to observe the lock as held.");

    // clang-format off
    options.add_options()
        ("h,help",      "Print usage")
        ("hold-lock",   "Acquire the named mutex and hold it indefinitely (for testing)",
                        cxxopts::value<std::string>());
    // clang-format on

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    // ── Testing mode: hold a single lock and spin ─────────────────────────
    if (result.count("hold-lock")) {
        const std::string mutex_name = result["hold-lock"].as<std::string>();
        RobustMutex m(mutex_name);
        m.initialize();
        if (auto owner = m.probe_lock()) {
            log_error(
                tt::LogUMD, "Lock '{}' is already held by PID={} TID={}", mutex_name, owner->first, owner->second);
            return 1;
        }
        log_info(tt::LogUMD, "Holding lock '{}' — press Ctrl-C to release.", mutex_name);
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    try {
        // ── 1. Scan /dev/shm for existing UMD locks ───────────────────────
        std::vector<std::string> found_locks = scan_umd_locks();

        log_info(tt::LogUMD, "=== UMD locks found in /dev/shm ({}) ===", found_locks.size());
        if (found_locks.empty()) {
            log_info(tt::LogUMD, "  (none)");
        } else {
            for (const auto& name : found_locks) {
                report_lock(name);
            }
        }

        // ── 2. Enumerate PCIe devices and compute expected lock names ─────
        std::vector<int> device_ids = PCIDevice::enumerate_devices();

        log_info(tt::LogUMD, "");
        log_info(tt::LogUMD, "=== PCIe devices found ({}) ===", device_ids.size());
        if (device_ids.empty()) {
            log_info(tt::LogUMD, "  (none)");
        } else {
            for (int id : device_ids) {
                log_info(tt::LogUMD, "  device {}", id);
            }
        }

        // Build the full set of expected lock names.
        std::set<std::string> found_set(found_locks.begin(), found_locks.end());
        std::vector<std::string> expected_names;
        expected_names.reserve(
            LockManager::SYSTEM_WIDE_MUTEX_TYPES.size() +
            device_ids.size() * LockManager::CHIP_SPECIFIC_MUTEX_TYPES.size());

        static const std::string prefix(RobustMutex::SHM_FILE_PREFIX);
        for (MutexType type : LockManager::SYSTEM_WIDE_MUTEX_TYPES) {
            expected_names.push_back(prefix + LockManager::MUTEX_TYPE_TO_STRING.at(type));
        }
        for (int id : device_ids) {
            for (const auto& name : chip_specific_mutex_names(id)) {
                expected_names.push_back(prefix + name);
            }
        }

        // ── 3. Report expected locks that are missing from /dev/shm ──────
        std::vector<std::string> missing;
        missing.reserve(expected_names.size());
        for (const auto& name : expected_names) {
            if (found_set.find(name) == found_set.end()) {
                missing.push_back(name);
            }
        }
        missing.shrink_to_fit();

        log_info(tt::LogUMD, "");
        log_info(tt::LogUMD, "=== Expected locks missing from /dev/shm ({}) ===", missing.size());
        if (missing.empty()) {
            log_info(tt::LogUMD, "  (none)");
        } else {
            for (const auto& name : missing) {
                log_info(tt::LogUMD, "  [MISSING]  {}", name);
            }
        }

    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Error: {}", e.what());
        return 1;
    }

    return 0;
}
