// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "tests/test_utils/protocol_mocks.hpp"
#include "tt-umd-utils/error.hpp"
#include "tt-umd/utils/lock_manager.hpp"
#include "tt_device/firmware/grendel_device_firmware.hpp"
#include "tt_device/firmware/quasar_smc_message_queue.hpp"

using namespace tt::umd;
using namespace tt::umd::test_utils;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

using Message = std::array<uint32_t, 8>;

// No hardware has this device id, so the tests never contend with a card's firmware message lock.
constexpr int UNUSED_DEVICE_ID = 250;

// Addresses on the Keraunos I/O chiplet's SMC, by system physical address.
constexpr uint64_t K_SMC_BASE = 0x1202000000;
constexpr uint64_t COLD_SCRATCH_3 = 0x120200280C;
constexpr uint64_t DOORBELL = 0x1202018000;
constexpr uint64_t DOORBELL_HIGH = 0x1202018004;

// The registers the host uses on one SMC, by system physical address.
struct Smc {
    uint64_t base;
    uint64_t cold_scratch_3;
    uint64_t doorbell;
    uint64_t doorbell_high;
};

constexpr Smc K_SMC = {K_SMC_BASE, COLD_SCRATCH_3, DOORBELL, DOORBELL_HIGH};
constexpr Smc M0_SMC = {0x1300000000, 0x130000280C, 0x1300018000, 0x1300018004};

// Where the firmware publishes its queue, as SMC-local pointers and as the SPAs they resolve to.
struct PublishedQueue {
    uint32_t queue_info_smc_local;
    uint64_t queue_info;
    uint32_t queue_smc_local;
    uint64_t queue;
};

constexpr uint64_t QUEUE_INFO = 0x1202001000;
constexpr PublishedQueue K_SMC_QUEUE = {0xC0001000, QUEUE_INFO, 0xC0002000, 0x1202002000};

constexpr uint64_t REQUEST_WPTR = 0x1202002000;
constexpr uint64_t RESPONSE_RPTR = 0x1202002004;
constexpr uint64_t REQUEST_RPTR = 0x1202002010;
constexpr uint64_t RESPONSE_WPTR = 0x1202002014;
constexpr uint64_t REQUEST_SLOT_0 = 0x1202002020;
constexpr uint64_t RESPONSE_SLOT_0 = 0x12020020A0;
constexpr uint64_t RESPONSE_SLOT_1 = 0x12020020C0;

// Header and payload words are opaque to the queue; none of these is a defined firmware message.
// Every word uses all 32 bits, so a word truncated or decoded on the way shows up.
constexpr uint32_t HEADER = 0x5EAD0090;
constexpr Message RESPONSE = {
    0xA5A500A0, 0xA5A500A1, 0xA5A500A2, 0xA5A500A3, 0xA5A500A4, 0xA5A500A5, 0xA5A500A6, 0xA5A500A7};

constexpr std::chrono::milliseconds SEND_TIMEOUT{1000};
constexpr std::chrono::milliseconds SHORT_TIMEOUT{10};

struct Access {
    bool is_write;
    uint64_t address;
    size_t size;
    tt_xy_pair core;
    NocId noc;
    RegisterAccessWidth width;
};

