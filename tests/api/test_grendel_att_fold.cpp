// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Does TTDevice fold a coordinate into the address the way a direct resolver call would?
//
// Quasar's NOC carries no coordinate: the ATT decodes a flat address into a destination tile and a
// tile-local offset, so TTDevice::resolve_target() folds the caller's CoreCoord into the address
// and sends the origin. Everything else in this suite addresses a system physical range with a
// LITERAL coordinate, which resolve_target() passes through untouched -- so nothing here exercises
// the fold at all.
//
// The two halves are tested separately on purpose:
//
//   - that att::EndpointResolver computes the right address is pinned without hardware by
//     tests/baremetal/test_grendel_att_map.cpp;
//   - that TTDevice actually routes through it was checked by reading the same tile twice, once at
//     the resolver's address with a LITERAL coordinate and once by handing TTDevice the coordinate
//     and letting it fold.
//
// That second check is gone for now, because it cannot run on ttsim: from v1.15.1 the model refuses
// every read into a NEO tile's MMIO window -- not one address, the whole window, offsets 0x0 through
// 0x100000 alike -- with "UnimplementedFunctionality: t_tile_mmio_rd32". v1.15.0 served all of them.
// The refusal is _Exit(1) inside libttsim, so it takes QEMU and the guest down rather than returning
// a value, which means the all-ones guard below cannot catch it and every later suite in the same
// process is lost too. Restore the fold check once the model answers again; nothing about the fold
// itself is in question, and it passes on the emulator.
//
// What is left reads only system physical addresses, which that regression does not touch.
//
// A package with no Quasar behind its D2D links reads all-ones everywhere, so an unreachable address
// would pass while proving nothing. The remaining device test checks against that explicitly.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/coordinates/att/configs/grendel_qsr1_att_map.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/noc_id.hpp"

using namespace tt::umd;

namespace {

/** The flat address names the whole target, so the coordinate is along for the ride. */
constexpr CoreCoord FOLD_ORIGIN{0, 0, tt::CoreType::UNSPECIFIED, tt::CoordSystem::LITERAL};

/** What a refused or unmodelled access returns; also what a stubbed NEO tile returns. */
constexpr uint32_t NO_ANSWER = 0xffffffffu;

/**
 * The first slot of the NEO L1 window, stated independently of the resolver.
 *
 * tests/baremetal/test_grendel_att_map.cpp pins resolve({2,2}, TENSIX, 0x100000, 4) to
 * 0x10000100000, and grendelemulation's py/tb/grendel/att_map.py gives the same window as
 * NEO_L1_BASE with a 16 MiB stride per tile. Repeating the constant here rather than asking the
 * resolver is the point: it catches the map drifting away from the hardware's window.
 */
constexpr uint64_t NEO_L1_BASE = 0x10000000000ULL;

class GrendelAttFoldTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int device_id : PCIDevice::enumerate_devices()) {
            if (PCIDevice(device_id).get_arch() == tt::ARCH::QUASAR) {
                device_ = create_tt_device(device_id, IODeviceType::PCIe);
                return;
            }
        }
        GTEST_SKIP() << "No Grendel device is bound to the driver.";
    }

    uint32_t read32(CoreCoord core, uint64_t addr) {
        uint32_t value = 0;
        device_->read_from_device(&value, core, addr, sizeof(value), NocId::NOC0);
        return value;
    }

    std::unique_ptr<TTDevice> device_;
};

// The map still describes the window this file reasons about. Pure arithmetic, no device, so a
// failure here means the map moved and every address below names something else.
TEST_F(GrendelAttFoldTest, TheResolverStillPlacesTheFirstTileWhereThisFileExpects) {
    const att::EndpointResolver resolver(att::GRENDEL_QSR1_MAP);

    EXPECT_EQ(resolver.resolve({2, 2}, tt::CoreType::TENSIX, 0, sizeof(uint32_t)), NEO_L1_BASE);
}

// A coordinate the caller states is already device-ready must never be folded, which is what keeps
// the system physical path in the rest of this suite working. Reading SPA through a LITERAL
// coordinate has to stay reachable with a resolver installed.
TEST_F(GrendelAttFoldTest, ALiteralCoordinateIsNotFolded) {
    constexpr uint64_t SMC_FIRMWARE_SENTINEL = 0x1202010100ULL;

    const uint32_t direct = read32(FOLD_ORIGIN, SMC_FIRMWARE_SENTINEL);

    EXPECT_NE(direct, NO_ANSWER) << "A LITERAL coordinate was folded, or the SMC is unreachable; either way the system "
                                    "physical path this suite depends on is broken.";
}

}  // namespace
