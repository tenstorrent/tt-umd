/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// End-to-end tests for EmuTTDevice: a TTDevice read/write must leave this process as an
// emu_axi access at the flat SPA the Grendel ATT expects.
//
// These need a live emu_axi command server and are skipped unless TT_UMD_EMU_SERVER is set
// to host:port. Any server speaking the protocol works; the cheap one is chippy's register-map
// backed mock, which needs no emulator:
//
//   python lib/transport/emu_axi_transport/emu_server/mock_server.py --map mimir_soc --port 8081
//   TT_UMD_EMU_SERVER=127.0.0.1:8081 ./api_tests --gtest_filter='EmuTTDevice.*'
//
// Against a SiVal server, bring the model up first (bringup_mimir_1x1.py) and point the test at
// that host:port. These tests do not send INIT: on SiVal it re-runs reset and drops that bring-up,
// after which a CCE or GDDR access stalls the AXI master. Set TT_UMD_EMU_INIT=1 to send INIT
// (the chippy mock treats it as a register-map reset).
//
// The mock server hands out a zeroed, fully writable slot for any address its RDL map does not
// cover, so an access outside the register windows round-trips like RAM. Address checks read that
// slot back through EmuTTDevice at the flat address the public resolver names.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/coordinates/grendel_noc_address_resolver.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/emu_tt_device.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/risc_type.hpp"

namespace tt::umd::test {

namespace {

struct ServerEndpoint {
    std::string host;
    uint32_t port;
};

std::optional<ServerEndpoint> endpoint_from_env() {
    const char* spec = std::getenv("TT_UMD_EMU_SERVER");
    if (spec == nullptr) {
        return std::nullopt;
    }
    const std::string text(spec);
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    return ServerEndpoint{text.substr(0, colon), static_cast<uint32_t>(std::stoul(text.substr(colon + 1)))};
}

SocDescriptor mimir_descriptor() {
    if (const char* path = std::getenv("TT_UMD_EMU_SOC_DESC")) {
        return SocDescriptor(std::make_shared<SocArchDescriptor>(path));
    }
    return SocDescriptor(std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mimir_1x1.yaml")));
}

bool send_init_on_connect() {
    // Default is attach. INIT on the SiVal server resets the DUT.
    return std::getenv("TT_UMD_EMU_INIT") != nullptr;
}

std::unique_ptr<EmuTTDevice> open_device(const SocDescriptor& soc_descriptor, const ServerEndpoint& server) {
    return EmuTTDevice::create(soc_descriptor, server.host, server.port, send_init_on_connect());
}

GrendelNocAddressResolver resolver_for(const SocDescriptor& soc_descriptor) {
    return GrendelNocAddressResolver(soc_descriptor, EmuTTDevice::mimir_address_windows(soc_descriptor));
}

uint64_t read_flat(EmuTTDevice& device, uint64_t address) {
    uint64_t value = 0;
    device.read_flat_address(address, &value, sizeof(value));
    return value;
}

}  // namespace

#define SKIP_WITHOUT_SERVER(endpoint)                                        \
    auto endpoint = endpoint_from_env();                                     \
    if (!endpoint.has_value()) {                                             \
        GTEST_SKIP() << "Set TT_UMD_EMU_SERVER=host:port to run this test."; \
    }

// Mimir's SMC is the only thing reachable on a freshly reset DUT, so it is what the default tests
// use. Its SRAM appears at 0x0006_0000 in the AXI view the emu server drives (the emulation rules'
// "AXI bus SRAM 0x0006_0000-0x0016_0000"; 0xC006_0000 is the same memory through the SMC's
// core-local alias, which is what firmware download and reset vectors use).
constexpr uint64_t kSmcSramOffset = 0x00060000;

// A write through the public TTDevice API must land at the flat address the resolver derives for
// that core. The flat read below is that address on the same device.
TEST(EmuTTDevice, WriteLandsAtTheResolvedFlatAddress) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = open_device(soc_descriptor, *server);

    const CoreCoord smc_core = soc_descriptor.get_cores(CoreType::SMC).front();
    const uint32_t written = 0xC0FFEE01;

    device->write_to_device(&written, smc_core, kSmcSramOffset, sizeof(written));