// Stands in for the SMC firmware behind a DeviceProtocol: plain 32-bit word memory addressed by
// SPA, plus a firmware that serves the request ring each time the host rings the doorbell.
class FakeSmcFirmware {
public:
    explicit FakeSmcFirmware(const Smc& smc = K_SMC) : smc_(smc) {
        ON_CALL(protocol, get_mmio_id()).WillByDefault(Return(UNUSED_DEVICE_ID));
        ON_CALL(protocol, read_ctrl(_, _, _, _, _, _))
            .WillByDefault(Invoke(
                [this](void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc, RegisterAccessWidth width) {
                    accesses.push_back({false, addr, size, core, noc, width});
                    accessed = true;
                    for (size_t i = 0; i < size / sizeof(uint32_t); i++) {
                        const uint32_t word = read_word(addr + i * sizeof(uint32_t));
                        std::memcpy(static_cast<uint8_t*>(dst) + i * sizeof(uint32_t), &word, sizeof(word));
                    }
                }));
        ON_CALL(protocol, write_ctrl(_, _, _, _, _, _))
            .WillByDefault(Invoke([this](
                                      const void* src,
                                      tt_xy_pair core,
                                      uint64_t addr,
                                      size_t size,
                                      NocId noc,
                                      RegisterAccessWidth width) {
                accesses.push_back({true, addr, size, core, noc, width});
                accessed = true;
                bool doorbell_rung = false;
                for (size_t i = 0; i < size / sizeof(uint32_t); i++) {
                    uint32_t word;
                    std::memcpy(&word, static_cast<const uint8_t*>(src) + i * sizeof(uint32_t), sizeof(word));
                    const uint64_t word_addr = addr + i * sizeof(uint32_t);
                    word_writes.push_back({word_addr, word});
                    memory[word_addr] = word;
                    doorbell_rung |= word_addr == smc_.doorbell;
                }
                if (doorbell_rung && !ignores_doorbell) {
                    serve_requests();
                }
            }));
    }

    void publish_queue(const PublishedQueue& published = K_SMC_QUEUE) {
        memory[smc_.cold_scratch_3] = published.queue_info_smc_local;
        memory[published.queue_info] = published.queue_smc_local;
        queue_ = published.queue;
    }

    void set_ring_pointers(uint32_t value) {
        for (uint64_t pointer : {REQUEST_WPTR, RESPONSE_RPTR, REQUEST_RPTR, RESPONSE_WPTR}) {
            memory[pointer] = value;
        }
    }

    Message words_at(uint64_t addr) {
        Message words{};
        for (size_t i = 0; i < words.size(); i++) {
            words[i] = memory[addr + i * sizeof(uint32_t)];
        }
        return words;
    }

    void put_words(uint64_t addr, const Message& words) {
        for (size_t i = 0; i < words.size(); i++) {
            memory[addr + i * sizeof(uint32_t)] = words[i];
        }
    }

    NiceMock<MockDeviceProtocol> protocol;
    std::map<uint64_t, uint32_t> memory;
    std::vector<Access> accesses;
    std::vector<std::pair<uint64_t, uint32_t>> word_writes;
    std::vector<Message> requests_seen;
    // Set on any host access to the device; safe to poll from another thread.
    std::atomic<bool> accessed{false};

    // Firmware's answer to each request it takes off the ring; nullopt takes it but never answers.
    std::function<std::optional<Message>(const Message&)> respond = [](const Message&) { return RESPONSE; };
    // A stalled firmware leaves the request ring as it is.
    bool ignores_doorbell = false;
    // Host reads of the response write pointer that see the old value before an answer shows up.
    int response_delay_polls = 0;

private:
    uint32_t read_word(uint64_t addr) {
        if (addr == queue_ + 0x14 && pending_response_) {
            if (response_delay_polls > 0) {
                response_delay_polls--;
            } else {
                push_response(*pending_response_);
                pending_response_.reset();
            }
        }
        // The mailbox reads back as all ones on the emulator.
        if (addr == smc_.doorbell || addr == smc_.doorbell_high) {
            return 0xFFFFFFFF;
        }
        return memory[addr];
    }

    void serve_requests() {
        const uint64_t request_wptr = queue_ + 0x00;
        const uint64_t request_rptr = queue_ + 0x10;
        while (memory[request_rptr] != memory[request_wptr]) {
            const uint32_t rptr = memory[request_rptr];
            const Message request = words_at(queue_ + 0x20 + 32 * (rptr % 4));
            memory[request_rptr] = (rptr + 1) % 8;
            requests_seen.push_back(request);
            const std::optional<Message> response = respond(request);
            if (!response) {
                continue;
            }
            if (response_delay_polls > 0) {
                pending_response_ = response;
            } else {
                push_response(*response);
            }
        }
    }

    void push_response(const Message& response) {
        const uint64_t response_wptr = queue_ + 0x14;
        const uint32_t wptr = memory[response_wptr];
        put_words(queue_ + 0xA0 + 32 * (wptr % 4), response);
        memory[response_wptr] = (wptr + 1) % 8;
    }

