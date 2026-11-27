// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <tt_emule/l1_pool.hpp>
#include <type_traits>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "tt-umd-workload/chip/sw_emule_chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/system_memory/host_mem_channels.hpp"
#include "tt-umd/system_memory/sysmem_buffer.hpp"
#include "tt-umd/system_memory/system_memory_allocator.hpp"
#include "tt-umd/types/host_memory.hpp"
#include "tt-umd/types/noc_id.hpp"

using namespace tt;
using namespace tt::umd;

TEST(ApiEmuleClusterTest, CreateEmuleSingleChipClusters) {
    // SWEmuleChip only supports single-chip clusters. Test the two single-chip descriptors:
    // - wormhole_N150.yaml (1 chip)
    // - wormhole_N150_unique_ids.yaml (1 chip, exercises chip_unique_ids parsing path)
    const std::vector<std::string> single_chip_descs = {
        test_utils::get_cluster_desc_path("wormhole_N150.yaml"),
        test_utils::get_cluster_desc_path("wormhole_N150_unique_ids.yaml"),
    };

    for (const auto& descriptor_file : single_chip_descs) {
        log_info(LogUMD, "Testing emule cluster creation from: {}", descriptor_file);
        std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(descriptor_file);
        ASSERT_NE(cluster_desc, nullptr) << "Cluster descriptor is null for: " << descriptor_file;
        ASSERT_FALSE(cluster_desc->get_all_chips().empty()) << "Cluster descriptor has no chips: " << descriptor_file;
        EXPECT_GT(cluster_desc->get_chips_grouped_by_closest_mmio().size(), 0);

        auto emule_cluster = std::make_unique<Cluster>(
            ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});
        ASSERT_NE(emule_cluster, nullptr) << "Emule cluster is null for: " << descriptor_file;

        std::vector<uint8_t> data(1024, 0);
        for (auto chip_id : emule_cluster->get_target_device_ids()) {
            CoreCoord any_tensix_core = emule_cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)[0];
            emule_cluster->write_to_device(data.data(), data.size(), chip_id, any_tensix_core, 0);
            emule_cluster->read_from_device(data.data(), chip_id, any_tensix_core, 0, data.size());
        }
    }
}

TEST(ApiEmuleClusterTest, UnsupportedSharedPoolRequestFailsExplicitly) {
    if constexpr (std::is_constructible_v<tt_emule::L1Pool, size_t, uint64_t, uint64_t>) {
        GTEST_SKIP() << "The supplied tt-emule dependency supports shared pools";
    }

    struct RestoreEnvironment {
        const char* original = std::getenv("TT_EMULE_CHIP_SHM");
        std::string saved = original ? original : "";

        ~RestoreEnvironment() {
            if (original) {
                ::setenv("TT_EMULE_CHIP_SHM", saved.c_str(), 1);
            } else {
                ::unsetenv("TT_EMULE_CHIP_SHM");
            }
        }
    } restore;

    ASSERT_EQ(::setenv("TT_EMULE_CHIP_SHM", "1", 1), 0);
    const SocDescriptor soc(
        std::make_shared<SocArchDescriptor>(test_utils::get_soc_desc_path("wormhole_b0_8x10.yaml")));
    try {
        SWEmuleChip chip(soc, 123);
        FAIL() << "Unsupported shared backing was accepted";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("does not support TT_EMULE_CHIP_SHM"), std::string::npos);
    }
}

