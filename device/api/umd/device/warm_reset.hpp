// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class WarmReset {
public:
    static void warm_reset(
        std::vector<int> pci_device_ids = {}, bool reset_m3 = false, bool secondary_bus_reset = true);

    static void ubb_warm_reset(const std::chrono::milliseconds timeout_ms = timeout::UBB_WARM_RESET_TIMEOUT);

private:
    static constexpr auto POST_RESET_WAIT = std::chrono::milliseconds(2'000);
    static constexpr auto UBB_POST_RESET_WAIT = std::chrono::milliseconds(30'000);

    static void warm_reset_blackhole_legacy(std::vector<int> pci_device_ids);

    static void warm_reset_wormhole_legacy(std::vector<int> pci_device_ids, bool reset_m3);

    static void warm_reset_arch_agnostic(
        std::vector<int> pci_device_ids,
        bool reset_m3,
        std::chrono::milliseconds reset_m3_timeout = timeout::WARM_RESET_M3_TIMEOUT,
        bool secondary_bus_reset = true);

    static void wormhole_ubb_ipmi_reset(int ubb_num, int dev_num, int op_mode, int reset_time);

    static void ubb_wait_for_driver_load(const std::chrono::milliseconds timeout_ms);
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
            std::function<void()>&& pre_event_callback, std::function<void()>&& post_event_callback);

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
