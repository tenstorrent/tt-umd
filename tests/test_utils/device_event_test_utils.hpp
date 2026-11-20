// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>

// Shared by the device event tests, which a unity build may compile into one translation unit.
namespace tt::umd::test_utils {

// Firmware's TT_SMC_MSG_TEST_EVENT_RECORD publishes TEST records with fields the host chooses, so tests can
// drive the device event path without a real error source.
inline constexpr uint32_t TEST_EVENT_RECORD_MSG = 0x91;
inline constexpr uint16_t EVENT_TYPE_TEST = 0x0000;

// How long a read waits for a record the test has just published.
inline constexpr auto EVENT_READ_TIMEOUT = std::chrono::milliseconds(1000);
// Long enough that a read returning well within it was ended by cancel(), not by the timeout.
inline constexpr auto CANCELLED_READ_TIMEOUT = std::chrono::seconds(30);
inline constexpr auto CANCELLED_READ_BOUND = std::chrono::seconds(10);

}  // namespace tt::umd::test_utils
