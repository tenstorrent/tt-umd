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
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/coordinates/grendel_noc_address_resolver.hpp"
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

SocDescriptor mimir_descriptor() {
    return SocDescriptor(std::make_shared<SocArchDescriptor>(test_utils::GetSocDescAbsPath("mimir_1x1.yaml")));
}

}  // namespace

#define SKIP_WITHOUT_SERVER(endpoint)                                                  \
    auto endpoint = endpoint_from_env();                                               \
    if (!endpoint.has_value()) {                                                       \
        GTEST_SKIP() << "Set TT_UMD_EMU_SERVER=host:port to run this test.";   \
    }

// A write through the public TTDevice API must land at the flat address the resolver derives for
// that core -- verified by reading it back through a raw chippy transport, which shares nothing
// with UMD's path but the server.
TEST(EmuTTDevice, WriteLandsAtTheResolvedFlatAddress) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);

    const CoreCoord dram_core = soc_descriptor.get_cores(CoreType::DRAM).front();
    constexpr uint64_t kOffset = 0x400;
    const uint32_t written = 0xC0FFEE01;

    device->write_to_device(&written, dram_core, kOffset, sizeof(written));

    uint32_t read_back = 0;
    device->read_from_device(&read_back, dram_core, kOffset, sizeof(read_back));
    EXPECT_EQ(read_back, written);

    // Independently confirm the address, not just the round trip: a resolver bug that flattened
    // consistently but wrongly would pass the read-back above on its own.
    chippy::transport::emu_axi::EmuAxiTransport raw(server->host, server->port);
    raw.initialize();
    const uint64_t expected = GrendelNocAddressResolver(soc_descriptor).to_flat_address(dram_core, kOffset, NocId::NOC0);
    EXPECT_EQ(raw.read32(expected), written);
    // No teardown(): that sends QUIT, which shuts the server down under the tests that follow.
}

// The two DRAM cores are separate GDDR instances and must not alias: chippy spaces the Mimir GDDR
// tiles 128 GiB apart in SPA, not by the 8 GiB bank size.
TEST(EmuTTDevice, DramCoresDoNotAlias) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);

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

// The SMC is reached through the Mimir config aperture rather than a DRAM window, so it must not
// collide with either DRAM core.
TEST(EmuTTDevice, SmcUsesTheConfigApertureNotADramWindow) {
    SKIP_WITHOUT_SERVER(server);

    const SocDescriptor soc_descriptor = mimir_descriptor();
    auto device = EmuTTDevice::create(soc_descriptor, server->host, server->port);

    const CoreCoord smc_core = soc_descriptor.get_cores(CoreType::SMC).front();
    const CoreCoord dram_core = soc_descriptor.get_cores(CoreType::DRAM).front();
    constexpr uint64_t kOffset = 0x40;

    const uint32_t smc_value = 0x5A5A5A5A;
    const uint32_t dram_value = 0xA5A5A5A5;
    device->write_to_device(&smc_value, smc_core, kOffset, sizeof(smc_value));
    device->write_to_device(&dram_value, dram_core, kOffset, sizeof(dram_value));

    uint32_t read_smc = 0;
    device->read_from_device(&read_smc, smc_core, kOffset, sizeof(read_smc));
    // The SMC write must survive the DRAM write to the same core-local offset.
    EXPECT_EQ(read_smc, smc_value);
}

}  // namespace tt::umd::test
