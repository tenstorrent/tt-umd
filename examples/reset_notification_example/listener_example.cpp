// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "umd/device/warm_reset.hpp"

using namespace tt;
using namespace tt::umd;

// Global flags shared between callbacks and loops.
std::atomic<bool> g_stop_requested{false};
std::atomic<bool> g_is_paused{false};

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [mode]\n"
              << "Modes:\n"
              << "  stop   - Client terminates immediately when PRE_RESET is received.\n"
              << "  pause  - Client goes dormant on PRE_RESET, resumes on POST_RESET, then exits.\n";
}

// Scenario 1: Work until PRE_RESET signal, then exit immediately.
void run_stop_mode_loop() {
    std::cout << "[Workload] Running... (Waiting for signal to STOP)\n";

    int counter = 0;
    while (!g_stop_requested) {
        if (counter++ % 10 == 0) {
            std::cout << "  [Workload] Processing..." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Workload] Stop signal received. Terminating loop.\n";
}

// Scenario 2: Work, pause during reset, resume work, then exit.
void run_pause_mode_loop() {
    std::cout << "[Workload] Running... (Will PAUSE on signal)\n";

    bool has_resumed = false;
    int post_reset_ticks = 0;
    int counter = 0;

    // Run until we have resumed and worked for 3 seconds.
    while (post_reset_ticks < 30) {
        // 1. If paused (Reset in progress), just sleep and wait.
        if (g_is_paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            has_resumed = true;  // Mark that we have entered the pause phase
            continue;
        }

        // 2. Active work
        if (counter++ % 10 == 0) {
            std::cout << "  [Workload] Processing..." << std::endl;
        }

        // 3. If we survived a reset, count down to exit
        if (has_resumed) {
            post_reset_ticks++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Workload] Survived reset and worked for 3s. Exiting.\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];
    std::function<void()> pre_cb;
    std::function<void()> post_cb;

    if (mode == "stop") {
        pre_cb = []() {
            std::cout << "[Callback] PRE_RESET! Stopping...\n";
            g_stop_requested = true;
        };
        post_cb = []() { /* ignored */ };
    } else if (mode == "pause") {
        pre_cb = []() {
            std::cout << "[Callback] PRE_RESET! Pausing...\n";
            g_is_paused = true;
        };
        post_cb = []() {
            std::cout << "[Callback] POST_RESET! Resuming...\n";
            g_is_paused = false;
        };
    } else {
        print_usage(argv[0]);
        return 1;
    }

    // Start Listener.
    if (!WarmResetCommunication::Monitor::start_monitoring(std::move(pre_cb), std::move(post_cb))) {
        std::cerr << "Failed to start monitoring.\n";
        return 1;
    }

    // Run the specific workload loop.
    if (mode == "stop") {
        run_stop_mode_loop();
    } else {
        run_pause_mode_loop();
    }

    WarmResetCommunication::Monitor::stop_monitoring();
    std::cout << "[Monitor] Exiting gracefully." << std::endl;

    return 0;
}
