// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Local prototype, not for merging: drives Blackhole HDMA read channel 0 in linked-list mode straight
// through BAR2, to find out whether descriptor chaining works before building it into PcieProtocol.
//
// Register offsets and bit fields come from the Synopsys DWC PCIe DM databook v5.97a (gen5 controller,
// HDMA chapter 7) and the Linux dw-hdma-v0 driver. Linked-list elements are read from the controller's
// local (AXI) side, so the transfer list lives in device memory, written through a TLB window, and the
// channel is pointed at that window's AXI address.

#include <gtest/gtest.h>
#include <immintrin.h>
#include <sys/mman.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/chip/chip.hpp"
#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/chip_helpers/sysmem_manager.hpp"
#include "tt-umd/cluster.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/tlb_handle.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/dma.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/tlb.hpp"
#include "tt-umd/utils/lock_manager.hpp"

using namespace tt::umd;

namespace {

// HDMA read channel 0 registers, relative to BAR2 (BAR2 offset 0 is the HDMA block, RDCH_0 at 0x100).
constexpr uint32_t EN = 0x100;
constexpr uint32_t DOORBELL = 0x104;
constexpr uint32_t ELEM_PF = 0x108;
constexpr uint32_t LLP_LOW = 0x110;
constexpr uint32_t LLP_HIGH = 0x114;
constexpr uint32_t CYCLE = 0x118;
constexpr uint32_t XFERSIZE = 0x11C;
constexpr uint32_t CONTROL1 = 0x134;
constexpr uint32_t QOS = 0x13C;
constexpr uint32_t STATUS = 0x180;
constexpr uint32_t INT_STATUS = 0x184;
constexpr uint32_t INT_SETUP = 0x188;
constexpr uint32_t INT_CLEAR = 0x18C;
constexpr uint32_t MSI_STOP_LOW = 0x190;
constexpr uint32_t MSI_STOP_HIGH = 0x194;
// The databook table names 0x198 the abort address and 0x1A0 the watermark one; Linux has them the other way
// round. Both are pointed at the abort flag, so the stop flag stays unambiguous either way.
constexpr uint32_t MSI_0x198_LOW = 0x198;
constexpr uint32_t MSI_0x198_HIGH = 0x19C;
constexpr uint32_t MSI_0x1A0_LOW = 0x1A0;
constexpr uint32_t MSI_0x1A0_HIGH = 0x1A4;
constexpr uint32_t MSI_MSGD = 0x1A8;

constexpr uint32_t DOORBELL_START = 1u << 0;
constexpr uint32_t CONTROL1_LLEN = 1u << 0;
constexpr uint32_t CYCLE_BIT = 1u << 0;
constexpr uint32_t CYCLE_STATE = 1u << 1;
constexpr uint32_t STATUS_MASK = 0x3;
constexpr uint32_t STATUS_RUNNING = 1;
constexpr uint32_t STATUS_STOPPED = 3;
// INT_SETUP: local enables latch INT_STATUS, remote enables send the MSI, masks keep the local pins quiet.
constexpr uint32_t INT_STOP_MASK = 1u << 0;
constexpr uint32_t INT_ABORT_MASK = 1u << 2;
constexpr uint32_t INT_REMOTE_STOP_EN = 1u << 3;
constexpr uint32_t INT_LOCAL_STOP_EN = 1u << 4;
constexpr uint32_t INT_REMOTE_ABORT_EN = 1u << 5;
constexpr uint32_t INT_LOCAL_ABORT_EN = 1u << 6;

// Linked-list element control DWORD.
constexpr uint32_t ELEMENT_CB = 1u << 0;

// The channel fetches ELEMENT_PREFETCH + 1 elements ahead, capped by the field width and by QOS.PF_DEPTH, the
// elements it can hold minus one. PF_DEPTH resets to the build-time queue depth minus one (sw_reference v5.97a).
constexpr uint32_t ELEMENT_PREFETCH_MAX = 0x7F;
constexpr uint32_t QOS_PF_DEPTH_SHIFT = 16;
constexpr uint32_t QOS_PF_DEPTH_MASK = 0x3FF;

constexpr uint32_t COMPLETION_VALUE = 0xfaca;
constexpr size_t WINDOW_SIZE = 2 * 1024 * 1024;
constexpr size_t CHUNK_SIZE = 16 * 1024;
// Where the transfer list is kept on the device, clear of the data written at the bottom of the banks.
constexpr uint64_t LIST_DEVICE_ADDR = 0x1000000;
constexpr auto COMPLETION_TIMEOUT = std::chrono::seconds(2);

// One linked-list data element, six DWORDs.
struct DataElement {
    uint32_t control;
    uint32_t size;
    uint64_t sar;
    uint64_t dar;
};

static_assert(sizeof(DataElement) == 24);

// How the host writes the transfer list through the list window.
enum class ListWrite { Uc32, Uc64, Wc };

struct ChainResult {
    bool stopped;
    bool aborted;
    uint32_t status;
    uint32_t int_status;
    uint32_t llp_low;
    uint32_t xfersize;
    // Doorbell to completion flag.
    double microseconds;
    std::chrono::steady_clock::time_point doorbell_at;
};

}  // namespace

