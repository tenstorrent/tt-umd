/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class WarmReset {
public:
    static void warm_reset(std::vector<int> pci_device_ids = {}, bool reset_m3 = false);

    static void ubb_warm_reset(const std::chrono::milliseconds timeout_ms = timeout::UBB_WARM_RESET_TIMEOUT);

private:
    static constexpr auto POST_RESET_WAIT = std::chrono::milliseconds(2'000);
    static constexpr auto UBB_POST_RESET_WAIT = std::chrono::milliseconds(30'000);

    static void warm_reset_blackhole_legacy(std::vector<int> pci_device_ids);

    static void warm_reset_wormhole_legacy(std::vector<int> pci_device_ids, bool reset_m3);

    static void warm_reset_arch_agnostic(
        std::vector<int> pci_device_ids,
        bool reset_m3,
        std::chrono::milliseconds reset_m3_timeout = timeout::WARM_RESET_M3_TIMEOUT);

    static void wormhole_ubb_ipmi_reset(int ubb_num, int dev_num, int op_mode, int reset_time);

    static void ubb_wait_for_driver_load(const std::chrono::milliseconds timeout_ms);
};

class WarmResetCommunication {
public:
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
        static std::vector<std::shared_ptr<asio::local::stream_protocol::socket>> get_connected_listeners(
            asio::io_context& io);
    };

private:
    static constexpr auto LISTENER_DIR = "/tmp/tt_umd_listeners";

    static int extract_pid_from_socket_name(const std::string& filename);
};

}  // namespace tt::umd