    Smc smc_;
    uint64_t queue_ = K_SMC_QUEUE.queue;
    std::optional<Message> pending_response_;
};

// The queue is built before any test publishes one: firmware may publish its queue only after the
// host has set up to talk to it.
class QuasarSmcMessageQueueTest : public ::testing::Test {
protected:
    FakeSmcFirmware firmware_;
    QuasarSmcMessageQueue queue_{&firmware_.protocol, K_SMC_BASE};
};

TEST_F(QuasarSmcMessageQueueTest, ReturnsTheFirmwareResponseWords) {
    firmware_.publish_queue();

    EXPECT_EQ(queue_.send_message(HEADER, {0x12345678}, SEND_TIMEOUT), RESPONSE);
}

TEST_F(QuasarSmcMessageQueueTest, SendsHeaderThenArgsPaddedWithZeros) {
    firmware_.publish_queue();
    // Leftovers from an earlier request must not leak into the unused words.
    firmware_.put_words(REQUEST_SLOT_0, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});

    queue_.send_message(HEADER, {0x12345678, 0x9ABCDEF0}, SEND_TIMEOUT);

    ASSERT_EQ(firmware_.requests_seen.size(), 1);
    EXPECT_THAT(firmware_.requests_seen[0], ElementsAre(0x5EAD0090, 0x12345678, 0x9ABCDEF0, 0, 0, 0, 0, 0));
}

TEST_F(QuasarSmcMessageQueueTest, SendsSevenArgs) {
    firmware_.publish_queue();

    queue_.send_message(HEADER, {1, 2, 3, 4, 5, 6, 7}, SEND_TIMEOUT);

    ASSERT_EQ(firmware_.requests_seen.size(), 1);
    EXPECT_THAT(firmware_.requests_seen[0], ElementsAre(0x5EAD0090, 1, 2, 3, 4, 5, 6, 7));
}

TEST_F(QuasarSmcMessageQueueTest, RejectsMoreThanSevenArgs) {
    firmware_.publish_queue();

    EXPECT_THROW(queue_.send_message(HEADER, {1, 2, 3, 4, 5, 6, 7, 8}, SEND_TIMEOUT), error::RuntimeError);
}

TEST_F(QuasarSmcMessageQueueTest, RingsTheDoorbellWithTwoZeroWordsAfterAdvancingTheRequestPointer) {
    firmware_.publish_queue();

    queue_.send_message(HEADER, {}, SEND_TIMEOUT);

    std::vector<std::pair<uint64_t, uint32_t>> doorbell_writes;
    std::optional<size_t> request_wptr_write;
    std::optional<size_t> first_doorbell_write;
    for (size_t i = 0; i < firmware_.word_writes.size(); i++) {
        const auto& [addr, value] = firmware_.word_writes[i];
        if (addr == REQUEST_WPTR && value == 1) {
            request_wptr_write = i;
        }
        if (addr == DOORBELL || addr == DOORBELL_HIGH) {
            doorbell_writes.push_back({addr, value});
            first_doorbell_write = first_doorbell_write.value_or(i);
        }
    }
    EXPECT_THAT(
        doorbell_writes,
        ElementsAre(std::pair<uint64_t, uint32_t>{DOORBELL, 0}, std::pair<uint64_t, uint32_t>{DOORBELL_HIGH, 0}));
    ASSERT_TRUE(request_wptr_write.has_value());
    ASSERT_TRUE(first_doorbell_write.has_value());
    EXPECT_LT(*request_wptr_write, *first_doorbell_write);
}

// The host reaches the SMC by system physical address alone.
TEST_F(QuasarSmcMessageQueueTest, Issues32BitControlAccessesOverNoc0Only) {
    firmware_.publish_queue();
    EXPECT_CALL(firmware_.protocol, read_data(_, _, _, _, _)).Times(0);
    EXPECT_CALL(firmware_.protocol, write_data(_, _, _, _, _)).Times(0);

    queue_.send_message(HEADER, {}, SEND_TIMEOUT);

    ASSERT_FALSE(firmware_.accesses.empty());
    for (const Access& access : firmware_.accesses) {
        EXPECT_EQ(access.core, tt_xy_pair(0, 0)) << std::hex << access.address;
        EXPECT_EQ(access.noc, NocId::NOC0) << std::hex << access.address;
        EXPECT_EQ(access.width, RegisterAccessWidth::BITS32) << std::hex << access.address;
    }
}

