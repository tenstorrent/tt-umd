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

// Arbitrary offset inside the window, not a hardware-defined location: the tests only check that
// an access at it is routed and bounded correctly.
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
    EXPECT_THROW(WormholeArcWindow::arc_apb(&protocol_, nullptr, nullptr, nullptr), error::UmdBaseException);
    EXPECT_THROW(WormholeArcWindow::arc_apb(&protocol_, &pcie_, &jtag_, nullptr), error::UmdBaseException);
    EXPECT_THROW(WormholeArcWindow::arc_apb(&protocol_, &pcie_, nullptr, &remote_), error::UmdBaseException);
    EXPECT_THROW(WormholeArcWindow::arc_apb(nullptr, &pcie_, nullptr, nullptr), error::UmdBaseException);
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
    EXPECT_THROW(pcie_window.read(&value, APB_OFFSET, 2, ARC_CORE, NocId::NOC0), error::UmdBaseException);
    EXPECT_THROW(pcie_window.write(&value, APB_OFFSET, 8, ARC_CORE, NocId::NOC0), error::UmdBaseException);

    auto jtag_window = over_jtag();
    EXPECT_THROW(jtag_window.read(&value, APB_OFFSET, 2, ARC_CORE, NocId::NOC0), error::UmdBaseException);
    EXPECT_THROW(jtag_window.write(&value, APB_OFFSET, 8, ARC_CORE, NocId::NOC0), error::UmdBaseException);
}

// The window bound covers the whole transfer, not just its first byte: a word access starting at
// the window size, or four bytes before its end plus one, runs past the end.
TEST_F(WormholeArcApbWindowTest, AccessMustFitEntirelyInsideTheWindow) {
    auto window = over_remote();
    uint32_t value = 0;

    // ARC_APB_ADDRESS_RANGE is END - START with an inclusive END, so the window is one byte larger
    // than it.
    constexpr uint64_t WINDOW_SIZE = static_cast<uint64_t>(wormhole::ARC_APB_ADDRESS_RANGE) + 1;

    // Starts inside the window but runs past its end.
    EXPECT_THROW(
        window.read(&value, WINDOW_SIZE - sizeof(value) + 1, sizeof(value), ARC_CORE, NocId::NOC0),
        error::UmdBaseException);
    // Starts at the first byte past the window.
    EXPECT_THROW(window.read(&value, WINDOW_SIZE, sizeof(value), ARC_CORE, NocId::NOC0), error::UmdBaseException);
    // Starts at zero but is larger than the whole window.
    EXPECT_THROW(window.read(&value, 0, WINDOW_SIZE + 1, ARC_CORE, NocId::NOC0), error::UmdBaseException);

    // The last word that fits ends exactly on the window's last byte.
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _));
    window.read(&value, WINDOW_SIZE - sizeof(value), sizeof(value), ARC_CORE, NocId::NOC0);
}

TEST_F(WormholeArcApbWindowTest, ZeroLengthAccessIsRejected) {
    auto window = over_remote();
    uint32_t value = 0;

    EXPECT_THROW(window.read(&value, APB_OFFSET, 0, ARC_CORE, NocId::NOC0), error::UmdBaseException);
    EXPECT_THROW(window.write(&value, APB_OFFSET, 0, ARC_CORE, NocId::NOC0), error::UmdBaseException);
}

// Arbitrary offset inside the window, not a hardware-defined location.
constexpr uint64_t CSM_OFFSET = 0x200;

class WormholeArcCsmWindowTest : public ::testing::Test {
protected:
    uint64_t noc_address() const { return wormhole::ARC_CSM_NOC_BASE_ADDRESS + CSM_OFFSET; }

    uint32_t bar_address() const { return wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START + CSM_OFFSET; }

    WormholeArcWindow over_pcie() { return WormholeArcWindow::arc_csm(&protocol_, &pcie_, nullptr, nullptr); }