    uint32_t read_back = 0;
    device->read_from_device(&read_back, smc_core, kSmcSramOffset, sizeof(read_back));
    EXPECT_EQ(read_back, written);

    // The flat read uses the address the public resolver names. A device that stored the word at a
    // different address fails here even though its own core read above succeeds.
    const uint64_t expected = resolver_for(soc_descriptor).to_flat_address(smc_core, kSmcSramOffset, NocId::NOC0);
    EXPECT_EQ(static_cast<uint32_t>(read_flat(*device, expected)), written);
}

// Consecutive words must not collide -- the cheapest check that the flat address tracks the offset
// rather than being pinned to a window base.
TEST(EmuTTDevice, ConsecutiveOffsetsAddressDistinctWords) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = open_device(soc_descriptor, *server);
    const CoreCoord smc_core = soc_descriptor.get_cores(CoreType::SMC).front();

    const uint32_t first = 0xAAAA1111;
    const uint32_t second = 0xBBBB2222;
    device->write_to_device(&first, smc_core, kSmcSramOffset, sizeof(first));
    device->write_to_device(&second, smc_core, kSmcSramOffset + sizeof(second), sizeof(second));

    uint32_t read_first = 0;
    uint32_t read_second = 0;
    device->read_from_device(&read_first, smc_core, kSmcSramOffset, sizeof(read_first));
    device->read_from_device(&read_second, smc_core, kSmcSramOffset + sizeof(read_second), sizeof(read_second));

    EXPECT_EQ(read_first, first);
    EXPECT_EQ(read_second, second);
}

TEST(EmuTTDevice, CceSramChannelsDoNotAlias) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = open_device(soc_descriptor, *server);
    const auto windows = EmuTTDevice::mimir_address_windows(soc_descriptor);
    constexpr uint64_t kOffset = 0x800;
    const uint64_t address = windows.dram_l1_noc_offset + kOffset;

    const uint32_t channel_count = soc_descriptor.get_num_dram_channels();
    const uint32_t locations_per_channel = static_cast<uint32_t>(soc_descriptor.get_grid_size(CoreType::DRAM).y);
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
        for (uint32_t location = 0; location < locations_per_channel; ++location) {
            const CoreCoord cce = soc_descriptor.get_dram_core_for_channel(channel, location, CoordSystem::NOC0);
            const uint32_t value = 0xC0FFEE10 + channel * locations_per_channel + location;
            device->write_to_device(&value, cce, address, sizeof(value));
        }
    }

    for (uint32_t channel = 0; channel < channel_count; ++channel) {
        for (uint32_t location = 0; location < locations_per_channel; ++location) {
            const CoreCoord cce = soc_descriptor.get_dram_core_for_channel(channel, location, CoordSystem::NOC0);
            const uint32_t expected = 0xC0FFEE10 + channel * locations_per_channel + location;
            uint32_t read_back = 0;
            device->read_from_device(&read_back, cce, address, sizeof(read_back));
            EXPECT_EQ(read_back, expected);
            const uint64_t flat = resolver_for(soc_descriptor).to_flat_address(cce, address, NocId::NOC0);
            EXPECT_EQ(static_cast<uint32_t>(read_flat(*device, flat)), expected);
        }
    }
}

TEST(EmuTTDevice, SmcSramDoesNotAliasAcrossMimirs) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    const auto smc_cores = soc_descriptor.get_cores(CoreType::SMC, CoordSystem::NOC0);
    if (smc_cores.size() < 2) {
        GTEST_SKIP() << "Requires the dual-Mimir package descriptor.";
    }
    auto device = open_device(soc_descriptor, *server);
    constexpr uint64_t kOffset = kSmcSramOffset + 0x100;
    const uint32_t first = 0x51AC0000;
    const uint32_t second = 0x51AC0001;
    device->write_to_device(&first, smc_cores[0], kOffset, sizeof(first));
    device->write_to_device(&second, smc_cores[1], kOffset, sizeof(second));

    const GrendelNocAddressResolver resolver = resolver_for(soc_descriptor);
    for (std::size_t mimir_index = 0; mimir_index < smc_cores.size(); ++mimir_index) {
        uint32_t read_back = 0;
        device->read_from_device(&read_back, smc_cores[mimir_index], kOffset, sizeof(read_back));
        const uint32_t expected = mimir_index == 0 ? first : second;
        EXPECT_EQ(read_back, expected);
        const uint64_t flat = resolver.to_flat_address(smc_cores[mimir_index], kOffset, NocId::NOC0);
        EXPECT_EQ(static_cast<uint32_t>(read_flat(*device, flat)), expected);
    }
}

