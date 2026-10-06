// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Does a host-originated access reach the Quasar NOC ATT?
//
// UMD can already compute an ATT address: att::EndpointResolver over att::GRENDEL_QSR1_MAP turns a
// core and a core-local offset into the flat address the ATT decodes, and
// tests/baremetal/test_grendel_att_map.cpp pins that arithmetic against the tables the firmware
// programs. What is not established is whether an access UMD issues through the driver's scalar
// path arrives somewhere the ATT translates, or goes out the default aperture and never meets it.
//
// Nothing downstream should be built on a guess about that, so this asks the hardware once.
//
// PROVENANCE. The rest of the Quasar tests only touch addresses that tt-kmd's
// tools/keraunos_read32.c has confirmed respond cleanly, because on the emulator a touch of an
// unmodelled or special-semantics region can hang the machine for everyone on it -- SEP SRAM
// wedged it once already. A NEO L1 address reached through the ATT has no such confirmation. This
// probe is what would establish it, which is exactly why it is the risk it is.
//
// So it is off unless asked for:
//
//   UMD_QUASAR_ATT_PROBE=1 ./api_tests --gtest_filter='GrendelAttProbe.*'
//
// and it is read only, one four byte access per run, against one aperture chosen by
// UMD_QUASAR_ATT_PROBE_NOC (noc0, the default, or system). Two unproven accesses back to back
// would leave you unable to say which one wedged the machine, so a run issues exactly one.
//
// Reading this result:
//
//   - a value that is not all-ones   the access was routed and something answered. The ATT is
//                                    reachable from the host and the wiring is worth doing.
//   - 0xffffffff                     refused or unmodelled, the same answer the SMC mailbox gives.
//                                    The ATT is not in this path as addressed.
//   - no result, machine stops       the address reached something with special semantics. Say so
//                                    on the emulator channel; do not re-run.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/coordinates/att/configs/grendel_qsr1_att_map.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

using namespace tt::umd;

namespace {

// The flat address is already the whole destination, so the coordinate carries nothing and must
// stay LITERAL to keep TTDevice::resolve_coordinate from translating it. This is the same shape
// the SPA tests use; only the address differs.
constexpr CoreCoord ORIGIN{0, 0, tt::CoreType::UNSPECIFIED, tt::CoordSystem::LITERAL};

// The first functional worker in quasar_32_arch.yaml, one mebibyte into its L1. This exact
// resolution is asserted in test_grendel_att_map.cpp, so a surprise here is the hardware
// disagreeing with the firmware's tables rather than UMD computing the wrong address.
constexpr tt_xy_pair PROBE_CORE{2, 2};
constexpr uint64_t PROBE_OFFSET = 0x100000;
constexpr uint64_t EXPECTED_FLAT_ADDRESS = 0x10000100000ULL;

// What a refused or unmodelled access returns, per the SMC mailbox note in test_quasar_device_io.
constexpr uint32_t NO_ANSWER = 0xffffffffu;

bool probe_requested() {
    const char* enabled = std::getenv("UMD_QUASAR_ATT_PROBE");
    return enabled != nullptr && std::string(enabled) == "1";
}

NocId probe_noc() {
    const char* noc = std::getenv("UMD_QUASAR_ATT_PROBE_NOC");
    return (noc != nullptr && std::string(noc) == "system") ? NocId::SYSTEM_NOC : NocId::NOC0;
}

class GrendelAttProbe : public ::testing::Test {
protected:
    void SetUp() override {
        if (!probe_requested()) {
            GTEST_SKIP() << "Set UMD_QUASAR_ATT_PROBE=1 to issue an access to an address no tool "
                            "has confirmed yet. Read the header first.";
        }

        for (int device_id : PCIDevice::enumerate_devices()) {
            if (PCIDevice(device_id).get_arch() == tt::ARCH::QUASAR) {
                device_ = TTDevice::create(device_id, IODeviceType::PCIe);
                return;
            }
        }
        GTEST_SKIP() << "No Grendel device is bound to the driver.";
    }

    std::unique_ptr<TTDevice> device_;
};

}  // namespace

TEST_F(GrendelAttProbe, AHostAccessReachesANeoThroughTheAtt) {
    const att::EndpointResolver resolver(att::GRENDEL_QSR1_MAP);
    const uint64_t flat_address = resolver.resolve(PROBE_CORE, tt::CoreType::TENSIX, PROBE_OFFSET, sizeof(uint32_t));

    // If this fails the map or the resolver moved under the probe, and the address below is not
    // the one the rest of this file reasons about. Stop rather than issue it.
    ASSERT_EQ(flat_address, EXPECTED_FLAT_ADDRESS)
        << "Resolved address disagrees with test_grendel_att_map.cpp; not issuing an access.";

    const NocId noc = probe_noc();
    RecordProperty("flat_address", std::to_string(flat_address));
    RecordProperty("noc", noc == NocId::SYSTEM_NOC ? "system" : "noc0");

    uint32_t value = 0;
    device_->read_from_device(&value, ORIGIN, flat_address, sizeof(value), noc);

    RecordProperty("value", std::to_string(value));
    testing::Test::RecordProperty("routed", value != NO_ANSWER ? "yes" : "no");

    EXPECT_NE(value, NO_ANSWER) << "Read 0x" << std::hex << flat_address << " through "
                                << (noc == NocId::SYSTEM_NOC ? "SYSTEM_NOC" : "NOC0")
                                << " and got all-ones: the access was refused or the region is "
                                   "unmodelled, so the ATT is not in this path as addressed. Try "
                                   "the other aperture with UMD_QUASAR_ATT_PROBE_NOC before "
                                   "concluding the ATT is unreachable.";
}
