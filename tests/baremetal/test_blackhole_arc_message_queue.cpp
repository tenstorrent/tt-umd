// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "tests/test_utils/protocol_mocks.hpp"
#include "umd/device/arc/blackhole_arc_message_queue.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/firmware/blackhole_arc_apb.hpp"
#include "umd/device/types/blackhole_arc.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

// Queue layout, in words.
//
// TODO: duplicated from BlackholeArcMessageQueue's private constants, so a change there does not
// fail this file -- it just makes it probe the wrong addresses. Tracked in the Base API refactor
// notes: these belong in the blackhole namespace as named constants both sides can use.
constexpr uint32_t HEADER_LEN = 8;
constexpr uint32_t ENTRY_LEN = 8;
constexpr uint32_t REQUEST_WPTR_OFFSET = 0;
constexpr uint32_t RESPONSE_RPTR_OFFSET = 1;
constexpr uint32_t RESPONSE_WPTR_OFFSET = 5;

constexpr uint64_t QUEUE_BASE = 0x40000;
constexpr uint64_t QUEUE_ENTRIES = 4;

uint64_t word_address(uint32_t word_offset) { return QUEUE_BASE + word_offset * sizeof(uint32_t); }

// Both dimensions the queue actually reads: the NOC each call routes over, and whether NOC
// translation is on -- together they decide which ARC core get_arc_core() returns.
using QueueParam = std::tuple<NocId, bool>;

class BlackholeArcMessageQueueTest : public ::testing::TestWithParam<QueueParam> {
protected:
    NocId noc() const { return std::get<0>(GetParam()); }

    bool noc_translation_enabled() const { return std::get<1>(GetParam()); }

    tt_xy_pair arc_core() const {
        return blackhole::get_arc_core(noc_translation_enabled(), /*use_noc1=*/noc() == NocId::NOC1);
    }

    void SetUp() override {
        // Back the protocol's data path with a plain word-addressed map, so the queue's pointer and
        // entry accesses round-trip instead of reading garbage.
        ON_CALL(protocol_, read_data(_, _, _, _, _))
            .WillByDefault(Invoke([this](void* dst, tt_xy_pair, uint64_t addr, size_t size, NocId) {
                auto* out = static_cast<uint32_t*>(dst);
                for (size_t i = 0; i < size / sizeof(uint32_t); i++) {
                    out[i] = memory_[addr + i * sizeof(uint32_t)];
                }
            }));
        ON_CALL(protocol_, write_data(_, _, _, _, _))
            .WillByDefault(Invoke([this](const void* src, tt_xy_pair, uint64_t addr, size_t size, NocId) {
                const auto* in = static_cast<const uint32_t*>(src);
                for (size_t i = 0; i < size / sizeof(uint32_t); i++) {
                    memory_[addr + i * sizeof(uint32_t)] = in[i];
                }
            }));
        // The ARC tile is reachable over AXI, so APB accesses land on the BAR and never disturb the
        // data path the queue uses.
        ON_CALL(pcie_, bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + blackhole::NOC_NODE_ID_OFFSET))
            .WillByDefault(Return(11));
    }

    // Stands in for the ARC firmware: publishes a response and advances the write pointer it owns,
    // so pop_response finds the queue non-empty on its first poll instead of spinning to timeout.
    void publish_response(uint32_t status, uint32_t payload, const std::vector<uint32_t>& return_values) {
        memory_[word_address(RESPONSE_WPTR_OFFSET)] = 1;
        const uint32_t response_offset = HEADER_LEN + QUEUE_ENTRIES * ENTRY_LEN;
        memory_[word_address(response_offset)] = (payload << 16) | status;
        for (size_t i = 0; i < return_values.size(); i++) {
            memory_[word_address(response_offset + 1 + i)] = return_values[i];
        }
    }

    BlackholeArcMessageQueue make_queue(BlackholeArcApb& apb) {
        return BlackholeArcMessageQueue(&protocol_, &apb, QUEUE_BASE, QUEUE_ENTRIES, noc_translation_enabled());
    }

    uint32_t send(
        BlackholeArcMessageQueue& queue, std::vector<uint32_t>& return_values, const std::vector<uint32_t>& args = {}) {
        return queue.send_message(
            static_cast<ArcMessageType>(blackhole::ArcMessageType::AICLK_GO_BUSY),
            return_values,
            args,
            timeout::ARC_MESSAGE_TIMEOUT,
            noc());
    }

    NiceMock<MockDeviceProtocol> protocol_;
    NiceMock<MockPcieInterface> pcie_;
    NiceMock<MockJtagInterface> jtag_;
    std::map<uint64_t, uint32_t> memory_;
};

