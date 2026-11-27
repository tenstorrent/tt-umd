// SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// SiliconTlbWindow's configuration contract (window flags, ordering, size), verified over a fake
// TlbHandle so it needs no card and no simulation build. No test here performs a device access.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>

#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/pcie/tlb_handle.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/tlb.hpp"
#include "tt-umd/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

namespace silicon_tlb_window_test {

constexpr size_t FAKE_TLB_SIZE = 2 * 1024 * 1024;

// Software-only handle: stores whatever configure() is handed and maps nothing (the base stays null).
class FakeTlbHandle : public TlbHandle {
public:
    explicit FakeTlbHandle(tt::ARCH arch) : arch_(arch) {
        tlb_id_ = 0;
        tlb_size_ = FAKE_TLB_SIZE;
        tlb_mapping_ = TlbMapping::WC;
    }

    void configure(const tlb_data& new_config) override { tlb_config_ = new_config; }

    tt::ARCH get_arch() const override { return arch_; }

private:
    void free_tlb() noexcept override {}

    tt::ARCH arch_;
};

SiliconTlbWindow make_window(tt::ARCH arch) { return SiliconTlbWindow(std::make_unique<FakeTlbHandle>(arch)); }

TargetIoWindowConfig make_target(WindowFlags flags = WindowFlags::None, tt_xy_pair core = tt_xy_pair(1, 1)) {
    TargetIoWindowConfig target;
    target.core_start = core;
    target.addr = 0x1000;
    target.noc = NocId::NOC0;
    target.flags = flags;
    return target;
}

// A Quasar window is flat over the system address, so its target names no core.
TargetIoWindowConfig make_quasar_target(WindowFlags flags = WindowFlags::None) {
    return make_target(flags, tt_xy_pair(0, 0));
}

// Wormhole's and Blackhole's TLB entries have no field for any window flag, so asking for one is an
// error rather than a transaction that silently lacks the requested attribute.
class SiliconTlbWindowFlagsRefused : public ::testing::TestWithParam<std::tuple<tt::ARCH, WindowFlags>> {};

TEST_P(SiliconTlbWindowFlagsRefused, ConfigureThrows) {
    const auto [arch, flag] = GetParam();
    SiliconTlbWindow window = make_window(arch);

    EXPECT_ANY_THROW(window.configure(make_target(flag)));
}

INSTANTIATE_TEST_SUITE_P(
    SiliconTlbWindowFlags,
    SiliconTlbWindowFlagsRefused,
    ::testing::Combine(
        ::testing::Values(tt::ARCH::WORMHOLE_B0, tt::ARCH::BLACKHOLE),
        ::testing::Values(WindowFlags::Atomic, WindowFlags::Snoop, WindowFlags::NonCacheable)));

TEST(SiliconTlbWindowFlags, TargetWithoutFlagsIsAcceptedOnBlackhole) {
    SiliconTlbWindow window = make_window(tt::ARCH::BLACKHOLE);

    window.configure(make_target());

    EXPECT_EQ(window.get_target_config().flags, WindowFlags::None);
}

TEST(SiliconTlbWindowFlags, NonCacheableIsReportedBackOnQuasar) {
    SiliconTlbWindow window = make_window(tt::ARCH::QUASAR);

    window.configure(make_quasar_target(WindowFlags::NonCacheable));

    EXPECT_EQ(window.get_target_config().flags, WindowFlags::NonCacheable);
}

// Reconfiguring without the flag clears it rather than leaving the earlier request standing.
TEST(SiliconTlbWindowFlags, ReconfigureWithoutNonCacheableClearsItOnQuasar) {
    SiliconTlbWindow window = make_window(tt::ARCH::QUASAR);
    window.configure(make_quasar_target(WindowFlags::NonCacheable));

    window.configure(make_quasar_target());

    EXPECT_EQ(window.get_target_config().flags, WindowFlags::None);
}

// A supported flag must not carry an unsupported one past the check.
TEST(SiliconTlbWindowFlags, NonCacheableWithSnoopIsRefusedOnQuasar) {
    SiliconTlbWindow window = make_window(tt::ARCH::QUASAR);

    EXPECT_ANY_THROW(window.configure(make_quasar_target(WindowFlags::NonCacheable | WindowFlags::Snoop)));
}

TEST(SiliconTlbWindowOrdering, DefaultsToStrictOnBlackhole) {
    SiliconTlbWindow window = make_window(tt::ARCH::BLACKHOLE);

    window.configure(make_target());

    EXPECT_EQ(window.get_io_ordering(), IoOrdering::STRICT);
}

TEST(SiliconTlbWindowOrdering, ExplicitOrderingIsReportedBackOnBlackhole) {
    SiliconTlbWindow window = make_window(tt::ARCH::BLACKHOLE);

    window.configure(make_target(), IoOrdering::POSTED);

    EXPECT_EQ(window.get_io_ordering(), IoOrdering::POSTED);
}

TEST(SiliconTlbWindowOrdering, DefaultsToRelaxedOnQuasar) {
    SiliconTlbWindow window = make_window(tt::ARCH::QUASAR);

    window.configure(make_quasar_target());

    EXPECT_EQ(window.get_io_ordering(), IoOrdering::RELAXED);
}

// Quasar's TLB entry has no ordering field, so only RELAXED can be honored.
class SiliconTlbWindowOrderingRefusedOnQuasar : public ::testing::TestWithParam<IoOrdering> {};

TEST_P(SiliconTlbWindowOrderingRefusedOnQuasar, ConfigureThrows) {
    SiliconTlbWindow window = make_window(tt::ARCH::QUASAR);

    EXPECT_ANY_THROW(window.configure(make_quasar_target(), GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    SiliconTlbWindowOrdering,
    SiliconTlbWindowOrderingRefusedOnQuasar,
    ::testing::Values(IoOrdering::STRICT, IoOrdering::POSTED));

// Flags and ordering are independent settings of the same mapping.
TEST(SiliconTlbWindowOrdering, FlagsAreKeptAlongsideRelaxedOrderingOnQuasar) {
    SiliconTlbWindow window = make_window(tt::ARCH::QUASAR);

    window.configure(make_quasar_target(WindowFlags::NonCacheable), IoOrdering::RELAXED);

    EXPECT_EQ(window.get_target_config().flags, WindowFlags::NonCacheable);
    EXPECT_EQ(window.get_io_ordering(), IoOrdering::RELAXED);
}

// An unaligned target places the window base partway into the 2 MiB mapping, and the window
// reaches only from there to the end of the mapping.
struct UnalignedTarget {
    uint64_t addr;
    uint64_t expected_base_address;
    size_t expected_size;
};

class SiliconTlbWindowSize : public ::testing::TestWithParam<UnalignedTarget> {};

TEST_P(SiliconTlbWindowSize, SizeShrinksByTheOffsetIntoTheMapping) {
    SiliconTlbWindow window = make_window(tt::ARCH::BLACKHOLE);

    tlb_data config;
    config.local_offset = GetParam().addr;
    config.x_end = 1;
    config.y_end = 1;
    window.configure(config);

    EXPECT_EQ(window.get_base_address(), GetParam().expected_base_address);
    EXPECT_EQ(window.get_size(), GetParam().expected_size);
}

INSTANTIATE_TEST_SUITE_P(
    SiliconTlbWindow,
    SiliconTlbWindowSize,
    ::testing::Values(
        UnalignedTarget{0x0, 0x0, 0x200000},
        UnalignedTarget{0x1000, 0x1000, 0x1FF000},
        UnalignedTarget{0x1FFFFC, 0x1FFFFC, 0x4},
        UnalignedTarget{0x200010, 0x200010, 0x1FFFF0}));

}  // namespace silicon_tlb_window_test
