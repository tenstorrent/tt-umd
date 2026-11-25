// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// A Quasar TLB window programmed through the driver, used the way a caller would: map it onto a
// system physical address, then load and store through it.
//
// The data cases check that a round trip through one window returns what was written, and that a
// window reconfigured away and back still reaches its data. Both read back through the window they
// wrote through, so they do not prove that the window reaches the same address as the driver's
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

// The SMC cold scratch bank starts here; one window covers all of it.
constexpr uint64_t IO_WINDOW_COLD_SCRATCH_BASE = 0x1202002800ULL;

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

// Cold scratch 1, the register the retarget case leaves its sentinel in.
constexpr uint64_t SENTINEL_SCRATCH = 0x1202002804ULL;

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
        window_base_ = IO_WINDOW_COLD_SCRATCH_BASE & ~(window_size_ - 1);

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

    // The window's configuration that covers the scratch bank.
    tlb_data home() const {
        tlb_data config;
        config.local_offset = window_base_;
        return config;
    }

    // Offset of a system physical address within the mapped window.
    uint64_t offset_of(const uint64_t address) const { return address - window_base_; }

    std::unique_ptr<PCIDevice> device_;
    std::unique_ptr<SiliconTlbWindow> window_;
    uint64_t window_size_ = 0;
    uint64_t window_base_ = 0;
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

// Reconfiguring a window has to retarget it, not just renumber it: point the same window at a
// different part of the address space and the data underneath must follow.
TEST_F(QuasarIoWindow, ReconfiguringTheWindowRetargetsIt) {
    constexpr uint32_t SENTINEL = 0x5EC0ADDEu;
    window_->write32(offset_of(SENTINEL_SCRATCH), SENTINEL);

    // Move the window one window-size away and back. If configure() were a no-op, the value would
    // still be readable at the same offset while the window points elsewhere.
    tlb_data elsewhere;
    elsewhere.local_offset = window_base_ + window_size_;
    window_->configure(elsewhere);

    window_->configure(home());

    EXPECT_EQ(window_->read32(offset_of(SENTINEL_SCRATCH)), SENTINEL);
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