INSTANTIATE_TEST_SUITE_P(
    NocAndTranslation,
    BlackholeArcMessageQueueTest,
    ::testing::Combine(::testing::Values(NocId::NOC0, NocId::NOC1), ::testing::Bool()),
    [](const ::testing::TestParamInfo<QueueParam>& info) {
        return noc_to_str(std::get<0>(info.param)) + (std::get<1>(info.param) ? "_Translated" : "_Untranslated");
    });

TEST_P(BlackholeArcMessageQueueTest, SendMessagePushesRequestAndReturnsResponse) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);
    auto queue = make_queue(apb);

    publish_response(/*status=*/0, /*payload=*/0x1234, /*return_values=*/{0xAA, 0xBB});

    std::vector<uint32_t> return_values;
    const uint32_t exit_code = send(queue, return_values, {0x11, 0x22});

    // The response payload lives in the high 16 bits of the first response word.
    EXPECT_EQ(exit_code, 0x1234);
    ASSERT_EQ(return_values.size(), ENTRY_LEN - 1);
    EXPECT_EQ(return_values[0], 0xAA);
    EXPECT_EQ(return_values[1], 0xBB);

    // The request landed in the first request entry, message type first and args after it.
    const uint32_t request_offset = HEADER_LEN;
    EXPECT_EQ(memory_[word_address(request_offset)], (uint32_t)blackhole::ArcMessageType::AICLK_GO_BUSY);
    EXPECT_EQ(memory_[word_address(request_offset + 1)], 0x11);
    EXPECT_EQ(memory_[word_address(request_offset + 2)], 0x22);
    // Unused argument slots are zeroed.
    EXPECT_EQ(memory_[word_address(request_offset + 3)], 0u);

    // Each side advances only the pointer it owns: the host bumps the request write pointer and the
    // response read pointer, both by one entry.
    EXPECT_EQ(memory_[word_address(REQUEST_WPTR_OFFSET)], 1u);
    EXPECT_EQ(memory_[word_address(RESPONSE_RPTR_OFFSET)], 1u);
}

TEST_P(BlackholeArcMessageQueueTest, SendMessageTriggersFirmwareInterrupt) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);
    auto queue = make_queue(apb);

    publish_response(/*status=*/0, /*payload=*/0, /*return_values=*/{});

    EXPECT_CALL(
        pcie_,
        bar_write32(blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + blackhole::ARC_FW_INT_ADDR, blackhole::ARC_FW_INT_VAL));

    std::vector<uint32_t> return_values;
    send(queue, return_values);
}

// The queue holds no NOC state: the ARC core it targets is derived per call, and with translation
// off the two NOCs really do resolve to different cores.
TEST_P(BlackholeArcMessageQueueTest, TargetsTheArcCoreForTheRequestedNoc) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);
    auto queue = make_queue(apb);

    publish_response(/*status=*/0, /*payload=*/0, /*return_values=*/{});

    EXPECT_CALL(protocol_, read_data(_, arc_core(), _, _, noc())).Times(AtLeast(1));

    std::vector<uint32_t> return_values;
    send(queue, return_values);
}

TEST_P(BlackholeArcMessageQueueTest, SendMessageRejectsMoreThanSevenArguments) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);
    auto queue = make_queue(apb);

    std::vector<uint32_t> return_values;
    EXPECT_THROW(send(queue, return_values, std::vector<uint32_t>(8, 0)), std::exception);
}

TEST_P(BlackholeArcMessageQueueTest, SendMessageThrowsOnFirmwareErrorStatus) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);
    auto queue = make_queue(apb);

    // 0xFF is the "message not recognized" status.
    publish_response(/*status=*/0xFF, /*payload=*/0, /*return_values=*/{});

    std::vector<uint32_t> return_values;
    EXPECT_THROW(send(queue, return_values), std::exception);
}

