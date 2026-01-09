// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "umd/device/warm_reset.hpp"

using namespace tt;
using namespace tt::umd;

/**
 * NOTE: The notification mechanism demonstrated below (PRE_RESET -> Reset -> POST_RESET)
 * is currently a Work-In-Progress feature.
 *
 * In the final implementation, these notification calls will be automatically incorporated
 * into WarmReset::warm_reset() itself.
 *
 * This standalone example is provided explicitly to help users understand the underlying
 * coordination flow and how the notification mechanics work before they are fully abstracted.
 */

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [timeout_seconds]\n"
              << "  timeout_seconds: Time to wait for clients to cleanup (default: 2)\n";
}

int main(int argc, char* argv[]) {
    int timeout_sec = 2;  // Default timeout

    if (argc > 1) {
        try {
            timeout_sec = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid timeout argument.\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (timeout_sec < 0) {
        std::cerr << "Timeout cannot be negative.\n";
        return 1;
    }

    std::cout << "=== Warm Reset with Notification example ===\n";
    std::cout << "Timeout set to: " << timeout_sec << " seconds.\n";

    std::cout << "[Notifier] Sending PRE_RESET to all connected listeners..." << std::endl;

    WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::seconds(timeout_sec));

    WarmReset::warm_reset();  // for 6U galaxy use WarmReset::ubb_warm_reset();

    std::cout << "[Notifier] Sending POST_RESET to wake up listeners..." << std::endl;

    WarmResetCommunication::Notifier::notify_all_listeners_post_reset();

    return 0;
}