class DmaLinkedListBh : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No Tenstorrent PCI devices found.";
        }
        if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
            GTEST_SKIP() << "Host-side source buffers need the IOMMU.";
        }

        cluster_ = test_utils::make_default_test_cluster();
        const ChipId chip = *cluster_->get_target_mmio_device_ids().begin();
        tt_device_ = cluster_->get_chip(chip)->get_tt_device();
        if (tt_device_->get_arch() != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "Blackhole HDMA only.";
        }
        // Same clock state as the DMA microbenchmark, so throughput numbers compare.
        cluster_->set_clock_state(DevicePowerState::BUSY);
        pci_device_ = tt_device_->get_pci_device();
        if (pci_device_->get_dma_buffer().completion == nullptr) {
            GTEST_SKIP() << "No DMA completion page was allocated.";
        }
        bar2_ = static_cast<volatile uint8_t*>(pci_device_->bar2_uc);

        const SocDescriptor& soc = cluster_->get_soc_descriptor(chip);
        // One NOC port per bank: the ports of a bank all reach the same memory.
        for (const std::vector<CoreCoord>& bank_ports : soc.get_dram_cores()) {
            dram_cores_.push_back(bank_ports.front());
            dram_xy_.push_back(soc.translate_chip_coord_to_translated(bank_ports.front(), get_selected_noc_id()));
        }
        SysmemManager* sysmem = cluster_->get_chip(chip)->get_sysmem_manager();
        if (std::getenv("DMA_LL_REGULAR_PAGES") != nullptr) {
            buffer_ = sysmem->allocate_sysmem_buffer(WINDOW_SIZE);
        } else {
            buffer_ = sysmem->map_sysmem_buffer(allocate_huge_page_backed(), WINDOW_SIZE);
        }
        sysmem_ = sysmem;
        pcie_xy_ = soc.translate_chip_coord_to_translated(soc.get_cores(CoreType::PCIE)[0], get_selected_noc_id());

        engine_lock_ =
            LockManager::acquire_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);

        // The list window is written by the host and read by the engine, so it is kept Strict.
        list_window_ = open_window(dram_xy_.back(), LIST_DEVICE_ADDR, WindowFlags::Bidirectional, tlb_data::Strict);
    }

    void TearDown() override {
        if (bar2_ == nullptr || engine_lock_.mutex() == nullptr) {
            return;
        }
        // Leave the channel the way register-mode transfers expect it: linked-list mode off, nothing running.
        if ((read_reg(STATUS) & STATUS_MASK) == STATUS_RUNNING) {
            write_reg(EN, 0);
        }
        write_reg(CONTROL1, 0);
        write_reg(ELEM_PF, 0);
        write_reg(INT_CLEAR, INT_STOP_MASK | INT_ABORT_MASK);
        cluster_->set_clock_state(DevicePowerState::LONG_IDLE);
    }

    ~DmaLinkedListBh() override {
        hairpin_window_.reset();
        host_list_buffer_.reset();
        buffer_.reset();
        if (mapping_ != nullptr) {
            munmap(mapping_, MAPPING_SIZE);
        }
    }

    // A 2 MiB-aligned buffer the kernel backs with one transparent huge page, so the IOMMU can map it with one
    // large page instead of 512 small ones.
    void* allocate_huge_page_backed() {
        mapping_ = mmap(nullptr, MAPPING_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        EXPECT_NE(mapping_, MAP_FAILED);
        const uintptr_t aligned = (reinterpret_cast<uintptr_t>(mapping_) + WINDOW_SIZE - 1) & ~(WINDOW_SIZE - 1);
        void* buffer = reinterpret_cast<void*>(aligned);
        EXPECT_EQ(madvise(buffer, WINDOW_SIZE, MADV_HUGEPAGE), 0);
        std::memset(buffer, 0, WINDOW_SIZE);
        std::ifstream smaps("/proc/self/smaps_rollup");
        for (std::string line; std::getline(smaps, line);) {
            if (line.rfind("AnonHugePages", 0) == 0) {
                std::printf("host buffer: %s\n", line.c_str());
            }
        }
        return buffer;
    }

    // Puts the transfer list in a host buffer bound to the NOC and points the engine at it through a window aimed at
    // the PCIe tile's own host-memory range: the engine's list reads go out through the NOC, back into the PCIe tile
    // and up to the host.
    void use_host_list(size_t bytes) {
        host_list_buffer_ = sysmem_->allocate_sysmem_buffer(bytes, /*map_to_noc=*/true);
        ASSERT_TRUE(host_list_buffer_->get_noc_address().has_value());
        const uint64_t noc_addr = *host_list_buffer_->get_noc_address();
        ASSERT_LE((noc_addr & (WINDOW_SIZE - 1)) + bytes, WINDOW_SIZE) << "host list crosses a window";
        hairpin_window_ = open_window(pcie_xy_, noc_addr, WindowFlags::Bidirectional, tlb_data::Relaxed);
        host_list_ = static_cast<uint8_t*>(host_list_buffer_->get_va());
        std::memset(host_list_, 0, bytes);
        host_list_axi_ = axi_address(*hairpin_window_, noc_addr);
        std::printf(
            "host list: NOC %#llx on PCIe tile (%zu, %zu), engine reads it at AXI %#llx\n",
            static_cast<unsigned long long>(noc_addr),
            pcie_xy_.x,
            pcie_xy_.y,
            static_cast<unsigned long long>(host_list_axi_));
    }

    void write_reg(uint32_t offset, uint32_t value) { *reinterpret_cast<volatile uint32_t*>(bar2_ + offset) = value; }

    uint32_t read_reg(uint32_t offset) { return *reinterpret_cast<volatile uint32_t*>(bar2_ + offset); }

    static tlb_data window_config(xy_pair core, uint64_t addr, WindowFlags flags, uint64_t ordering) {
        tlb_data config{};
        config.local_offset = addr & ~(WINDOW_SIZE - 1);
        config.x_end = core.x;
        config.y_end = core.y;
        config.noc_sel = static_cast<uint64_t>(get_selected_noc_id());
        config.ordering = ordering;
        config.set_static_vc(get_architecture_tlbs(tt::ARCH::BLACKHOLE).get_static_vc(flags));
        return config;
    }

    std::unique_ptr<TlbHandle> open_window(
        xy_pair core, uint64_t addr, WindowFlags flags, uint64_t ordering, TlbMapping mapping = TlbMapping::UC) {
        auto handle = pci_device_->allocate_tlb(WINDOW_SIZE, mapping, /*verify_config=*/true);
        handle->configure(window_config(core, addr, flags, ordering));
        return handle;
    }

    // Address the engine reaches device address addr through, given a window aimed at addr's 2 MiB block.
    static uint64_t axi_address(const TlbHandle& window, uint64_t addr) {
        return get_architecture_tlbs(tt::ARCH::BLACKHOLE).get_configuration(window.get_tlb_id()).tlb_offset +
               (addr & (WINDOW_SIZE - 1));
    }

    // Writes the list, then one more element whose cycle bit does not match, which is where the engine stops.
    // The engine reads nothing before the doorbell, so the order of the stores within the list does not matter.
    void write_list(const std::vector<DataElement>& elements, ListWrite mode) {
        std::vector<DataElement> with_end = elements;
        with_end.push_back(DataElement{});
        // A list in host memory is ordinary cached memory the engine reads coherently, so plain stores ordered
        // before the doorbell are enough: no window, no read back.
        if (host_list_ != nullptr) {
            if (std::getenv("DMA_LL_HOST_LIST_NT") != nullptr) {
                // Non-temporal stores send the list to DRAM, so the engine's fetches do not hit lines the CPU holds.
                long long* list = reinterpret_cast<long long*>(host_list_);
                const long long* words = reinterpret_cast<const long long*>(with_end.data());
                for (size_t w = 0; w < with_end.size() * sizeof(DataElement) / sizeof(long long); w++) {
                    _mm_stream_si64(list + w, words[w]);
                }
            } else {
                std::memcpy(host_list_, with_end.data(), with_end.size() * sizeof(DataElement));
            }
            _mm_sfence();
            return;
        }
        uint8_t* base = list_window_->get_base();
        switch (mode) {
            case ListWrite::Uc32: {
                volatile uint32_t* list = reinterpret_cast<volatile uint32_t*>(base);
                const uint32_t* words = reinterpret_cast<const uint32_t*>(with_end.data());
                for (size_t w = 0; w < with_end.size() * sizeof(DataElement) / sizeof(uint32_t); w++) {
                    list[w] = words[w];
                }
                break;
            }
            case ListWrite::Uc64: {
                volatile uint64_t* list = reinterpret_cast<volatile uint64_t*>(base);
                const uint64_t* words = reinterpret_cast<const uint64_t*>(with_end.data());
                for (size_t w = 0; w < with_end.size() * sizeof(DataElement) / sizeof(uint64_t); w++) {
                    list[w] = words[w];
                }
                break;
            }
            case ListWrite::Wc:
                std::memcpy(base, with_end.data(), with_end.size() * sizeof(DataElement));
                // Drains the write-combining buffers, so the flush read below is ordered behind every store.
                _mm_sfence();
                break;
        }
        // A read through the same window makes the list land before the doorbell goes out on BAR2.
        if (flush_list_) {
            (void)*reinterpret_cast<volatile uint32_t*>(base + (with_end.size() - 1) * sizeof(DataElement));
        }
    }

    ChainResult run_chain(const std::vector<DataElement>& elements, ListWrite mode = ListWrite::Uc32) {
        write_list(elements, mode);

        DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();
        volatile uint32_t* stop_flag = reinterpret_cast<volatile uint32_t*>(dma_buffer.completion);
        volatile uint32_t* abort_flag = stop_flag + 1;
        const uint64_t stop_pa = dma_buffer.completion_pa;
        const uint64_t abort_pa = dma_buffer.completion_pa + sizeof(uint32_t);
        const uint64_t list_axi = host_list_ != nullptr ? host_list_axi_ : axi_address(*list_window_, LIST_DEVICE_ADDR);

        write_reg(EN, 1);
        write_reg(
            INT_SETUP,
            INT_LOCAL_STOP_EN | INT_LOCAL_ABORT_EN | INT_REMOTE_STOP_EN | INT_REMOTE_ABORT_EN | INT_STOP_MASK |
                INT_ABORT_MASK);
        write_reg(MSI_STOP_LOW, static_cast<uint32_t>(stop_pa));
        write_reg(MSI_STOP_HIGH, static_cast<uint32_t>(stop_pa >> 32));
        write_reg(MSI_0x198_LOW, static_cast<uint32_t>(abort_pa));
        write_reg(MSI_0x198_HIGH, static_cast<uint32_t>(abort_pa >> 32));
        write_reg(MSI_0x1A0_LOW, static_cast<uint32_t>(abort_pa));
        write_reg(MSI_0x1A0_HIGH, static_cast<uint32_t>(abort_pa >> 32));
        write_reg(MSI_MSGD, COMPLETION_VALUE);
        write_reg(INT_CLEAR, INT_STOP_MASK | INT_ABORT_MASK);
        write_reg(CONTROL1, CONTROL1_LLEN);
        write_reg(LLP_LOW, static_cast<uint32_t>(list_axi));
        write_reg(LLP_HIGH, static_cast<uint32_t>(list_axi >> 32));
        write_reg(CYCLE, CYCLE_STATE | CYCLE_BIT);
        *stop_flag = 0;
        *abort_flag = 0;

        const auto start = std::chrono::steady_clock::now();
        write_reg(DOORBELL, DOORBELL_START);
        while (*stop_flag != COMPLETION_VALUE && *abort_flag != COMPLETION_VALUE &&
               std::chrono::steady_clock::now() - start < COMPLETION_TIMEOUT) {
        }
        const auto end = std::chrono::steady_clock::now();

        ChainResult result{
            *stop_flag == COMPLETION_VALUE,
            *abort_flag == COMPLETION_VALUE,
            0,
            0,
            0,
            0,
            std::chrono::duration<double, std::micro>(end - start).count(),
            start};
        // The diagnostic reads are uncached PCIe round trips, which a timed chain must not pay.
        if (!quiet_) {
            result.status = read_reg(STATUS);
            result.int_status = read_reg(INT_STATUS);
            result.llp_low = read_reg(LLP_LOW);
            result.xfersize = read_reg(XFERSIZE);
            std::printf(
                "chain of %zu: stop_flag=%d abort_flag=%d status=%#x int_status=%#x llp_low=%#x (list at %#llx) "
                "xfersize=%#x %.1f us\n",
                elements.size(),
                result.stopped,
                result.aborted,
                result.status,
                result.int_status,
                result.llp_low,
                static_cast<unsigned long long>(list_axi),
                result.xfersize,
                result.microseconds);
        }
        write_reg(INT_CLEAR, INT_STOP_MASK | INT_ABORT_MASK);
        write_reg(CONTROL1, 0);
        return result;
    }

    // Chunks of the host buffer laid back to back on one DRAM core, one element each.
    std::vector<DataElement> contiguous_chain(
        size_t count, const TlbHandle& data_window, uint64_t device_addr, size_t chunk_size = CHUNK_SIZE) {
        std::vector<DataElement> elements;
        elements.reserve(count);
        for (size_t i = 0; i < count; i++) {
            elements.push_back(DataElement{
                ELEMENT_CB,
                static_cast<uint32_t>(chunk_size),
                buffer_->get_iova(i * chunk_size),
                axi_address(data_window, device_addr + i * chunk_size)});
        }
        return elements;
    }

    // Fills the source and pushes it out of the CPU caches: the engine reads host memory about 1.5x faster from DRAM
    // than from lines the CPU still holds.
    void fill_host(size_t size) {
        uint8_t* data = static_cast<uint8_t*>(buffer_->get_va());
        test_utils::fill_with_random_bytes(data, size);
        if (std::getenv("DMA_LL_KEEP_IN_CACHE") == nullptr) {
            for (size_t line = 0; line < size; line += 64) {
                _mm_clflush(data + line);
            }
            _mm_sfence();
        }
    }

    void expect_on_device(size_t host_offset, const CoreCoord& core, uint64_t device_addr, size_t size) {
        std::vector<uint8_t> readback(size);
        tt_device_->read_from_device(readback.data(), core, device_addr, size);
        EXPECT_EQ(std::memcmp(readback.data(), static_cast<uint8_t*>(buffer_->get_va()) + host_offset, size), 0)
            << "host offset " << host_offset << ", device address " << device_addr;
    }

    std::unique_ptr<Cluster> cluster_;
    TTDevice* tt_device_ = nullptr;
    PCIDevice* pci_device_ = nullptr;
    volatile uint8_t* bar2_ = nullptr;
    std::vector<CoreCoord> dram_cores_;
    std::vector<xy_pair> dram_xy_;
    static constexpr size_t MAPPING_SIZE = 2 * WINDOW_SIZE;
    void* mapping_ = nullptr;
    std::unique_ptr<SysmemBuffer> buffer_;
    std::unique_lock<MutexInterface> engine_lock_;
    std::unique_ptr<TlbHandle> list_window_;
    bool quiet_ = false;
    bool flush_list_ = true;
    SysmemManager* sysmem_ = nullptr;
    xy_pair pcie_xy_;
    std::unique_ptr<SysmemBuffer> host_list_buffer_;
    std::unique_ptr<TlbHandle> hairpin_window_;
    uint8_t* host_list_ = nullptr;
    uint64_t host_list_axi_ = 0;
};

