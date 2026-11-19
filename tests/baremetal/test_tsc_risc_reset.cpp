// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <optional>

#include "tests/test_utils/protocol_mocks.hpp"
#include "tt_device/reset/tsc_risc_reset.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

// Named, not anonymous: a unity build merges every anonymous namespace of its batch, and
// test_risc_reset.cpp has helpers of the same names.
namespace tsc_risc_reset_test {

constexpr tt_xy_pair TENSIX_CORE = {1, 0};
constexpr uint64_t TSC_RESET_CTRL = 0x0303'3088;
constexpr uint64_t OVERLAY_CORE_SOFT_RESET = 0x0300'0328;
constexpr uint64_t OVERLAY_UNCORE_SOFT_RESET = 0x0300'032C;

// Every core released, with the NOC (bit 26) and AI (bit 32) the testbench released at bring-up.
constexpr uint64_t ALL_RELEASED = 0x0000'00F1'0500'FF00;
// The same with every core and the DM uncore held.
constexpr uint64_t ALL_HELD = 0x0000'0001'0400'0000;

auto serves_register(uint64_t value) {
    return Invoke([value](void* dst, tt_xy_pair, uint64_t, size_t, NocId, RegisterAccessWidth) {
        std::memcpy(dst, &value, sizeof(value));
    });
}

auto captures_register(uint64_t* written) {
    return Invoke([written](const void* src, tt_xy_pair, uint64_t, size_t, NocId, RegisterAccessWidth) {
        std::memcpy(written, src, sizeof(*written));
    });
}

class TscRiscResetTest : public ::testing::Test {
protected:
    NiceMock<MockDeviceProtocol> protocol_;
    TscRiscReset risc_reset_{&protocol_, HORIZON_TSC_RESET};
};

struct AssertCase {
    RiscType riscs;
    uint64_t written;
};

class TscRiscResetAssert : public TscRiscResetTest, public ::testing::WithParamInterface<AssertCase> {};

TEST_P(TscRiscResetAssert, ClearsTheSelectedCoresBits) {
    uint64_t written = 0;
    EXPECT_CALL(
        protocol_,
        read_ctrl(_, TENSIX_CORE, TSC_RESET_CTRL, sizeof(uint64_t), NocId::NOC0, RegisterAccessWidth::BITS64))
        .WillOnce(serves_register(ALL_RELEASED));
    EXPECT_CALL(
        protocol_,
        write_ctrl(_, TENSIX_CORE, TSC_RESET_CTRL, sizeof(uint64_t), NocId::NOC0, RegisterAccessWidth::BITS64))
        .WillOnce(captures_register(&written));

    risc_reset_.assert_risc_reset(TENSIX_CORE, GetParam().riscs, NocId::NOC0);

    EXPECT_EQ(written, GetParam().written);
}

INSTANTIATE_TEST_SUITE_P(
    Horizon,
    TscRiscResetAssert,
    ::testing::Values(
        AssertCase{RiscType::DM0, 0x0000'00F1'0500'FE00},
        AssertCase{RiscType::ALL_NEO_DMS, 0x0000'00F1'0500'0000},
        AssertCase{RiscType::ALL_NEO0_TRISCS, 0x0000'0001'0500'FF00},
        AssertCase{RiscType::NEO_DM_UNCORE, 0x0000'00F1'0400'FF00},
        AssertCase{RiscType::ALL, ALL_HELD}));

// Releasing DM cores also releases the overlay's soft resets, which hold them otherwise.
TEST_F(TscRiscResetTest, DeassertingDmCoresReleasesTheOverlayToo) {
    uint64_t written = 0;
    EXPECT_CALL(protocol_, read_ctrl(_, _, TSC_RESET_CTRL, _, _, _)).WillOnce(serves_register(ALL_HELD));
    EXPECT_CALL(protocol_, write_ctrl(_, _, TSC_RESET_CTRL, sizeof(uint64_t), _, RegisterAccessWidth::BITS64))
        .WillOnce(captures_register(&written));
    EXPECT_CALL(
        protocol_, write_ctrl(_, _, OVERLAY_UNCORE_SOFT_RESET, sizeof(uint32_t), _, RegisterAccessWidth::BITS32))
        .WillOnce(Invoke([](const void* src, tt_xy_pair, uint64_t, size_t, NocId, RegisterAccessWidth) {
            EXPECT_EQ(*static_cast<const uint32_t*>(src), 0x1u);
        }));
    EXPECT_CALL(protocol_, write_ctrl(_, _, OVERLAY_CORE_SOFT_RESET, sizeof(uint32_t), _, RegisterAccessWidth::BITS32))
        .WillOnce(Invoke([](const void* src, tt_xy_pair, uint64_t, size_t, NocId, RegisterAccessWidth) {
            EXPECT_EQ(*static_cast<const uint32_t*>(src), 0xFFFu);
        }));

    risc_reset_.deassert_risc_reset(TENSIX_CORE, RiscType::ALL_NEO_DMS, /*staggered_start=*/false, NocId::NOC0);

    EXPECT_EQ(written, 0x0000'0001'0400'FF00);
}

TEST_F(TscRiscResetTest, DeassertingTriscsLeavesTheOverlayAlone) {
    uint64_t written = 0;
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _, _)).WillOnce(serves_register(ALL_HELD));
    EXPECT_CALL(protocol_, write_ctrl(_, _, _, _, _, _)).WillOnce(captures_register(&written));

    risc_reset_.deassert_risc_reset(TENSIX_CORE, RiscType::ALL_NEO0_TRISCS, /*staggered_start=*/false, NocId::NOC0);

    EXPECT_EQ(written, 0x0000'00F1'0400'0000);
}

TEST_F(TscRiscResetTest, ReportsTheHeldCores) {
    EXPECT_CALL(protocol_, read_ctrl(_, _, TSC_RESET_CTRL, sizeof(uint64_t), _, RegisterAccessWidth::BITS64))
        .WillOnce(serves_register(0x0000'0011'0400'0100));

    EXPECT_EQ(
        risc_reset_.get_risc_reset_state(TENSIX_CORE, NocId::NOC0),
        std::optional<RiscType>(
            RiscType::DM1 | RiscType::DM2 | RiscType::DM3 | RiscType::DM4 | RiscType::DM5 | RiscType::DM6 |
            RiscType::DM7 | RiscType::NEO0_TRISC1 | RiscType::NEO0_TRISC2 | RiscType::NEO0_TRISC3 |
            RiscType::NEO_DM_UNCORE));
}

}  // namespace tsc_risc_reset_test
