// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "l2cpu_test_utils.hpp"
#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"

// The rest is the mailbox ABI of x280_experiments/02_dmac (README.md there); keep it in sync with the kernel.
// The mailbox is two lines in the tile's local DRAM bank, at bank offset MAILBOX. The host reaches it through the
// DRAM tile and the kernel through its uncached alias (X280 0x3000_0000 + MAILBOX), so neither side goes through the
// L3 and no flushing is needed. The kernel image must sit below it.
constexpr uint64_t MAILBOX = 0x0010'0000;

// Request line, written by the host. The host writes everything before doorbell, reads it back, then writes txn_id
// to doorbell. The kernel serves a request when doorbell differs from the last one it served and equals txn_id.
struct DmaRequest {
    uint64_t src;           // Source: byte offset in the tile's local DRAM bank.
    uint64_t dst_noc_addr;  // Destination: NOC address of the pinned host buffer, as KMD returns it (1 << 60 | IOVA).
    uint32_t dst_noc_x;     // PCIe tile that the destination address is sent to, NOC0 coordinates.
    uint32_t dst_noc_y;
    uint64_t size;      // Bytes to copy.
    uint64_t flags;     // Reserved for selecting a transfer mode; 0.
    uint64_t txn_id;    // Non-zero, new for each request.
    uint64_t reserved;  // 0.
    uint64_t doorbell;  // Written last: txn_id.
};
static_assert(sizeof(DmaRequest) == CACHE_LINE);

// Status line, written by the kernel, right after the request line.
struct DmaStatus {
    uint64_t magic;          // KERNEL_MAGIC once the kernel is up.
    uint64_t state;          // One of the STATE_ values below.
    uint64_t txn_id;         // Request that state refers to; 0 before the first one.
    uint64_t detail;         // Why a request was INVALID or failed (e.g. the DMAC error status); 0 otherwise.
    uint64_t elapsed_ticks;  // mtime ticks (50 MHz) from doorbell seen to transfer done.
    uint64_t mcause;         // These three only when state is STATE_TRAP.
    uint64_t mepc;
    uint64_t mtval;
};
static_assert(sizeof(DmaStatus) == CACHE_LINE);

constexpr uint64_t STATUS = MAILBOX + sizeof(DmaRequest);

// Low 16 bits are the mailbox ABI version. A kernel with another version must not be driven by this test.
constexpr uint64_t KERNEL_MAGIC = 0x0000'0280'0D3A'0001;

constexpr uint64_t STATE_READY = 1;        // Up and waiting for a doorbell.
constexpr uint64_t STATE_RECEIVED = 2;     // Saw the doorbell, checking the request.
constexpr uint64_t STATE_DMA_STARTED = 3;  // DMAC programmed and kicked.
constexpr uint64_t STATE_DMA_DONE = 4;     // DMAC reported the transfer complete. Terminal.
constexpr uint64_t STATE_INVALID = 5;      // Request rejected before any DMA; detail says why. Terminal.
constexpr uint64_t STATE_DMA_ERROR = 6;    // DMAC reported an error; detail holds its status. Terminal.
constexpr uint64_t STATE_DMA_TIMEOUT = 7;  // DMAC did not finish within the kernel's deadline. Terminal.
constexpr uint64_t STATE_TRAP = 8;         // The kernel took an exception; mcause, mepc and mtval are set.

const char* state_name(uint64_t state) {
    switch (state) {
        case 0:
            return "none";
        case STATE_READY:
            return "READY";
        case STATE_RECEIVED:
            return "RECEIVED";
        case STATE_DMA_STARTED:
            return "DMA_STARTED";
        case STATE_DMA_DONE:
            return "DMA_DONE";
        case STATE_INVALID:
            return "INVALID";
        case STATE_DMA_ERROR:
            return "DMA_ERROR";
        case STATE_DMA_TIMEOUT:
            return "DMA_TIMEOUT";
        case STATE_TRAP:
            return "TRAP";
        default:
            return "unknown";
    }
}

bool is_terminal(uint64_t state) {
    return state == STATE_DMA_DONE || state == STATE_INVALID || state == STATE_DMA_ERROR ||
           state == STATE_DMA_TIMEOUT;
}

// Source block in the local DRAM bank. 16 MiB in: far from the kernel and the mailbox, and inside the first 256 MiB,
// which the DMAC's EXTERN master reaches through DMA_TLB[0] = 0 (x280_experiments/02_dmac/DMAC_GUIDE.md section 3).
constexpr uint64_t SRC_OFFSET = 0x0100'0000;

