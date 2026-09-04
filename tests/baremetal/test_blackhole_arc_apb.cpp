// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>

#include "tests/test_utils/protocol_mocks.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/firmware/blackhole_arc_apb.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

// Blackhole has two PCIe tiles and only one of them is wired to ARC over AXI, so which tile the
// board brought up decides whether an ARC access can bypass the NOC. The tile identifies itself by
// its x-coordinate: blackhole::PCIE_CORES_TYPE1_NOC0 at x=11 has the direct AXI path, while
// blackhole::PCIE_CORES_TYPE2_NOC0 at x=2 does not and has to go over the NOC.
constexpr uint32_t PCIE_X_ARC_OVER_AXI = 11;
constexpr uint32_t PCIE_X_ARC_OVER_NOC = 2;

constexpr uint64_t BLACKHOLE_APB_OFFSET = 0x100;

// Every routing case is run against both NOCs. BlackholeArcApb does not branch on the NOC today --
// it forwards whatever it is given -- so this is not covering a branch so much as pinning that
// contract: WormholeTTDevice::read_from_arc_apb pinned its JTAG path to ARC_CORES_NOC0[0] and
// DEFAULT_NOC, and reintroducing that here should fail a test rather than need to be spotted in
// review.
class BlackholeArcApbTest : public ::testing::TestWithParam<NocId> {
protected:
    NocId noc() const { return GetParam(); }

    // The ARC core is not at the same coordinate on both NOCs, so it is derived rather than written
    // out: passing a NOC0 coordinate together with NocId::NOC1 is a pairing no caller produces.
    // Translation is off because that is the case where the two NOCs actually differ.
    tt_xy_pair arc_core() const {
        return blackhole::get_arc_core(/*noc_translation_enabled=*/false, /*use_noc1=*/noc() == NocId::NOC1);
    }

    void expect_pcie_x_coordinate(uint32_t x) {
        EXPECT_CALL(pcie_, bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + blackhole::NOC_NODE_ID_OFFSET))
            .WillRepeatedly(Return(x));
    }

    uint64_t noc_address() const { return blackhole::ARC_NOC_XBAR_ADDRESS_START + BLACKHOLE_APB_OFFSET; }

    uint32_t bar_address() const { return blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + BLACKHOLE_APB_OFFSET; }

    NiceMock<MockDeviceProtocol> protocol_;
    NiceMock<MockPcieInterface> pcie_;
    NiceMock<MockJtagInterface> jtag_;
};

INSTANTIATE_TEST_SUITE_P(
    BothNocs,
    BlackholeArcApbTest,
    ::testing::Values(NocId::NOC0, NocId::NOC1),
    [](const ::testing::TestParamInfo<NocId>& info) { return noc_to_str(info.param); });

// A device opened over JTAG has no PcieInterface, so the access is routed over the NOC through the
// device protocol, one word at a time regardless of the requested size.
TEST_P(BlackholeArcApbTest, ReadOverJtagGoesThroughDeviceProtocol) {
    BlackholeArcApb apb(&protocol_, /*pcie_interface=*/nullptr, &jtag_);

    EXPECT_CALL(protocol_, read_ctrl(_, arc_core(), noc_address(), sizeof(uint32_t), noc()));

    uint32_t value = 0;
    apb.read(&value, BLACKHOLE_APB_OFFSET, sizeof(uint64_t), arc_core(), noc());
}

TEST_P(BlackholeArcApbTest, WriteOverJtagGoesThroughDeviceProtocol) {
    BlackholeArcApb apb(&protocol_, /*pcie_interface=*/nullptr, &jtag_);

    EXPECT_CALL(protocol_, write_ctrl(_, arc_core(), noc_address(), sizeof(uint32_t), noc()));

    uint32_t value = 0xABCD;
    apb.write(&value, BLACKHOLE_APB_OFFSET, sizeof(uint64_t), arc_core(), noc());
}

