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
    uint64_t detail;         // DMA_DONE: the ROUTE_ taken. INVALID: why. DMA_ERROR / DMA_TIMEOUT: DMAC status.
    uint64_t elapsed_ticks;  // mtime ticks (50 MHz) from doorbell seen to transfer done.
    uint64_t mcause;         // These three only when state is STATE_TRAP.
    uint64_t mepc;
    uint64_t mtval;
};
static_assert(sizeof(DmaStatus) == CACHE_LINE);

constexpr uint64_t STATUS = MAILBOX + sizeof(DmaRequest);

// Low 16 bits are the mailbox ABI version. A kernel with another version must not be driven by this test.
constexpr uint64_t KERNEL_MAGIC = 0x0000'0280'0D3A'0002;

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

// Source block, at this offset in whichever DRAM tile is the source. 16 MiB in: far from the kernel and the mailbox,
// and inside the first 256 MiB, which the DMAC's direct route reaches through DMA_TLB[0] = 0
// (x280_experiments/02_dmac/DMAC_GUIDE.md section 3).
constexpr uint64_t SRC_OFFSET = 0x0100'0000;

// The transfer lands in the middle of a one-page host buffer. The bytes before and after it stay poisoned, so a copy
// that is too long, too short or misplaced shows up. One page also works without an IOMMU, where KMD maps at most a
// page to the NOC.
constexpr size_t HOST_BUFFER_SIZE = 4096;
constexpr size_t DST_OFFSET = 1024;
constexpr size_t DMA_SIZE = 2048;
constexpr uint32_t POISON = 0xBAAD'F00D;

// The kernel's TLB windows are 2 MiB; neither source nor destination may cross one.
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
                << "L2CPU" << IDX << " is released but not running this DMA kernel (ABI v2). Run `tt-smi -r` first.";
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

    // Plants DMA_SIZE bytes at src_dram SRC_OFFSET, has the kernel copy them into a fresh pinned host buffer, and
    // checks the buffer and the route the kernel reports. Call through ASSERT_NO_FATAL_FAILURE after boot_or_attach.
    void dram_to_host(const CoreCoord& src_dram, uint64_t expected_route) {
        const SocDescriptor& soc_desc = tt_device_->get_soc_descriptor();
        ASSERT_TRUE(soc_desc.is_core_of_type(src_dram, CoreType::DRAM, CoordSystem::NOC0))
            << src_dram.str() << " is not a live DRAM tile on this chip.";

        // A pinned host buffer with a NOC address that one TLB window covers.
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

        // Plant the source block and read it back. Poison the whole host buffer.
        const uint64_t txn_id = next_txn_id_++;
        const std::vector<uint32_t> pattern = make_pattern(DMA_SIZE, txn_id);
        tt_device_->write_to_device(pattern.data(), src_dram, SRC_OFFSET, DMA_SIZE);
        std::vector<uint32_t> src_readback(pattern.size());
        tt_device_->read_from_device(src_readback.data(), src_dram, SRC_OFFSET, DMA_SIZE);
        ASSERT_EQ(src_readback, pattern) << "Source block at DRAM " << src_dram.str() << " reads back wrong.";

        const std::vector<uint32_t> poison(HOST_BUFFER_SIZE / sizeof(uint32_t), POISON);
        host_buffer->write_to_sysmem(poison.data(), HOST_BUFFER_SIZE, 0);
        log_info(
            tt::LogUMD,
            "Planted {} B at DRAM {} 0x{:x}, poisoned the host buffer.",
            DMA_SIZE,
            src_dram.str(),
            SRC_OFFSET);

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
            "Rang doorbell for txn {}: {} B from DRAM {} 0x{:x} to NOC 0x{:x} at {}.",
            txn_id,
            DMA_SIZE,
            src_dram.str(),
            SRC_OFFSET,
            dst_noc_addr,
            pcie_core.str());

        // Poll the status line until this request reaches a terminal state or the kernel traps. Log every state
        // seen on the way; with 1 ms polling the short ones may be missed.
        DmaStatus status = read_status();
        bool finished = false;
        uint64_t last_state = status.state;
        const auto deadline = rung_at + DMA_TIMEOUT;
        while (std::chrono::steady_clock::now() < deadline) {
            status = read_status();
            if (status.state != last_state) {
                log_info(
                    tt::LogUMD, "Kernel state {} ({}), txn {}.", status.state, state_name(status.state), status.txn_id);
                last_state = status.state;
            }
            if (status.txn_id == txn_id && (status.state == STATE_TRAP || is_terminal(status.state))) {
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
            "Txn {} DMA_DONE via {} read: kernel {} ticks ({:.1f} us at 50 MHz), host saw it after {} us.",
            txn_id,
            route_name(status.detail),
            status.elapsed_ticks,
            status.elapsed_ticks / 50.0,
            host_us.count());
        EXPECT_EQ(status.detail, expected_route) << "The kernel read the source by the " << route_name(status.detail)
                                                 << " route, expected " << route_name(expected_route) << ".";

        // Check the host buffer: the pattern at DST_OFFSET, poison everywhere else. If it does not match at once,
        // keep re-reading for LATE_DATA_GRACE to tell data that arrived after the done flag from data that never
        // arrived.
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