TEST(ApiEmuleClusterTest, EmuleRoundtripIO) {
    std::string descriptor_file = test_utils::get_cluster_desc_path("wormhole_N150.yaml");
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(descriptor_file);
    ASSERT_NE(cluster_desc, nullptr);

    auto emule_cluster = std::make_unique<Cluster>(
        ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});

    ChipId chip_id = *emule_cluster->get_target_device_ids().begin();
    const auto& tensix_cores = emule_cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

    constexpr size_t kDataSize = 1024;
    constexpr uint64_t kOffsets[] = {0, 4096, 8192};

    for (const auto& core : tensix_cores) {
        for (uint64_t offset : kOffsets) {
            // Fill with a pattern unique to this core + offset.
            std::vector<uint8_t> write_data(kDataSize);
            uint8_t seed = static_cast<uint8_t>(core.x ^ core.y ^ (offset >> 12));
            for (size_t i = 0; i < kDataSize; ++i) {
                write_data[i] = static_cast<uint8_t>(seed + i);
            }

            emule_cluster->write_to_device(write_data.data(), write_data.size(), chip_id, core, offset);

            std::vector<uint8_t> read_data(kDataSize, 0);
            emule_cluster->read_from_device(read_data.data(), chip_id, core, offset, read_data.size());

            EXPECT_EQ(write_data, read_data)
                << "Roundtrip mismatch at core (" << core.x << "," << core.y << ") offset " << offset;
        }
    }
}

// The slot map is the premise of shared chip backing: a rank resolves a fabric write into a chip it
// does not own by deriving that chip's layout from the SoC descriptor alone. These pin the
// properties that makes true, none of which needs shared memory or a second process.

TEST(ApiEmuleClusterTest, WorkerSlotMapIsDeterministicAndDense) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::get_cluster_desc_path("wormhole_N150.yaml"));
    auto cluster = std::make_unique<Cluster>(
        ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});
    ChipId chip_id = *cluster->get_target_device_ids().begin();
    const SocDescriptor& soc = cluster->get_soc_descriptor(chip_id);

    // Two independent builds from one descriptor must agree — this stands in for two PROCESSES,
    // which is what a shared segment actually requires.
    const auto a = build_worker_slot_map(soc);
    const auto b = build_worker_slot_map(soc);
    EXPECT_EQ(a, b);

    const auto tensix = soc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    ASSERT_FALSE(tensix.empty());
    EXPECT_EQ(a.size(), tensix.size());

    // Dense and collision-free: exactly {0 .. n-1}. A duplicate would alias two cores onto one
    // slot; a gap would waste a slot in the scarce low-4 GB window.
    std::set<size_t> slots;
    for (const auto& core : tensix) {
        auto it = a.find(tt_xy_pair(core.x, core.y));
        ASSERT_NE(it, a.end()) << "translated core (" << core.x << "," << core.y << ") has no slot";
        EXPECT_TRUE(slots.insert(it->second).second) << "slot " << it->second << " assigned twice";
    }
    EXPECT_EQ(*slots.begin(), 0u);
    EXPECT_EQ(*slots.rbegin(), tensix.size() - 1);
}

TEST(ApiEmuleClusterTest, PoolIsSizedToTensixCount) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::get_cluster_desc_path("wormhole_N150.yaml"));
    auto cluster = std::make_unique<Cluster>(
        ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});
    ChipId chip_id = *cluster->get_target_device_ids().begin();

    auto* chip = dynamic_cast<SWEmuleChip*>(cluster->get_chip(chip_id));
    ASSERT_NE(chip, nullptr);
    EXPECT_EQ(
        chip->num_pool_slots(),
        cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED).size());
}

