// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstring>

#include "umd/device/simulation/simulation_server_api.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt::umd;

namespace {

// A trivial in-memory implementation, demonstrating that a client can be written against the
// API contract. Stands in for the real socket-backed client (#2795) until it lands.
class FakeSimulationServerClient : public SimulationServerApi {
public:
    SimulationDeviceDescription attach() override {
        attached_ = true;
        return {/*arch=*/1, /*board=*/1, /*num_chips=*/1};
    }

    void detach() override { attached_ = false; }

    void read_from_device(CoreCoord, uint64_t address, void* dst, size_t size) override {
        std::memcpy(dst, &storage_[address], size);
    }

    void write_to_device(CoreCoord, uint64_t address, const void* src, size_t size) override {
        std::memcpy(&storage_[address], src, size);
    }

    bool attached() const { return attached_; }

private:
    bool attached_ = false;
    uint8_t storage_[256] = {};
};

}  // namespace

TEST(SimulationServerApi, ClientCanImplementAndCallTheContract) {
    FakeSimulationServerClient client;

    const SimulationDeviceDescription desc = client.attach();
    EXPECT_TRUE(client.attached());
    EXPECT_EQ(desc.num_chips, 1u);

    const uint32_t value = 0xabcd1234;
    client.write_to_device(
        CoreCoord(1, 1, tt::CoreType::TENSIX, tt::CoordSystem::TRANSLATED), 0x10, &value, sizeof(value));
    uint32_t read_back = 0;
    client.read_from_device(
        CoreCoord(1, 1, tt::CoreType::TENSIX, tt::CoordSystem::TRANSLATED), 0x10, &read_back, sizeof(read_back));
    EXPECT_EQ(read_back, value);

    client.detach();
    EXPECT_FALSE(client.attached());
}

// Placeholder message proves the FlatBuffers serialization path works end-to-end.
TEST(SimulationServerApiWire, MessageRoundTrip) {
    Message msg;
    msg.value = 42;
    msg.text = "hello";

    const Message out = decode(encode(msg));

    EXPECT_EQ(out.value, 42u);
    EXPECT_EQ(out.text, "hello");
}

TEST(SimulationServerApiWire, FramePrependsLittleEndianLength) {
    const std::vector<uint8_t> payload = {1, 2, 3, 4, 5};

    const std::vector<uint8_t> framed = frame(payload);

    ASSERT_EQ(framed.size(), payload.size() + 4);
    const uint32_t len = framed[0] | (framed[1] << 8) | (framed[2] << 16) | (uint32_t(framed[3]) << 24);
    EXPECT_EQ(len, payload.size());
    EXPECT_EQ(std::vector<uint8_t>(framed.begin() + 4, framed.end()), payload);
}
