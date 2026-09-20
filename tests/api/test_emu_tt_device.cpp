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
// The mock server hands out a zeroed, fully writable slot for any address its RDL map does not
// cover, so an access outside the register windows round-trips like RAM. That is what makes the
// address assertions below meaningful: the value only comes back if UMD issued it at exactly the
// address a raw chippy transport then reads. This is a chippy-aware translation unit (C++20).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "emu_axi_transport.h"  // chippy
#include "mimir.h"              // chippy
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/coordinates/grendel_noc_address_resolver.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/emu_tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"

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

std::shared_ptr<chippy::transport::TransportInterface> select_mimir(
    const std::shared_ptr<chippy::transport::emu_axi::EmuAxiTransport>& root,
    std::size_t mimir_index,
    std::size_t mimir_count) {
    if (mimir_count == 1) {
        return root;
    }
    return std::make_shared<chippy::transport::emu_axi::MultiChipletEmuAxiTransport>(
        root, "m" + std::to_string(mimir_index));
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
// that core -- verified by reading it back through a raw chippy transport, which shares nothing
// with UMD's path but the server.
TEST(EmuTTDevice, WriteLandsAtTheResolvedFlatAddress) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);

    const CoreCoord smc_core = soc_descriptor.get_cores(CoreType::SMC).front();
    const uint32_t written = 0xC0FFEE01;

    device->write_to_device(&written, smc_core, kSmcSramOffset, sizeof(written));

    uint32_t read_back = 0;
    device->read_from_device(&read_back, smc_core, kSmcSramOffset, sizeof(read_back));
    EXPECT_EQ(read_back, written);

    // Independently confirm the address, not just the round trip: a resolver bug that flattened
    // consistently but wrongly would pass the read-back above on its own.
    // No initialize(): the constructor already connects, and INIT is not a handshake. On a real
    // model the server maps it to a DUT reset (the mimir test server's reinitialize() calls
    // tb.reset()), which would re-run SMC boot and repopulate this SRAM before the read below.
    chippy::transport::emu_axi::EmuAxiTransport raw(server->host, server->port);
    const uint64_t expected =
        GrendelNocAddressResolver(soc_descriptor, EmuTTDevice::mimir_address_windows(soc_descriptor))
            .to_flat_address(smc_core, kSmcSramOffset, NocId::NOC0);
    EXPECT_EQ(raw.read32(expected), written);
    // No teardown(): that sends QUIT, which shuts the server down under the tests that follow.
}

// Consecutive words must not collide -- the cheapest check that the flat address tracks the offset
// rather than being pinned to a window base.
TEST(EmuTTDevice, ConsecutiveOffsetsAddressDistinctWords) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);
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

TEST(EmuTTDevice, CceSramChannelsDoNotAliasThroughChippyMemory) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);
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

    auto raw = std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(server->host, server->port);
    const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
        for (uint32_t location = 0; location < locations_per_channel; ++location) {
            const CoreCoord cce = soc_descriptor.get_dram_core_for_channel(channel, location, CoordSystem::NOC0);
            const uint32_t expected = 0xC0FFEE10 + channel * locations_per_channel + location;
            uint32_t read_back = 0;
            device->read_from_device(&read_back, cce, address, sizeof(read_back));
            EXPECT_EQ(read_back, expected);

            chippy::grendel::Mimir mimir(
                select_mimir(raw, channel, mimir_count),
                chippy::grendel::ChipletMetadata(chippy::grendel::ChipletType::Mimir, channel, channel),
                false);
            EXPECT_EQ(static_cast<uint32_t>(mimir.cce(location).sram[kOffset / sizeof(uint64_t)].read_raw()), expected);
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
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);
    constexpr uint64_t kOffset = kSmcSramOffset + 0x100;
    const uint32_t first = 0x51AC0000;
    const uint32_t second = 0x51AC0001;
    device->write_to_device(&first, smc_cores[0], kOffset, sizeof(first));
    device->write_to_device(&second, smc_cores[1], kOffset, sizeof(second));

    auto raw = std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(server->host, server->port);
    for (std::size_t mimir_index = 0; mimir_index < smc_cores.size(); ++mimir_index) {
        const auto selected = select_mimir(raw, mimir_index, smc_cores.size());
        EXPECT_EQ(selected->read32(kOffset), mimir_index == 0 ? first : second);
    }
}