// The linked-list registers exist and hold what is written to them.
TEST_F(DmaLinkedListBh, LinkedListRegistersHoldWrites) {
    write_reg(LLP_LOW, 0x00abcd00);
    write_reg(CYCLE, CYCLE_STATE | CYCLE_BIT);
    write_reg(CONTROL1, CONTROL1_LLEN);

    EXPECT_EQ(read_reg(LLP_LOW), 0x00abcd00u);
    EXPECT_EQ(read_reg(CYCLE) & (CYCLE_STATE | CYCLE_BIT), CYCLE_STATE | CYCLE_BIT);
    EXPECT_EQ(read_reg(CONTROL1) & CONTROL1_LLEN, CONTROL1_LLEN);
}

// A list whose first element is already the end stops at once, with the stop interrupt and no data moved.
TEST_F(DmaLinkedListBh, EmptyListStops) {
    const ChainResult result = run_chain({});

    EXPECT_TRUE(result.stopped);
    EXPECT_FALSE(result.aborted);
    EXPECT_EQ(result.status & STATUS_MASK, STATUS_STOPPED);
}

class DmaLinkedListChainLength : public DmaLinkedListBh, public ::testing::WithParamInterface<size_t> {};

// Every element of a chain lands, back to back on one DRAM core.
TEST_P(DmaLinkedListChainLength, ChainLandsOnDevice) {
    const size_t count = GetParam();
    const auto data_window = open_window(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed);
    fill_host(count * CHUNK_SIZE);

    const ChainResult result = run_chain(contiguous_chain(count, *data_window, 0x0));

    ASSERT_TRUE(result.stopped);
    EXPECT_FALSE(result.aborted);
    expect_on_device(0, dram_cores_[0], 0x0, count * CHUNK_SIZE);
}

