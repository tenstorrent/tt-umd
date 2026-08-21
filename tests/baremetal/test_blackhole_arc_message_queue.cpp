// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <vector>

#include "tests/test_utils/protocol_mocks.hpp"
#include "umd/device/arc/blackhole_arc_message_queue.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/firmware/blackhole_arc_apb.hpp"
#include "umd/device/types/blackhole_arc.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

// Word-addressed stand-in for the queue memory the ARC firmware exposes on its core.
class FakeArcMemory {
public:
    void read(void* dst, uint64_t addr, size_t size) {
        auto* out = static_cast<uint32_t*>(dst);
        for (size_t i = 0; i < size / sizeof(uint32_t); i++) {
            out[i] = words_[addr + i * sizeof(uint32_t)];
        }
    }

    void write(const void* src, uint64_t addr, size_t size) {
        const auto* in = static_cast<const uint32_t*>(src);
        for (size_t i = 0; i < size / sizeof(uint32_t); i++) {
            words_[addr + i * sizeof(uint32_t)] = in[i];
        }
    }

    uint32_t& at(uint64_t addr) { return words_[addr]; }

private:
    std::map<uint64_t, uint32_t> words_;
};

// Queue layout, in words. Mirrors the private constants of BlackholeArcMessageQueue.
constexpr uint32_t HEADER_LEN = 8;
constexpr uint32_t ENTRY_LEN = 8;
constexpr uint32_t REQUEST_WPTR_OFFSET = 0;
constexpr uint32_t RESPONSE_RPTR_OFFSET = 1;
constexpr uint32_t RESPONSE_WPTR_OFFSET = 5;

constexpr uint64_t QUEUE_BASE = 0x40000;
constexpr uint64_t QUEUE_ENTRIES = 4;
const tt_xy_pair ARC_CORE_NOC0 = {8, 0};

uint64_t word_address(uint32_t word_offset) { return QUEUE_BASE + word_offset * sizeof(uint32_t); }

class BlackholeArcMessageQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Back the protocol's data path with the fake queue memory.
        ON_CALL(protocol_, read_data(_, _, _, _, _))
            .WillByDefault(Invoke(
                [this](void* dst, tt_xy_pair, uint64_t addr, size_t size, NocId) { memory_.read(dst, addr, size); }));
        ON_CALL(protocol_, write_data(_, _, _, _, _))
            .WillByDefault(Invoke([this](const void* src, tt_xy_pair, uint64_t addr, size_t size, NocId) {
                memory_.write(src, addr, size);
            }));
        // The ARC tile is reachable over AXI, so APB accesses land on the BAR and never disturb the
        // data path the queue uses.
        ON_CALL(pcie_, bar_read32(arch_.get_read_checking_offset())).WillByDefault(Return(11));
    }

    // Pre-arms the response the firmware would have published, so pop_response returns immediately.
    void publish_response(uint32_t status, uint32_t payload, const std::vector<uint32_t>& return_values) {
        memory_.at(word_address(RESPONSE_WPTR_OFFSET)) = 1;
        const uint32_t response_offset = HEADER_LEN + QUEUE_ENTRIES * ENTRY_LEN;
        memory_.at(word_address(response_offset)) = (payload << 16) | status;
        for (size_t i = 0; i < return_values.size(); i++) {
            memory_.at(word_address(response_offset + 1 + i)) = return_values[i];
        }
    }

    blackhole_implementation arch_;
    NiceMock<MockDeviceProtocol> protocol_;
    NiceMock<MockPcieInterface> pcie_;
    NiceMock<MockJtagInterface> jtag_;
    FakeArcMemory memory_;
};

