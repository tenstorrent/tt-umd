// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <fmt/format.h>

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
    uint64_t src;           // Source: byte offset at DRAM tile (src_noc_x, src_noc_y).
    uint64_t dst_noc_addr;  // Destination: NOC address of the pinned host buffer, as KMD returns it (1 << 60 | IOVA).
    uint32_t dst_noc_x;     // PCIe tile that the destination address is sent to, NOC0 coordinates.
    uint32_t dst_noc_y;
    uint64_t size;       // Bytes to copy.
    uint64_t flags;      // Reserved for selecting a transfer mode; 0.
    uint64_t txn_id;     // Non-zero, new for each request.
    uint32_t src_noc_x;  // Source DRAM tile, NOC0 coordinates. A port of the tile's own bank is read directly; any
    uint32_t src_noc_y;  // other DRAM tile is read over the NOC.
    uint64_t doorbell;   // Written last: txn_id.
};
static_assert(sizeof(DmaRequest) == CACHE_LINE);

// Status line, written by the kernel, right after the request line.
struct DmaStatus {
    uint64_t magic;          // KERNEL_MAGIC once the kernel is up.
    uint64_t state;          // One of the STATE_ values below.
    uint64_t txn_id;         // Request that state refers to; 0 before the first one.
    uint64_t detail;         // DMA_STARTED: bytes done. DMA_DONE: the ROUTE_ taken. INVALID: why. Else DMAC status.
    uint64_t elapsed_ticks;  // mtime ticks (50 MHz) from doorbell seen to transfer done.
    uint64_t mcause;         // These three only when state is STATE_TRAP.
    uint64_t mepc;
    uint64_t mtval;
};
static_assert(sizeof(DmaStatus) == CACHE_LINE);

constexpr uint64_t STATUS = MAILBOX + sizeof(DmaRequest);

// Low 16 bits are the mailbox ABI version. A kernel with another version must not be driven by this test.
constexpr uint64_t KERNEL_MAGIC = 0x0000'0280'0D3A'0003;

constexpr uint64_t STATE_READY = 1;        // Up and waiting for a doorbell.
constexpr uint64_t STATE_RECEIVED = 2;     // Saw the doorbell, checking the request.
constexpr uint64_t STATE_DMA_STARTED = 3;  // DMAC programmed and kicked.
constexpr uint64_t STATE_DMA_DONE = 4;     // DMAC reported the transfer complete. Terminal.
constexpr uint64_t STATE_INVALID = 5;      // Request rejected before any DMA; detail says why. Terminal.
constexpr uint64_t STATE_DMA_ERROR = 6;    // DMAC reported an error; detail holds its status. Terminal.
constexpr uint64_t STATE_DMA_TIMEOUT = 7;  // DMAC did not finish within the kernel's deadline. Terminal.
constexpr uint64_t STATE_TRAP = 8;         // The kernel took an exception; mcause, mepc and mtval are set.

// How the kernel read the source: detail on DMA_DONE.
constexpr uint64_t ROUTE_DIRECT = 1;  // Direct port to the tile's own bank, no NOC.
constexpr uint64_t ROUTE_NOC = 2;     // NOC read from the source DRAM tile.

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

const char* route_name(uint64_t route) {
    return route == ROUTE_DIRECT ? "direct" : route == ROUTE_NOC ? "NOC" : "unknown";
}

bool is_terminal(uint64_t state) {
    return state == STATE_DMA_DONE || state == STATE_INVALID || state == STATE_DMA_ERROR ||
           state == STATE_DMA_TIMEOUT;
}

// Remote source for L2CPUDmaRemoteTest: a port of D6, L2CPU1's bank. The only non-local DRAM tile that has been read
// from L2CPU0 over the NOC (tt-llm-engine src/test_noc.c); untested targets risk a hang that needs `tt-smi -r`.
const tt_xy_pair REMOTE_DRAM = {9, 8};

// Source block, at this offset in whichever DRAM tile is the source. 16 MiB in: far from the kernel and the mailbox.
constexpr uint64_t SRC_OFFSET = 0x0100'0000;