// The mailbox reads back as all ones on the emulator, so it is written only.
TEST_F(QuasarSmcMessageQueueTest, NeverReadsTheDoorbell) {
    firmware_.publish_queue();

    queue_.send_message(HEADER, {}, SEND_TIMEOUT);

    for (const Access& access : firmware_.accesses) {
        const bool overlaps_doorbell = access.address < DOORBELL + 8 && access.address + access.size > DOORBELL;
        EXPECT_FALSE(!access.is_write && overlaps_doorbell) << std::hex << access.address;
    }
}

struct RingPosition {
    uint32_t start_pointer;
    uint64_t request_slot;
    uint64_t response_slot;
    uint32_t next_pointer;
};

class QuasarSmcMessageQueueRingTest : public QuasarSmcMessageQueueTest,
                                      public ::testing::WithParamInterface<RingPosition> {};

// Pointers run modulo 8 over four slots, so slot = pointer mod 4.
INSTANTIATE_TEST_SUITE_P(
    Pointers,
    QuasarSmcMessageQueueRingTest,
    ::testing::Values(
        RingPosition{0, 0x1202002020, 0x12020020A0, 1},
        RingPosition{1, 0x1202002040, 0x12020020C0, 2},
        RingPosition{3, 0x1202002080, 0x1202002100, 4},
        RingPosition{4, 0x1202002020, 0x12020020A0, 5},
        RingPosition{7, 0x1202002080, 0x1202002100, 0}),
    [](const ::testing::TestParamInfo<RingPosition>& info) {
        return "Pointer" + std::to_string(info.param.start_pointer);
    });

TEST_P(QuasarSmcMessageQueueRingTest, UsesTheSlotsAtThePointersAndAdvancesThem) {
    firmware_.publish_queue();
    firmware_.set_ring_pointers(GetParam().start_pointer);

    const Message response = queue_.send_message(HEADER, {0x12345678}, SEND_TIMEOUT);

    EXPECT_EQ(response, RESPONSE);
    EXPECT_EQ(firmware_.memory[GetParam().request_slot], 0x5EAD0090);
    EXPECT_EQ(firmware_.memory[GetParam().request_slot + 4], 0x12345678);
    EXPECT_EQ(firmware_.memory[GetParam().response_slot], 0xA5A500A0);
    EXPECT_EQ(firmware_.memory[REQUEST_WPTR], GetParam().next_pointer);
    EXPECT_EQ(firmware_.memory[RESPONSE_RPTR], GetParam().next_pointer);
}

TEST_F(QuasarSmcMessageQueueTest, DiscardsAResponseLeftInTheRing) {
    firmware_.publish_queue();
    // An earlier caller gave up before its response arrived in slot 0.
    firmware_.put_words(RESPONSE_SLOT_0, {0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A});
    firmware_.memory[RESPONSE_WPTR] = 1;

    EXPECT_EQ(queue_.send_message(HEADER, {}, SEND_TIMEOUT), RESPONSE);
    EXPECT_EQ(firmware_.words_at(RESPONSE_SLOT_1), RESPONSE);
    EXPECT_EQ(firmware_.memory[RESPONSE_RPTR], 2);
}

struct UnpublishedQueue {
    const char* name;
    uint32_t cold_scratch_3;
    uint32_t queue_info_word_0;
};

class QuasarSmcMessageQueueUnpublishedTest : public QuasarSmcMessageQueueTest,
                                             public ::testing::WithParamInterface<UnpublishedQueue> {};

INSTANTIATE_TEST_SUITE_P(
    Pointers,
    QuasarSmcMessageQueueUnpublishedTest,
    ::testing::Values(UnpublishedQueue{"NoQueueInfo", 0, 0xC0002000}, UnpublishedQueue{"NoQueue", 0xC0001000, 0}),
    [](const ::testing::TestParamInfo<UnpublishedQueue>& info) { return info.param.name; });