INSTANTIATE_TEST_SUITE_P(Lengths, DmaLinkedListChainLength, ::testing::Values(1, 3, 10, 50));

// A short chain whose list sits in host memory, fetched by the engine through the NOC and its own PCIe tile.
TEST_F(DmaLinkedListBh, HairpinListShortChain) {
    constexpr size_t COUNT = 4;
    use_host_list(64 * 1024);
    const auto data_window = open_window(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed);
    fill_host(COUNT * CHUNK_SIZE);

    const ChainResult result = run_chain(contiguous_chain(COUNT, *data_window, 0x0));

    ASSERT_TRUE(result.stopped) << "status " << result.status << " int_status " << result.int_status;
    EXPECT_FALSE(result.aborted);
    EXPECT_EQ(result.llp_low, static_cast<uint32_t>(host_list_axi_ + COUNT * sizeof(DataElement)));
    expect_on_device(0, dram_cores_[0], 0x0, COUNT * CHUNK_SIZE);
}

// One chain reaches two DRAM banks by alternating between two windows aimed at them.
TEST_F(DmaLinkedListBh, ChainAlternatesBetweenTwoBanks) {
    constexpr size_t COUNT = 8;
    const std::vector<std::unique_ptr<TlbHandle>> windows = [&] {
        std::vector<std::unique_ptr<TlbHandle>> opened;
        opened.push_back(open_window(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed));
        opened.push_back(open_window(dram_xy_[1], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed));
        return opened;
    }();
    fill_host(COUNT * CHUNK_SIZE);

    // Element i goes to bank i % 2, at the next free chunk of that bank.
    std::vector<DataElement> elements;
    elements.reserve(COUNT);
    for (size_t i = 0; i < COUNT; i++) {
        elements.push_back(DataElement{
            ELEMENT_CB,
            CHUNK_SIZE,
            buffer_->get_iova(i * CHUNK_SIZE),
            axi_address(*windows[i % 2], (i / 2) * CHUNK_SIZE)});
    }
    const ChainResult result = run_chain(elements);

    ASSERT_TRUE(result.stopped);
    EXPECT_FALSE(result.aborted);
    for (size_t i = 0; i < COUNT; i++) {
        expect_on_device(i * CHUNK_SIZE, dram_cores_[i % 2], (i / 2) * CHUNK_SIZE, CHUNK_SIZE);
    }
}

