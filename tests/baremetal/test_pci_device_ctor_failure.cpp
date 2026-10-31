// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "tt-umd/pcie/pci_device.hpp"

using namespace tt::umd;

namespace {

size_t open_fd_count() {
    size_t count = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        count++;
    }
    return count;
}

}  // namespace

// A PCIDevice whose constructor throws must leave no fd open and must name the failing device in the error.
TEST(PciDeviceCtorFailure, NonexistentDeviceThrowsAndLeaksNoFd) {
    constexpr int missing_device_number = 99999;
    const size_t fds_before = open_fd_count();

    try {
        PCIDevice device(missing_device_number);
        FAIL() << "Expected PCIDevice construction to throw";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("99999"), std::string::npos) << e.what();
    }

    EXPECT_EQ(open_fd_count(), fds_before);
}