TEST(EmuTTDevice, CceResetVectorAndAllHartResetUseChippyAccessors) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);
    constexpr uint32_t kResetVector = 0x123400;

    auto raw = std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(server->host, server->port);
    const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
    const uint32_t locations_per_channel = static_cast<uint32_t>(soc_descriptor.get_grid_size(CoreType::DRAM).y);
    for (uint32_t channel = 0; channel < soc_descriptor.get_num_dram_channels(); ++channel) {
        for (uint32_t location = 0; location < locations_per_channel; ++location) {
            const CoreCoord cce = soc_descriptor.get_dram_core_for_channel(channel, location, CoordSystem::TRANSLATED);
            const uint32_t expected_vector = kResetVector + (channel * locations_per_channel + location) * 0x100;
            device->write_to_device_reg(
                &expected_vector, cce, grendel::CCE_RESET_VECTOR_BASE, sizeof(expected_vector), NocId::NOC0);
            device->deassert_risc_reset(cce, RiscType::ALL, false);

            chippy::grendel::Mimir mimir(
                select_mimir(raw, channel, mimir_count),
                chippy::grendel::ChipletMetadata(chippy::grendel::ChipletType::Mimir, channel, channel),
                false);
            auto& chippy_cce = mimir.cce(location);
            for (auto& reset_vector : chippy_cce.registers.tt_cluster_ctrl.reset_vector) {
                EXPECT_EQ(reset_vector.read().get_data(), expected_vector);
            }
            auto reset = chippy_cce.registers.pf_ctrl.reset.read();
            EXPECT_EQ(reset.fields.uncore_reset, 1);
            EXPECT_EQ(reset.fields.core_reset, 0xFF);

            device->assert_risc_reset(cce, RiscType::ALL);
            reset = chippy_cce.registers.pf_ctrl.reset.read();
            EXPECT_EQ(reset.fields.uncore_reset, 1);
            EXPECT_EQ(reset.fields.core_reset, 0);
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
// Set TT_UMD_EMU_DRAM=1 only against a model whose GDDR has been brought up.
TEST(EmuTTDevice, DramCoresDoNotAlias) {
    SKIP_WITHOUT_SERVER(server);
    if (std::getenv("TT_UMD_EMU_DRAM") == nullptr) {
        GTEST_SKIP() << "DRAM needs GDDR bringup; set TT_UMD_EMU_DRAM=1 on a configured model.";
    }

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);

    const uint32_t channel_count = soc_descriptor.get_num_dram_channels();
    constexpr uint64_t kOffset = 0x800;

    for (uint32_t channel = 0; channel < channel_count; ++channel) {
        const uint32_t value = 0xAAAA1111 + channel;
        const CoreCoord dram = soc_descriptor.get_dram_core_for_channel(channel, 0, CoordSystem::NOC0);
        device->write_to_device(&value, dram, kOffset, sizeof(value));
    }

    auto raw = std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(server->host, server->port);
    const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
        const uint32_t expected = 0xAAAA1111 + channel;
        const CoreCoord dram = soc_descriptor.get_dram_core_for_channel(channel, 0, CoordSystem::NOC0);
        uint32_t read_back = 0;
        device->read_from_device(&read_back, dram, kOffset, sizeof(read_back));
        EXPECT_EQ(read_back, expected);
        const auto selected = select_mimir(raw, channel, mimir_count);
        EXPECT_EQ(selected->read32(chippy::grendel::kMimirGddrDramLocalAddr + kOffset), expected);
    }
}

}  // namespace tt::umd::test
