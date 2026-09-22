// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include "tests/test_utils/protocol_mocks.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/tt_device/reset/risc_reset_implementation.hpp"
#include "umd/device/types/arch.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

namespace {

constexpr tt_xy_pair TENSIX_CORE = {1, 1};

// Serves the current register contents to the read under test.
auto serves_register(uint32_t value) {
    return Invoke([value](void* dst, tt_xy_pair, uint64_t, size_t, NocId) { std::memcpy(dst, &value, sizeof(value)); });
}

// Captures what the read-modify-write wrote back.
auto captures_register(uint32_t* written) {
    return Invoke([written](const void* src, tt_xy_pair, uint64_t, size_t, NocId) {
        std::memcpy(written, src, sizeof(*written));
    });
}

// The same implementation serves every architecture that keeps its RISCs in one soft-reset register
// per tile, so each case runs against both of them.
class RiscResetImplementationTest : public ::testing::TestWithParam<tt::ARCH> {
protected:
    RiscResetImplementationTest() :
        architecture_impl_(ArchitectureImplementation::create(GetParam())),
        risc_reset_(&protocol_, architecture_impl_.get()) {}

    uint32_t bits_for(RiscType riscs) const { return architecture_impl_->get_soft_reset_reg_value(riscs); }

    uint64_t reset_address() const { return architecture_impl_->get_tensix_soft_reset_addr(); }

    NiceMock<MockDeviceProtocol> protocol_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    ClassicTileRiscReset risc_reset_;
};

TEST_P(RiscResetImplementationTest, AssertAddsTheSelectedBitsToTheRegister) {
    uint32_t written = 0;
    EXPECT_CALL(protocol_, read_ctrl(_, TENSIX_CORE, reset_address(), sizeof(uint32_t), NocId::NOC0))
        .WillOnce(serves_register(0));
    EXPECT_CALL(protocol_, write_ctrl(_, TENSIX_CORE, reset_address(), sizeof(uint32_t), NocId::NOC0))
        .WillOnce(captures_register(&written));

    risc_reset_.assert_risc_reset(TENSIX_CORE, RiscType::ALL_TENSIX, NocId::NOC0);

    EXPECT_EQ(written, bits_for(RiscType::ALL_TENSIX));
}

TEST_P(RiscResetImplementationTest, AssertKeepsTheBitsThatAreAlreadySet) {
    uint32_t written = 0;
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _)).WillOnce(serves_register(bits_for(RiscType::BRISC)));
    EXPECT_CALL(protocol_, write_ctrl(_, _, _, _, _)).WillOnce(captures_register(&written));

    risc_reset_.assert_risc_reset(TENSIX_CORE, RiscType::ALL_TENSIX_TRISCS, NocId::NOC0);

    EXPECT_EQ(written, bits_for(RiscType::BRISC) | bits_for(RiscType::ALL_TENSIX_TRISCS));
}

TEST_P(RiscResetImplementationTest, DeassertClearsOnlyTheSelectedBits) {
    uint32_t written = 0;
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _)).WillOnce(serves_register(bits_for(RiscType::ALL_TENSIX)));
    EXPECT_CALL(protocol_, write_ctrl(_, _, _, _, _)).WillOnce(captures_register(&written));

    risc_reset_.deassert_risc_reset(TENSIX_CORE, RiscType::ALL_TENSIX_TRISCS, /*staggered_start=*/false, NocId::NOC0);

    EXPECT_EQ(written, bits_for(RiscType::ALL_TENSIX) & ~bits_for(RiscType::ALL_TENSIX_TRISCS));
}

TEST_P(RiscResetImplementationTest, DeassertWithStaggeredStartSetsTheStaggeredBit) {
    uint32_t written = 0;
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _)).WillOnce(serves_register(0));
    EXPECT_CALL(protocol_, write_ctrl(_, _, _, _, _)).WillOnce(captures_register(&written));

    risc_reset_.deassert_risc_reset(TENSIX_CORE, RiscType::ALL_TENSIX, /*staggered_start=*/true, NocId::NOC0);

    EXPECT_EQ(written, architecture_impl_->get_soft_reset_staggered_start());
}

TEST_P(RiscResetImplementationTest, ReportsTheRiscsHeldInReset) {
    EXPECT_CALL(protocol_, read_ctrl(_, _, _, _, _)).WillOnce(serves_register(bits_for(RiscType::BRISC)));

    std::optional<RiscType> state = risc_reset_.get_risc_reset_state(TENSIX_CORE, NocId::NOC0);

    ASSERT_TRUE(state.has_value());
    EXPECT_NE(state.value() & RiscType::BRISC, RiscType::NONE);
    EXPECT_EQ(state.value() & RiscType::ALL_TENSIX_TRISCS, RiscType::NONE);
}

TEST_P(RiscResetImplementationTest, RoutesOnTheGivenNoc) {
    EXPECT_CALL(protocol_, read_ctrl(_, TENSIX_CORE, reset_address(), sizeof(uint32_t), NocId::NOC1))
        .WillOnce(serves_register(0));
    EXPECT_CALL(protocol_, write_ctrl(_, TENSIX_CORE, reset_address(), sizeof(uint32_t), NocId::NOC1));

    risc_reset_.assert_risc_reset(TENSIX_CORE, RiscType::ALL_TENSIX, NocId::NOC1);
}

INSTANTIATE_TEST_SUITE_P(
    Architectures,
    RiscResetImplementationTest,
    ::testing::Values(tt::ARCH::WORMHOLE_B0, tt::ARCH::BLACKHOLE),
    [](const ::testing::TestParamInfo<tt::ARCH>& info) { return arch_to_str(info.param); });

}  // namespace
