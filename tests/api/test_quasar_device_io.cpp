// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Device I/O against a real Quasar device, over the kernel driver's scalar accesses.
//
// These need a Quasar device bound to the driver and skip when there is none, so they run on the
// emulator and on silicon and are inert everywhere else.
//
// SAFETY. On the emulator every access is real seconds over the transactor and a touch of an
// unmodelled or special-semantics region can hang the machine for everyone on it. The addresses
// below are only the ones tt-kmd's tools/keraunos_read32.c has confirmed respond cleanly, and the
// writes go only where that tool permits writes:
//
//   - The SMC cold scratch bank is plain RW storage in a block nothing watches. Registers 1-7 are
//     writable; each is restored afterwards.
//   - The SMC cpu_ctrl scratch bank is READ ONLY here. chippy's ker_load_fw test polls it and
//     exits its run loop on the sentinels 0xACAFACA1 / 0xDEADBEEF, and when that process exits the
//     AXI/PCIe proxy goes with it. Writing cpu_ctrl SCRATCH_1 has already wedged the emulator once.
//   - SEP SRAM (0x1201800000) and the SMC mailbox (0x1202018000) are never touched: the first
//     wedged the emulator, the second answers all-ones.
//
// Anything added here needs the same provenance. An address that merely looks safe is not.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/arch/configs/keraunos_address_map.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt::umd;

namespace {

// The SMC block seen two ways. The kernel reaches it as a system physical address through the
// default aperture, and as a chiplet-local address through the system NOC; the remapper puts the
// same block at both, so an access through either must land on the same register.
constexpr uint64_t SMC_SPA_BASE = 0x1202000000ULL;
constexpr uint64_t SMC_LOCAL_BASE = 0x08000000ULL;

// Every address below has to name a region the package actually maps. On the emulator an access
// to an unmodelled address can hang the machine rather than fail, so this is checked before the
// access is issued rather than discovered by issuing it.
void expect_reachable(uint64_t spa) {
    const keraunos::SpaRegion* region = keraunos::find_region(spa);
    ASSERT_NE(region, nullptr) << std::hex << "0x" << spa << " is outside every mapped region.";
}

// Plain RW storage, 8 registers 4 bytes apart. Register 0 is left alone: the tool's write list
// starts at 1.
constexpr uint64_t COLD_SCRATCH_BASE = 0x1202002800ULL;
constexpr uint64_t COLD_SCRATCH_STRIDE = 0x4;
constexpr uint32_t FIRST_WRITABLE_COLD_SCRATCH = 1;
constexpr uint32_t LAST_WRITABLE_COLD_SCRATCH = 7;

// Read only. Register 0 carries the firmware's "init complete" sentinel.
constexpr uint64_t CPUCTRL_SCRATCH_BASE = 0x1202010100ULL;

constexpr uint64_t cold_scratch(uint32_t index) { return COLD_SCRATCH_BASE + index * COLD_SCRATCH_STRIDE; }

/** The same register named as a chiplet-local address instead of a system physical one. */
constexpr uint64_t as_local_address(uint64_t spa) { return spa - SMC_SPA_BASE + SMC_LOCAL_BASE; }

// An address is flat on Quasar and carries its own target, so every access names the origin.
// LITERAL is what keeps TTDevice::resolve_coordinate from translating it through the descriptor:
// there is nothing to translate, and the coordinate is only along for the ride.
constexpr CoreCoord ORIGIN{0, 0, tt::CoreType::UNSPECIFIED, tt::CoordSystem::LITERAL};

class QuasarDeviceIOTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int device_id : PCIDevice::enumerate_devices()) {
            if (PCIDevice(device_id).get_arch() == tt::ARCH::QUASAR) {
                device_ = TTDevice::create(device_id, IODeviceType::PCIe);
                return;
            }
        }
        GTEST_SKIP() << "No Quasar device is bound to the driver.";
    }

    uint32_t read32(uint64_t addr, NocId noc = NocId::NOC0) {
        // A local address has already been rebased out of the system physical range, so it is the
        // system physical form that the region table can speak about.
        expect_reachable(noc == NocId::SYSTEM_NOC ? addr - SMC_LOCAL_BASE + SMC_SPA_BASE : addr);

        uint32_t value = 0;
        device_->read_from_device(&value, ORIGIN, addr, sizeof(value), noc);
        return value;
    }

    void write32(uint64_t addr, uint32_t value, NocId noc = NocId::NOC0) {
        expect_reachable(noc == NocId::SYSTEM_NOC ? addr - SMC_LOCAL_BASE + SMC_SPA_BASE : addr);

        device_->write_to_device(&value, ORIGIN, addr, sizeof(value), noc);
    }

    /** Round-trips a pattern through a scratch register and puts back what was there. */
    void expect_round_trip(uint64_t addr, uint32_t pattern) {
        SCOPED_TRACE(::testing::Message() << "scratch register " << std::hex << addr);

        const uint32_t original = read32(addr);

        write32(addr, pattern);
        EXPECT_EQ(read32(addr), pattern);

        write32(addr, original);
        EXPECT_EQ(read32(addr), original);
    }

    std::unique_ptr<TTDevice> device_;
};

}  // namespace

