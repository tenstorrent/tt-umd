// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "tt-umd/utils/timeouts.hpp"

namespace tt::umd {

// Outcome of a warm reset. Every reset device not named in missing_pci_bdfs came back and is usable.
struct WarmResetResult {
    // False whenever the reset did not fully complete, including failures that name no device.
    bool success = false;
    // Reset devices that did not come back, so the caller can escalate for these only.
    std::vector<std::string> missing_pci_bdfs;
};

class WarmReset {
public:
    static bool warm_reset(
        std::vector<int> pci_device_ids = {},
        bool reset_m3 = false,
        bool secondary_bus_reset = true,
        std::chrono::milliseconds m3_delay = timeout::WARM_RESET_M3_TIMEOUT);

    // Same as warm_reset(), and names each device that did not come back. A device that does not come back
    // does not keep the others from being brought back.
    static WarmResetResult warm_reset_with_result(
        std::vector<int> pci_device_ids = {},
        bool reset_m3 = false,
        bool secondary_bus_reset = true,
        std::chrono::milliseconds m3_delay = timeout::WARM_RESET_M3_TIMEOUT);

    // Removes each listed device that is still on the PCIe bus, rescans the bus and waits up to timeout_ms for
    // every listed device to come back. Meant for devices a warm reset did not bring back, including ones that
    // left the bus. Requires root. Returns the BDFs still missing; all of them when not permitted.
    static std::vector<std::string> rescan_pci_bdfs(
        const std::vector<std::string>& pci_bdfs,
        std::chrono::milliseconds timeout_ms = timeout::WARM_RESET_DEVICES_REAPPEAR_TIMEOUT);

    // Resets devices identified by their UMD chip IDs (indices into the list returned by
    // PCIDevice::enumerate_devices(), corresponding to /dev/tenstorrent/<N> device numbers).
    // These are equivalent to chip_id values from Cluster::get_target_mmio_device_ids().
    // This differs from warm_reset() which takes raw PCI device IDs directly.
    static bool warm_reset_chip_id(
        const std::vector<int>& chip_ids = {},
        bool reset_m3 = false,
        bool secondary_bus_reset = true,
        std::chrono::milliseconds m3_delay = timeout::WARM_RESET_M3_TIMEOUT);

    static bool warm_reset_pci_bdfs(
        const std::vector<std::string>& pci_bdfs = {},
        bool reset_m3 = false,
        bool secondary_bus_reset = true,
        std::chrono::milliseconds m3_delay = timeout::WARM_RESET_M3_TIMEOUT);

    static bool ubb_warm_reset(const std::chrono::milliseconds timeout_ms = timeout::UBB_WARM_RESET_TIMEOUT);

    // Power-cycles devices by removing them from the PCIe bus and triggering a rescan.
    // Requires CAP_SYS_ADMIN (root).  Returns false if unsupported or permission is denied.
    static bool warm_reset_sysfs(std::vector<int> pci_device_ids = {});

private:
    static constexpr auto POST_RESET_WAIT = std::chrono::milliseconds(2'000);
    static constexpr auto UBB_POST_RESET_WAIT = std::chrono::milliseconds(30'000);

    static bool warm_reset_blackhole_legacy(std::vector<int> pci_device_ids);

    static bool warm_reset_wormhole_legacy(std::vector<int> pci_device_ids, bool reset_m3);

    static WarmResetResult warm_reset_arch_agnostic(
        std::vector<int> pci_device_ids,
        bool reset_m3,
        std::chrono::milliseconds reset_m3_timeout = timeout::WARM_RESET_M3_TIMEOUT,
        bool secondary_bus_reset = true);

    static bool galaxy_ubb_ipmi_reset(int ubb_num, int dev_num, int op_mode, int reset_time);
};

/**
 * Handles the Inter-Process Communication (IPC) for Warm Reset synchronization.
 *
 * This system uses Unix Domain Sockets to coordinate a "Reset" event across multiple
 * independent processes attached to the cluster.
 *
 * Architecture:
 * - The scope is currently system-wide (Cluster level), not per-device.
 * - Notifier (Writer): The process performing the reset notification.
 * It scans the listener directory and sends a notification to all connected sockets.
 * - Monitor (Listener): Any process that needs to prepare for a reset.
 * It creates a named socket in the listener directory and waits for notifications.
 */

class WarmResetCommunication {
public:
    enum class MessageType : uint8_t { PreReset = 0x01, PostReset = 0x02 };

    static constexpr MessageType PRE_RESET = MessageType::PreReset;
    static constexpr MessageType POST_RESET = MessageType::PostReset;
    static constexpr auto LISTENER_DIR = "/tmp/tt_umd_listeners";

    struct Monitor {
        static bool start_monitoring(
            std::function<void()>&& on_cleanup_request, std::function<void()>&& post_cleanup_request);

        static void stop_monitoring();
    };

    struct Notifier {
    public:
        static void notify_all_listeners_pre_reset(std::chrono::milliseconds timeout_ms);
        static void notify_all_listeners_post_reset();

    private:
        static void notify_all_listeners(MessageType msg_type, std::optional<std::chrono::milliseconds> timeout_ms);
    };
};

}  // namespace tt::umd