// Over PCIe with the ARC tile unreachable over AXI, the access goes over the NOC and keeps the
// caller's size.
TEST_P(BlackholeArcApbTest, ReadFallsBackToNocWhenArcNotAvailableOverAxi) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_NOC);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    EXPECT_CALL(protocol_, read_ctrl(_, arc_core(), noc_address(), sizeof(uint64_t), noc()));

    uint64_t value = 0;
    apb.read(&value, BLACKHOLE_APB_OFFSET, sizeof(value), arc_core(), noc());
}

TEST_P(BlackholeArcApbTest, WriteFallsBackToNocWhenArcNotAvailableOverAxi) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_NOC);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    EXPECT_CALL(protocol_, write_ctrl(_, arc_core(), noc_address(), sizeof(uint64_t), noc()));

    uint64_t value = 0x1234;
    apb.write(&value, BLACKHOLE_APB_OFFSET, sizeof(value), arc_core(), noc());
}

// Over PCIe with the ARC tile reachable over AXI, the access is a plain BAR access and never
// touches the device protocol -- on either NOC, since the BAR path has no NOC to route over.
TEST_P(BlackholeArcApbTest, ReadUsesBarWhenArcAvailableOverAxi) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_AXI);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    EXPECT_CALL(pcie_, bar_read32(bar_address())).WillOnce(Return(0xDEADBEEF));
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _)).Times(0);

    uint32_t value = 0;
    apb.read(&value, BLACKHOLE_APB_OFFSET, sizeof(value), arc_core(), noc());
    EXPECT_EQ(value, 0xDEADBEEF);
}

TEST_P(BlackholeArcApbTest, WriteUsesBarWhenArcAvailableOverAxi) {
    expect_pcie_x_coordinate(PCIE_X_ARC_OVER_AXI);
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    EXPECT_CALL(pcie_, bar_write32(bar_address(), 0xFEEDFACE));
    EXPECT_CALL(protocol_, write_ctrl(_, _, _, _, _)).Times(0);

    uint32_t value = 0xFEEDFACE;
    apb.write(&value, BLACKHOLE_APB_OFFSET, sizeof(value), arc_core(), noc());
}

TEST_P(BlackholeArcApbTest, RejectsOffsetOutsideTheXbarRange) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    const uint64_t out_of_range = static_cast<uint64_t>(blackhole::ARC_XBAR_ADDRESS_END) + 1;
    uint32_t value = 0;

    EXPECT_THROW(apb.read(&value, out_of_range, sizeof(value), arc_core(), noc()), std::exception);
    EXPECT_THROW(apb.write(&value, out_of_range, sizeof(value), arc_core(), noc()), std::exception);
}

// The two NOCs really do target different cores, so the parametrization above is not running the
// same case twice.
TEST(BlackholeArcApbCoordinateTest, TheArcCoreDiffersBetweenNocs) {
    EXPECT_NE(
        blackhole::get_arc_core(/*noc_translation_enabled=*/false, /*use_noc1=*/false),
        blackhole::get_arc_core(/*noc_translation_enabled=*/false, /*use_noc1=*/true));
}

class BlackholeArcApbConstructionTest : public ::testing::Test {
protected:
    NiceMock<MockDeviceProtocol> protocol_;
    NiceMock<MockPcieInterface> pcie_;
    NiceMock<MockJtagInterface> jtag_;
};

// Which interface is present is how the route is picked, so neither zero nor two transports can be
// routed. Rejecting them at construction is what makes the PCIe fallthrough safe to dereference.
TEST_F(BlackholeArcApbConstructionTest, RejectsAnythingOtherThanExactlyOneTransport) {
    EXPECT_THROW(BlackholeArcApb(&protocol_, /*pcie_interface=*/nullptr, /*jtag_interface=*/nullptr), std::exception);
    EXPECT_THROW(BlackholeArcApb(&protocol_, &pcie_, &jtag_), std::exception);
}

TEST_F(BlackholeArcApbConstructionTest, RejectsAMissingDeviceProtocol) {
    EXPECT_THROW(BlackholeArcApb(/*device_protocol=*/nullptr, &pcie_, /*jtag_interface=*/nullptr), std::exception);
}

}  // namespace