// The cheapest proof that the whole path works: driver open, ioctl, and a register that answers
// with something other than all-ones or zero.
TEST_F(QuasarDeviceIOTest, ReadsTheFirmwareInitSentinel) {
    const uint32_t sentinel = read32(CPUCTRL_SCRATCH_BASE);

    EXPECT_NE(sentinel, 0xffffffffu) << "All-ones is what a refused or unmodelled access returns.";
}

TEST_F(QuasarDeviceIOTest, RoundTripsAScratchRegister) {
    expect_round_trip(cold_scratch(FIRST_WRITABLE_COLD_SCRATCH), 0xa5a5a5a5);
}

// A stride that is wrong by a power of two still round-trips at one register and only shows up at
// the ends of the bank, so both ends are touched rather than one address.
TEST_F(QuasarDeviceIOTest, RoundTripsBothEndsOfTheScratchBank) {
    expect_round_trip(cold_scratch(FIRST_WRITABLE_COLD_SCRATCH), 0x11111111);
    expect_round_trip(cold_scratch(LAST_WRITABLE_COLD_SCRATCH), 0x77777777);
}

// Writing distinct values everywhere before reading any of them back means a device that ignored
// the address and drove one register returns the last value written at every offset.
TEST_F(QuasarDeviceIOTest, ScratchRegistersAreDistinctStorage) {
    std::vector<uint32_t> originals;
    for (uint32_t i = FIRST_WRITABLE_COLD_SCRATCH; i <= LAST_WRITABLE_COLD_SCRATCH; i++) {
        originals.push_back(read32(cold_scratch(i)));
    }

    for (uint32_t i = FIRST_WRITABLE_COLD_SCRATCH; i <= LAST_WRITABLE_COLD_SCRATCH; i++) {
        write32(cold_scratch(i), 0xc0de0000 | i);
    }

    for (uint32_t i = FIRST_WRITABLE_COLD_SCRATCH; i <= LAST_WRITABLE_COLD_SCRATCH; i++) {
        EXPECT_EQ(read32(cold_scratch(i)), 0xc0de0000 | i) << "register " << i;
    }

    for (uint32_t i = FIRST_WRITABLE_COLD_SCRATCH; i <= LAST_WRITABLE_COLD_SCRATCH; i++) {
        write32(cold_scratch(i), originals[i - FIRST_WRITABLE_COLD_SCRATCH]);
    }
}

// The two apertures are the only thing that differs between these reads. Reaching the same
// register through both is what shows the system-NOC path is wired to the driver's local-address
// flag rather than quietly falling back to the default one.
TEST_F(QuasarDeviceIOTest, BothAperturesReachTheSameRegister) {
    const uint64_t addr = cold_scratch(FIRST_WRITABLE_COLD_SCRATCH);
    const uint32_t original = read32(addr);

    write32(addr, 0x5eeded);
    EXPECT_EQ(read32(addr, NocId::NOC0), 0x5eededu);
    EXPECT_EQ(read32(as_local_address(addr), NocId::SYSTEM_NOC), 0x5eededu);

    write32(addr, original);
}

// A write through the local-address aperture must reach the same storage the default one reads,
// or the two are addressing different things and only one of them is right.
TEST_F(QuasarDeviceIOTest, AWriteThroughTheLocalApertureIsSeenByTheDefaultOne) {
    const uint64_t addr = cold_scratch(LAST_WRITABLE_COLD_SCRATCH);
    const uint32_t original = read32(addr);

    write32(as_local_address(addr), 0x10ca1, NocId::SYSTEM_NOC);
    EXPECT_EQ(read32(addr, NocId::NOC0), 0x10ca1u);

    write32(addr, original);
}

// Quasar addresses carry their own target, so a caller still passing a coordinate is asking for
// something this path cannot do and must be told rather than silently served at the origin.
TEST_F(QuasarDeviceIOTest, RejectsACoordinateOtherThanTheOrigin) {
    constexpr CoreCoord elsewhere{1, 1, tt::CoreType::UNSPECIFIED, tt::CoordSystem::LITERAL};
    uint32_t value = 0;

    EXPECT_THROW(
        device_->read_from_device(&value, elsewhere, cold_scratch(1), sizeof(value), NocId::NOC0), std::exception);
}
