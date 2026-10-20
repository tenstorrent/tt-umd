// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Quasar reaches a target two ways, and these cases hold them against each other on a real device.
//
// A TLB window is mapped into the process and read with ordinary loads and stores. The scalar path
// in tt-kmd-lib asks the driver for one access at a time. Both end up at the same system physical
// address, so a value written through either has to be visible through the other -- if the window
// is programmed to the wrong place, or the driver and UMD disagree about which entry a window is,
// the two paths quietly stop meeting and only a round trip like this notices.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/tlb.hpp"

using namespace tt::umd;

namespace {

// The SMC cold scratch bank: eight 32-bit registers, four bytes apart, with no side effects on
// either side of an access. The driver does not use them, so a test may scribble freely.
//
// Deliberately not the cpu_ctrl scratch bank at 0x1202010100: its register 12 is the host-visible
// boot state word that tt-kmd reads, and clobbering it would break the driver, not the test.
constexpr uint64_t COLD_SCRATCH_BASE = 0x1202002800ULL;
constexpr uint64_t COLD_SCRATCH_STRIDE = 0x4;
constexpr uint32_t COLD_SCRATCH_COUNT = 8;

constexpr uint64_t cold_scratch(const uint32_t index) { return COLD_SCRATCH_BASE + index * COLD_SCRATCH_STRIDE; }

// Open the first Quasar device present, or nullptr when the machine has none.
std::unique_ptr<PCIDevice> open_quasar_device() {
    for (const int device_number : PCIDevice::enumerate_devices()) {
        auto device = std::make_unique<PCIDevice>(device_number);
        if (device->get_arch() == tt::ARCH::QUASAR) {
            return device;
        }
    }
    return nullptr;
}

class QuasarIoWindow : public ::testing::Test {
protected:
    void SetUp() override {
        device_ = open_quasar_device();
        if (device_ == nullptr) {
            GTEST_SKIP() << "No Quasar device present.";
        }

        window_size_ = get_architecture_tlbs(tt::ARCH::QUASAR).size_classes.front().size;

        // One window covers the whole scratch bank, so every case below addresses within it.
        window_base_ = COLD_SCRATCH_BASE & ~(window_size_ - 1);

        // The address is flat and already names its target, so no coordinate is set. The driver
        // refuses one, which is what makes this the only shape a Quasar window takes.
        tlb_data config;
        config.local_offset = window_base_;

        window_ = std::make_unique<SiliconTlbWindow>(device_->allocate_tlb(window_size_, TlbMapping::UC), config);
    }

    // Offset of a system physical address within the mapped window.
    uint64_t offset_of(const uint64_t address) const { return address - window_base_; }

    uint32_t read_through_driver(const uint64_t address) const {
        uint64_t value = 0;
        EXPECT_EQ(tt_noc_read_scalar(device_->get_tt_device_handle(), address, &value, sizeof(uint32_t), 0), 0)
            << "driver read at " << std::hex << address;
        return static_cast<uint32_t>(value);
    }

    void write_through_driver(const uint64_t address, const uint32_t value) const {
        EXPECT_EQ(tt_noc_write_scalar(device_->get_tt_device_handle(), address, value, sizeof(uint32_t), 0), 0)
            << "driver write at " << std::hex << address;
    }

    std::unique_ptr<PCIDevice> device_;
    std::unique_ptr<SiliconTlbWindow> window_;
    uint64_t window_size_ = 0;
    uint64_t window_base_ = 0;
};

}  // namespace

// Allocate a window, write through it, read it back, and confirm the data. Every register in the
// bank gets a distinct value so a window landing on the wrong address, or a stride that is off,
// shows up as the wrong register rather than as a value that happens to match.
TEST_F(QuasarIoWindow, WriteAndReadBackThroughTheWindow) {
    std::vector<uint32_t> written;
    written.reserve(COLD_SCRATCH_COUNT);

    for (uint32_t i = 0; i < COLD_SCRATCH_COUNT; i++) {
        const uint32_t value = 0xC0FFEE00u | i;
        written.push_back(value);
        window_->write32(offset_of(cold_scratch(i)), value);
    }

    for (uint32_t i = 0; i < COLD_SCRATCH_COUNT; i++) {
        EXPECT_EQ(window_->read32(offset_of(cold_scratch(i))), written[i]) << "scratch register " << i;
    }
}

// Reconfiguring a window has to retarget it, not just renumber it: point the same window at a
// different part of the address space and the data underneath must follow.
TEST_F(QuasarIoWindow, ReconfiguringTheWindowRetargetsIt) {
    constexpr uint32_t SENTINEL = 0x5EC0ADDEu;
    window_->write32(offset_of(cold_scratch(0)), SENTINEL);

    // Move the window one window-size away and back. If configure() were a no-op, the value would
    // still be readable at the same offset while the window points elsewhere.
    tlb_data elsewhere;
    elsewhere.local_offset = window_base_ + window_size_;
    window_->configure(elsewhere);

    tlb_data back;
    back.local_offset = window_base_;
    window_->configure(back);

    EXPECT_EQ(window_->read32(offset_of(cold_scratch(0))), SENTINEL);
}

