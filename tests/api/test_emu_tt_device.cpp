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
#include <iomanip>
#include <memory>
#include <optional>
#include <string>

#include "emu_axi_transport.h"      // chippy
#include "jtag2axi_v2_transport.h"  // chippy
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/coordinates/att/att_resolver.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/emu_tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

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

// Which transport the run exercises. emu_axi by default; TT_UMD_EMU_TRANSPORT=jtag points at an
// OpenOCD already attached to the model's jtag_vpi server, so the emulated TAP carries the access
// instead of the AXI transactor.
EmuTTDevice::Transport transport_from_env() {
    const char* name = std::getenv("TT_UMD_EMU_TRANSPORT");
    return (name != nullptr && std::string(name) == "jtag") ? EmuTTDevice::Transport::Jtag2Axi
                                                            : EmuTTDevice::Transport::EmuAxi;
}

bool jtag_mode() { return transport_from_env() == EmuTTDevice::Transport::Jtag2Axi; }

// A second, independent transport of the same kind, used to confirm the address UMD issued.
std::unique_ptr<chippy::transport::TransportInterface> raw_transport(const ServerEndpoint& server) {
    if (jtag_mode()) {
        return std::make_unique<chippy::transport::jtag2axi::v2::Jtag2AxiV2Transport>(
            server.host, static_cast<uint16_t>(server.port));
    }
    auto emu = std::make_unique<chippy::transport::emu_axi::EmuAxiTransport>(server.host, server.port);
    emu->initialize();
    return emu;
}

SocDescriptor mimir_descriptor() {
    return SocDescriptor(std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mimir_1x1.yaml")));
}

}  // namespace

#define SKIP_WITHOUT_SERVER(endpoint)                                                  \
    auto endpoint = endpoint_from_env();                                               \
    if (!endpoint.has_value()) {                                                       \
        GTEST_SKIP() << "Set TT_UMD_EMU_SERVER=host:port to run this test.";   \
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
    auto device = EmuTTDevice::create(soc_descriptor, transport_from_env(), server->host, server->port);

    const CoreCoord smc_core = soc_descriptor.get_cores(CoreType::SMC).front();
    const uint32_t written = 0xC0FFEE01;

    device->write_to_device(&written, smc_core, kSmcSramOffset, sizeof(written));

    uint32_t read_back = 0;
    device->read_from_device(&read_back, smc_core, kSmcSramOffset, sizeof(read_back));
    EXPECT_EQ(read_back, written);

    // Independently confirm the address, not just the round trip: a resolver bug that flattened
    // consistently but wrongly would pass the read-back above on its own.
    auto raw = raw_transport(*server);
    const uint64_t expected =
        att::resolve_core(
            att::Resolver(EmuTTDevice::mimir_att_map(soc_descriptor)),
            soc_descriptor,
            smc_core,
            kSmcSramOffset,
            sizeof(written));
    EXPECT_EQ(raw->read32(expected), written);
    // No teardown(): that sends QUIT, which shuts the server down under the tests that follow.
}

// Consecutive words must not collide -- the cheapest check that the flat address tracks the offset
// rather than being pinned to a window base.
TEST(EmuTTDevice, ConsecutiveOffsetsAddressDistinctWords) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, transport_from_env(), server->host, server->port);
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

// The UMD counterpart of chippy's scratch_reg validation test, so the two stacks can be compared
// doing the same thing to the same register. chippy resolves it through the RDL register map
// (chiplet.smc().registers.smc_cpu_ctrl.scratch[index]); UMD has no register map, so the address is
// the AXI-view one the emulation rules give for Mimir SMC scratch[0] -- 0x0001_0100, the same
// memory 0xC001_0100 reaches through the SMC core-local alias.
//
// The value carries a die ordinal exactly as chippy's does. On a single-Mimir package it cannot
// catch mis-routing the way it does across a multi-die one, but keeping the shape identical means
// the test extends to mmk without changing what it asserts.
constexpr uint64_t kSmcScratch0Offset = 0x00010100;
constexpr uint32_t kScratchUniqueValue = 0xA5A50000;  // chippy's unique_value_base, Mimir instance 0

TEST(EmuTTDevice, SmcScratchRegisterRoundTrip) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, transport_from_env(), server->host, server->port);
    const CoreCoord smc_core = soc_descriptor.get_cores(CoreType::SMC).front();

    uint32_t initial = 0;
    device->read_from_device(&initial, smc_core, kSmcScratch0Offset, sizeof(initial));

    device->write_to_device(&kScratchUniqueValue, smc_core, kSmcScratch0Offset, sizeof(kScratchUniqueValue));

    uint32_t readback = 0;
    device->read_from_device(&readback, smc_core, kSmcScratch0Offset, sizeof(readback));
    EXPECT_EQ(readback, kScratchUniqueValue)
        << "scratch[0] initial was 0x" << std::hex << initial;

    // Restore, so a later run does not start from this test's value.
    device->write_to_device(&initial, smc_core, kSmcScratch0Offset, sizeof(initial));
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
    auto device = EmuTTDevice::create(soc_descriptor, transport_from_env(), server->host, server->port);

    const std::vector<CoreCoord> dram_cores = soc_descriptor.get_cores(CoreType::DRAM);
    ASSERT_EQ(dram_cores.size(), 2);
    constexpr uint64_t kOffset = 0x800;

    const uint32_t first = 0xAAAA1111;
    const uint32_t second = 0xBBBB2222;
    device->write_to_device(&first, dram_cores[0], kOffset, sizeof(first));
    device->write_to_device(&second, dram_cores[1], kOffset, sizeof(second));

    uint32_t read_first = 0;
    uint32_t read_second = 0;
    device->read_from_device(&read_first, dram_cores[0], kOffset, sizeof(read_first));
    device->read_from_device(&read_second, dram_cores[1], kOffset, sizeof(read_second));

    EXPECT_EQ(read_first, first);
    EXPECT_EQ(read_second, second);
}

}  // namespace tt::umd::test