TEST(EmuTTDevice, CceHartMaskHoldsOnlyTheSelectedHart) {
    // ALL_NEO_DMS is DM0|..|DM7. A single DM must not take the all-hart path.
    EXPECT_EQ(grendel::cce_hart_release_bits(RiscType::DM0), 1ULL << 1);
    EXPECT_EQ(grendel::cce_hart_release_bits(RiscType::DM7), 1ULL << 8);
    EXPECT_EQ(grendel::cce_hart_release_bits(RiscType::ALL) >> 1, 0xFFULL);
    EXPECT_EQ(grendel::cce_hart_release_bits(RiscType::ALL_NEO_DMS) >> 1, 0xFFULL);
}

TEST(EmuTTDevice, CceResetVectorAndAllHartResetUseTheDevice) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = open_device(soc_descriptor, *server);
    const GrendelNocAddressResolver resolver = resolver_for(soc_descriptor);
    constexpr uint32_t kResetVector = 0x123400;
    // Bit 0 is the uncore. Bits 1-8 are harts 0-7. 1 means released.
    auto hart_mask = [](uint64_t reset) { return (reset >> 1) & 0xFFULL; };

    const uint32_t locations_per_channel = static_cast<uint32_t>(soc_descriptor.get_grid_size(CoreType::DRAM).y);
    const auto& channels = soc_descriptor.get_arch_descriptor().get_mimir_channels();
    for (uint32_t channel = 0; channel < soc_descriptor.get_num_dram_channels(); ++channel) {
        for (uint32_t location = 0; location < locations_per_channel; ++location) {
            const CoreCoord cce = soc_descriptor.get_dram_core_for_channel(channel, location, CoordSystem::TRANSLATED);
            const uint32_t expected_vector = kResetVector + (channel * locations_per_channel + location) * 0x100;
            // The vector is a register write. Leave the harts held: releasing them fetches this
            // address, and on a SiVal model that fetch never completes. The host AXI master then
            // reports WR_STALL for every later client, including a Metal SRAM write at 0x40000800.
            device->write_to_device_reg(
                &expected_vector, cce, grendel::CCE_RESET_VECTOR_BASE, sizeof(expected_vector), NocId::NOC0);

            // Hart 0 is broadcast to every hart.
            for (uint32_t hart = 0; hart < grendel::CCE_NUM_HARTS; ++hart) {
                const uint64_t offset = grendel::CCE_RESET_VECTOR_BASE + hart * grendel::CCE_HART_RESET_VECTOR_STRIDE;
                EXPECT_EQ(read_flat(*device, resolver.cce_control_address(cce, offset)), expected_vector);
            }

            // Bring-up left every hart held and the uncore released. Holding one hart must not
            // clear the uncore or the other harts.
            device->assert_risc_reset(cce, RiscType::DM0);
            uint64_t reset = read_flat(*device, resolver.cce_control_address(cce, grendel::CCE_PF_CTRL_RESET_BASE));
            EXPECT_EQ(reset & grendel::CCE_UNCORE_RELEASED, grendel::CCE_UNCORE_RELEASED);
            EXPECT_EQ(hart_mask(reset), 0u);

            // Releasing harts, or asserting RiscType::ALL (which holds the uncore), closes CCE
            // SRAM until the model is brought up again. The mock has no harts and no AXI slave,
            // so the full mask check stays there. TT_UMD_EMU_INIT=1 selects that path.
            if (!send_init_on_connect()) {
                continue;
            }
            device->deassert_risc_reset(cce, RiscType::ALL, false);
            reset = read_flat(*device, resolver.cce_control_address(cce, grendel::CCE_PF_CTRL_RESET_BASE));
            EXPECT_EQ(reset & grendel::CCE_UNCORE_RELEASED, grendel::CCE_UNCORE_RELEASED);
            EXPECT_EQ(hart_mask(reset), 0xFFu);
            // Full release reprograms DMRISC while the harts are still held. Entry 2 is this
            // Mimir's GDDR window. The register stores the byte address.
            uint32_t instance = channel;
            for (const auto& entry : channels) {
                if (entry.channel == channel) {
                    instance = entry.instance;
                    break;
                }
            }
            const CceDmriscRemapEntry gddr = cce_dmrisc_remap_entries(GrendelAddressWindows{}, instance)[2];
            const uint64_t entry = grendel::CCE_DMRISC_REMAP_BASE + 2 * grendel::CCE_DMRISC_REMAP_ENTRY_STRIDE;
            EXPECT_EQ(read_flat(*device, resolver.cce_control_address(cce, entry + 0x00)), gddr.region_start);
            EXPECT_EQ(read_flat(*device, resolver.cce_control_address(cce, entry + 0x08)), gddr.region_end);
            EXPECT_EQ(read_flat(*device, resolver.cce_control_address(cce, entry + 0x10)), gddr.local_base);
            EXPECT_EQ(read_flat(*device, resolver.cce_control_address(cce, entry + 0x18)), 1u);

            // A single hart follows the worker rule: the uncore bit stays as it is.
            device->assert_risc_reset(cce, RiscType::DM0);
            reset = read_flat(*device, resolver.cce_control_address(cce, grendel::CCE_PF_CTRL_RESET_BASE));
            EXPECT_EQ(reset & grendel::CCE_UNCORE_RELEASED, grendel::CCE_UNCORE_RELEASED);
            EXPECT_EQ(hart_mask(reset), 0xFEu);

            device->assert_risc_reset(cce, RiscType::ALL);
            reset = read_flat(*device, resolver.cce_control_address(cce, grendel::CCE_PF_CTRL_RESET_BASE));
            EXPECT_EQ(reset & grendel::CCE_UNCORE_RELEASED, 0u);
            EXPECT_EQ(hart_mask(reset), 0u);
            // Holding the uncore closes the host path to this CCE. Put it back for the mock.
            device->release_cce_uncore_for_firmware_load(cce);
        }
    }
}