// Both fixtures copy the same 1 GiB, so their times compare directly. At this size the kernel's per-chunk setup and
// the host's 1 ms polling are small next to the transfer.
constexpr size_t DMA_SIZE = 1ULL << 30;

// The transfer lands one page into the pinned host buffer, with a poisoned page before and after it, so a copy that
// is too long, too short or misplaced shows up.
constexpr size_t GUARD_SIZE = 4096;
constexpr size_t DST_OFFSET = GUARD_SIZE;
constexpr size_t HOST_BUFFER_SIZE = GUARD_SIZE + DMA_SIZE + GUARD_SIZE;
constexpr uint32_t POISON = 0xBAAD'F00D;

// The source is written in pieces, so the pattern never exists in full on the host. Only both ends are read back:
// host MMIO reads run at tens of MiB/s, which would make a full read-back take most of a minute.
constexpr size_t PLANT_CHUNK = 64ULL << 20;
constexpr size_t SRC_CHECK_SIZE = 64 * 1024;

constexpr auto READY_TIMEOUT = std::chrono::seconds(5);
constexpr auto POLL_INTERVAL = std::chrono::milliseconds(1);

// The kernel gives up on any 2 MiB chunk after 2 s and reports bytes done while it runs, so the host fails a request
// only when that count stops moving, or when the whole request takes absurdly long.
constexpr auto STALL_TIMEOUT = std::chrono::seconds(10);
constexpr auto DMA_TIMEOUT = std::chrono::minutes(10);
constexpr auto PROGRESS_INTERVAL = std::chrono::seconds(5);

// After DMA_DONE, how long to keep re-checking a host buffer that does not match yet. Posted PCIe writes could still
// be in flight when the done flag lands, so a late match is reported rather than failed.
constexpr auto LATE_DATA_GRACE = std::chrono::seconds(1);