TEST_P(QuasarSmcMessageQueueUnpublishedTest, ThrowsWithoutWriting) {
    firmware_.memory[COLD_SCRATCH_3] = GetParam().cold_scratch_3;
    firmware_.memory[QUEUE_INFO] = GetParam().queue_info_word_0;

    EXPECT_THROW(queue_.send_message(HEADER, {}, SEND_TIMEOUT), error::RuntimeError);
    EXPECT_TRUE(firmware_.word_writes.empty());
}

// Every chiplet's SMC publishes its own queue the same way, relative to its own base.
TEST(QuasarSmcMessageQueueOtherSmcTest, TalksToTheSmcAtTheGivenBase) {
    FakeSmcFirmware firmware(M0_SMC);
    firmware.publish_queue({0xC0001000, 0x1300001000, 0xC0002000, 0x1300002000});
    QuasarSmcMessageQueue queue(&firmware.protocol, M0_SMC.base);

    EXPECT_EQ(queue.send_message(HEADER, {0x12345678}, SEND_TIMEOUT), RESPONSE);
    EXPECT_EQ(firmware.memory[0x1300002020], 0x5EAD0090);
    EXPECT_EQ(firmware.memory[0x1300002024], 0x12345678);
    EXPECT_THAT(
        firmware.word_writes,
        ::testing::IsSupersetOf(
            {std::pair<uint64_t, uint32_t>{0x1300018000, 0}, std::pair<uint64_t, uint32_t>{0x1300018004, 0}}));
}

TEST_F(QuasarSmcMessageQueueTest, FollowsAQueueThatMoved) {
    firmware_.publish_queue();
    queue_.send_message(HEADER, {}, SEND_TIMEOUT);

    firmware_.publish_queue({0xC0004000, 0x1202004000, 0xC0003000, 0x1202003000});
    queue_.send_message(HEADER, {0x12345678}, SEND_TIMEOUT);

    EXPECT_EQ(firmware_.memory[0x1202003020], 0x5EAD0090);
    EXPECT_EQ(firmware_.memory[0x1202003024], 0x12345678);
    EXPECT_EQ(firmware_.memory[0x1202003000], 1);
}

TEST_F(QuasarSmcMessageQueueTest, TimesOutWhenTheFirmwareNeverAnswers) {
    firmware_.publish_queue();
    firmware_.respond = [](const Message&) { return std::nullopt; };

    EXPECT_THROW(queue_.send_message(HEADER, {}, SHORT_TIMEOUT), error::RuntimeError);
}

struct FullRing {
    const char* name;
    uint32_t request_wptr;
    uint32_t request_rptr;
    // The slot the next request would go to, still holding a request the firmware has not taken.
    uint64_t pending_slot;
};

class QuasarSmcMessageQueueFullRingTest : public QuasarSmcMessageQueueTest,
                                          public ::testing::WithParamInterface<FullRing> {};

// Four requests outstanding fill the ring, also when the pointers have wrapped.
INSTANTIATE_TEST_SUITE_P(
    Pointers,
    QuasarSmcMessageQueueFullRingTest,
    ::testing::Values(FullRing{"Unwrapped", 4, 0, 0x1202002020}, FullRing{"Wrapped", 2, 6, 0x1202002060}),
    [](const ::testing::TestParamInfo<FullRing>& info) { return info.param.name; });

TEST_P(QuasarSmcMessageQueueFullRingTest, TimesOutWithoutOverwritingAPendingRequest) {
    constexpr Message PENDING_REQUEST = {
        0x5A5A0001, 0x5A5A0002, 0x5A5A0003, 0x5A5A0004, 0x5A5A0005, 0x5A5A0006, 0x5A5A0007, 0x5A5A0008};
    firmware_.publish_queue();
    firmware_.ignores_doorbell = true;
    firmware_.memory[REQUEST_WPTR] = GetParam().request_wptr;
    firmware_.memory[REQUEST_RPTR] = GetParam().request_rptr;
    firmware_.put_words(GetParam().pending_slot, PENDING_REQUEST);

    EXPECT_THROW(queue_.send_message(HEADER, {}, SHORT_TIMEOUT), error::RuntimeError);
    EXPECT_EQ(firmware_.words_at(GetParam().pending_slot), PENDING_REQUEST);
    EXPECT_EQ(firmware_.memory[REQUEST_WPTR], GetParam().request_wptr);
}