namespace {

using Clock = std::chrono::steady_clock;

double microseconds_between(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::micro>(to - from).count();
}

double median(std::vector<double> values) {
    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2];
}

}  // namespace

class DmaLinkedListBenchmark : public DmaLinkedListBh, public ::testing::WithParamInterface<size_t> {
protected:
    static constexpr int RUNS = 50;

    // Moves WINDOW_SIZE bytes RUNS times with run, which gets its start time and returns how long the host spent
    // submitting, and prints the medians.
    template <typename Run>
    void measure(const std::string& name, size_t count, Run run) {
        std::vector<double> totals;
        std::vector<double> hosts;
        for (int r = 0; r < RUNS; r++) {
            const auto start = Clock::now();
            hosts.push_back(run(start));
            totals.push_back(microseconds_between(start, Clock::now()));
        }
        const double total = median(totals);
        std::printf(
            "%-36s %8.1f us %7.2f GB/s   host %7.3f us/element\n",
            name.c_str(),
            total,
            WINDOW_SIZE / total / 1e3,
            median(hosts) / count);
    }

    // Main's async API waits on one transfer before the device takes the next.
    double run_async(uint64_t iova, uint64_t device_addr, size_t size) {
        const auto submit = Clock::now();
        EXPECT_TRUE(tt_device_->dma_write_zero_copy_start(iova, device_addr, size, dram_cores_[0]));
        const double host = microseconds_between(submit, Clock::now());
        DmaTransferState state;
        while ((state = tt_device_->dma_write_zero_copy_check()) == DmaTransferState::IN_PROGRESS) {
        }
        EXPECT_EQ(state, DmaTransferState::FINISHED);
        return host;
    }
};

// Moves 2 MiB to one DRAM bank in elements of the given size: main's async register-mode API one element at a time,
// against one chain for each list window ordering and way of writing the list. Each chain also pays what the async
// API pays per transfer: the engine lock and aiming the data window.
TEST_P(DmaLinkedListBenchmark, ChainVsAsyncRegisterMode) {
    const size_t element_size = GetParam();
    const size_t count = WINDOW_SIZE / element_size;
    quiet_ = true;
    // Main's API takes the engine lock itself.
    engine_lock_.unlock();
    std::printf("--- %zu x %zu KiB\n", count, element_size / 1024);

    fill_host(WINDOW_SIZE);
    measure("async register mode, per element", count, [&](Clock::time_point) {
        double host = 0;
        for (size_t i = 0; i < count; i++) {
            host += run_async(buffer_->get_iova(i * element_size), i * element_size, element_size);
        }
        return host;
    });
    expect_on_device(0, dram_cores_[0], 0x0, WINDOW_SIZE);

    fill_host(WINDOW_SIZE);
    measure("async register mode, one 2 MiB", count, [&](Clock::time_point) {
        return run_async(buffer_->get_iova(0), 0x0, WINDOW_SIZE);
    });
    expect_on_device(0, dram_cores_[0], 0x0, WINDOW_SIZE);

    const auto data_window = open_window(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed);
    const tlb_data data_config = window_config(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed);
    const std::pair<uint64_t, const char*> orderings[] = {{tlb_data::Strict, "Strict"}, {tlb_data::Relaxed, "Relaxed"}};
    const std::pair<ListWrite, const char*> writes[] = {
        {ListWrite::Uc32, "UC 32-bit"}, {ListWrite::Uc64, "UC 64-bit"}, {ListWrite::Wc, "WC"}};
    for (const auto& [ordering, ordering_name] : orderings) {
        for (const auto& [write, mode_name] : writes) {
            const ListWrite mode = write;
            list_window_ = open_window(
                dram_xy_.back(),
                LIST_DEVICE_ADDR,
                WindowFlags::Bidirectional,
                ordering,
                mode == ListWrite::Wc ? TlbMapping::WC : TlbMapping::UC);
            fill_host(WINDOW_SIZE);
            measure(std::string("chain, ") + ordering_name + " list, " + mode_name, count, [&](Clock::time_point start) {
                const auto lock = LockManager::acquire_mutex(
                    MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);
                data_window->configure(data_config);
                const ChainResult result = run_chain(contiguous_chain(count, *data_window, 0x0, element_size), mode);
                EXPECT_TRUE(result.stopped);
                return microseconds_between(start, result.doorbell_at);
            });
            expect_on_device(0, dram_cores_[0], 0x0, WINDOW_SIZE);
        }
    }
    engine_lock_.lock();
}