// Word i of the source block for a request: distinct per word and per request, so stale data cannot pass.
uint32_t pattern_word(size_t i, uint64_t txn_id) {
    return static_cast<uint32_t>(i * 0x9E37'79B9U) ^ static_cast<uint32_t>(txn_id << 24);
}

double gb_per_s(size_t bytes, double seconds) { return seconds > 0 ? bytes / seconds / 1e9 : 0; }

// Boots the DMA kernel on L2CPU tile IDX, or attaches to it when an earlier test or run already booted it: a tile
// can leave reset only once per chip reset, and the kernel stays resident between requests. Then copies one block to
// a pinned host buffer per call of dram_to_host().
class L2CPUDmaFixture : public L2CPUDeviceTest {
protected:
    // L2CPU tile under test.
    static constexpr size_t IDX = 0;

    void SetUp() override {
        L2CPUDeviceTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        // Without an IOMMU, KMD maps at most one page of host memory to the NOC.
        if (!tt_device_->get_pci_device()->is_iommu_enabled()) {
            GTEST_SKIP() << "A " << (DMA_SIZE >> 20) << " MiB pinned host buffer needs the IOMMU, which is off.";
        }
        // No hugepage or IOMMU channels: the tests only need their own buffers.
        sysmem_manager_ = std::make_unique<SiliconSysmemManager>(tt_device_.get(), 0);
    }

    void TearDown() override {
        sysmem_manager_.reset();
        L2CPUDeviceTest::TearDown();
    }

    CoreCoord local_dram() const { return CoreCoord(L2CPU_LOCAL_DRAM[IDX], CoreType::DRAM, CoordSystem::NOC0); }

    DmaStatus read_status() {
        DmaStatus status{};
        tt_device_->read_from_device(&status, local_dram(), STATUS, sizeof(status));
        return status;
    }

    static void log_trap(const DmaStatus& status) {
        log_warning(
            tt::LogUMD,
            "Kernel trapped: mcause={} mepc=0x{:x} mtval=0x{:x}.",
            status.mcause,
            status.mepc,
            status.mtval);
    }

    // Leaves the kernel idle and sets next_txn_id_ to an id it has not served. Call through ASSERT_NO_FATAL_FAILURE;
    // skips when the tile needs booting and TT_UMD_L2CPU_DMA_ELF is not set.
    void boot_or_attach() {
        const CoreCoord arc_core = tt_device_->get_soc_descriptor().get_cores(CoreType::ARC).at(0);
        uint32_t l2cpu_reset = 0;
        tt_device_->read_from_device_reg(&l2cpu_reset, arc_core, L2CPU_RESET, sizeof(l2cpu_reset));

        if ((l2cpu_reset >> (4 + IDX)) & 1) {
            // Released: attach, but only to an idle DMA kernel that speaks this ABI.
            const DmaStatus status = read_status();
            log_info(
                tt::LogUMD,
                "L2CPU{} already released; status magic = 0x{:016x}, state {} ({}), txn {}.",
                IDX,
                status.magic,
                status.state,
                state_name(status.state),
                status.txn_id);
            ASSERT_EQ(status.magic, KERNEL_MAGIC)
                << "L2CPU" << IDX << " is released but not running this DMA kernel (ABI v3). Run `tt-smi -r` first.";
            if (status.state == STATE_TRAP) {
                log_trap(status);
            }
            ASSERT_TRUE(status.state == STATE_READY || is_terminal(status.state) || status.state == STATE_TRAP)
                << "The kernel is busy with txn " << status.txn_id << " (" << state_name(status.state) << ").";
            DmaRequest request{};
            tt_device_->read_from_device(&request, local_dram(), MAILBOX, sizeof(request));
            next_txn_id_ = std::max(status.txn_id, request.doorbell) + 1;
            log_info(tt::LogUMD, "Attached to the running DMA kernel; next txn {}.", next_txn_id_);
            return;
        }

        const char* elf_path = std::getenv("TT_UMD_L2CPU_DMA_ELF");
        if (elf_path == nullptr) {
            GTEST_SKIP() << "L2CPU" << IDX
                         << " is in reset: set TT_UMD_L2CPU_DMA_ELF to the DMA kernel ELF to boot it.";
        }

        // Pre-flight: tile bootable, kernel below the mailbox. Nothing is written before this passes.
        const ElfImage elf = read_elf(elf_path);
        ASSERT_NO_FATAL_FAILURE(check_l2cpu_bootable(tt_device_.get(), IDX, elf, DRAM_CACHED + MAILBOX));

        // Clear the mailbox, so the kernel finds no doorbell and the host finds no stale status.
        const std::array<uint64_t, 2 * CACHE_LINE / sizeof(uint64_t)> zero_mailbox = {};
        tt_device_->write_to_device(zero_mailbox.data(), local_dram(), MAILBOX, sizeof(zero_mailbox));

        ASSERT_NO_FATAL_FAILURE(boot_l2cpu(tt_device_.get(), IDX, elf));

        // Wait until the kernel says it is up and speaks this ABI.
        DmaStatus status{};
        const auto deadline = std::chrono::steady_clock::now() + READY_TIMEOUT;
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
        next_txn_id_ = 1;
    }

    // Plants DMA_SIZE bytes at src_dram SRC_OFFSET, has the kernel copy them into a fresh pinned host buffer, checks
    // the buffer and the route the kernel reports, and logs and records the times. Call through
    // ASSERT_NO_FATAL_FAILURE after boot_or_attach.
    void dram_to_host(const CoreCoord& src_dram, uint64_t expected_route) {
        const SocDescriptor& soc_desc = tt_device_->get_soc_descriptor();
        ASSERT_TRUE(soc_desc.is_core_of_type(src_dram, CoreType::DRAM, CoordSystem::NOC0))
            << src_dram.str() << " is not a live DRAM tile on this chip.";

        // A pinned host buffer with a NOC address.
        std::unique_ptr<SysmemBuffer> host_buffer = sysmem_manager_->allocate_sysmem_buffer(HOST_BUFFER_SIZE, true);
        ASSERT_TRUE(host_buffer->get_noc_address().has_value()) << "KMD gave the host buffer no NOC address.";
        const uint64_t dst_noc_addr = host_buffer->get_noc_address().value() + DST_OFFSET;
        uint32_t* const host_words = static_cast<uint32_t*>(host_buffer->get_va());

        const CoreCoord pcie_core = soc_desc.get_cores(CoreType::PCIE, CoordSystem::NOC0).at(0);
        log_info(
            tt::LogUMD,
            "Host buffer: {} MiB, IOVA 0x{:x}, NOC address 0x{:x} at PCIe tile {} (TRANSLATED {}).",
            HOST_BUFFER_SIZE >> 20,
            host_buffer->get_iova(),
            host_buffer->get_noc_address().value(),
            pcie_core.str(),
            soc_desc.translate_coord_to(pcie_core, CoordSystem::TRANSLATED).str());

        // Plant the source block piece by piece, then read back both ends. Poison the whole host buffer.
        const uint64_t txn_id = next_txn_id_++;
        const auto plant_start = std::chrono::steady_clock::now();
        std::vector<uint32_t> piece(PLANT_CHUNK / sizeof(uint32_t));
        for (size_t offset = 0; offset < DMA_SIZE; offset += PLANT_CHUNK) {
            const size_t first_word = offset / sizeof(uint32_t);
            for (size_t i = 0; i < piece.size(); i++) {
                piece[i] = pattern_word(first_word + i, txn_id);
            }
            tt_device_->write_to_device(piece.data(), src_dram, SRC_OFFSET + offset, PLANT_CHUNK);
        }
        const double plant_s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - plant_start).count();

        for (const size_t offset : {size_t{0}, DMA_SIZE - SRC_CHECK_SIZE}) {
            std::vector<uint32_t> readback(SRC_CHECK_SIZE / sizeof(uint32_t));
            tt_device_->read_from_device(readback.data(), src_dram, SRC_OFFSET + offset, SRC_CHECK_SIZE);
            for (size_t i = 0; i < readback.size(); i++) {
                ASSERT_EQ(readback[i], pattern_word(offset / sizeof(uint32_t) + i, txn_id))
                    << "Source block at DRAM " << src_dram.str() << " reads back wrong at +0x" << std::hex
                    << offset + i * sizeof(uint32_t) << ".";
            }
        }

        std::fill(host_words, host_words + HOST_BUFFER_SIZE / sizeof(uint32_t), POISON);
        log_info(
            tt::LogUMD,
            "Planted {} MiB at DRAM {} 0x{:x} in {:.2f} s ({:.2f} GB/s host to device), poisoned the host buffer.",
            DMA_SIZE >> 20,
            src_dram.str(),
            SRC_OFFSET,
            plant_s,
            gb_per_s(DMA_SIZE, plant_s));

        // Fill the request, read it back, then ring the doorbell. The read-back makes sure the request is in DRAM
        // before the doorbell write is sent.
        DmaRequest request{};
        request.src = SRC_OFFSET;
        request.dst_noc_addr = dst_noc_addr;
        request.dst_noc_x = static_cast<uint32_t>(pcie_core.x);
        request.dst_noc_y = static_cast<uint32_t>(pcie_core.y);
        request.size = DMA_SIZE;
        request.flags = 0;
        request.txn_id = txn_id;
        request.src_noc_x = static_cast<uint32_t>(src_dram.x);
        request.src_noc_y = static_cast<uint32_t>(src_dram.y);
        constexpr size_t request_body = offsetof(DmaRequest, doorbell);
        tt_device_->write_to_device(&request, local_dram(), MAILBOX, request_body);

        DmaRequest request_readback{};
        tt_device_->read_from_device(&request_readback, local_dram(), MAILBOX, request_body);
        ASSERT_EQ(std::memcmp(&request_readback, &request, request_body), 0) << "The mailbox request reads back wrong.";

        tt_device_->write_to_device(&txn_id, local_dram(), MAILBOX + offsetof(DmaRequest, doorbell), sizeof(txn_id));
        const auto rung_at = std::chrono::steady_clock::now();
        log_info(
            tt::LogUMD,
            "Rang doorbell for txn {}: {} MiB from DRAM {} 0x{:x} to NOC 0x{:x} at {}.",
            txn_id,
            DMA_SIZE >> 20,
            src_dram.str(),
            SRC_OFFSET,
            dst_noc_addr,
            pcie_core.str());

        // Poll the status line until this request reaches a terminal state or the kernel traps. Log every state
        // change and, while the copy runs, its progress. Fail if the bytes-done count stops moving.
        DmaStatus status = read_status();
        bool finished = false;
        uint64_t last_state = status.state;
        uint64_t last_progress = 0;
        auto progress_at = rung_at;
        auto logged_at = rung_at;
        const char* stop_reason = "the request deadline passed";
        while (std::chrono::steady_clock::now() < rung_at + DMA_TIMEOUT) {
            status = read_status();
            const auto now = std::chrono::steady_clock::now();
            if (status.state != last_state) {
                log_info(
                    tt::LogUMD, "Kernel state {} ({}), txn {}.", status.state, state_name(status.state), status.txn_id);
                last_state = status.state;
                progress_at = now;
            }
            if (status.txn_id == txn_id && (status.state == STATE_TRAP || is_terminal(status.state))) {
                finished = true;
                break;
            }
            if (status.txn_id == txn_id && status.state == STATE_DMA_STARTED) {
                if (status.detail != last_progress) {
                    last_progress = status.detail;
                    progress_at = now;
                }
                if (now - logged_at >= PROGRESS_INTERVAL) {
                    log_info(
                        tt::LogUMD,
                        "Txn {}: {} of {} MiB copied after {:.1f} s.",
                        txn_id,
                        last_progress >> 20,
                        DMA_SIZE >> 20,
                        std::chrono::duration<double>(now - rung_at).count());
                    logged_at = now;
                }
            }
            if (now - progress_at >= STALL_TIMEOUT) {
                stop_reason = "the kernel stopped making progress";
                break;
            }
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
        const double host_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - rung_at).count();

        if (!finished) {
            // The DMAC may still write into the buffer later, so it must stay pinned: leak it on purpose.
            static_cast<void>(host_buffer.release());
            FAIL() << "No terminal state for txn " << txn_id << ": " << stop_reason << " after " << host_s
                   << " s; last state " << state_name(status.state) << " for txn " << status.txn_id << ", "
                   << (last_progress >> 20) << " MiB copied. Host buffer left pinned.";
        }
        if (status.state == STATE_TRAP) {
            log_trap(status);
        }
        ASSERT_EQ(status.state, STATE_DMA_DONE)
            << "Txn " << txn_id << " ended " << state_name(status.state) << ", detail 0x" << std::hex << status.detail
            << ".";
        EXPECT_EQ(status.detail, expected_route) << "The kernel read the source by the " << route_name(status.detail)
                                                 << " route, expected " << route_name(expected_route) << ".";

        // Times: the kernel's, from doorbell seen to transfer done (mtime, 50 MHz), and the host's, from the doorbell
        // write to seeing DMA_DONE (1 ms polling).
        const double kernel_s = static_cast<double>(status.elapsed_ticks) / 50e6;
        log_info(
            tt::LogUMD,
            "D2H {} read, {} MiB from DRAM {}: kernel {:.3f} ms ({:.3f} GB/s), host {:.3f} ms ({:.3f} GB/s).",
            route_name(status.detail),
            DMA_SIZE >> 20,
            src_dram.str(),
            kernel_s * 1e3,
            gb_per_s(DMA_SIZE, kernel_s),
            host_s * 1e3,
            gb_per_s(DMA_SIZE, host_s));
        RecordProperty("route", route_name(status.detail));
        RecordProperty("bytes", std::to_string(DMA_SIZE));
        RecordProperty("kernel_ticks", std::to_string(status.elapsed_ticks));
        RecordProperty("kernel_ms", fmt::format("{:.3f}", kernel_s * 1e3));
        RecordProperty("kernel_gb_per_s", fmt::format("{:.3f}", gb_per_s(DMA_SIZE, kernel_s)));
        RecordProperty("host_ms", fmt::format("{:.3f}", host_s * 1e3));
        RecordProperty("host_gb_per_s", fmt::format("{:.3f}", gb_per_s(DMA_SIZE, host_s)));

        // Check the host buffer in place: the pattern at DST_OFFSET, poison in the guard pages. If it does not match
        // at once, keep re-checking for LATE_DATA_GRACE to tell data that arrived after the done flag from data that
        // never arrived.
        const size_t total_words = HOST_BUFFER_SIZE / sizeof(uint32_t);
        const size_t first_transfer_word = DST_OFFSET / sizeof(uint32_t);
        const size_t end_transfer_word = first_transfer_word + DMA_SIZE / sizeof(uint32_t);
        auto expected_word = [&](size_t i) {
            return i >= first_transfer_word && i < end_transfer_word ? pattern_word(i - first_transfer_word, txn_id)
                                                                      : POISON;
        };
        auto count_mismatches = [&](bool log_first) {
            size_t mismatches = 0;
            for (size_t i = 0; i < total_words; i++) {
                const uint32_t expected = expected_word(i);
                if (host_words[i] == expected) {
                    continue;
                }
                if (log_first && mismatches < 8) {
                    const bool in_transfer = i >= first_transfer_word && i < end_transfer_word;
                    log_warning(
                        tt::LogUMD,
                        "Host buffer +0x{:x} ({}): 0x{:08x}, expected 0x{:08x}.",
                        i * sizeof(uint32_t),
                        in_transfer ? "transfer" : "guard",
                        host_words[i],
                        expected);
                }
                mismatches++;
            }
            return mismatches;
        };

        const auto done_seen_at = std::chrono::steady_clock::now();
        size_t mismatches = count_mismatches(false);
        const bool matched_at_once = mismatches == 0;
        while (mismatches != 0 && std::chrono::steady_clock::now() < done_seen_at + LATE_DATA_GRACE) {
            std::this_thread::sleep_for(POLL_INTERVAL);
            mismatches = count_mismatches(false);
        }
        if (mismatches == 0 && !matched_at_once) {
            log_warning(
                tt::LogUMD,
                "Host buffer matched only {:.1f} ms after DMA_DONE was seen: the done flag overtook the data.",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - done_seen_at).count());
        }
        if (mismatches != 0) {
            count_mismatches(true);
            log_warning(tt::LogUMD, "{} of {} words differ.", mismatches, total_words);
        }
        EXPECT_EQ(mismatches, 0U) << "The host buffer does not hold the source block at +0x" << std::hex << DST_OFFSET
                                  << " with poison around it; see the log.";
    }

    std::unique_ptr<SiliconSysmemManager> sysmem_manager_;
    uint64_t next_txn_id_ = 0;
};

