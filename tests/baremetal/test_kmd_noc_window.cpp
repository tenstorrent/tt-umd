// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "tests/test_utils/protocol_mocks.hpp"
#include "umd/device/io_window/kmd_noc_window.hpp"
#include "umd/device/tt_device/protocol/scalar_noc_access.hpp"
#include "umd/device/types/io_window_config.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::SetArgPointee;

namespace {

constexpr uint64_t WINDOW_BASE = 0x12000000;

// A Quasar window has no mapping behind it: there is no host pointer, and an access through it is a
// driver round trip at the window's base plus the offset. It is an IoWindow so that callers above
// need not know that, not because anything is mapped.
class KmdNocWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto access = std::make_unique<NiceMock<MockScalarNocAccess>>();
        access_ = access.get();
        window_ = std::make_unique<KmdNocWindow>(std::move(access), target(WINDOW_BASE));
    }

    static TargetIoWindowConfig target(uint64_t addr) {
        TargetIoWindowConfig config{};
        config.core_start = tt_xy_pair(0, 0);
        config.addr = addr;
        return config;
    }

    NiceMock<MockScalarNocAccess>* access_ = nullptr;
    std::unique_ptr<KmdNocWindow> window_;
};

}  // namespace

TEST_F(KmdNocWindowTest, OffsetIsRelativeToTheConfiguredBase) {
    EXPECT_CALL(*access_, read(WINDOW_BASE + 0x40, _, 4, 0));

    window_->read32(0x40);
}

TEST_F(KmdNocWindowTest, ReadsBackTheValueTheDriverReturned) {
    EXPECT_CALL(*access_, read(WINDOW_BASE, _, 4, 0)).WillOnce(SetArgPointee<1>(0xabcd1234));

    EXPECT_EQ(window_->read32(0), 0xabcd1234u);
}

TEST_F(KmdNocWindowTest, WritesTheValueAtTheOffset) {
    EXPECT_CALL(*access_, write(WINDOW_BASE + 0x8, 0x5a5a5a5a, 4, 0));

    window_->write32(0x8, 0x5a5a5a5a);
}

TEST_F(KmdNocWindowTest, SixteenBitAccessUsesATwoByteWidth) {
    EXPECT_CALL(*access_, write(WINDOW_BASE + 0x2, 0xbeef, 2, 0));

    window_->write16(0x2, 0xbeef);
}

// Reconfiguring is what a window is for: the same handle points somewhere else afterwards.
TEST_F(KmdNocWindowTest, ConfigureMovesTheWindow) {
    window_->configure(target(0x34000000));

    EXPECT_CALL(*access_, read(0x34000000 + 0x10, _, 4, 0));
    window_->read32(0x10);

    EXPECT_EQ(window_->get_target_config().addr, 0x34000000u);
}

TEST_F(KmdNocWindowTest, BlockAccessIsAWholeNumberOfWordAccesses) {
    EXPECT_CALL(*access_, read(WINDOW_BASE + 0x100, _, 4, 0));
    EXPECT_CALL(*access_, read(WINDOW_BASE + 0x104, _, 4, 0));

    uint32_t values[2] = {0, 0};
    window_->read_block(0x100, values, sizeof(values));
}

// The window reports no host mapping strategy that would be a lie: nothing is mapped, so every
// access reaches hardware in issue order the way an uncached mapping would.
TEST_F(KmdNocWindowTest, ReportsAnUncachedStrictWindow) {
    EXPECT_EQ(window_->get_memory_caching_type(), HostMemoryCaching::UC);
    EXPECT_EQ(window_->get_io_ordering(), IoOrdering::Strict);
}
