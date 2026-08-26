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
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

constexpr uint64_t APB_OFFSET = 0x100;
const tt_xy_pair ARC_CORE = {0, 10};

class WormholeArcApbWindowTest : public ::testing::Test {
protected:
    uint64_t noc_address() const { return wormhole::ARC_APB_NOC_BASE_ADDRESS + APB_OFFSET; }

    uint32_t bar_address() const { return wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + APB_OFFSET; }

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

// A remote device is reached over the NOC, and the APB window holds registers, so the access has to
// go through the protocol's register path and keep the caller's size.
TEST_F(WormholeArcApbWindowTest, RemoteReadIsARegisterAccessKeepingSize) {
    auto window = over_remote();

    EXPECT_CALL(protocol_, read_ctrl(_, ARC_CORE, noc_address(), 16, NocId::NOC0));
    EXPECT_CALL(protocol_, read_data(_, _, _, _, _)).Times(0);

    std::array<uint32_t, 4> values{};
    window.read(values.data(), APB_OFFSET, sizeof(values), ARC_CORE, NocId::NOC0);
}

TEST_F(WormholeArcApbWindowTest, RemoteWriteIsARegisterAccessKeepingSize) {
    auto window = over_remote();

    EXPECT_CALL(protocol_, write_ctrl(_, ARC_CORE, noc_address(), 16, NocId::NOC1));
    EXPECT_CALL(protocol_, write_data(_, _, _, _, _)).Times(0);

    std::array<uint32_t, 4> values{};
    window.write(values.data(), APB_OFFSET, sizeof(values), ARC_CORE, NocId::NOC1);
}

// A device opened over JTAG has no PcieInterface, so the access goes over the NOC, one word at a
// time. WormholeTTDevice pinned this branch to NOC0; the window uses what the caller passed.
TEST_F(WormholeArcApbWindowTest, JtagReadUsesTheCallersCoreAndNoc) {
    auto window = over_jtag();

    EXPECT_CALL(protocol_, read_ctrl(_, ARC_CORE, noc_address(), sizeof(uint32_t), NocId::NOC1));

    uint32_t value = 0;
    window.read(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC1);
}

TEST_F(WormholeArcApbWindowTest, JtagWriteUsesTheCallersCoreAndNoc) {
    auto window = over_jtag();

    EXPECT_CALL(protocol_, write_ctrl(_, ARC_CORE, noc_address(), sizeof(uint32_t), NocId::NOC1));

    uint32_t value = 0xABCD;
    window.write(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC1);
}

// A local PCIe device goes through the BAR, which never touches the NOC.
TEST_F(WormholeArcApbWindowTest, PcieReadGoesThroughTheBar) {
    auto window = over_pcie();

    EXPECT_CALL(pcie_, bar_read32(bar_address())).WillOnce(Return(0xDEADBEEF));
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _)).Times(0);

    uint32_t value = 0;
    window.read(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
    EXPECT_EQ(value, 0xDEADBEEF);
}

TEST_F(WormholeArcApbWindowTest, PcieWriteGoesThroughTheBar) {
    auto window = over_pcie();

    EXPECT_CALL(pcie_, bar_write32(bar_address(), 0xABCD1234));
    EXPECT_CALL(protocol_, write_ctrl(_, _, _, _, _)).Times(0);

    uint32_t value = 0xABCD1234;
    window.write(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
}

// The JTAG and BAR routes always move one word, so a size they cannot honor is rejected instead of
// overrunning or short-changing the caller's buffer.
TEST_F(WormholeArcApbWindowTest, NonWordSizeIsRejectedOnTheWordSizedRoutes) {
    uint32_t value = 0;

    auto pcie_window = over_pcie();
    EXPECT_THROW(pcie_window.read(&value, APB_OFFSET, 2, ARC_CORE, NocId::NOC0), std::exception);
    EXPECT_THROW(pcie_window.write(&value, APB_OFFSET, 8, ARC_CORE, NocId::NOC0), std::exception);

    auto jtag_window = over_jtag();
    EXPECT_THROW(jtag_window.read(&value, APB_OFFSET, 2, ARC_CORE, NocId::NOC0), std::exception);
    EXPECT_THROW(jtag_window.write(&value, APB_OFFSET, 8, ARC_CORE, NocId::NOC0), std::exception);
}

// The window bound covers the whole transfer, not just its first byte: a word access starting at
// the window size, or four bytes before its end plus one, runs past the end.
TEST_F(WormholeArcApbWindowTest, AccessMustFitEntirelyInsideTheWindow) {
    auto window = over_remote();
    uint32_t value = 0;

    constexpr uint32_t RANGE = wormhole::ARC_APB_ADDRESS_RANGE;
    EXPECT_THROW(window.read(&value, RANGE, sizeof(value), ARC_CORE, NocId::NOC0), std::exception);
    EXPECT_THROW(window.read(&value, RANGE - sizeof(value) + 1, sizeof(value), ARC_CORE, NocId::NOC0), std::exception);
    EXPECT_THROW(window.read(&value, 0, static_cast<size_t>(RANGE) + 1, ARC_CORE, NocId::NOC0), std::exception);

    // The last word that fits starts exactly sizeof(uint32_t) before the end.
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _));
    window.read(&value, RANGE - sizeof(value), sizeof(value), ARC_CORE, NocId::NOC0);
}

TEST_F(WormholeArcApbWindowTest, ZeroLengthAccessIsRejected) {
    auto window = over_remote();
    uint32_t value = 0;

    EXPECT_THROW(window.read(&value, APB_OFFSET, 0, ARC_CORE, NocId::NOC0), std::exception);
    EXPECT_THROW(window.write(&value, APB_OFFSET, 0, ARC_CORE, NocId::NOC0), std::exception);
}

}  // namespace
