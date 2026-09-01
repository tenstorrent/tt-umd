// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "tests/test_utils/protocol_mocks.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/firmware/wormhole_arc_window.hpp"
#include "umd/device/utils/error.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::NiceMock;

namespace {

class WormholeArcApbWindowTest : public ::testing::Test {
protected:
    WormholeArcWindow over_pcie() { return WormholeArcWindow::arc_apb(&protocol_, &pcie_, nullptr, nullptr); }

    WormholeArcWindow over_jtag() { return WormholeArcWindow::arc_apb(&protocol_, nullptr, &jtag_, nullptr); }

    WormholeArcWindow over_remote() { return WormholeArcWindow::arc_apb(&protocol_, nullptr, nullptr, &remote_); }

    NiceMock<MockDeviceProtocol> protocol_;
    NiceMock<MockPcieInterface> pcie_;
    NiceMock<MockJtagInterface> jtag_;
    NiceMock<MockRemoteInterface> remote_;
};

// Which interface is present is how the window picks its route, so it cannot be built with an
// ambiguous set. Giving none leaves no route at all; giving two makes the null checks meaningless.
TEST_F(WormholeArcApbWindowTest, RequiresExactlyOneTransport) {
    EXPECT_THROW(WormholeArcWindow::arc_apb(&protocol_, nullptr, nullptr, nullptr), std::exception);
    EXPECT_THROW(WormholeArcWindow::arc_apb(&protocol_, &pcie_, &jtag_, nullptr), std::exception);
    EXPECT_THROW(WormholeArcWindow::arc_apb(&protocol_, &pcie_, nullptr, &remote_), std::exception);
    EXPECT_THROW(WormholeArcWindow::arc_apb(nullptr, &pcie_, nullptr, nullptr), std::exception);
}

}  // namespace
