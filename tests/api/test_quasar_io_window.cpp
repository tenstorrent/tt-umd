// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// A Quasar TLB window programmed through the driver, used the way a caller would: map it onto a
// system physical address, then load and store through it.
//
// The data cases check that a round trip through one window returns what was written, and that
// reconfiguring a window moves the address its offsets land on. Both read back through the window
// they wrote through, so they do not prove that the window reaches the same address as the driver's
// scalar access. The refusal cases check that a window configured with a coordinate or multicast is
// refused.
//
// SAFETY. On the emulator a touch of an unproven address can hang the machine for everyone on it.
// The cases write only cold scratch registers that tt-kmd's tools/keraunos_read32.c permits writes
// to, and restore each one afterwards.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/tlb.hpp"

using namespace tt::umd;

namespace {

struct ScratchRegister {
    uint64_t address;
    uint32_t test_value;
};

// The cold scratch registers a test may write, each with a distinct value so two offsets that land
// on one register show up as a wrong value.
//
// Register 0 is absent because the tool that established these addresses starts its write list at
// 1. Register 3 is absent because it is not plain storage: it holds the SMC firmware's message
// queue pointer, and a host message sent after a clobber would follow it into an arbitrary address.
constexpr std::array<ScratchRegister, 6> WRITABLE_COLD_SCRATCH = {{
    {0x1202002804ULL, 0xC0FFEE01u},
    {0x1202002808ULL, 0xC0FFEE02u},
    {0x1202002810ULL, 0xC0FFEE04u},
    {0x1202002814ULL, 0xC0FFEE05u},
    {0x1202002818ULL, 0xC0FFEE06u},
    {0x120200281CULL, 0xC0FFEE07u},
}};

// The window's home address, cold scratch 1: the lowest writable register, so every writable
// register sits at a non-negative offset from it, and offset 0 is itself writable.
constexpr uint64_t HOME_SCRATCH = 0x1202002804ULL;

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

        // The address is flat and already names its target, so no coordinate is set: configuring
        // a Quasar window with one is refused.
        window_ = std::make_unique<SiliconTlbWindow>(device_->allocate_tlb(window_size_, TlbMapping::UC), home());

        for (size_t i = 0; i < WRITABLE_COLD_SCRATCH.size(); i++) {
            saved_values_[i] = window_->read32(offset_of(WRITABLE_COLD_SCRATCH[i].address));
        }
    }

    // Leave the scratch bank as the test found it, wherever the case left the window pointing.
    void TearDown() override {
        if (window_ == nullptr) {
            return;
        }
        window_->configure(home());
        for (size_t i = 0; i < WRITABLE_COLD_SCRATCH.size(); i++) {
            window_->write32(offset_of(WRITABLE_COLD_SCRATCH[i].address), saved_values_[i]);
        }
    }

    // The window's configuration that covers the writable scratch registers.
    static tlb_data home() { return at(HOME_SCRATCH); }

    // A window configuration whose offset 0 lands on the given address.
    static tlb_data at(const uint64_t address) {
        tlb_data config;
        config.local_offset = address;
        return config;
    }

    // Offset of a system physical address within the window at home.
    static uint64_t offset_of(const uint64_t address) { return address - HOME_SCRATCH; }

    std::unique_ptr<PCIDevice> device_;
    std::unique_ptr<SiliconTlbWindow> window_;
    uint64_t window_size_ = 0;
    std::array<uint32_t, WRITABLE_COLD_SCRATCH.size()> saved_values_{};
};

// Allocate a window, write through it, read it back, and confirm the data.
TEST_F(QuasarIoWindow, WriteAndReadBackThroughTheWindow) {
    for (const ScratchRegister& reg : WRITABLE_COLD_SCRATCH) {
        window_->write32(offset_of(reg.address), reg.test_value);
    }

    for (const ScratchRegister& reg : WRITABLE_COLD_SCRATCH) {
        EXPECT_EQ(window_->read32(offset_of(reg.address)), reg.test_value)
            << "scratch register at 0x" << std::hex << reg.address;
    }
}

// Reconfiguring a window has to move it: an offset that reached one register before must reach
// the new address afterwards, and the register it left behind must keep its own value. If
// configure() were a no-op, the second write would overwrite cold scratch 1 and the final check
// would catch it. Every address touched, even by a broken configure(), is a writable register.
TEST_F(QuasarIoWindow, ReconfiguringTheWindowMovesWhereItsOffsetsLand) {
    window_->write32(0, 0xC0FFEE01u);

    window_->configure(at(0x1202002808ULL));
    window_->write32(0, 0xC0FFEE02u);
    EXPECT_EQ(window_->read32(0), 0xC0FFEE02u);

    window_->configure(home());
    EXPECT_EQ(window_->read32(0), 0xC0FFEE01u);
    EXPECT_EQ(window_->read32(4), 0xC0FFEE02u);
}

// A window configuration the driver must refuse. Each case is applied on top of the home address,
// so even a regression that accepted it would map only the scratch block.
struct RefusedConfig {
    const char* name;
    uint64_t x_start;
    uint64_t y_start;
    uint64_t x_end;
    uint64_t y_end;
    uint64_t mcast;
};

// A Quasar address is flat and already names its target, so a window configured with a NOC
// coordinate or multicast is refused rather than silently mapped to the flat address alone.
class QuasarIoWindowRefusal : public QuasarIoWindow, public ::testing::WithParamInterface<RefusedConfig> {};

TEST_P(QuasarIoWindowRefusal, ConfigureIsRefused) {
    const RefusedConfig& refused = GetParam();
    tlb_data config = home();
    config.x_start = refused.x_start;
    config.y_start = refused.y_start;
    config.x_end = refused.x_end;
    config.y_end = refused.y_end;
    config.mcast = refused.mcast;

    EXPECT_THROW(window_->configure(config), std::runtime_error);
}

INSTANTIATE_TEST_SUITE_P(
    QuasarIoWindow,
    QuasarIoWindowRefusal,
    ::testing::Values(RefusedConfig{"Coordinate", 1, 1, 1, 1, 0}, RefusedConfig{"Multicast", 0, 0, 0, 0, 1}),
    [](const ::testing::TestParamInfo<RefusedConfig>& info) { return std::string(info.param.name); });

}  // namespace