TEST(ApiEmuleClusterTest, NonTranslatedCoreNamingStillResolvesToAPoolSlot) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::get_cluster_desc_path("wormhole_N150.yaml"));
    auto cluster = std::make_unique<Cluster>(
        ClusterOptions{.chip_type = ChipType::SWEMULE, .cluster_descriptor = cluster_desc.get()});
    ChipId chip_id = *cluster->get_target_device_ids().begin();
    const SocDescriptor& soc = cluster->get_soc_descriptor(chip_id);
    auto* chip = dynamic_cast<SWEmuleChip*>(cluster->get_chip(chip_id));
    ASSERT_NE(chip, nullptr);

    // get_cores() defaults to NOC0, which is what a caller naturally reaches for, and on Wormhole
    // NOC0 Tensix (x <= 9) are DISJOINT from TRANSLATED (from 18). A write addressed in one naming
    // must be visible when read back in the other -- a tile's names share one L1 on silicon, so two
    // encodings of the same worker must not resolve to two separate backings.
    const auto noc0_cores = soc.get_cores(CoreType::TENSIX, CoordSystem::NOC0);
    const auto translated_cores = soc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    ASSERT_EQ(noc0_cores.size(), translated_cores.size());

    for (size_t i = 0; i < noc0_cores.size(); ++i) {
        EXPECT_TRUE(chip->slot_of(tt_xy_pair(translated_cores[i].x, translated_cores[i].y)).has_value());

        const std::vector<uint8_t> written(64, static_cast<uint8_t>(i + 1));
        cluster->write_to_device(written.data(), written.size(), chip_id, noc0_cores[i], /*addr=*/0);

        std::vector<uint8_t> read_back(written.size(), 0);
        cluster->read_from_device(read_back.data(), chip_id, translated_cores[i], /*addr=*/0, read_back.size());
        EXPECT_EQ(written, read_back) << "NOC0 core (" << noc0_cores[i].x << "," << noc0_cores[i].y
                                      << ") and translated core (" << translated_cores[i].x << ","
                                      << translated_cores[i].y << ") resolved to different backings";
    }
}

namespace {

// quasar_simulation_2x3.yaml declares worker_l1_size 4 MiB and two Tensix workers.
std::unique_ptr<Cluster> make_quasar_2x3_cluster(ClusterDescriptor& cluster_desc) {
    ClusterOptions options{};
    options.chip_type = ChipType::SWEMULE;
    options.topology_discovery_options.soc_descriptor_path =
        test_utils::get_soc_desc_path("quasar_simulation_2x3.yaml");
    options.cluster_descriptor = &cluster_desc;
    return std::make_unique<Cluster>(options);
}

}  // namespace

TEST(ApiEmuleClusterTest, WorkerL1LargerThan2MiBDoesNotOverlapTheNextCore) {
    if constexpr (!std::is_constructible_v<tt_emule::L1Pool, size_t, size_t>) {
        GTEST_SKIP() << "The supplied tt-emule dependency has fixed 2 MiB L1 slots";
    }
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_mock_cluster({0}, tt::ARCH::QUASAR, /*noc_translation_enabled=*/true);
    std::unique_ptr<Cluster> cluster = make_quasar_2x3_cluster(*cluster_desc);
    const ChipId chip_id = *cluster->get_target_device_ids().begin();
    const SocDescriptor& soc = cluster->get_soc_descriptor(chip_id);
    const std::vector<CoreCoord> cores = soc.get_cores(CoreType::TENSIX);
    ASSERT_GE(cores.size(), 2u);

    // Mark the start of every core's L1, then fill the rest of every core's L1 but the last core's. If a
    // core's L1 runs into the next core's, the fill overwrites that core's mark.
    const size_t l1_size = static_cast<size_t>(soc.worker_l1_size);
    constexpr size_t kMarkSize = 64;
    auto mark = [](size_t core) { return std::vector<uint8_t>(kMarkSize, static_cast<uint8_t>(core + 1)); };
    for (size_t c = 0; c < cores.size(); ++c) {
        const std::vector<uint8_t> data = mark(c);
        cluster->write_to_device(data.data(), data.size(), chip_id, cores[c], 0);
    }
    const std::vector<uint8_t> fill(l1_size - kMarkSize, 0xA5);
    for (size_t c = 0; c + 1 < cores.size(); ++c) {
        cluster->write_to_device(fill.data(), fill.size(), chip_id, cores[c], kMarkSize);
    }
    for (size_t c = 0; c < cores.size(); ++c) {
        std::vector<uint8_t> read_back(kMarkSize, 0);
        cluster->read_from_device(read_back.data(), chip_id, cores[c], 0, read_back.size());
        EXPECT_EQ(read_back, mark(c)) << "core (" << cores[c].x << "," << cores[c].y
                                      << ") start of L1 was overwritten by another core";
    }
}