// DRAM is opt-in, and deliberately so.
//
// The SiVal server runs preload and reset only -- test_sival_server.py skips tb.configure() -- so
// GDDR has had no bringup and its window answers nothing. On a real model an access there does not
// merely fail: the testbench AXI master stalls with no slave responding (`axi4m WR_STALL stalled at
// 0x800000400, AWREADY=0`), the server drops the client, and the stalled transaction survives the
// server's own DUT re-init -- every later access, reads included, reports the same stale stall. One
// stray DRAM write wedges the model for the rest of the session.
//
// Set TT_UMD_EMU_DRAM=1 only against a model whose GDDR has been brought up, and leave
// TT_UMD_EMU_INIT unset so that bring-up is still in place.
TEST(EmuTTDevice, DramCoresDoNotAlias) {
    SKIP_WITHOUT_SERVER(server);
    if (std::getenv("TT_UMD_EMU_DRAM") == nullptr) {
        GTEST_SKIP() << "DRAM needs GDDR bringup; set TT_UMD_EMU_DRAM=1 on a configured model.";
    }

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = open_device(soc_descriptor, *server);

    const uint32_t channel_count = soc_descriptor.get_num_dram_channels();
    constexpr uint64_t kOffset = 0x800;

    for (uint32_t channel = 0; channel < channel_count; ++channel) {
        const uint32_t value = 0xAAAA1111 + channel;
        const CoreCoord dram = soc_descriptor.get_dram_core_for_channel(channel, 0, CoordSystem::NOC0);
        device->write_to_device(&value, dram, kOffset, sizeof(value));
    }

    const GrendelNocAddressResolver resolver = resolver_for(soc_descriptor);
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
        const uint32_t expected = 0xAAAA1111 + channel;
        const CoreCoord dram = soc_descriptor.get_dram_core_for_channel(channel, 0, CoordSystem::NOC0);
        uint32_t read_back = 0;
        device->read_from_device(&read_back, dram, kOffset, sizeof(read_back));
        EXPECT_EQ(read_back, expected);
        const uint64_t flat = resolver.to_flat_address(dram, kOffset, NocId::NOC0);
        EXPECT_EQ(static_cast<uint32_t>(read_flat(*device, flat)), expected);
    }
}

}  // namespace tt::umd::test
