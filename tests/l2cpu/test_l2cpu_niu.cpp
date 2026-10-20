// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "l2cpu_test_utils.hpp"

// Can X280 code start NOC requests through the L2CPU tile's NIU command buffers, the way Tensix firmware does? The
// ISA docs and the L2CPU hardware designers say the X280 reaches the NOC only through AXI (TLB windows and the
// DMAC), but nobody has recorded a hardware result. This test boots x280_experiments/03_niu, which programs NIU #0
// command buffer 0 for a copy write, a read and an inline write, and then checks from the host whether any of them
// moved data. Everything below mirrors that kernel's README.md; keep them in sync.

namespace {

// Bank offset of the parameter and result block in the tile's local DRAM. The kernel image must sit below it.
constexpr uint64_t RESULTS = 0x0010'0000;

struct NiuParams {
    uint64_t magic;
    uint32_t dram_x;           // DRAM tile the requests target, NOC0 coordinates.
    uint32_t dram_y;
    uint64_t write_src_local;  // X280 address the copy write reads.
    uint64_t write_dst;        // DRAM tile address the copy write writes.
    uint64_t read_src;         // DRAM tile address the read reads.
    uint64_t read_dst_local;   // X280 address the read writes.
    uint64_t inline_dst;       // DRAM tile address the inline write writes.
    uint32_t inline_value;
    uint32_t length;  // Bytes per copy write and read.
};
static_assert(sizeof(NiuParams) == CACHE_LINE);

struct NiuHeader {
    uint64_t magic;
    uint64_t state;
    uint64_t step;
    uint64_t detail;  // NOC_NODE_ID of NIU #0 as the X280 reads it.
    uint64_t reserved;
    uint64_t mcause;
    uint64_t mepc;
    uint64_t mtval;
};
static_assert(sizeof(NiuHeader) == CACHE_LINE);

constexpr size_t NIU_REG_WORDS = 256;     // NIU offsets 0x000-0x3FF.
constexpr size_t NIU_STATUS_WORDS = 128;  // NIU offsets 0x200-0x3FF.
constexpr size_t NIU_STATUS_OFFSET = 0x200;

struct NiuStep {
    uint32_t outcome;
    uint32_t cmd_ctrl;
    uint32_t cmd_ctrl_before;
    uint32_t counter_index;
    uint32_t counter_before;
    uint32_t counter_after;
    uint32_t reserved0[2];
    uint64_t ready_ticks;
    uint64_t response_ticks;
    uint64_t trap_mcause;
    uint64_t trap_mepc;
    uint64_t trap_mtval;
    uint64_t reserved1[7];
    uint32_t status_before[NIU_STATUS_WORDS];
    uint32_t status_after[NIU_STATUS_WORDS];
};
static_assert(sizeof(NiuStep) == 0x480);

constexpr size_t NUM_STEPS = 5;

struct NiuResults {
    NiuParams params;
    NiuHeader header;
    uint64_t reserved[16];
    uint32_t niu0[NIU_REG_WORDS];
    uint32_t niu1[NIU_REG_WORDS];
    NiuStep steps[NUM_STEPS];
};
static_assert(offsetof(NiuResults, niu0) == 0x100);
static_assert(offsetof(NiuResults, steps) == 0x900);

constexpr uint64_t NIU_KERNEL_MAGIC = 0x0000'0280'4E49'0001;
constexpr uint64_t NIU_PARAMS_MAGIC = 0x0000'0280'504E'0001;

// States. STATE_TRAP (8) shows up only while the kernel moves past a step that trapped.
constexpr uint64_t STATE_DONE = 2;
constexpr uint64_t STATE_BAD_PARAMS = 3;

constexpr size_t STEP_WRITE = 2;
constexpr size_t STEP_READ = 3;
constexpr size_t STEP_INLINE = 4;
constexpr std::array<const char*, NUM_STEPS> STEP_NAMES = {
    "probe NIU #0", "probe NIU #1", "copy write", "read", "inline write"};

constexpr uint32_t OUTCOME_TRAPPED = 5;

const char* outcome_name(uint32_t outcome) {
    static const std::array<const char*, 7> NAMES = {
        "not run",
        "ready, response counter moved",
        "NOC_CMD_CTRL never returned to ready",
        "ready, but no response counted",
        "command buffer busy before programming, nothing issued",
        "trapped",
        "read"};
    return outcome < NAMES.size() ? NAMES[outcome] : "unknown";
}

// L2CPU tile under test. Tile 1, so the experiment doesn't spend tile 0's one release from reset, which the DMA
// tests use. Booting it lowers PLL4 for every L2CPU tile for a moment, including one already running.
constexpr size_t IDX = 1;

// Data, at these offsets in the tile's local bank. The kernel reaches the bank directly at X280 0x3000_0000 + N, and
// over the NOC through the DRAM tile (the requests under test).
constexpr uint64_t X280_DRAM_UNCACHED = 0x3000'0000;
constexpr uint64_t WRITE_SRC = 0x0100'0000;
constexpr uint64_t WRITE_DST = 0x0100'1000;
constexpr uint64_t READ_SRC = 0x0100'2000;
constexpr uint64_t READ_DST = 0x0100'3000;
constexpr uint64_t INLINE_DST = 0x0100'4000;
constexpr uint32_t LENGTH = 64;
constexpr uint32_t INLINE_VALUE = 0x600D'1A7E;

// Each destination is poisoned over twice the transfer, so a short, long or misplaced write shows up.
constexpr size_t CHECK_SIZE = 2 * LENGTH;
constexpr uint32_t POISON = 0xBAAD'F00D;

constexpr auto RUN_TIMEOUT = std::chrono::seconds(5);
constexpr auto POLL_INTERVAL = std::chrono::milliseconds(1);

uint32_t niu_pattern(size_t i, uint32_t seed) { return static_cast<uint32_t>(i * 0x9E37'79B9U) ^ seed; }

// Name of NIU status counter idx (tt-metal blackhole noc_parameters.h).
std::string counter_name(size_t idx) {
    static const std::array<const char*, 16> MST = {
        "MST_ATOMIC_RESP_RECEIVED",
        "MST_WR_ACK_RECEIVED",
        "MST_RD_RESP_RECEIVED",
        "MST_RD_DATA_WORD_RECEIVED",
        "MST_CMD_ACCEPTED",
        "MST_RD_REQ_SENT",
        "MST_NONPOSTED_ATOMIC_SENT",
        "MST_POSTED_ATOMIC_SENT",
        "MST_NONPOSTED_WR_DATA_WORD_SENT",
        "MST_POSTED_WR_DATA_WORD_SENT",
        "MST_NONPOSTED_WR_REQ_SENT",
        "MST_POSTED_WR_REQ_SENT",
        "MST_NONPOSTED_WR_REQ_STARTED",
        "MST_POSTED_WR_REQ_STARTED",
        "MST_RD_REQ_STARTED",
        "MST_NONPOSTED_ATOMIC_STARTED"};
    static const std::array<const char*, 14> SLV = {
        "SLV_ATOMIC_RESP_SENT",
        "SLV_WR_ACK_SENT",
        "SLV_RD_RESP_SENT",
        "SLV_RD_DATA_WORD_SENT",
        "SLV_REQ_ACCEPTED",
        "SLV_RD_REQ_RECEIVED",
        "SLV_NONPOSTED_ATOMIC_RECEIVED",
        "SLV_POSTED_ATOMIC_RECEIVED",
        "SLV_NONPOSTED_WR_DATA_WORD_RECEIVED",
        "SLV_POSTED_WR_DATA_WORD_RECEIVED",
        "SLV_NONPOSTED_WR_REQ_RECEIVED",
        "SLV_POSTED_WR_REQ_RECEIVED",
        "SLV_NONPOSTED_WR_REQ_STARTED",
        "SLV_POSTED_WR_REQ_STARTED"};
    if (idx < 0x10) {
        return MST[idx];
    }
    if (idx < 0x20) {
        return fmt::format("MST_REQS_OUTSTANDING_ID{}", idx - 0x10);
    }
    if (idx < 0x30) {
        return fmt::format("MST_WRITE_REQS_OUTGOING_ID{}", idx - 0x20);
    }
    if (idx < 0x3E) {
        return SLV[idx - 0x30];
    }
    if (idx >= 0x40 && idx < 0x5E) {
        return fmt::format("ROUTER_STATUS{}", idx - 0x40);
    }
    if (idx == 0x5E || idx == 0x5F) {
        return idx == 0x5E ? "TRANS_COUNT_RTZ_NUM" : "TRANS_COUNT_RTZ_SOURCE";
    }
    if (idx >= 0x60) {
        return fmt::format("NIU_STATUS{}", idx - 0x60);
    }
    return fmt::format("STATUS_0x{:02x}", idx);
}

// Non-zero words of an NIU register dump, as "+offset=value".
std::string nonzero_words(const uint32_t* words, size_t count) {
    std::string out;
    for (size_t i = 0; i < count; i++) {
        if (words[i] != 0) {
            out += fmt::format(" +0x{:03x}=0x{:08x}", i * 4, words[i]);
        }
    }
    return out.empty() ? " (all zero)" : out;
}

}  // namespace

class L2CPUNiuTest : public L2CPUDeviceTest {
protected:
    CoreCoord local_dram() const { return CoreCoord(L2CPU_LOCAL_DRAM[IDX], CoreType::DRAM, CoordSystem::NOC0); }

    std::vector<uint32_t> read_words(uint64_t offset, size_t bytes) {
        std::vector<uint32_t> words(bytes / sizeof(uint32_t));
        tt_device_->read_from_device(words.data(), local_dram(), offset, bytes);
        return words;
    }

    // Writes the parameters, the two sources and the poisoned destinations, and clears the result block. Runs after
    // the kernel is loaded and before reset is released.
    void prepare() {
        const std::vector<uint8_t> zero_results(sizeof(NiuResults), 0);
        tt_device_->write_to_device(zero_results.data(), local_dram(), RESULTS, zero_results.size());

        std::vector<uint32_t> source(LENGTH / sizeof(uint32_t));
        for (size_t i = 0; i < source.size(); i++) {
            source[i] = niu_pattern(i, 0xA000'0000);
        }
        tt_device_->write_to_device(source.data(), local_dram(), WRITE_SRC, LENGTH);
        for (size_t i = 0; i < source.size(); i++) {
            source[i] = niu_pattern(i, 0xB000'0000);
        }
        tt_device_->write_to_device(source.data(), local_dram(), READ_SRC, LENGTH);

        const std::vector<uint32_t> poison(CHECK_SIZE / sizeof(uint32_t), POISON);
        for (const uint64_t dst : {WRITE_DST, READ_DST, INLINE_DST}) {
            tt_device_->write_to_device(poison.data(), local_dram(), dst, CHECK_SIZE);
        }

        NiuParams params{};
        params.magic = NIU_PARAMS_MAGIC;
        params.dram_x = static_cast<uint32_t>(local_dram().x);
        params.dram_y = static_cast<uint32_t>(local_dram().y);
        params.write_src_local = X280_DRAM_UNCACHED + WRITE_SRC;
        params.write_dst = WRITE_DST;
        params.read_src = READ_SRC;
        params.read_dst_local = X280_DRAM_UNCACHED + READ_DST;
        params.inline_dst = INLINE_DST;
        params.inline_value = INLINE_VALUE;
        params.length = LENGTH;
        tt_device_->write_to_device(&params, local_dram(), RESULTS, sizeof(params));
    }

    // How many leading words of the destination hold what the request should have written, and how many words
    // after them are not poison any more.
    struct Landed {
        size_t matching_words = 0;
        size_t stray_words = 0;
    };

    Landed check_destination(uint64_t dst, const std::vector<uint32_t>& expected) {
        const std::vector<uint32_t> words = read_words(dst, CHECK_SIZE);
        Landed landed;
        while (landed.matching_words < expected.size() &&
               words[landed.matching_words] == expected[landed.matching_words]) {
            landed.matching_words++;
        }
        for (size_t i = landed.matching_words; i < words.size(); i++) {
            if (words[i] != POISON) {
                landed.stray_words++;
            }
        }
        return landed;
    }

    static void log_step(size_t index, const NiuStep& step) {
        const char* outcome = outcome_name(step.outcome);
        if (index < STEP_WRITE) {
            log_info(tt::LogUMD, "Step {} ({}): {}.", index, STEP_NAMES[index], outcome);
            return;
        }
        log_info(
            tt::LogUMD,
            "Step {} ({}): {}. NOC_CMD_CTRL 0x{:x} before, 0x{:x} after {:.1f} us; {} 0x{:x} -> 0x{:x} after {:.1f} "
            "us more.",
            index,
            STEP_NAMES[index],
            outcome,
            step.cmd_ctrl_before,
            step.cmd_ctrl,
            step.ready_ticks / 50.0,
            counter_name(step.counter_index),
            step.counter_before,
            step.counter_after,
            step.response_ticks / 50.0);
        if (step.outcome == OUTCOME_TRAPPED) {
            log_warning(
                tt::LogUMD,
                "Step {} trapped: mcause={} mepc=0x{:x} mtval=0x{:x}.",
                index,
                step.trap_mcause,
                step.trap_mepc,
                step.trap_mtval);
        }
        std::string changed;
        for (size_t i = 0; i < NIU_STATUS_WORDS; i++) {
            if (step.status_after[i] != step.status_before[i]) {
                changed += fmt::format(
                    " {} 0x{:x}->0x{:x};", counter_name(i), step.status_before[i], step.status_after[i]);
            }
        }
        log_info(
            tt::LogUMD, "Step {} NIU #0 status counters that changed:{}", index, changed.empty() ? " none" : changed);
    }
};

// Boots 03_niu on L2CPU tile IDX, then reports what each command-buffer request did and whether its data arrived. A
// failure of the data checks below is the negative result: the X280 can't start requests through the command buffer
// this way. The log has the counters behind it.
TEST_F(L2CPUNiuTest, CommandBufferFromX280) {
    const char* elf_path = std::getenv("TT_UMD_L2CPU_NIU_ELF");
    if (elf_path == nullptr) {
        GTEST_SKIP() << "Set TT_UMD_L2CPU_NIU_ELF to the 03_niu kernel ELF to run the one-shot NIU experiment.";
    }
    const CoreCoord arc_core = tt_device_->get_soc_descriptor().get_cores(CoreType::ARC).at(0);
    uint32_t l2cpu_reset = 0;
    tt_device_->read_from_device_reg(&l2cpu_reset, arc_core, L2CPU_RESET, sizeof(l2cpu_reset));
    if ((l2cpu_reset >> (4 + IDX)) & 1) {
        GTEST_SKIP() << "L2CPU" << IDX << " was already released since the last chip reset. Run `tt-smi -r` first.";
    }

    const ElfImage elf = read_elf(elf_path);
    ASSERT_NO_FATAL_FAILURE(check_l2cpu_bootable(tt_device_.get(), IDX, elf, DRAM_CACHED + RESULTS));
    ASSERT_NO_FATAL_FAILURE(boot_l2cpu(tt_device_.get(), IDX, elf, [&] { prepare(); }));

    // The kernel runs every step once and then parks. STATE_TRAP shows up while it moves past a step that trapped.
    NiuHeader header{};
    const auto deadline = std::chrono::steady_clock::now() + RUN_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline) {
        tt_device_->read_from_device(&header, local_dram(), RESULTS + offsetof(NiuResults, header), sizeof(header));
        if (header.magic == NIU_KERNEL_MAGIC && (header.state == STATE_DONE || header.state == STATE_BAD_PARAMS)) {
            break;
        }
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    auto results = std::make_unique<NiuResults>();
    tt_device_->read_from_device(results.get(), local_dram(), RESULTS, sizeof(NiuResults));
    header = results->header;
    log_info(
        tt::LogUMD,
        "Kernel: magic 0x{:016x}, state {}, last step {} ({}), NIU #0 NOC_NODE_ID 0x{:08x} -> ({}, {}).",
        header.magic,
        header.state,
        header.step,
        header.step < NUM_STEPS ? STEP_NAMES[header.step] : "none",
        header.detail,
        header.detail & 0x3F,
        (header.detail >> 6) & 0x3F);

    log_info(tt::LogUMD, "NIU #0 (X280 0x2005_6000) non-zero words:{}", nonzero_words(results->niu0, NIU_REG_WORDS));
    log_info(tt::LogUMD, "NIU #1 (X280 0x2005_7000) non-zero words:{}", nonzero_words(results->niu1, NIU_REG_WORDS));
    for (size_t i = 0; i < NUM_STEPS; i++) {
        log_step(i, results->steps[i]);
    }

    ASSERT_EQ(header.magic, NIU_KERNEL_MAGIC) << "No heartbeat from the NIU kernel.";
    ASSERT_NE(header.state, STATE_BAD_PARAMS) << "The kernel rejected its parameters.";
    ASSERT_EQ(header.state, STATE_DONE) << "The kernel did not finish: it hangs in step " << header.step << " ("
                                        << (header.step < NUM_STEPS ? STEP_NAMES[header.step] : "?")
                                        << ") or trapped too often. `tt-smi -r` before the next run.";

    // What arrived, checked from the host.
    std::vector<uint32_t> write_expected(LENGTH / sizeof(uint32_t));
    std::vector<uint32_t> read_expected(LENGTH / sizeof(uint32_t));
    for (size_t i = 0; i < write_expected.size(); i++) {
        write_expected[i] = niu_pattern(i, 0xA000'0000);
        read_expected[i] = niu_pattern(i, 0xB000'0000);
    }
    const Landed write = check_destination(WRITE_DST, write_expected);
    const Landed read = check_destination(READ_DST, read_expected);
    const Landed inline_write = check_destination(INLINE_DST, {INLINE_VALUE});

    const size_t length_words = LENGTH / sizeof(uint32_t);
    log_info(
        tt::LogUMD,
        "Data: copy write {}/{} words landed ({} stray); read {}/{} ({} stray); inline write {}/1 ({} stray).",
        write.matching_words,
        length_words,
        write.stray_words,
        read.matching_words,
        length_words,
        read.stray_words,
        inline_write.matching_words,
        inline_write.stray_words);
    RecordProperty("write_words_landed", std::to_string(write.matching_words));
    RecordProperty("read_words_landed", std::to_string(read.matching_words));
    RecordProperty("inline_words_landed", std::to_string(inline_write.matching_words));
    for (const size_t step : {STEP_WRITE, STEP_READ, STEP_INLINE}) {
        RecordProperty(fmt::format("step{}_outcome", step), std::to_string(results->steps[step].outcome));
    }

    const bool any_landed = write.matching_words + read.matching_words + inline_write.matching_words > 0;
    log_info(
        tt::LogUMD,
        "{}",
        any_landed ? "At least one command-buffer request from the X280 moved data."
                   : "No command-buffer request from the X280 moved any data.");

    EXPECT_EQ(write.matching_words, length_words)
        << "Copy write did not land in full (step: " << outcome_name(results->steps[STEP_WRITE].outcome)
        << "). If nothing landed anywhere, this is the negative result.";
    EXPECT_EQ(read.matching_words, length_words)
        << "Read did not land in full (step: " << outcome_name(results->steps[STEP_READ].outcome) << ").";
    EXPECT_EQ(inline_write.matching_words, 1U)
        << "Inline write did not land (step: " << outcome_name(results->steps[STEP_INLINE].outcome) << ").";
    EXPECT_EQ(write.stray_words + read.stray_words + inline_write.stray_words, 0U)
        << "Something wrote past or around a destination.";
}
