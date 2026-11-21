/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Unit tests for the GrendelJtagProtocol routing/glue: core->transport dispatch,
// per-core caching, the NOC0 guard, and chippy-exception translation, and for the
// TAP each GrendelJtagTTDevice chiplet is opened on. These do
// NOT exercise chippy's SMN/transport internals — a chippy MockTransportInterface
// stands in for the per-core transport (that path is covered by the integration
// test). This is a chippy-aware translation unit (C++20).

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "grendel_jtag_protocol_impl.hpp"   // internal: SmnTransportProvider, GrendelJtagProtocolTestAccess
#include "grendel_jtag_tt_device_impl.hpp"  // internal: GrendelJtagTTDeviceFactory
#include "mock_transport.h"                 // chippy
#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/protocol/grendel_jtag_protocol.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd::test {

using chippy::transport::MockTransportInterface;

TEST(GrendelJtagProtocol, RoundTripsBytesPerCore) {
    auto mock = std::make_shared<MockTransportInterface>();
    auto proto = GrendelJtagProtocolTestAccess::make([mock](tt_xy_pair) { return mock; });

    uint32_t in = 0xDEADBEEF;
    uint32_t out = 0;
    proto->write_data(&in, tt_xy_pair(1, 1), 0x100, sizeof(in), NocId::NOC0);
    proto->read_data(&out, tt_xy_pair(1, 1), 0x100, sizeof(out), NocId::NOC0);
    EXPECT_EQ(out, 0xDEADBEEFu);
}

TEST(GrendelJtagProtocol, CachesTransportPerCore) {
    int provider_calls = 0;
    auto proto = GrendelJtagProtocolTestAccess::make([&provider_calls](tt_xy_pair) {
        ++provider_calls;
        return std::make_shared<MockTransportInterface>();
    });

    uint32_t v = 0;
    proto->read_data(&v, tt_xy_pair(1, 1), 0x0, sizeof(v), NocId::NOC0);  // new core -> provider called
    proto->read_data(&v, tt_xy_pair(1, 1), 0x4, sizeof(v), NocId::NOC0);  // same core -> cached
    proto->read_data(&v, tt_xy_pair(2, 1), 0x0, sizeof(v), NocId::NOC0);  // new core -> provider called
    EXPECT_EQ(provider_calls, 2);
}

TEST(GrendelJtagProtocol, RejectsNonNoc0) {
    auto proto =
        GrendelJtagProtocolTestAccess::make([](tt_xy_pair) { return std::make_shared<MockTransportInterface>(); });

    uint32_t v = 0;
    EXPECT_THROW(proto->read_data(&v, tt_xy_pair(1, 1), 0x0, sizeof(v), NocId::NOC1), std::exception);
    EXPECT_THROW(proto->write_data(&v, tt_xy_pair(1, 1), 0x0, sizeof(v), NocId::NOC1), std::exception);
}

// The mock throws in order to exercise the translation path, which is NOT how a real bus error
// arrives: chippy logs an AXI SLVERR/DECERR and returns normally. This test therefore covers
// alignment rejections and transport failures, not bus errors. See grendel_jtag_protocol.cpp.
TEST(GrendelJtagProtocol, TranslatesTransportErrors) {
    struct ThrowingTransport : MockTransportInterface {
        std::uint32_t read32(std::uint64_t) override { throw std::runtime_error("boom"); }
    };

    auto proto = GrendelJtagProtocolTestAccess::make([](tt_xy_pair) { return std::make_shared<ThrowingTransport>(); });

    uint32_t v = 0;
    EXPECT_THROW(proto->read_data(&v, tt_xy_pair(1, 1), 0x100, sizeof(v), NocId::NOC0), std::exception);
}

TEST(GrendelJtagProtocol, CtrlAccessesValidateAlignmentThenShareTheDataPath) {
    auto mock = std::make_shared<MockTransportInterface>();
    auto proto = GrendelJtagProtocolTestAccess::make([mock](tt_xy_pair) { return mock; });

    uint32_t in = 0x5A5A5A5A;
    uint32_t out = 0;
    proto->write_ctrl(&in, tt_xy_pair(1, 1), 0x200, sizeof(in), NocId::NOC0, RegisterAccessWidth::BITS32);
    proto->read_ctrl(&out, tt_xy_pair(1, 1), 0x200, sizeof(out), NocId::NOC0, RegisterAccessWidth::BITS32);
    EXPECT_EQ(out, 0x5A5A5A5Au);

    // Register accesses must be 4-byte aligned in both address and size.
    EXPECT_THROW(
        proto->read_ctrl(&out, tt_xy_pair(1, 1), 0x202, sizeof(out), NocId::NOC0, RegisterAccessWidth::BITS32),
        std::exception);
    EXPECT_THROW(
        proto->write_ctrl(&in, tt_xy_pair(1, 1), 0x200, 3, NocId::NOC0, RegisterAccessWidth::BITS32), std::exception);
}

