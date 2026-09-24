// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "tests/test_utils/protocol_mocks.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/protocol/kmd_scalar_noc_access.hpp"
#include "umd/device/tt_device/protocol/quasar_protocol.hpp"
#include "umd/device/tt_device/protocol/scalar_noc_access.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::NiceMock;

namespace {

constexpr int MMIO_ID = 0;

// Quasar reaches a target through the kernel's scalar access rather than a mapped window, so the
// protocol's job is to decompose a transfer into those accesses and to refuse a request the kernel
// would reject anyway.
class QuasarProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto access = std::make_unique<NiceMock<MockScalarNocAccess>>();
        access_ = access.get();
        protocol_ = std::make_unique<QuasarProtocol>(std::move(access), MMIO_ID);
    }

    NiceMock<MockScalarNocAccess>* access_ = nullptr;
    std::unique_ptr<QuasarProtocol> protocol_;
};

}  // namespace

// The address is already flat: the caller folded the target into it, so no coordinate travels with
// the access and a caller that still carries one is telling us something we cannot honour.
TEST_F(QuasarProtocolTest, RejectsACoordinateOtherThanTheOrigin) {
    uint32_t value = 0;

    EXPECT_THROW(protocol_->read_ctrl(&value, tt_xy_pair(1, 0), 0x1000, sizeof(value), NocId::NOC0), std::exception);
    EXPECT_THROW(protocol_->read_ctrl(&value, tt_xy_pair(0, 1), 0x1000, sizeof(value), NocId::NOC0), std::exception);
    EXPECT_THROW(protocol_->write_ctrl(&value, tt_xy_pair(1, 1), 0x1000, sizeof(value), NocId::NOC0), std::exception);
}

// There is one route to the target, so a request naming the second NOC cannot be served rather
// than being quietly served over the first.
TEST_F(QuasarProtocolTest, RejectsTheSecondNoc) {
    uint32_t value = 0;

    EXPECT_THROW(protocol_->read_ctrl(&value, tt_xy_pair(0, 0), 0x1000, sizeof(value), NocId::NOC1), std::exception);
}

TEST_F(QuasarProtocolTest, ReadsAWordAsOneScalarAccess) {
    EXPECT_CALL(*access_, read(0x1000, _, 4, 0));

    uint32_t value = 0;
    protocol_->read_ctrl(&value, tt_xy_pair(0, 0), 0x1000, sizeof(value), NocId::NOC0);
}

TEST_F(QuasarProtocolTest, WritesAWordAsOneScalarAccess) {
    EXPECT_CALL(*access_, write(0x1000, 0xdeadbeef, 4, 0));

    const uint32_t value = 0xdeadbeef;
    protocol_->write_ctrl(&value, tt_xy_pair(0, 0), 0x1000, sizeof(value), NocId::NOC0);
}

// Each access is a separate round trip to the device, so a block is that many round trips and the
// protocol must not pretend otherwise by silently truncating.
TEST_F(QuasarProtocolTest, DecomposesABlockIntoConsecutiveWordAccesses) {
    EXPECT_CALL(*access_, read(0x2000, _, 4, 0));
    EXPECT_CALL(*access_, read(0x2004, _, 4, 0));
    EXPECT_CALL(*access_, read(0x2008, _, 4, 0));

    std::vector<uint32_t> values(3, 0);
    protocol_->read_ctrl(values.data(), tt_xy_pair(0, 0), 0x2000, values.size() * sizeof(uint32_t), NocId::NOC0);
}

// The system NOC is a separate inbound aperture with its own translation, and it is the only way
// to reach the translation and firewall configuration blocks. Selecting it is what the driver's
// local-address flag means, so the two are the same choice spelled at different layers.
TEST_F(QuasarProtocolTest, SystemNocSelectsTheLocalAddressPath) {
    EXPECT_CALL(*access_, read(0x3000, _, 4, ScalarNocAccess::FLAG_LOCAL_ADDRESS));

    uint32_t value = 0;
    protocol_->read_ctrl(&value, tt_xy_pair(0, 0), 0x3000, sizeof(value), NocId::SYSTEM_NOC);
}

// A transfer the driver cannot serve must be refused here rather than silently rounded, because a
// short access at the wrong width reaches the target and returns the wrong bytes.
TEST_F(QuasarProtocolTest, RejectsATransferThatIsNotWordShaped) {
    uint32_t value = 0;

    EXPECT_THROW(protocol_->read_ctrl(&value, tt_xy_pair(0, 0), 0x1002, 4, NocId::NOC0), std::exception);
    EXPECT_THROW(protocol_->read_ctrl(&value, tt_xy_pair(0, 0), 0x1000, 3, NocId::NOC0), std::exception);
    EXPECT_THROW(protocol_->read_ctrl(&value, tt_xy_pair(0, 0), 0x1000, 0, NocId::NOC0), std::exception);
}

// There is no hardware multicast on this path. Returning false is the interface's way of telling
// the caller to unicast in software; claiming success would drop the write.
TEST_F(QuasarProtocolTest, ReportsThatItCannotMulticast) {
    const uint32_t value = 0;

    EXPECT_FALSE(
        protocol_->write_to_core_range(&value, tt_xy_pair(0, 0), tt_xy_pair(1, 1), 0x1000, sizeof(value), NocId::NOC0));
}

TEST_F(QuasarProtocolTest, ReportsItsMmioId) { EXPECT_EQ(protocol_->get_mmio_id(), MMIO_ID); }

// The device's driver handle is what performs the access, so an access path without a device has
// nothing to forward to and must say so at construction rather than on first use.
TEST(KmdScalarNocAccessTest, RequiresADevice) { EXPECT_THROW(KmdScalarNocAccess(nullptr), std::exception); }
