// SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// TlbWindow's WindowFlags contract, verified against a fake TlbHandle so it needs no card and no
// simulation build. The concrete RTL-sim mapping of Snoop onto AXI user bits lives in
// test_rtl_sim_tlb_window.cpp, which is necessarily gated on TT_UMD_BUILD_SIMULATION; this file
// covers the base-class behavior that every implementation inherits.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/io_window_config.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

constexpr size_t FAKE_TLB_SIZE = 1 << 21;

// Software-only handle: stores whatever configure() is handed and maps nothing. tlb_base_ stays
// null, which is safe because no test here performs an actual access.
class FakeTlbHandle : public TlbHandle {
public:
    FakeTlbHandle() {
        tlb_id_ = 0;
        tlb_size_ = FAKE_TLB_SIZE;
        tlb_mapping_ = TlbMapping::WC;
    }

    void configure(const tlb_data& new_config) override { tlb_config_ = new_config; }

    tt::ARCH get_arch() const override { return tt::ARCH::BLACKHOLE; }

private:
    void free_tlb() noexcept override {}
};

// Minimal concrete TlbWindow. Every access method is a no-op: these tests exercise configure() and
// get_target_config() only. `supported` is what supported_window_flags() reports, which is the knob
// the base class consults when deciding whether to accept a caller's flags.
class FakeTlbWindow : public TlbWindow {
public:
    explicit FakeTlbWindow(WindowFlags supported) :
        TlbWindow(std::make_unique<FakeTlbHandle>()), supported_(supported) {}

    void write16(uint64_t, uint16_t) override {}

    uint16_t read16(uint64_t) override { return 0; }

    void write32(uint64_t, uint32_t) override {}

    uint32_t read32(uint64_t) override { return 0; }

    void write_register(uint64_t, const void*, size_t) override {}

    void read_register(uint64_t, void*, size_t) override {}

    void write_block(uint64_t, const void*, size_t) override {}

    void read_block(uint64_t, void*, size_t) override {}

    void safe_write16(uint64_t, uint16_t) override {}

    uint16_t safe_read16(uint64_t) override { return 0; }

protected:
    WindowFlags supported_window_flags() const override { return supported_; }

private:
    WindowFlags supported_;
};

TargetIoWindowConfig make_target() {
    TargetIoWindowConfig target;
    target.core_start = tt_xy_pair(1, 1);
    target.addr = 0x1000;
    target.noc = NocId::NOC0;
    return target;
}

}  // namespace

// The default is to support nothing, which is what silicon and TTSim rely on: a caller asking for a
// flag the window cannot express gets an error instead of a silently unsnooped transaction.
TEST(TlbWindowFlags, UnsupportedFlagsAreRejectedByDefault) {
    FakeTlbWindow window(WindowFlags::None);

    TargetIoWindowConfig target = make_target();
    target.flags = WindowFlags::Snoop;
    EXPECT_ANY_THROW(window.configure(target));

    target.flags = WindowFlags::Atomic;
    EXPECT_ANY_THROW(window.configure(target));

    // No flags at all stays the ordinary, always-valid case.
    target.flags = WindowFlags::None;
    EXPECT_NO_THROW(window.configure(target));
    EXPECT_EQ(window.get_target_config().flags, WindowFlags::None);
}

// A window that declares support for a flag must accept it and report it back, otherwise a caller
// has no way to confirm the request took effect.
TEST(TlbWindowFlags, SupportedFlagIsStoredAndReported) {
    FakeTlbWindow window(WindowFlags::Snoop);

    TargetIoWindowConfig target = make_target();
    target.flags = WindowFlags::Snoop;
    window.configure(target);
    EXPECT_EQ(window.get_target_config().flags, WindowFlags::Snoop);

    // Reconfiguring without the flag must clear it rather than leave the old request standing.
    target.flags = WindowFlags::None;
    window.configure(target);
    EXPECT_EQ(window.get_target_config().flags, WindowFlags::None);
}

// Only the unsupported subset matters: a supported flag must not act as a carrier for one the
// window cannot honor.
TEST(TlbWindowFlags, PartiallySupportedFlagSetIsRejected) {
    FakeTlbWindow window(WindowFlags::Snoop);

    TargetIoWindowConfig target = make_target();
    target.flags = WindowFlags::Snoop | WindowFlags::Atomic;
    EXPECT_ANY_THROW(window.configure(target));

    // The rejected call must not have left the flag applied.
    EXPECT_EQ(window.get_target_config().flags, WindowFlags::None);
}

// The _reconfigure family drives the window with the tlb_data overload of configure(), which knows
// nothing about flags. Flags are window state, so they have to survive it -- otherwise a transfer
// large enough to be split into chunks would lose snoop partway through.
TEST(TlbWindowFlags, FlagsSurviveTlbDataReconfigure) {
    FakeTlbWindow window(WindowFlags::Snoop);

    TargetIoWindowConfig target = make_target();
    target.flags = WindowFlags::Snoop;
    window.configure(target);

    tlb_data config;
    config.local_offset = 0x2000;
    config.x_end = 2;
    config.y_end = 2;
    window.configure(config);

    EXPECT_EQ(window.get_target_config().flags, WindowFlags::Snoop);
}

// Ordering and flags are independent axes of the same mapping; changing one must not disturb the
// other.
TEST(TlbWindowFlags, FlagsAndOrderingAreIndependent) {
    FakeTlbWindow window(WindowFlags::Snoop);

    TargetIoWindowConfig target = make_target();
    target.flags = WindowFlags::Snoop;
    window.configure(target, IoOrdering::Posted);

    EXPECT_EQ(window.get_target_config().flags, WindowFlags::Snoop);
    EXPECT_EQ(window.get_io_ordering(), IoOrdering::Posted);
}