TEST_F(QuasarSmcMessageQueueTest, ZeroTimeoutWaitsForASlowAnswer) {
    firmware_.publish_queue();
    firmware_.response_delay_polls = 3;

    EXPECT_EQ(queue_.send_message(HEADER, {}, std::chrono::milliseconds(0)), RESPONSE);
}

// Grendel sends its commands to the Keraunos I/O chiplet's SMC. The lock is taken without the KMD
// lock table, since no device backs the fake protocol.
class GrendelDeviceFirmwareTest : public ::testing::TestWithParam<NocId> {
protected:
    FakeSmcFirmware firmware_;
    GrendelDeviceFirmware device_firmware_{&firmware_.protocol, /*kmd_lock_available=*/false};
};

// The queue is reachable by system physical address only, so every NOC reaches it.
INSTANTIATE_TEST_SUITE_P(
    Noc,
    GrendelDeviceFirmwareTest,
    ::testing::Values(NocId::NOC0, NocId::NOC1),
    [](const ::testing::TestParamInfo<NocId>& info) { return noc_to_str(info.param); });

TEST_P(GrendelDeviceFirmwareTest, SendDeviceCommandReturnsTheRawResponse) {
    firmware_.publish_queue();

    const DeviceCommandResult result =
        device_firmware_.send_device_command(HEADER, {0x12345678}, SEND_TIMEOUT, GetParam());

    EXPECT_EQ(result.exit_code, 0xA5A500A0);
    EXPECT_THAT(
        result.return_values,
        ElementsAre(0xA5A500A1, 0xA5A500A2, 0xA5A500A3, 0xA5A500A4, 0xA5A500A5, 0xA5A500A6, 0xA5A500A7));
}

TEST_P(GrendelDeviceFirmwareTest, SendDeviceCommandSendsMsgCodeAndArgs) {
    firmware_.publish_queue();

    device_firmware_.send_device_command(HEADER, {0x12345678, 0x9ABCDEF0}, SEND_TIMEOUT, GetParam());

    ASSERT_EQ(firmware_.requests_seen.size(), 1);
    EXPECT_THAT(firmware_.requests_seen[0], ElementsAre(0x5EAD0090, 0x12345678, 0x9ABCDEF0, 0, 0, 0, 0, 0));
}

TEST_P(GrendelDeviceFirmwareTest, SendDeviceCommandThrowsWhenTheQueueIsNotPublished) {
    EXPECT_THROW(device_firmware_.send_device_command(HEADER, {}, SEND_TIMEOUT, GetParam()), error::RuntimeError);
}

// Nothing on the device or in the KMD serializes host users of the queue, so the lock is all that keeps
// another thread or process from interleaving its request.
TEST(GrendelDeviceFirmwareLockTest, SendDeviceCommandWaitsForTheFirmwareMessageLock) {
    FakeSmcFirmware firmware;
    firmware.publish_queue();
    GrendelDeviceFirmware device_firmware(&firmware.protocol, /*kmd_lock_available=*/false);
    LockManager::initialize_mutex(
        MutexType::ARC_MSG, UNUSED_DEVICE_ID, IODeviceType::PCIe, /*kmd_lock_available=*/false);
    firmware.accessed = false;

    std::future<DeviceCommandResult> sent;
    {
        auto held = LockManager::acquire_mutex(MutexType::ARC_MSG, UNUSED_DEVICE_ID, IODeviceType::PCIe);
        sent = std::async(
            std::launch::async, [&] { return device_firmware.send_device_command(HEADER, {}, SEND_TIMEOUT); });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Finding the queue, filling a slot or moving a pointer outside the lock lets another sender interleave.
        EXPECT_FALSE(firmware.accessed);
    }

    EXPECT_EQ(sent.get().exit_code, 0xA5A500A0);
}

}  // namespace
