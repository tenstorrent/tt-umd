// SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/pcie/rtl_sim_tlb_handle.hpp"
#include "umd/device/pcie/rtl_sim_tlb_window.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/io_window_config.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

// Arbitrary nonzero base; nothing here dereferences a TLB, so the value only feeds address math.
constexpr uint64_t TEST_BAR0_BASE = 0x10000000ULL;

// Quasar has no real TLBs -- empty allocator pools, dummy auto-incrementing indices, and a 4GB
// dummy window size in SimulationTTDevice::setup_cached_tlb_window(). Sizing the window per arch
// the way the product code does keeps the Quasar case on its real code path.
size_t window_size_for(tt::ARCH arch) {
    return arch == tt::ARCH::QUASAR ? (4ULL * 1024 * 1024 * 1024) : (1 << 21);
}

TargetIoWindowConfig make_target() {
    TargetIoWindowConfig target;
    target.core_start = tt_xy_pair(1, 1);
    target.addr = 0x1000;
    target.noc = NocId::NOC0;
    return target;
}

// RtlSimTlbHandle keeps its config in software and RtlSimTlbWindow only touches the communicator on
// an actual read/write, so a window can be built for configure()-only tests with no card, no
// simulator process, and a null communicator.
//
// Parameterized over arch because Quasar takes different branches on the way here: the allocator
// keeps no pools for it and RtlSimTlbHandle skips the base-address query. Blackhole is the
// ordinary-TLB counterpart.
class RtlSimTlbWindowFlags : public ::testing::TestWithParam<tt::ARCH> {
protected:
    std::unique_ptr<RtlSimTlbWindow> make_window() {
        const size_t size = window_size_for(GetParam());
        allocator_ = std::make_shared<SimulationTlbAllocator>(TEST_BAR0_BASE, GetParam());
        const int tlb_id = allocator_->allocate_tlb_index(size);
        EXPECT_NE(tlb_id, -1);
        return std::make_unique<RtlSimTlbWindow>(
            RtlSimTlbHandle::create(allocator_, tlb_id, size, TlbMapping::WC), /*communicator=*/nullptr);
    }

private:
    std::shared_ptr<SimulationTlbAllocator> allocator_;
};

}  // namespace

// The RTL sim carries AXI awuser/aruser on the wire, so it is the one TLB-backed IoWindow that can
// honor WindowFlags::Snoop. Accepting it and reporting it back is what lets a caller ask for a
// snooped transaction at all.
TEST_P(RtlSimTlbWindowFlags, ConfigureAcceptsAndReportsSnoopFlag) {
    std::unique_ptr<RtlSimTlbWindow> window = make_window();

    TargetIoWindowConfig target = make_target();
    EXPECT_EQ(window->get_target_config().flags, WindowFlags::None) << "A fresh window should request no flags";

    target.flags = WindowFlags::Snoop;
    window->configure(target);
    EXPECT_EQ(window->get_target_config().flags, WindowFlags::Snoop);

    // Flags are window state, not part of the mapping, so reconfiguring to a different target
    // without flags must clear them rather than leave the previous request in force.
    target.flags = WindowFlags::None;
    window->configure(target);
    EXPECT_EQ(window->get_target_config().flags, WindowFlags::None);
}

// The _reconfigure family walks the window over a transfer using the tlb_data overload of
// configure(). That overload knows nothing about flags, so a snoop request must survive it --
// otherwise a multi-chunk transfer would silently drop snoop after the first chunk. This is the
// path SimulationTTDevice::host_read/host_write take through cached_tlb_window_, which is how
// Quasar reaches the RTL sim for ordinary NOC traffic.
TEST_P(RtlSimTlbWindowFlags, SnoopFlagSurvivesTlbDataReconfigure) {
    std::unique_ptr<RtlSimTlbWindow> window = make_window();

    TargetIoWindowConfig target = make_target();
    target.flags = WindowFlags::Snoop;
    window->configure(target);

    tlb_data config;
    config.local_offset = 0x2000;
    config.x_end = 2;
    config.y_end = 2;
    window->configure(config);

    EXPECT_EQ(window->get_target_config().flags, WindowFlags::Snoop);
}

// Snoop is expressible on this path; Atomic is not. Rejecting the unsupported one keeps the
// "fail loudly rather than silently drop" contract that applies to every other TLB-backed window.
TEST_P(RtlSimTlbWindowFlags, ConfigureRejectsUnsupportedFlags) {
    std::unique_ptr<RtlSimTlbWindow> window = make_window();

    TargetIoWindowConfig target = make_target();
    target.flags = WindowFlags::Atomic;
    EXPECT_ANY_THROW(window->configure(target));

    target.flags = WindowFlags::Snoop | WindowFlags::Atomic;
    EXPECT_ANY_THROW(window->configure(target)) << "A supported flag must not smuggle an unsupported one through";
}

INSTANTIATE_TEST_SUITE_P(
    Arch,
    RtlSimTlbWindowFlags,
    ::testing::Values(tt::ARCH::BLACKHOLE, tt::ARCH::QUASAR),
    [](const ::testing::TestParamInfo<tt::ARCH>& info) { return std::string(tt::arch_to_str(info.param)); });