INSTANTIATE_TEST_SUITE_P(
    ElementSizes, DmaLinkedListBenchmark, ::testing::Values(4 * 1024, 16 * 1024, 64 * 1024));

class DmaLinkedListListWrite : public DmaLinkedListBh,
                               public ::testing::WithParamInterface<std::tuple<uint64_t, ListWrite>> {
protected:
    static constexpr size_t COUNT = 128;
    static constexpr size_t ELEMENT_SIZE = 4 * 1024;
    static constexpr int ITERATIONS = 200;

    // Chains that ended before the last element, out of ITERATIONS. The list area is zeroed before every chain, so
    // an element that has not landed by the doorbell reads as the end of the list.
    int count_short_chains(const std::vector<DataElement>& elements, ListWrite mode) {
        const std::vector<uint8_t> zeros((COUNT + 1) * sizeof(DataElement));
        const uint32_t last_llp =
            static_cast<uint32_t>(axi_address(*list_window_, LIST_DEVICE_ADDR) + COUNT * sizeof(DataElement));
        int short_chains = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            tt_device_->write_to_device(zeros.data(), dram_cores_.back(), LIST_DEVICE_ADDR, zeros.size());
            uint32_t landed;
            tt_device_->read_from_device(&landed, dram_cores_.back(), LIST_DEVICE_ADDR, sizeof(landed));
            EXPECT_EQ(landed, 0u);
            const ChainResult result = run_chain(elements, mode);
            EXPECT_TRUE(result.stopped);
            short_chains += read_reg(LLP_LOW) != last_llp;
        }
        return short_chains;
    }
};

// The engine sees the whole freshly written list, for every list window ordering and way of writing the list. The
// same chains without the flush read show whether this check can catch a list that has not landed.
TEST_P(DmaLinkedListListWrite, EngineSeesWholeList) {
    const auto [ordering, mode] = GetParam();
    list_window_ = open_window(
        dram_xy_.back(),
        LIST_DEVICE_ADDR,
        WindowFlags::Bidirectional,
        ordering,
        mode == ListWrite::Wc ? TlbMapping::WC : TlbMapping::UC);
    const auto data_window = open_window(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed);
    const std::vector<DataElement> elements = contiguous_chain(COUNT, *data_window, 0x0, ELEMENT_SIZE);
    fill_host(COUNT * ELEMENT_SIZE);
    quiet_ = true;

    flush_list_ = false;
    std::printf("short chains without flush: %d / %d\n", count_short_chains(elements, mode), ITERATIONS);
    flush_list_ = true;
    EXPECT_EQ(count_short_chains(elements, mode), 0);
    expect_on_device(0, dram_cores_[0], 0x0, COUNT * ELEMENT_SIZE);
}

INSTANTIATE_TEST_SUITE_P(
    OrderingsAndWrites,
    DmaLinkedListListWrite,
    ::testing::Combine(
        ::testing::Values(tlb_data::Strict, tlb_data::Relaxed),
        ::testing::Values(ListWrite::Uc32, ListWrite::Uc64, ListWrite::Wc)));

// Element size, and whether the list sits in host memory (hairpin) instead of device DRAM.
class DmaLinkedListPrefetch : public DmaLinkedListBh, public ::testing::WithParamInterface<std::tuple<size_t, bool>> {
protected:
    static constexpr int RUNS = 50;
    static constexpr int MAX_READ_CHANNELS = 16;
    static constexpr uint32_t CHANNEL_STRIDE = 0x200;

    void TearDown() override {
        if (qos_at_reset_ != 0) {
            write_reg(QOS, qos_at_reset_);
        }
        DmaLinkedListBh::TearDown();
    }

    // Read channels whose QOS reads the same reset value as channel 0; registers of channels that were not built
    // read differently.
    int count_read_channels(uint32_t qos) {
        int channels = 0;
        for (int i = 0; i < MAX_READ_CHANNELS; i++) {
            const uint32_t channel_qos = read_reg(QOS + i * CHANNEL_STRIDE);
            const uint32_t channel_status = read_reg(STATUS + i * CHANNEL_STRIDE);
            std::printf("read channel %2d: QOS=%#x STATUS=%#x\n", i, channel_qos, channel_status);
            if (channel_qos != qos) {
                break;
            }
            EXPECT_NE(channel_status & STATUS_MASK, STATUS_RUNNING) << "read channel " << i << " is running";
            channels++;
        }
        return channels;
    }

    uint32_t qos_at_reset_ = 0;
};