// The transfer lands in the middle of a one-page host buffer. The bytes before and after it stay poisoned, so a copy
// that is too long, too short or misplaced shows up. One page also works without an IOMMU, where KMD maps at most a
// page to the NOC.
constexpr size_t HOST_BUFFER_SIZE = 4096;
constexpr size_t DST_OFFSET = 1024;
constexpr size_t DMA_SIZE = 2048;
constexpr uint32_t POISON = 0xBAAD'F00D;

// The kernel's TLB window to the PCIe tile is 2 MiB; the destination must not cross one.
constexpr uint64_t NOC_WINDOW_SIZE = 2ULL << 20;

constexpr auto READY_TIMEOUT = std::chrono::seconds(5);
constexpr auto DMA_TIMEOUT = std::chrono::seconds(5);
constexpr auto POLL_INTERVAL = std::chrono::milliseconds(1);

// After DMA_DONE, how long to keep re-reading a host buffer that does not match yet. Posted PCIe writes could still
// be in flight when the done flag lands, so a late match is reported rather than failed.
constexpr auto LATE_DATA_GRACE = std::chrono::milliseconds(100);

// Distinct per word and per request, so stale data from an earlier run cannot pass.
std::vector<uint32_t> make_pattern(size_t bytes, uint64_t txn_id) {
    std::vector<uint32_t> words(bytes / sizeof(uint32_t));
    for (size_t i = 0; i < words.size(); i++) {
        words[i] = static_cast<uint32_t>(i * 0x9E37'79B9U) ^ static_cast<uint32_t>(txn_id << 24);
    }
    return words;
}

class L2CPUDmaTest : public L2CPUDeviceTest {
protected:
    void SetUp() override {
        L2CPUDeviceTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        // No hugepage or IOMMU channels: the test only needs its own buffer.
        sysmem_manager_ = std::make_unique<SiliconSysmemManager>(tt_device_.get(), 0);
    }

    void TearDown() override {
        sysmem_manager_.reset();
        L2CPUDeviceTest::TearDown();
    }

    std::unique_ptr<SiliconSysmemManager> sysmem_manager_;
};

// One-shot: plants a block in local DRAM, boots the DMA kernel, asks it to copy the block into a pinned host buffer
// through the mailbox, then checks the buffer. Needs `tt-smi -r` before each run.
TEST_F(L2CPUDmaTest, DramToHost) {
    const char* elf_path = std::getenv("TT_UMD_L2CPU_DMA_ELF");
    if (elf_path == nullptr) {
        GTEST_SKIP() << "Set TT_UMD_L2CPU_DMA_ELF to the DMA kernel ELF to run the one-shot DMA test.";
    }

    const SocDescriptor& soc_desc = tt_device_->get_soc_descriptor();

    // L2CPU tile to boot.
    const size_t idx = 0;
    const CoreCoord dram_core(L2CPU_LOCAL_DRAM[idx], CoreType::DRAM, CoordSystem::NOC0);

    // 1. Pre-flight: tile bootable, kernel below the mailbox, a pinned host buffer with a NOC address that one TLB
    // window covers. Nothing is written to the device before this passes.
    const ElfImage elf = read_elf(elf_path);
    ASSERT_NO_FATAL_FAILURE(check_l2cpu_bootable(tt_device_.get(), idx, elf, DRAM_CACHED + MAILBOX));

    std::unique_ptr<SysmemBuffer> host_buffer = sysmem_manager_->allocate_sysmem_buffer(HOST_BUFFER_SIZE, true);
    ASSERT_TRUE(host_buffer->get_noc_address().has_value()) << "KMD gave the host buffer no NOC address.";
    const uint64_t dst_noc_addr = host_buffer->get_noc_address().value() + DST_OFFSET;
    ASSERT_LE(dst_noc_addr % NOC_WINDOW_SIZE + DMA_SIZE, NOC_WINDOW_SIZE)
        << "Destination 0x" << std::hex << dst_noc_addr << " + 0x" << DMA_SIZE << " crosses a 2 MiB window.";

    const CoreCoord pcie_core = soc_desc.get_cores(CoreType::PCIE, CoordSystem::NOC0).at(0);
    log_info(
        tt::LogUMD,
        "Host buffer: {} B, IOVA 0x{:x}, NOC address 0x{:x} at PCIe tile {} (TRANSLATED {}); IOMMU {}.",
        HOST_BUFFER_SIZE,
        host_buffer->get_iova(),
        host_buffer->get_noc_address().value(),
        pcie_core.str(),
        soc_desc.translate_coord_to(pcie_core, CoordSystem::TRANSLATED).str(),
        tt_device_->get_pci_device()->is_iommu_enabled() ? "on" : "off");

    // 2. Plant the source block in DRAM and read it back. Clear the mailbox, so the kernel finds no doorbell and the
    // host finds no stale status. Poison the whole host buffer.
    const uint64_t txn_id = 1;
    const std::vector<uint32_t> pattern = make_pattern(DMA_SIZE, txn_id);
    tt_device_->write_to_device(pattern.data(), dram_core, SRC_OFFSET, DMA_SIZE);
    std::vector<uint32_t> src_readback(pattern.size());
    tt_device_->read_from_device(src_readback.data(), dram_core, SRC_OFFSET, DMA_SIZE);
    ASSERT_EQ(src_readback, pattern) << "Source block at DRAM " << dram_core.str() << " reads back wrong.";

    const std::array<uint64_t, 2 * CACHE_LINE / sizeof(uint64_t)> zero_mailbox = {};
    tt_device_->write_to_device(zero_mailbox.data(), dram_core, MAILBOX, sizeof(zero_mailbox));

    const std::vector<uint32_t> poison(HOST_BUFFER_SIZE / sizeof(uint32_t), POISON);
    host_buffer->write_to_sysmem(poison.data(), HOST_BUFFER_SIZE, 0);
    log_info(
        tt::LogUMD,
        "Planted {} B at DRAM {} 0x{:x}, cleared the mailbox at 0x{:x}, poisoned the host buffer.",
        DMA_SIZE,
        dram_core.str(),
        SRC_OFFSET,
        MAILBOX);

    // 3. Boot the kernel, as L2CPULoaderTest.Boot does.
    ASSERT_NO_FATAL_FAILURE(boot_l2cpu(tt_device_.get(), idx, elf));

    auto read_status = [&] {
        DmaStatus status{};
        tt_device_->read_from_device(&status, dram_core, STATUS, sizeof(status));
        return status;
    };
    auto log_trap = [&](const DmaStatus& status) {
        log_warning(
            tt::LogUMD,
            "Kernel trapped: mcause={} mepc=0x{:x} mtval=0x{:x}.",
            status.mcause,
            status.mepc,
            status.mtval);
    };

    // 4. Wait until the kernel says it is up and speaks this ABI.
    DmaStatus status{};
    auto deadline = std::chrono::steady_clock::now() + READY_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline) {
        status = read_status();
        if ((status.magic == KERNEL_MAGIC && status.state == STATE_READY) || status.state == STATE_TRAP) {
            break;
        }
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    log_info(
        tt::LogUMD,
        "Kernel status: magic = 0x{:016x}, state {} ({}).",
        status.magic,
        status.state,
        state_name(status.state));
    if (status.state == STATE_TRAP) {
        log_trap(status);
    }
    ASSERT_NE(status.state, STATE_TRAP) << "The kernel took an exception before serving any request.";
    ASSERT_EQ(status.magic, KERNEL_MAGIC) << (status.magic == 0 ? "No heartbeat from the kernel."
                                                                : "The kernel speaks another mailbox ABI version.");
    ASSERT_EQ(status.state, STATE_READY) << "The kernel is up but not waiting for a request.";

    // 5. Fill the request, read it back, then ring the doorbell. The read-back makes sure the request is in DRAM
    // before the doorbell write is sent.
    DmaRequest request{};
    request.src = SRC_OFFSET;
    request.dst_noc_addr = dst_noc_addr;
    request.dst_noc_x = static_cast<uint32_t>(pcie_core.x);
    request.dst_noc_y = static_cast<uint32_t>(pcie_core.y);
    request.size = DMA_SIZE;
    request.flags = 0;
    request.txn_id = txn_id;
    constexpr size_t request_body = offsetof(DmaRequest, doorbell);
    tt_device_->write_to_device(&request, dram_core, MAILBOX, request_body);

    DmaRequest request_readback{};
    tt_device_->read_from_device(&request_readback, dram_core, MAILBOX, request_body);
    ASSERT_EQ(std::memcmp(&request_readback, &request, request_body), 0) << "The mailbox request reads back wrong.";

    tt_device_->write_to_device(&txn_id, dram_core, MAILBOX + offsetof(DmaRequest, doorbell), sizeof(txn_id));
    const auto rung_at = std::chrono::steady_clock::now();
    log_info(
        tt::LogUMD,
        "Rang doorbell for txn {}: {} B from DRAM offset 0x{:x} to NOC 0x{:x} at {}.",
        txn_id,
        DMA_SIZE,
        SRC_OFFSET,
        dst_noc_addr,
        pcie_core.str());

    // 6. Poll the status line until this request reaches a terminal state or the kernel traps. Log every state
    // seen on the way; with 1 ms polling the short ones may be missed.
    bool finished = false;
    uint64_t last_state = status.state;
    deadline = rung_at + DMA_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline) {
        status = read_status();
        if (status.state != last_state) {
            log_info(
                tt::LogUMD, "Kernel state {} ({}), txn {}.", status.state, state_name(status.state), status.txn_id);
            last_state = status.state;
        }
        if (status.state == STATE_TRAP || (status.txn_id == txn_id && is_terminal(status.state))) {
            finished = true;
            break;
        }
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    const auto host_us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - rung_at);

    if (!finished) {
        // The DMAC may still write into the buffer later, so it must stay pinned: leak it on purpose.
        static_cast<void>(host_buffer.release());
        FAIL() << "No terminal state for txn " << txn_id << " within " << DMA_TIMEOUT.count() << " s; last state "
               << state_name(status.state) << " for txn " << status.txn_id << ". Host buffer left pinned.";
    }
    if (status.state == STATE_TRAP) {
        log_trap(status);
    }
    ASSERT_EQ(status.state, STATE_DMA_DONE)
        << "Txn " << txn_id << " ended " << state_name(status.state) << ", detail 0x" << std::hex << status.detail
        << ".";
    log_info(
        tt::LogUMD,
        "Txn {} DMA_DONE: kernel {} ticks ({:.1f} us at 50 MHz), host saw it after {} us.",
        txn_id,
        status.elapsed_ticks,
        status.elapsed_ticks / 50.0,
        host_us.count());

    // 7. Check the host buffer: the pattern at DST_OFFSET, poison everywhere else. If it does not match at once,
    // keep re-reading for LATE_DATA_GRACE to tell data that arrived after the done flag from data that never arrived.
    std::vector<uint32_t> expected = poison;
    std::copy(pattern.begin(), pattern.end(), expected.begin() + DST_OFFSET / sizeof(uint32_t));
    std::vector<uint32_t> received(expected.size());
    auto read_host_buffer = [&] {
        host_buffer->read_from_sysmem(received.data(), HOST_BUFFER_SIZE, 0);
        return received == expected;
    };

    const auto done_seen_at = std::chrono::steady_clock::now();
    bool matches = read_host_buffer();
    while (!matches && std::chrono::steady_clock::now() < done_seen_at + LATE_DATA_GRACE) {
        std::this_thread::sleep_for(POLL_INTERVAL);
        matches = read_host_buffer();
    }
    const auto late_us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - done_seen_at);
    if (matches && late_us >= POLL_INTERVAL) {
        log_warning(
            tt::LogUMD,
            "Host buffer matched only {} us after DMA_DONE was seen: the done flag overtook the data.",
            late_us.count());
    }

    if (!matches) {
        size_t mismatches = 0;
        for (size_t i = 0; i < expected.size(); i++) {
            if (received[i] == expected[i]) {
                continue;
            }
            if (mismatches < 8) {
                const bool in_transfer =
                    i * sizeof(uint32_t) >= DST_OFFSET && i * sizeof(uint32_t) < DST_OFFSET + DMA_SIZE;
                log_warning(
                    tt::LogUMD,
                    "Host buffer +0x{:x} ({}): 0x{:08x}, expected 0x{:08x}.",
                    i * sizeof(uint32_t),
                    in_transfer ? "transfer" : "guard",
                    received[i],
                    expected[i]);
            }
            mismatches++;
        }
        log_warning(tt::LogUMD, "{} of {} words differ.", mismatches, expected.size());
    }
    EXPECT_TRUE(matches) << "The host buffer does not hold the source block at +0x" << std::hex << DST_OFFSET
                         << " with poison around it; see the log.";
}
