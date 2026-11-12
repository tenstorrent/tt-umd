// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include "tests/test_utils/protocol_mocks.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt_device/firmware/blackhole_device_firmware.hpp"
#include "tt_device/firmware/wormhole_device_firmware.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

namespace {

// Serves one 32-bit register read.
auto serves_register(uint32_t value) {
    return Invoke([value](void* dst, tt_xy_pair, uint64_t, size_t, NocId, RegisterAccessWidth) {
        std::memcpy(dst, &value, sizeof(value));
    });
}

// The device is opened over JTAG so that every ARC register read on both architectures reaches the
// protocol's register path, where the counter words are served.
class RefclkCounterTest : public ::testing::TestWithParam<tt::ARCH> {
protected:
    RefclkCounterTest() : architecture_impl_(ArchitectureImplementation::create(GetParam())) {
        if (GetParam() == tt::ARCH::WORMHOLE_B0) {
            firmware_ = std::make_unique<WormholeDeviceFirmware>(
                &protocol_, nullptr, &jtag_, nullptr, architecture_impl_.get(), /*kmd_lock_available=*/false);
        } else {
            firmware_ = std::make_unique<BlackholeDeviceFirmware>(
                &protocol_, nullptr, &jtag_, architecture_impl_.get(), /*kmd_lock_available=*/false);
        }
    }

    uint64_t high_word_address() const {
        return GetParam() == tt::ARCH::WORMHOLE_B0
                   ? wormhole::ARC_APB_NOC_BASE_ADDRESS + wormhole::ARC_RESET_REFCLK_HIGH_OFFSET
                   : blackhole::ARC_NOC_XBAR_ADDRESS_START + blackhole::ARC_RESET_REFCLK_HIGH_OFFSET;
    }

    uint64_t low_word_address() const {
        return GetParam() == tt::ARCH::WORMHOLE_B0
                   ? wormhole::ARC_APB_NOC_BASE_ADDRESS + wormhole::ARC_RESET_REFCLK_LOW_OFFSET
                   : blackhole::ARC_NOC_XBAR_ADDRESS_START + blackhole::ARC_RESET_REFCLK_LOW_OFFSET;
    }

    NiceMock<MockDeviceProtocol> protocol_;
    NiceMock<MockJtagInterface> jtag_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    std::unique_ptr<DeviceFirmware> firmware_;
};

TEST_P(RefclkCounterTest, ReturnsBothWords) {
    EXPECT_CALL(protocol_, read_ctrl(_, _, high_word_address(), _, _, _)).WillRepeatedly(serves_register(0x00000012));
    EXPECT_CALL(protocol_, read_ctrl(_, _, low_word_address(), _, _, _)).WillOnce(serves_register(0x34567890));

    EXPECT_EQ(firmware_->get_refclk_counter(), 0x0000001234567890);
}

// The low word wraps between the reads, carrying into the high word: the stale low word must not be
// paired with the new high word.
TEST_P(RefclkCounterTest, PairsTheNewHighWordWithAFreshLowWordAfterACarry) {
    EXPECT_CALL(protocol_, read_ctrl(_, _, high_word_address(), _, _, _))
        .WillOnce(serves_register(0x00000012))
        .WillOnce(serves_register(0x00000013));
    EXPECT_CALL(protocol_, read_ctrl(_, _, low_word_address(), _, _, _))
        .WillOnce(serves_register(0xFFFFFFFF))
        .WillOnce(serves_register(0x00000005));

    EXPECT_EQ(firmware_->get_refclk_counter(), 0x0000001300000005);
}

INSTANTIATE_TEST_SUITE_P(
    Architectures,
    RefclkCounterTest,
    ::testing::Values(tt::ARCH::WORMHOLE_B0, tt::ARCH::BLACKHOLE),
    [](const ::testing::TestParamInfo<tt::ARCH>& info) { return tt::arch_to_str(info.param); });

}  // namespace
