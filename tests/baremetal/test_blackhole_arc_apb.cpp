// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tests/test_utils/protocol_mocks.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/firmware/blackhole_arc_apb.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

// The PCIe tile's x-coordinate decides whether the ARC tile is reachable over AXI: 11 means it is,
// anything else means the access has to go over the NOC.
constexpr uint32_t PCIE_X_ARC_OVER_AXI = 11;
constexpr uint32_t PCIE_X_ARC_OVER_NOC = 2;

constexpr uint64_t APB_OFFSET = 0x100;
const tt_xy_pair ARC_CORE = {8, 0};

class BlackholeArcApbTest : public ::testing::Test {
protected:
    void expect_pcie_x_coordinate(uint32_t x) {
        EXPECT_CALL(pcie_, bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + blackhole::NOC_NODE_ID_OFFSET))
            .WillRepeatedly(Return(x));
    }

    uint64_t noc_address() const { return blackhole::ARC_NOC_XBAR_ADDRESS_START + APB_OFFSET; }

    uint32_t bar_address() const { return blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + APB_OFFSET; }

    NiceMock<MockDeviceProtocol> protocol_;
    NiceMock<MockPcieInterface> pcie_;
    NiceMock<MockJtagInterface> jtag_;
};

// A device opened over JTAG has no PcieInterface, so the access is routed over the NOC through the
// device protocol, one word at a time regardless of the requested size.
TEST_F(BlackholeArcApbTest, ReadOverJtagGoesThroughDeviceProtocol) {
    BlackholeArcApb apb(&protocol_, /*pcie_interface=*/nullptr, &jtag_);

    EXPECT_CALL(
        protocol_,
        read_ctrl(_, ARC_CORE, blackhole::ARC_NOC_XBAR_ADDRESS_START + APB_OFFSET, sizeof(uint32_t), NocId::NOC1));

    uint32_t value = 0;
    apb.read(&value, APB_OFFSET, sizeof(uint64_t), ARC_CORE, NocId::NOC1);
}

TEST_F(BlackholeArcApbTest, WriteOverJtagGoesThroughDeviceProtocol) {
    BlackholeArcApb apb(&protocol_, /*pcie_interface=*/nullptr, &jtag_);

    EXPECT_CALL(
        protocol_,
        write_ctrl(_, ARC_CORE, blackhole::ARC_NOC_XBAR_ADDRESS_START + APB_OFFSET, sizeof(uint32_t), NocId::NOC0));

    uint32_t value = 0xABCD;
    apb.write(&value, APB_OFFSET, sizeof(uint64_t), ARC_CORE, NocId::NOC0);
}

// Over PCIe with the ARC tile unreachable over AXI, the access goes over the NOC and keeps the
// caller's size.
TEST_F(BlackholeArcApbTest, ReadFallsBackToNocWhenArcNotAvailableOverAxi) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_NOC);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    EXPECT_CALL(protocol_, read_ctrl(_, ARC_CORE, noc_address(), sizeof(uint64_t), NocId::NOC0));

    uint64_t value = 0;
    apb.read(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
}

TEST_F(BlackholeArcApbTest, WriteFallsBackToNocWhenArcNotAvailableOverAxi) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_NOC);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    EXPECT_CALL(protocol_, write_ctrl(_, ARC_CORE, noc_address(), sizeof(uint64_t), NocId::NOC0));

    uint64_t value = 0x1234;
    apb.write(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
}

// Over PCIe with the ARC tile reachable over AXI, the access is a plain BAR access and never
// touches the device protocol.
TEST_F(BlackholeArcApbTest, ReadUsesBarWhenArcAvailableOverAxi) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_AXI);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    EXPECT_CALL(pcie_, bar_read32(bar_address())).WillOnce(Return(0xDEADBEEF));
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _)).Times(0);

    uint32_t value = 0;
    apb.read(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
    EXPECT_EQ(value, 0xDEADBEEF);
}

TEST_F(BlackholeArcApbTest, WriteUsesBarWhenArcAvailableOverAxi) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_AXI);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    EXPECT_CALL(pcie_, bar_write32(bar_address(), 0xFEEDFACE));
    EXPECT_CALL(protocol_, write_ctrl(_, _, _, _, _)).Times(0);

    uint32_t value = 0xFEEDFACE;
    apb.write(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
}

// The ARC core coordinate is supplied per call, so the same object serves both NOCs.
TEST_F(BlackholeArcApbTest, RoutesToTheCoordinateAndNocGivenPerCall) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_NOC);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    const tt_xy_pair arc_core_noc1 = {8, 11};
    EXPECT_CALL(protocol_, read_ctrl(_, ARC_CORE, noc_address(), sizeof(uint32_t), NocId::NOC0));
    EXPECT_CALL(protocol_, read_ctrl(_, arc_core_noc1, noc_address(), sizeof(uint32_t), NocId::NOC1));

    uint32_t value = 0;
    apb.read(&value, APB_OFFSET, sizeof(value), ARC_CORE, NocId::NOC0);
    apb.read(&value, APB_OFFSET, sizeof(value), arc_core_noc1, NocId::NOC1);
}

TEST_F(BlackholeArcApbTest, RejectsOffsetOutsideTheXbarRange) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    const uint64_t out_of_range = static_cast<uint64_t>(blackhole::ARC_XBAR_ADDRESS_END) + 1;
    uint32_t value = 0;

    EXPECT_THROW(apb.read(&value, out_of_range, sizeof(value), ARC_CORE, NocId::NOC0), std::exception);
    EXPECT_THROW(apb.write(&value, out_of_range, sizeof(value), ARC_CORE, NocId::NOC0), std::exception);
}

// Which interface is present is how the route is picked, so neither zero nor two transports can be
// routed. Rejecting them at construction is what makes the PCIe fallthrough safe to dereference.
TEST_F(BlackholeArcApbTest, RejectsAnythingOtherThanExactlyOneTransport) {
    EXPECT_THROW(BlackholeArcApb(&protocol_, /*pcie_interface=*/nullptr, /*jtag_interface=*/nullptr), std::exception);
    EXPECT_THROW(BlackholeArcApb(&protocol_, &pcie_, &jtag_), std::exception);
}

TEST_F(BlackholeArcApbTest, RejectsAMissingDeviceProtocol) {
    EXPECT_THROW(BlackholeArcApb(/*device_protocol=*/nullptr, &pcie_, /*jtag_interface=*/nullptr), std::exception);
}

}  // namespace