TEST_F(BlackholeArcMessageQueueTest, SendMessagePushesRequestAndReturnsResponse) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr, &arch_);
    BlackholeArcMessageQueue queue(&protocol_, &apb, QUEUE_BASE, QUEUE_ENTRIES, /*noc_translation_enabled=*/false);

    publish_response(/*status=*/0, /*payload=*/0x1234, /*return_values=*/{0xAA, 0xBB});

    std::vector<uint32_t> return_values;
    const uint32_t exit_code = queue.send_message(
        static_cast<ArcMessageType>(blackhole::ArcMessageType::AICLK_GO_BUSY), return_values, {0x11, 0x22});

    // The response payload lives in the high 16 bits of the first response word.
    EXPECT_EQ(exit_code, 0x1234);
    ASSERT_EQ(return_values.size(), ENTRY_LEN - 1);
    EXPECT_EQ(return_values[0], 0xAA);
    EXPECT_EQ(return_values[1], 0xBB);

    // The request landed in the first request entry, message type first and args after it.
    const uint32_t request_offset = HEADER_LEN;
    EXPECT_EQ(memory_.at(word_address(request_offset)), (uint32_t)blackhole::ArcMessageType::AICLK_GO_BUSY);
    EXPECT_EQ(memory_.at(word_address(request_offset + 1)), 0x11);
    EXPECT_EQ(memory_.at(word_address(request_offset + 2)), 0x22);
    // Unused argument slots are zeroed.
    EXPECT_EQ(memory_.at(word_address(request_offset + 3)), 0u);

    // Both pointers advanced by one entry.
    EXPECT_EQ(memory_.at(word_address(REQUEST_WPTR_OFFSET)), 1u);
    EXPECT_EQ(memory_.at(word_address(RESPONSE_RPTR_OFFSET)), 1u);
}

TEST_F(BlackholeArcMessageQueueTest, SendMessageTriggersFirmwareInterrupt) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr, &arch_);
    BlackholeArcMessageQueue queue(&protocol_, &apb, QUEUE_BASE, QUEUE_ENTRIES, /*noc_translation_enabled=*/false);

    publish_response(/*status=*/0, /*payload=*/0, /*return_values=*/{});

    EXPECT_CALL(
        pcie_,
        bar_write32(blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + blackhole::ARC_FW_INT_ADDR, blackhole::ARC_FW_INT_VAL));

    std::vector<uint32_t> return_values;
    queue.send_message(static_cast<ArcMessageType>(blackhole::ArcMessageType::AICLK_GO_BUSY), return_values);
}

TEST_F(BlackholeArcMessageQueueTest, SendMessageRejectsMoreThanSevenArguments) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr, &arch_);
    BlackholeArcMessageQueue queue(&protocol_, &apb, QUEUE_BASE, QUEUE_ENTRIES, /*noc_translation_enabled=*/false);

    std::vector<uint32_t> return_values;
    const std::vector<uint32_t> too_many_args(8, 0);

    EXPECT_THROW(
        queue.send_message(
            static_cast<ArcMessageType>(blackhole::ArcMessageType::AICLK_GO_BUSY), return_values, too_many_args),
        std::exception);
}

TEST_F(BlackholeArcMessageQueueTest, SendMessageThrowsOnFirmwareErrorStatus) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr, &arch_);
    BlackholeArcMessageQueue queue(&protocol_, &apb, QUEUE_BASE, QUEUE_ENTRIES, /*noc_translation_enabled=*/false);

    // 0xFF is the "message not recognized" status.
    publish_response(/*status=*/0xFF, /*payload=*/0, /*return_values=*/{});

    std::vector<uint32_t> return_values;
    EXPECT_THROW(
        queue.send_message(static_cast<ArcMessageType>(blackhole::ArcMessageType::AICLK_GO_BUSY), return_values),
        std::exception);
}