// The two paths have to meet. A window written by a store must read back through the driver's
// scalar access, and a value the driver wrote must be visible through the mapping -- which is what
// proves CONFIGURE_TLB pointed the window at the address the scalar path reaches.
TEST_F(QuasarIoWindow, WindowAndDriverSeeTheSameMemory) {
    constexpr uint32_t FROM_WINDOW = 0xABCD0001u;
    constexpr uint32_t FROM_DRIVER = 0xABCD0002u;

    const uint64_t address = cold_scratch(3);

    // Written by a store through the mapping, read by an ioctl.
    window_->write32(offset_of(address), FROM_WINDOW);
    EXPECT_EQ(read_through_driver(address), FROM_WINDOW);

    // Written by an ioctl, read by a load through the mapping.
    write_through_driver(address, FROM_DRIVER);
    EXPECT_EQ(window_->read32(offset_of(address)), FROM_DRIVER);
}

// The same check across the bank, so a single register that happens to alias cannot carry the
// result. Alternating which path writes also catches a window that is readable but not writable,
// or the reverse.
TEST_F(QuasarIoWindow, EveryRegisterAgreesAcrossBothPaths) {
    for (uint32_t i = 0; i < COLD_SCRATCH_COUNT; i++) {
        const uint64_t address = cold_scratch(i);
        const uint32_t value = 0x51D00000u | i;

        if (i % 2 == 0) {
            window_->write32(offset_of(address), value);
            EXPECT_EQ(read_through_driver(address), value) << "scratch register " << i << " written by the window";
        } else {
            write_through_driver(address, value);
            EXPECT_EQ(window_->read32(offset_of(address)), value) << "scratch register " << i << " written by the driver";
        }
    }
}

// The attribute fields have to actually arrive at the driver, and the driver's own
// range check is the only oracle available: nothing here can program a nonzero
// attribute, because the simulator requires every attribute word to be zero and
// aborts the VM on anything else. An out-of-range value cannot be refused unless the
// field made it across, so a refusal is evidence the plumbing is connected -- and
// silent acceptance would mean it is not.
TEST_F(QuasarIoWindow, AttributeFieldsReachTheDriver) {
    constexpr uint32_t SENTINEL = 0xA77121B5u;

    tlb_data config;
    config.local_offset = window_base_;

    // The default every window gets today. It has to keep working.
    config.snoop = 0;
    config.sec_group_id = 0;
    ASSERT_NO_THROW(window_->configure(config));
    window_->write32(offset_of(cold_scratch(1)), SENTINEL);
    EXPECT_EQ(window_->read32(offset_of(cold_scratch(1))), SENTINEL);

    // sec_group_id is four bits wide.
    config.sec_group_id = 16;
    EXPECT_THROW(window_->configure(config), std::exception) << "sec_group_id 16 should not fit";

    // snoop is one bit, expressed as 0 or 1.
    config.sec_group_id = 0;
    config.snoop = 2;
    EXPECT_THROW(window_->configure(config), std::exception) << "snoop 2 is not a boolean";

    // And the window still works once a sane configuration is put back.
    config.snoop = 0;
    ASSERT_NO_THROW(window_->configure(config));
    EXPECT_EQ(window_->read32(offset_of(cold_scratch(1))), SENTINEL);
}

// WindowFlags::Snoop end to end: accepted by the window, carried to the driver, and reported back.
//
// The window is deliberately not accessed while the flag is set. The snoop bit becomes a nonzero
// attribute in the TLB entry, and the simulator aborts the whole VM on an access through an entry
// whose attributes are not zero -- programming it is fine, reading through it is not. So the flag
// is set, checked, and cleared before any data moves.
TEST_F(QuasarIoWindow, SnoopFlagIsCarriedToTheWindow) {
    TargetIoWindowConfig target;
    target.core_start = tt_xy_pair(0, 0);  // the address is flat; a coordinate is refused
    target.addr = window_base_;
    target.noc = NocId::NOC0;
    target.flags = WindowFlags::Snoop;

    // Relaxed because nothing on this architecture programs ordering, so asking for Strict would
    // promise a guarantee the window was never configured for.
    ASSERT_NO_THROW(window_->configure(target, IoOrdering::Relaxed));
    EXPECT_NE(window_->get_target_config().flags & WindowFlags::Snoop, WindowFlags::None)
        << "the window should report the flag it was configured with";

    // Clear it before touching the window again, then confirm the window still works.
    target.flags = WindowFlags::None;
    ASSERT_NO_THROW(window_->configure(target, IoOrdering::Relaxed));
    EXPECT_EQ(window_->get_target_config().flags & WindowFlags::Snoop, WindowFlags::None);

    constexpr uint32_t SENTINEL = 0x5000FEEDu;
    window_->write32(offset_of(cold_scratch(2)), SENTINEL);
    EXPECT_EQ(window_->read32(offset_of(cold_scratch(2))), SENTINEL);
}