// Moves 2 MiB to one DRAM bank as one chain (Relaxed list window, WC list writes) at growing ELEMENT_PREFETCH and
// prints the engine time from doorbell to completion flag. Channel 0 is the only channel used, so PF_DEPTH is raised
// to the whole read engine's queue first, the most the reference manual allows with one active channel.
TEST_P(DmaLinkedListPrefetch, EngineTimePerElement) {
    const auto [element_size, host_list] = GetParam();
    const size_t count = WINDOW_SIZE / element_size;
    const uint32_t qos = read_reg(QOS);
    const uint32_t queue_depth = ((qos >> QOS_PF_DEPTH_SHIFT) & QOS_PF_DEPTH_MASK) + 1;
    const int channels = count_read_channels(qos);
    ASSERT_GT(channels, 0);
    if (std::getenv("DMA_LL_PROBE_ONLY") != nullptr) {
        GTEST_SKIP() << "Probe only.";
    }
    const uint32_t pf_depth = std::min<uint32_t>(queue_depth * channels - 1, ELEMENT_PREFETCH_MAX);
    qos_at_reset_ = qos;
    write_reg(QOS, (qos & ~(QOS_PF_DEPTH_MASK << QOS_PF_DEPTH_SHIFT)) | (pf_depth << QOS_PF_DEPTH_SHIFT));
    std::printf(
        "--- %zu x %zu KiB, list in %s: QOS at reset %#x (queue %u per channel, %d read channels), raised to %#x "
        "(PF_DEPTH %u)\n",
        count,
        element_size / 1024,
        host_list ? "host memory" : "device DRAM",
        qos,
        queue_depth,
        channels,
        read_reg(QOS),
        pf_depth);

    // Prefetch reads past the terminator, so the list area is cleared of leftovers from longer lists first.
    const size_t list_bytes = (count + 2 + ELEMENT_PREFETCH_MAX) * sizeof(DataElement);
    if (host_list) {
        use_host_list(list_bytes);
    } else {
        list_window_ = open_window(
            dram_xy_.back(), LIST_DEVICE_ADDR, WindowFlags::Bidirectional, tlb_data::Relaxed, TlbMapping::WC);
        const std::vector<uint8_t> zeros(list_bytes);
        tt_device_->write_to_device(zeros.data(), dram_cores_.back(), LIST_DEVICE_ADDR, zeros.size());
    }
    const auto data_window = open_window(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed);
    const std::vector<DataElement> elements = contiguous_chain(count, *data_window, 0x0, element_size);
    quiet_ = true;

    for (const uint32_t prefetch : {0u, 1u, 2u, 3u, 5u, 7u, 11u, 15u, 31u, 63u, 127u}) {
        if (prefetch > pf_depth) {
            break;
        }
        fill_host(WINDOW_SIZE);
        write_reg(ELEM_PF, prefetch);
        std::vector<double> engine;
        std::vector<double> totals;
        std::vector<double> hosts;
        int failed = 0;
        for (int r = 0; r < RUNS; r++) {
            const auto start = Clock::now();
            const ChainResult result = run_chain(elements, ListWrite::Wc);
            failed += !result.stopped || result.aborted;
            engine.push_back(result.microseconds);
            totals.push_back(microseconds_between(start, Clock::now()));
            hosts.push_back(microseconds_between(start, result.doorbell_at));
        }
        const double engine_us = median(engine);
        std::printf(
            "ELEMENT_PREFETCH %3u: engine %7.1f us = %5.3f us/element %6.2f GB/s, total %7.1f us %6.2f GB/s, "
            "host to doorbell %6.2f us = %5.3f us/element, ELEM_PF after %#x, LLP %#x\n",
            prefetch,
            engine_us,
            engine_us / count,
            WINDOW_SIZE / engine_us / 1e3,
            median(totals),
            WINDOW_SIZE / median(totals) / 1e3,
            median(hosts),
            median(hosts) / count,
            read_reg(ELEM_PF),
            read_reg(LLP_LOW));
        EXPECT_EQ(failed, 0) << "ELEMENT_PREFETCH " << prefetch;
        expect_on_device(0, dram_cores_[0], 0x0, WINDOW_SIZE);
    }
}

INSTANTIATE_TEST_SUITE_P(
    ElementSizesAndListLocations,
    DmaLinkedListPrefetch,
    ::testing::Combine(::testing::Values(4 * 1024, 8 * 1024, 16 * 1024, 64 * 1024), ::testing::Bool()));

class DmaLinkedListWindows : public DmaLinkedListBh {};

// The same 2 MiB chain of 16 KiB elements through different data windows, to see whether throughput depends on
// which TLB the data goes through.
TEST_F(DmaLinkedListWindows, ThroughputPerDataWindow) {
    constexpr size_t WINDOWS = 16;
    constexpr size_t COUNT = WINDOW_SIZE / CHUNK_SIZE;
    constexpr int RUNS = 20;
    list_window_ = open_window(
        dram_xy_.back(), LIST_DEVICE_ADDR, WindowFlags::Bidirectional, tlb_data::Relaxed, TlbMapping::WC);
    write_reg(ELEM_PF, 1);
    quiet_ = true;

    std::vector<std::unique_ptr<TlbHandle>> windows;
    windows.reserve(WINDOWS);
    for (size_t w = 0; w < WINDOWS; w++) {
        windows.push_back(open_window(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed));
        fill_host(WINDOW_SIZE);
        const std::vector<DataElement> elements = contiguous_chain(COUNT, *windows.back(), 0x0);
        std::vector<double> engine;
        for (int r = 0; r < RUNS; r++) {
            const ChainResult result = run_chain(elements, ListWrite::Wc);
            EXPECT_TRUE(result.stopped && !result.aborted);
            engine.push_back(result.microseconds);
        }
        const double engine_us = median(engine);
        std::printf(
            "data window tlb %3u (AXI %#llx): engine %6.1f us %6.2f GB/s\n",
            windows.back()->get_tlb_id(),
            static_cast<unsigned long long>(axi_address(*windows.back(), 0)),
            engine_us,
            WINDOW_SIZE / engine_us / 1e3);
        expect_on_device(0, dram_cores_[0], 0x0, WINDOW_SIZE);
    }
}