// The queue holds no NOC state: the ARC core it targets is derived from the NOC of each call.
TEST_F(BlackholeArcMessageQueueTest, TargetsTheArcCoreForTheRequestedNoc) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr, &arch_);
    BlackholeArcMessageQueue queue(&protocol_, &apb, QUEUE_BASE, QUEUE_ENTRIES, /*noc_translation_enabled=*/false);

    publish_response(/*status=*/0, /*payload=*/0, /*return_values=*/{});

    const tt_xy_pair arc_core_noc1 = blackhole::get_arc_core(/*noc_translation_enabled=*/false, /*use_noc1=*/true);
    EXPECT_NE(arc_core_noc1, ARC_CORE_NOC0);
    EXPECT_CALL(protocol_, read_data(_, arc_core_noc1, _, _, NocId::NOC1)).Times(::testing::AtLeast(1));

    std::vector<uint32_t> return_values;
    queue.send_message(
        static_cast<ArcMessageType>(blackhole::ArcMessageType::AICLK_GO_BUSY),
        return_values,
        {},
        timeout::ARC_MESSAGE_TIMEOUT,
        NocId::NOC1);
}

// The queue descriptor is read from ARC scratch and decoded into a base address and entry count.
TEST_F(BlackholeArcMessageQueueTest, FactoryDecodesTheQueueDescriptor) {
    BlackholeArcApb apb(&protocol_, &pcie_, /*jtag_interface=*/nullptr, &arch_);

    constexpr uint32_t CONTROL_BLOCK_ADDR = 0x50000;
    constexpr uint32_t FIRST_QUEUE_BASE = 0x60000;
    constexpr uint32_t ENTRIES_PER_QUEUE = 8;

    // SCRATCH_RAM_11 holds the address of the descriptor; the descriptor packs the first queue's
    // address in the low 32 bits and the entry count in the next 8.
    // The routing probe also reads a BAR register; only the scratch read is of interest here.
    EXPECT_CALL(pcie_, bar_read32(_)).Times(::testing::AnyNumber());
    EXPECT_CALL(pcie_, bar_read32(blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + blackhole::SCRATCH_RAM_11))
        .WillOnce(Return(CONTROL_BLOCK_ADDR));
    memory_.at(CONTROL_BLOCK_ADDR) = FIRST_QUEUE_BASE;
    memory_.at(CONTROL_BLOCK_ADDR + sizeof(uint32_t)) = ENTRIES_PER_QUEUE;

    auto queue = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        &protocol_,
        /*jtag_interface=*/nullptr,
        &apb,
        /*noc_translation_enabled=*/false,
        BlackholeArcMessageQueueIndex::APPLICATION);
    ASSERT_NE(queue, nullptr);

    // Queue N starts after N whole queues: each is a header plus a request and a response ring.
    const uint32_t queue_size =
        2 * ENTRIES_PER_QUEUE * blackhole::ARC_QUEUE_ENTRY_SIZE + blackhole::ARC_MSG_QUEUE_HEADER_SIZE;
    const uint64_t expected_base = FIRST_QUEUE_BASE + BlackholeArcMessageQueueIndex::APPLICATION * queue_size;

    // Reading the request write pointer must land at the start of the selected queue. The queue
    // makes several other reads while sending; only the pointer read is of interest here.
    EXPECT_CALL(protocol_, read_data(_, _, _, _, _)).Times(::testing::AnyNumber());
    EXPECT_CALL(protocol_, read_data(_, _, expected_base, sizeof(uint32_t), _)).Times(::testing::AtLeast(1));

    // Publish a response at the selected queue so pop_response returns immediately.
    memory_.at(expected_base + RESPONSE_WPTR_OFFSET * sizeof(uint32_t)) = 1;

    std::vector<uint32_t> return_values;
    queue->send_message(static_cast<ArcMessageType>(blackhole::ArcMessageType::AICLK_GO_BUSY), return_values);
}

// Over JTAG the descriptor is read as two 32-bit MMIO accesses instead of one 64-bit NOC read.
TEST_F(BlackholeArcMessageQueueTest, FactoryReadsTheDescriptorOverJtagWhenPresent) {
    BlackholeArcApb apb(&protocol_, /*pcie_interface=*/nullptr, &jtag_, &arch_);

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
        &protocol_,
        &jtag_,
        &apb,
        /*noc_translation_enabled=*/false,
        BlackholeArcMessageQueueIndex::KMD);
    EXPECT_NE(queue, nullptr);
}

}  // namespace
