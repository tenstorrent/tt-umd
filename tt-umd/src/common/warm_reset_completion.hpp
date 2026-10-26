// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace tt::umd {

using ResetDeviceCheck = std::function<bool(const std::string& bdf, std::chrono::milliseconds timeout)>;

inline std::chrono::milliseconds time_left(std::chrono::steady_clock::time_point deadline) {
    return std::max(
        std::chrono::milliseconds(0),
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
}

/**
 * Finish a warm reset of `devices` (PCI index -> BDF): wait for each device to reappear and for its reset
 * marker to clear, then send POST_RESET to every device that came back. Each check gets one deadline shared
 * by all devices, so a dead device costs the wait once and never keeps the others from being checked.
 * `post_reset` is never called with an empty set, since an empty set targets every device on the host.
 *
 * @return BDFs of the devices that did not come back and got no POST_RESET; empty when all came back.
 */
inline std::vector<std::string> complete_warm_reset(
    const std::map<int, std::string>& devices,
    std::chrono::milliseconds reappear_timeout,
    std::chrono::milliseconds marker_timeout,
    const ResetDeviceCheck& reappeared,
    const ResetDeviceCheck& marker_cleared,
    const std::function<void(const std::unordered_set<int>&)>& post_reset) {
    std::vector<std::string> failed_bdfs;
    std::map<int, std::string> reappeared_devices;
    const auto reappear_deadline = std::chrono::steady_clock::now() + reappear_timeout;
    for (const auto& [pci_id, bdf] : devices) {
        if (reappeared(bdf, time_left(reappear_deadline))) {
            reappeared_devices.emplace(pci_id, bdf);
        } else {
            failed_bdfs.push_back(bdf);
        }
    }

    std::unordered_set<int> recovered;
    const auto marker_deadline = std::chrono::steady_clock::now() + marker_timeout;
    for (const auto& [pci_id, bdf] : reappeared_devices) {
        if (marker_cleared(bdf, time_left(marker_deadline))) {
            recovered.insert(pci_id);
        } else {
            failed_bdfs.push_back(bdf);
        }
    }

    if (!recovered.empty()) {
        post_reset(recovered);
    }
    return failed_bdfs;
}

/**
 * Remove each device in `bdfs` that is still listed under `sysfs_root`, rescan the PCI bus and wait for each
 * device to reappear, on one deadline shared by all devices. Removing a device that is still listed makes the
 * rescan probe it afresh, which a device left in a bad state by a failed reset needs.
 *
 * @return BDFs of the devices that did not reappear; empty when all did.
 */
inline std::vector<std::string> remove_and_rescan_pci(
    const std::filesystem::path& sysfs_root,
    const std::vector<std::string>& bdfs,
    std::chrono::milliseconds timeout,
    const ResetDeviceCheck& reappeared) {
    const auto devices_dir = sysfs_root / "bus" / "pci" / "devices";
    for (const auto& bdf : bdfs) {
        if (const auto remove = devices_dir / bdf / "remove"; std::filesystem::exists(remove)) {
            std::ofstream(remove) << "1\n";
        }
    }
    std::ofstream(sysfs_root / "bus" / "pci" / "rescan") << "1\n";

    std::vector<std::string> missing_bdfs;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (const auto& bdf : bdfs) {
        if (!reappeared(bdf, time_left(deadline))) {
            missing_bdfs.push_back(bdf);
        }
    }
    return missing_bdfs;
}

}  // namespace tt::umd