    WormholeArcWindow over_jtag() { return WormholeArcWindow::arc_csm(&protocol_, nullptr, &jtag_, nullptr); }

    WormholeArcWindow over_remote() { return WormholeArcWindow::arc_csm(&protocol_, nullptr, nullptr, &remote_); }

    NiceMock<MockDeviceProtocol> protocol_;
    NiceMock<MockPcieInterface> pcie_;
    NiceMock<MockJtagInterface> jtag_;
    NiceMock<MockRemoteInterface> remote_;
};

// CSM holds memory rather than registers, so a remote access takes the data path where the APB
// window takes the register one. This is the only routing difference between the two.
TEST_F(WormholeArcCsmWindowTest, RemoteReadIsADataAccess) {
    auto window = over_remote();

    EXPECT_CALL(protocol_, read_data(_, ARC_CORE, noc_address(), 16, NocId::NOC0));
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _)).Times(0);

    std::array<uint32_t, 4> values{};
    window.read(values.data(), CSM_OFFSET, sizeof(values), ARC_CORE, NocId::NOC0);
}

TEST_F(WormholeArcCsmWindowTest, RemoteWriteIsADataAccess) {
    auto window = over_remote();

    EXPECT_CALL(protocol_, write_data(_, ARC_CORE, noc_address(), 16, NocId::NOC0));
    EXPECT_CALL(protocol_, write_ctrl(_, _, _, _, _)).Times(0);

    std::array<uint32_t, 4> values{};
    window.write(values.data(), CSM_OFFSET, sizeof(values), ARC_CORE, NocId::NOC0);
}

// The memory-vs-registers distinction is a remote-route one only: JTAG reaches every window over
// the register path, as the TTDevice-era CSM accessor this replaced did.
TEST_F(WormholeArcCsmWindowTest, JtagReadStaysOnTheRegisterPath) {
    auto window = over_jtag();

    EXPECT_CALL(protocol_, read_ctrl(_, ARC_CORE, noc_address(), sizeof(uint32_t), NocId::NOC1));
    EXPECT_CALL(protocol_, read_data(_, _, _, _, _)).Times(0);

    uint32_t value = 0;
    window.read(&value, CSM_OFFSET, sizeof(value), ARC_CORE, NocId::NOC1);
}

TEST_F(WormholeArcCsmWindowTest, PcieReadGoesThroughTheCsmBarOffset) {
    auto window = over_pcie();

    EXPECT_CALL(pcie_, bar_read32(bar_address())).WillOnce(Return(0x12345678));

    uint32_t value = 0;
    window.read(&value, CSM_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
    EXPECT_EQ(value, 0x12345678);
}

TEST_F(WormholeArcCsmWindowTest, PcieWriteGoesThroughTheCsmBarOffset) {
    auto window = over_pcie();

    EXPECT_CALL(pcie_, bar_write32(bar_address(), 0x87654321));

    uint32_t value = 0x87654321;
    window.write(&value, CSM_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
}

// The two windows are separate address ranges, so each is bounded by its own size.
TEST_F(WormholeArcCsmWindowTest, AccessIsBoundedByTheCsmWindow) {
    auto window = over_remote();
    uint32_t value = 0;

    constexpr uint64_t WINDOW_SIZE = static_cast<uint64_t>(wormhole::ARC_CSM_ADDRESS_RANGE) + 1;
    EXPECT_THROW(window.read(&value, WINDOW_SIZE, sizeof(value), ARC_CORE, NocId::NOC0), error::UmdBaseException);
    EXPECT_THROW(
        window.read(&value, WINDOW_SIZE - sizeof(value) + 1, sizeof(value), ARC_CORE, NocId::NOC0),
        error::UmdBaseException);

    EXPECT_CALL(protocol_, read_data(_, _, _, _, _));
    window.read(&value, WINDOW_SIZE - sizeof(value), sizeof(value), ARC_CORE, NocId::NOC0);
}

}  // namespace
