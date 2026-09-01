/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Unit tests for the GrendelJtagProtocol routing/glue: core->transport dispatch,
// per-core caching, the NOC0 guard, and chippy-exception translation. These do
// NOT exercise chippy's SMN/transport internals — a chippy MockTransportInterface
// stands in for the per-core transport (that path is covered by the integration
// test). This is a chippy-aware translation unit (C++20).

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>

#include "grendel_jtag_protocol_impl.hpp"  // internal: SmnTransportProvider, GrendelJtagProtocolTestAccess
#include "mock_transport.h"                // chippy
#include "umd/device/tt_device/protocol/grendel_jtag_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

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
    proto->write_ctrl(&in, tt_xy_pair(1, 1), 0x200, sizeof(in), NocId::NOC0);
    proto->read_ctrl(&out, tt_xy_pair(1, 1), 0x200, sizeof(out), NocId::NOC0);
    EXPECT_EQ(out, 0x5A5A5A5Au);

    // Register accesses must be 4-byte aligned in both address and size.
    EXPECT_THROW(proto->read_ctrl(&out, tt_xy_pair(1, 1), 0x202, sizeof(out), NocId::NOC0), std::exception);
    EXPECT_THROW(proto->write_ctrl(&in, tt_xy_pair(1, 1), 0x200, 3, NocId::NOC0), std::exception);
}

TEST(GrendelJtagProtocol, WriteToCoreRangeReturnsFalse) {
    auto proto =
        GrendelJtagProtocolTestAccess::make([](tt_xy_pair) { return std::make_shared<MockTransportInterface>(); });

    uint32_t v = 0;
    EXPECT_FALSE(proto->write_to_core_range(&v, tt_xy_pair(1, 1), tt_xy_pair(2, 2), 0x0, sizeof(v), NocId::NOC0));
}

}  // namespace tt::umd::test
