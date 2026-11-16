// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#ifdef TT_UMD_HAS_DMA_QUEUE

#include <fmt/base.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "tt-umd-workload/chip/chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/chip_helpers/sysmem_manager.hpp"
#include "tt-umd/pcie/dma_queue.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

uint64_t env_or(const char* name, uint64_t fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::stoull(value) : fallback;
}

struct ChipQueue {
    std::unique_ptr<SysmemBuffer> buffer;
    std::unique_ptr<DmaQueue> queue;
    std::vector<tt::xy_pair> banks;
    uint64_t submitted = 0;
    uint64_t completed = 0;
    uint64_t next_write = 0;
};

}  // namespace

// One thread keeps every chip's queue fed with batches of equal writes striped over the DRAM banks, the way
// tt-d-gen's dmk_perf_test addresses them: write i of a chip goes to bank i % banks, at the next slot of an address
// span that every bank cycles through. Shaped by the environment:
//   DMA_QUEUE_WRITE_BYTES (16384), DMA_QUEUE_BATCH_WRITES (64), DMA_QUEUE_IN_FLIGHT (4) batches per chip,
//   DMA_QUEUE_SECONDS (3), DMA_QUEUE_BANKS (all), DMA_QUEUE_SPAN_BYTES (1 GiB) of each bank, starting at 1 GiB,
//   DMA_QUEUE_HINT_RANGE_BYTES (what one batch touches of a bank) to try other window sets.
TEST(MicrobenchmarkDmaQueue, AllChipsStripedOverDramBanks) {
    constexpr uint64_t BUFFER_SIZE = 64ULL * 1024 * 1024;
    constexpr uint64_t DRAM_BASE = 1ULL << 30;
    constexpr uint64_t WINDOW_2M = 2ULL * 1024 * 1024;
    const uint64_t write_bytes = env_or("DMA_QUEUE_WRITE_BYTES", 16384);
    const uint64_t batch_writes = env_or("DMA_QUEUE_BATCH_WRITES", 64);
    const uint64_t in_flight = env_or("DMA_QUEUE_IN_FLIGHT", 4);
    const uint64_t span = env_or("DMA_QUEUE_SPAN_BYTES", 1ULL << 30);
    const auto duration = std::chrono::seconds(env_or("DMA_QUEUE_SECONDS", 3));

    Cluster cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 0});
    std::vector<ChipQueue> chips;
    for (const ChipId chip_id : cluster.get_target_mmio_device_ids()) {
        Chip* chip = cluster.get_chip(chip_id);
        if (chip->get_tt_device()->get_arch() != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "The driver queues DMA writes on Blackhole only.";
        }

        ChipQueue& entry = chips.emplace_back();
        const SocDescriptor& soc = cluster.get_soc_descriptor(chip_id);
        for (int bank = 0; bank < soc.get_num_dram_channels(); bank++) {
            entry.banks.push_back(soc.get_dram_core_for_channel(bank, 0, CoordSystem::TRANSLATED));
        }
        entry.banks.resize(env_or("DMA_QUEUE_BANKS", entry.banks.size()));
        entry.buffer = chip->get_sysmem_manager()->allocate_sysmem_buffer(BUFFER_SIZE);
        // A batch's writes to one bank are adjacent, and may straddle one more 2 MiB block than they fill.
        const uint64_t bank_bytes = (batch_writes / entry.banks.size() + 1) * write_bytes;
        const uint64_t range = env_or("DMA_QUEUE_HINT_RANGE_BYTES", (bank_bytes / WINDOW_2M + 2) * WINDOW_2M);
        entry.queue = std::make_unique<DmaQueue>(
            *chip->get_tt_device()->get_pci_device(), DmaTraffic{static_cast<uint32_t>(entry.banks.size()), range});
    }

    const uint64_t slots = span / write_bytes;
    const uint64_t source_slots = BUFFER_SIZE / write_bytes;
    std::vector<DmaWrite> batch(batch_writes);
    const auto submit_next_batch = [&](ChipQueue& chip) {
        for (DmaWrite& write : batch) {
            const uint64_t i = chip.next_write++;
            write = {
                chip.buffer->get_iova((i % source_slots) * write_bytes),
                chip.banks[i % chip.banks.size()],
                DRAM_BASE + ((i / chip.banks.size()) % slots) * write_bytes,
                static_cast<uint32_t>(write_bytes),
            };
        }
        chip.submitted = chip.queue->submit(batch);
    };

    const auto start = std::chrono::steady_clock::now();
    auto now = start;
    while (now - start < duration) {
        for (ChipQueue& chip : chips) {
            chip.completed = chip.queue->completed();
            while (chip.submitted - chip.completed < in_flight) {
                submit_next_batch(chip);
            }
        }
        now = std::chrono::steady_clock::now();
    }

    uint64_t completed_batches = 0;
    for (ChipQueue& chip : chips) {
        completed_batches += chip.completed;
        chip.queue->wait(chip.submitted);
    }

    const double seconds = std::chrono::duration<double>(now - start).count();
    const double gigabytes = static_cast<double>(completed_batches * batch_writes * write_bytes) / 1e9;
    fmt::print(
        "chips={} banks={} windows={}x{} write_bytes={} batch_writes={} in_flight={} GB/s={:.1f}\n",
        chips.size(),
        chips[0].banks.size(),
        chips[0].queue->window_count(),
        chips[0].queue->window_size(),
        write_bytes,
        batch_writes,
        in_flight,
        gigabytes / seconds);
}

#endif  // TT_UMD_HAS_DMA_QUEUE