TEST(GrendelJtagProtocol, WriteToCoreRangeReturnsFalse) {
    auto proto =
        GrendelJtagProtocolTestAccess::make([](tt_xy_pair) { return std::make_shared<MockTransportInterface>(); });

    uint32_t v = 0;
    EXPECT_FALSE(proto->write_to_core_range(&v, tt_xy_pair(1, 1), tt_xy_pair(2, 2), 0x0, sizeof(v), NocId::NOC0));
}

// Stands in for OpenOCD: one mock transport per TAP, in the order the device opens them.
struct MockTaps {
    std::vector<uint32_t> opened;
    std::map<uint32_t, std::shared_ptr<MockTransportInterface>> by_tap;

    GrendelJtagTransportFactory factory() {
        return [this](uint32_t tap) {
            opened.push_back(tap);
            auto mock = std::make_shared<MockTransportInterface>();
            by_tap[tap] = mock;
            return mock;
        };
    }

    // The TAPs whose transport saw any access.
    std::vector<uint32_t> touched() const {
        std::vector<uint32_t> taps;
        for (const auto& [tap, mock] : by_tap) {
            if (!mock->mem.empty()) {
                taps.push_back(tap);
            }
        }
        return taps;
    }

    void clear() {
        for (auto& [tap, mock] : by_tap) {
            mock->mem.clear();
        }
    }
};

TEST(GrendelJtagTTDevice, MmkOpensEachChipletOnItsPackageTap) {
    const SocDescriptor soc_descriptor(std::make_shared<SocArchDescriptor>(test_utils::get_soc_desc_path("mmk.yaml")));
    MockTaps taps;
    auto device = GrendelJtagTTDeviceFactory::create(soc_descriptor, 0, taps.factory());

    // chippy's MMK chain is k0, m0, m1. Transports are opened Mimir channels first, then Keraunos.
    EXPECT_EQ(taps.opened, (std::vector<uint32_t>{1, 2, 0}));
    EXPECT_EQ(device->get_communication_device_id(), 0);

    const uint32_t value = 0xC0FFEE00;
    const std::vector<std::pair<CoreCoord, uint32_t>> expected = {
        {soc_descriptor.get_dram_core_for_channel(0, 0, CoordSystem::NOC0), 1},
        {soc_descriptor.get_dram_core_for_channel(1, 0, CoordSystem::NOC0), 2},
        {soc_descriptor.get_cores(CoreType::ETH).front(), 0},
    };
    for (const auto& [core, tap] : expected) {
        taps.clear();
        device->write_to_device(&value, core, 0x100, sizeof(value));
        EXPECT_EQ(taps.touched(), std::vector<uint32_t>{tap}) << core.str();
    }
}

TEST(GrendelJtagTTDevice, OneMimirWithoutATapUsesTheCallerChiplet) {
    const SocDescriptor soc_descriptor(
        std::make_shared<SocArchDescriptor>(test_utils::get_soc_desc_path("mimir_1x1.yaml")));
    ASSERT_FALSE(soc_descriptor.get_arch_descriptor().get_mimir_channels()[0].tap.has_value());
    MockTaps taps;
    auto device = GrendelJtagTTDeviceFactory::create(soc_descriptor, 3, taps.factory());
    EXPECT_EQ(taps.opened, std::vector<uint32_t>{3});
    EXPECT_EQ(device->get_communication_device_id(), 3);
}

TEST(GrendelJtagTTDevice, TwoMimirsWithoutTapsAreRefused) {
    std::ifstream in(test_utils::get_soc_desc_path("mimir_2x_package.yaml"));
    std::stringstream yaml;
    yaml << in.rdbuf();
    const std::string untapped = std::regex_replace(yaml.str(), std::regex("\n    tap: [0-9]+"), "");
    ASSERT_NE(untapped, yaml.str());
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "mimir_2x_package_untapped.yaml";
    std::ofstream(path) << untapped;

    const SocDescriptor soc_descriptor(std::make_shared<SocArchDescriptor>(path.string()));
    MockTaps taps;
    EXPECT_THROW(GrendelJtagTTDeviceFactory::create(soc_descriptor, 0, taps.factory()), std::exception);
    EXPECT_TRUE(taps.opened.empty());
    std::filesystem::remove(path);
}

}  // namespace tt::umd::test