TEST(ApiEmuleClusterTest, FixedSlotPoolRefusesAWorkerL1LargerThanItsSlot) {
    if constexpr (std::is_constructible_v<tt_emule::L1Pool, size_t, size_t>) {
        GTEST_SKIP() << "The supplied tt-emule dependency sizes its L1 slots from the worker L1";
    }
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_mock_cluster({0}, tt::ARCH::QUASAR, /*noc_translation_enabled=*/true);
    EXPECT_THROW(make_quasar_2x3_cluster(*cluster_desc), std::exception);
}

TEST(ApiEmuleClusterTest, SharedBackingRefusesASynthesizedUniqueId) {
    // wormhole_N150.yaml carries no chip_unique_ids block, so the descriptor synthesizes chip << 32
    // -- stable within a process, but it names no physical chip. Two ranks each holding a different
    // chip 0 would compute the same segment key, so sharing must refuse it rather than collide.
    std::unique_ptr<ClusterDescriptor> synthesized =
        ClusterDescriptor::create_from_yaml(test_utils::get_cluster_desc_path("wormhole_N150.yaml"));
    EXPECT_FALSE(synthesized->has_authentic_chip_unique_ids());

    std::unique_ptr<ClusterDescriptor> authentic =
        ClusterDescriptor::create_from_yaml(test_utils::get_cluster_desc_path("wormhole_N150_unique_ids.yaml"));
    EXPECT_TRUE(authentic->has_authentic_chip_unique_ids());

    // Mock descriptors synthesize too, and their ids are bare logical ids -- the worst case.
    std::unique_ptr<ClusterDescriptor> mock =
        ClusterDescriptor::create_mock_cluster({0, 1}, tt::ARCH::WORMHOLE_B0, /*noc_translation_enabled=*/true);
    EXPECT_FALSE(mock->has_authentic_chip_unique_ids());
}

namespace {
SocDescriptor blackhole_soc() {
    return SocDescriptor(
        std::make_shared<SocArchDescriptor>(test_utils::get_soc_desc_path("blackhole_140_arch.yaml")),
        ChipInfo{.noc_translation_enabled = true, .harvesting_masks = {.eth_harvesting_mask = 0x120}});
}
}  // namespace

// The TTDevice's firmware answers from the chip's SocDescriptor, so the two never disagree.
TEST(ApiEmuleClusterTest, DeviceFirmwareMatchesTheSocDescriptor) {
    const SocDescriptor soc = blackhole_soc();
    SWEmuleChip chip(soc);
    TTDevice* device = chip.get_tt_device();
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->get_noc_translation_enabled(), soc.noc_translation_enabled);
    const ChipInfo info = device->get_chip_info();
    EXPECT_EQ(info.noc_translation_enabled, soc.noc_translation_enabled);
    EXPECT_EQ(info.harvesting_masks.tensix_harvesting_mask, soc.harvesting_masks.tensix_harvesting_mask);
    EXPECT_EQ(info.harvesting_masks.dram_harvesting_mask, soc.harvesting_masks.dram_harvesting_mask);
    EXPECT_EQ(info.harvesting_masks.eth_harvesting_mask, soc.harvesting_masks.eth_harvesting_mask);
}

// An access that starts in a core's memory may not run past it, on either tt-emule API.
TEST(ApiEmuleClusterTest, AccessRunningPastL1Throws) {
    SWEmuleChip chip(blackhole_soc());
    const CoreCoord core = chip.get_soc_descriptor().get_cores(CoreType::TENSIX)[0];
    std::vector<uint8_t> buf(8, 0xab);
    const uint64_t last_word = chip.l1_size() - 4;
    EXPECT_THROW(chip.write_to_device(core, buf.data(), last_word, buf.size()), std::exception);
    EXPECT_THROW(chip.read_from_device(core, buf.data(), last_word, buf.size()), std::exception);
    EXPECT_NO_THROW(chip.write_to_device(core, buf.data(), last_word, 4));
}