class DmaLinkedListSizes : public DmaLinkedListBenchmark {};

// The microbenchmark's exact call (SysmemBuffer::dma_write_to_device, 1 MiB) in this fixture, next to main's async
// API and chains of 16 KiB elements at several total sizes, to find where this fixture loses throughput.
TEST_F(DmaLinkedListSizes, ThroughputBySize) {
    constexpr int REPEATS = 200;
    const size_t sizes[] = {256 * 1024, 512 * 1024, 1024 * 1024, WINDOW_SIZE};
    engine_lock_.unlock();
    fill_host(WINDOW_SIZE);
    const auto rate = [](size_t size, double us) { return size / us / 1e3; };

    for (const size_t size : sizes) {
        std::vector<double> sysmem;
        std::vector<double> async;
        sysmem.reserve(REPEATS);
        async.reserve(REPEATS);
        for (int r = 0; r < REPEATS; r++) {
            auto start = Clock::now();
            buffer_->dma_write_to_device(0, size, dram_xy_[0], 0x0);
            sysmem.push_back(microseconds_between(start, Clock::now()));
            start = Clock::now();
            run_async(buffer_->get_iova(0), 0x0, size);
            async.push_back(microseconds_between(start, Clock::now()));
        }
        std::printf(
            "%7zu KiB: SysmemBuffer::dma_write_to_device %6.1f us %6.2f GB/s (best %6.2f), async API %6.1f us "
            "%6.2f GB/s\n",
            size / 1024,
            median(sysmem),
            rate(size, median(sysmem)),
            rate(size, *std::min_element(sysmem.begin(), sysmem.end())),
            median(async),
            rate(size, median(async)));
    }

    engine_lock_.lock();
    list_window_ = open_window(
        dram_xy_.back(), LIST_DEVICE_ADDR, WindowFlags::Bidirectional, tlb_data::Relaxed, TlbMapping::WC);
    const auto data_window = open_window(dram_xy_[0], 0x0, WindowFlags::UnicastWrite, tlb_data::Relaxed);
    write_reg(ELEM_PF, 1);
    quiet_ = true;
    for (const size_t size : sizes) {
        const std::vector<DataElement> elements = contiguous_chain(size / CHUNK_SIZE, *data_window, 0x0);
        std::vector<double> engine;
        engine.reserve(REPEATS);
        for (int r = 0; r < REPEATS; r++) {
            engine.push_back(run_chain(elements, ListWrite::Wc).microseconds);
        }
        std::printf(
            "%7zu KiB: chain of 16 KiB elements, engine %6.1f us %6.2f GB/s (best %6.2f)\n",
            size / 1024,
            median(engine),
            rate(size, median(engine)),
            rate(size, *std::min_element(engine.begin(), engine.end())));
    }
}

class DmaMicrobenchmarkReplica : public ::testing::TestWithParam<size_t> {};

// The DMA microbenchmark's setup without this file's fixture: its own Cluster, BUSY clock, one sysmem buffer of the
// given size, 1 MiB synchronous writes to address 0 of the first DRAM core.
TEST_P(DmaMicrobenchmarkReplica, OneMibWrites) {
    constexpr size_t ONE_MIB = 1024 * 1024;
    constexpr int REPEATS = 500;
    auto cluster = std::make_unique<Cluster>(ClusterOptions{.num_host_mem_ch_per_mmio_device = 0});
    cluster->set_clock_state(DevicePowerState::BUSY);
    const ChipId chip = *cluster->get_target_mmio_device_ids().begin();
    std::unique_ptr<SysmemBuffer> buffer = cluster->get_chip(chip)->get_sysmem_manager()->allocate_sysmem_buffer(GetParam());
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];
    const xy_pair dram_xy =
        cluster->get_soc_descriptor(chip).translate_chip_coord_to_translated(dram_core, get_selected_noc_id());

    uint8_t* data = static_cast<uint8_t*>(buffer->get_va());
    const auto measure = [&](const char* state) {
        std::vector<double> times;
        times.reserve(REPEATS);
        for (int r = 0; r < REPEATS; r++) {
            const auto start = Clock::now();
            buffer->dma_write_to_device(0, ONE_MIB, dram_xy, 0x0);
            times.push_back(microseconds_between(start, Clock::now()));
        }
        std::printf(
            "buffer %4zu MiB, %-28s 1 MiB write median %5.1f us %6.2f GB/s, best %6.2f GB/s\n",
            GetParam() / ONE_MIB,
            state,
            median(times),
            ONE_MIB / median(times) / 1e3,
            ONE_MIB / *std::min_element(times.begin(), times.end()) / 1e3);
    };
    const auto flush_from_cache = [&]() {
        for (size_t line = 0; line < ONE_MIB; line += 64) {
            _mm_clflush(data + line);
        }
        _mm_sfence();
    };

    measure("as allocated:");
    std::memset(data, 0x5a, ONE_MIB);
    measure("after CPU write:");
    flush_from_cache();
    measure("after CPU write + clflush:");
    cluster->set_clock_state(DevicePowerState::LONG_IDLE);
}

INSTANTIATE_TEST_SUITE_P(BufferSizes, DmaMicrobenchmarkReplica, ::testing::Values(2 * 1024 * 1024, 200 * 1024 * 1024));