// Source in the tile's own DRAM bank: the kernel reads it through its direct port, no NOC.
class L2CPUDmaTest : public L2CPUDmaFixture {};

// Source in another bank: the kernel reads it over the NOC through a CPU NoC TLB window.
class L2CPUDmaRemoteTest : public L2CPUDmaFixture {};

// Boots the DMA kernel (or attaches to it) and copies a block of the local bank to a pinned host buffer. The first
// test after `tt-smi -r` boots; later ones attach.
TEST_F(L2CPUDmaTest, DramToHost) {
    ASSERT_NO_FATAL_FAILURE(boot_or_attach());
    if (IsSkipped()) {
        return;
    }
    ASSERT_NO_FATAL_FAILURE(dram_to_host(local_dram(), ROUTE_DIRECT));
}

// Same, but the block sits at REMOTE_DRAM, so the DMAC reads it over the NOC before writing it to the host.
TEST_F(L2CPUDmaRemoteTest, RemoteDramToHost) {
    ASSERT_NO_FATAL_FAILURE(boot_or_attach());
    if (IsSkipped()) {
        return;
    }
    ASSERT_NO_FATAL_FAILURE(dram_to_host(CoreCoord(REMOTE_DRAM, CoreType::DRAM, CoordSystem::NOC0), ROUTE_NOC));
}