// The queue descriptor is read from ARC scratch and decoded into a base address and entry count.
TEST_P(BlackholeArcMessageQueueTest, FactoryDecodesTheQueueDescriptor) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr);

    constexpr uint32_t CONTROL_BLOCK_ADDR = 0x50000;
    constexpr uint32_t FIRST_QUEUE_BASE = 0x60000;
    constexpr uint32_t ENTRIES_PER_QUEUE = 8;

    // SCRATCH_RAM_11 holds the address of the descriptor; the descriptor packs the first queue's
    // address in the low 32 bits and the entry count in the next 8.
    // The routing probe also reads a BAR register; only the scratch read is of interest here.
    EXPECT_CALL(pcie_, bar_read32(_)).Times(AnyNumber());
    EXPECT_CALL(pcie_, bar_read32(blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + blackhole::SCRATCH_RAM_11))
        .WillOnce(Return(CONTROL_BLOCK_ADDR));
    memory_[CONTROL_BLOCK_ADDR] = FIRST_QUEUE_BASE;
    memory_[CONTROL_BLOCK_ADDR + sizeof(uint32_t)] = ENTRIES_PER_QUEUE;

    auto queue = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        &protocol_,
        /*jtag_interface=*/nullptr,
        &apb,
        noc_translation_enabled(),
        BlackholeArcMessageQueueIndex::APPLICATION,
        noc());
    ASSERT_NE(queue, nullptr);

    // Queue N starts after N whole queues: each is a header plus a request and a response ring.
    const uint32_t queue_size =
        2 * ENTRIES_PER_QUEUE * blackhole::ARC_QUEUE_ENTRY_SIZE + blackhole::ARC_MSG_QUEUE_HEADER_SIZE;
    const uint64_t expected_base = FIRST_QUEUE_BASE + BlackholeArcMessageQueueIndex::APPLICATION * queue_size;

    // Reading the request write pointer must land at the start of the selected queue. The queue
    // makes several other reads while sending; only the pointer read is of interest here.
    EXPECT_CALL(protocol_, read_data(_, _, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(protocol_, read_data(_, _, expected_base, sizeof(uint32_t), _)).Times(AtLeast(1));

    // Publish a response at the selected queue so pop_response returns immediately.
    memory_[expected_base + RESPONSE_WPTR_OFFSET * sizeof(uint32_t)] = 1;

    std::vector<uint32_t> return_values;
    queue->send_message(
        static_cast<ArcMessageType>(blackhole::ArcMessageType::AICLK_GO_BUSY),
        return_values,
        {},
        timeout::ARC_MESSAGE_TIMEOUT,
        noc());
}

// Over JTAG the descriptor is read as two 32-bit MMIO accesses instead of one 64-bit NOC read.
TEST_P(BlackholeArcMessageQueueTest, FactoryReadsTheDescriptorOverJtagWhenPresent) {
    BlackholeArcApb apb(&protocol_, /*pcie_interface=*/nullptr, &jtag_);

    constexpr uint32_t CONTROL_BLOCK_ADDR = 0x50000;
    constexpr uint32_t FIRST_QUEUE_BASE = 0x60000;
    constexpr uint32_t ENTRIES_PER_QUEUE = 2;

    ON_CALL(protocol_, read_ctrl(_, _, _, _, _))
        .WillByDefault(Invoke(
            [](void* dst, tt_xy_pair, uint64_t, size_t, NocId) { *static_cast<uint32_t*>(dst) = CONTROL_BLOCK_ADDR; }));
    EXPECT_CALL(jtag_, mmio_read32(CONTROL_BLOCK_ADDR)).WillOnce(Return(FIRST_QUEUE_BASE));
    EXPECT_CALL(jtag_, mmio_read32(CONTROL_BLOCK_ADDR + 4)).WillOnce(Return(ENTRIES_PER_QUEUE));
    EXPECT_CALL(protocol_, read_data(_, _, _, _, _)).Times(0);

    auto queue = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        &protocol_, &jtag_, &apb, noc_translation_enabled(), BlackholeArcMessageQueueIndex::KMD, noc());
    EXPECT_NE(queue, nullptr);
}

}  // namespace