// A zero-length access is a no-op wherever it points, including past the core's memory.
TEST(ApiEmuleClusterTest, ZeroLengthAccessPastL1IsANoOp) {
    SWEmuleChip chip(blackhole_soc());
    const CoreCoord core = chip.get_soc_descriptor().get_cores(CoreType::TENSIX)[0];
    uint32_t word = 0;
    EXPECT_NO_THROW(chip.write_to_device(core, &word, chip.l1_size() + 0x1000, 0));
    EXPECT_NO_THROW(chip.read_from_device(core, &word, chip.l1_size() + 0x1000, 0));
}

// The chip-level register path has LocalChip's rules: address and size are multiples of 4 bytes.
TEST(ApiEmuleClusterTest, RegisterAccessMustBeWordAligned) {
    SWEmuleChip chip(blackhole_soc());
    const CoreCoord core = chip.get_soc_descriptor().get_cores(CoreType::TENSIX)[0];
    std::vector<uint8_t> buf(8, 0);
    EXPECT_THROW(chip.write_to_device_reg(core, buf.data(), 0x100, 6), std::exception);
    EXPECT_THROW(chip.read_from_device_reg(core, buf.data(), 0x102, 4), std::exception);
    // In memory an aligned multi-word payload (a launch message) is one write and reads back whole.
    const std::vector<uint8_t> pattern = {1, 2, 3, 4, 5, 6, 7, 8};
    chip.write_to_device_reg(core, pattern.data(), 0x100, static_cast<uint32_t>(pattern.size()));
    chip.read_from_device_reg(core, buf.data(), 0x100, static_cast<uint32_t>(buf.size()));
    EXPECT_EQ(buf, pattern);
}

// Host memory is the chip's own SystemMemoryAllocator: the channels the cluster asked for, then buffers whose NOC
// addresses lie past them and resolve back to their host memory while the buffer lives.
TEST(ApiEmuleClusterTest, HostMemoryGoesThroughTheChipsAllocator) {
    SWEmuleChip chip(blackhole_soc(), std::nullopt, 2);
    SystemMemoryAllocator* allocator = chip.get_system_memory_allocator();
    ASSERT_NE(allocator, nullptr);
    EXPECT_GT(allocator->get_noc_base(), chip.dram_bank_size());

    const HostMemChannels* channels = chip.get_host_mem_channels();
    ASSERT_NE(channels, nullptr);
    ASSERT_EQ(chip.get_num_host_channels(), 2);
    EXPECT_EQ(chip.get_host_channel_size(1), HostMemChannels::CHANNEL_STRIDE);
    const uint32_t word = 0xC0FFEE;
    uint32_t readback = 0;
    chip.write_to_sysmem(1, &word, 0x40, sizeof(word));
    chip.read_from_sysmem(1, &readback, 0x40, sizeof(readback));
    EXPECT_EQ(readback, word);
    EXPECT_EQ(*reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(channels->get_channel_address(1)) + 0x40), word);

    uint64_t noc_addr = 0;
    {
        std::unique_ptr<SysmemBuffer> buffer = allocator->allocate_buffer(0x2000, true);
        ASSERT_TRUE(buffer->get_noc_address().has_value());
        noc_addr = *buffer->get_noc_address();
        EXPECT_GE(noc_addr, allocator->get_noc_base() + 2 * HostMemChannels::CHANNEL_STRIDE);
        EXPECT_EQ(chip.get_mapped_host_ptr(noc_addr + 0x10), static_cast<uint8_t*>(buffer->get_va()) + 0x10);
        EXPECT_EQ(chip.get_mapped_host_ptr(noc_addr + 0x2000), nullptr);

        std::vector<uint8_t> user(0x100);
        std::unique_ptr<SysmemBuffer> mapped = allocator->map_user_buffer(user.data(), user.size(), true);
        ASSERT_TRUE(mapped->get_noc_address().has_value());
        EXPECT_EQ(chip.get_mapped_host_ptr(*mapped->get_noc_address()), user.data());
        EXPECT_THROW(
            allocator->map_user_buffer(user.data(), user.size(), true, DeviceBufferAccess::READ_ONLY), std::exception);
    }
    // A freed buffer's NOC address no longer reaches host memory.
    EXPECT_EQ(chip.get_mapped_host_ptr(noc_addr), nullptr);
}
