// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Cluster::broadcast_write_to_cluster on Wormhole clusters: every targeted core of every chip, remote chips included,
// reads the data back, and every core or chip the call excludes keeps what it held. Runs on silicon and on TTSim
// (wh, wh_x2) through make_default_test_cluster.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "tests/test_utils/assembly_programs_for_tests.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/setup_risc_cores.hpp"
#include "tt-umd-workload/chip/remote_chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/ethernet_broadcast.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/risc_type.hpp"
#include "tt-umd/types/wormhole_l1.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/semver.hpp"

using namespace tt::umd;

namespace {

constexpr uint64_t BROADCAST_ADDRESS = wormhole::DATA_BUFFER_SPACE_BASE;

// Wormhole NOC0 rows and columns that hold no Tensix core (DRAM, Ethernet, ARC, PCIe): a Tensix broadcast excludes
// them, since one call targets either Tensix or DRAM cores.
const std::set<uint32_t> NON_TENSIX_ROWS = {0, 6};
const std::set<uint32_t> NON_TENSIX_COLS = {0, 5};
// Every Wormhole NOC0 column that holds Tensix cores: a DRAM broadcast excludes them.
const std::set<uint32_t> TENSIX_COLS = {1, 2, 3, 4, 6, 7, 8, 9};

// A Tensix column excluded on top of the non-Tensix ones. A NOC0 broadcast excludes Tensix columns only: Wormhole
// harvests Tensix rows, so a NOC0 row names different cores on different chips.
constexpr uint32_t EXCLUDED_TENSIX_COL = 3;

// Translated rows and columns of a Tensix broadcast that also excludes some Tensix rows and a Tensix column: 16 and 17
// hold the Ethernet and DRAM cores, Tensix cores start at 18.
const std::set<uint32_t> TRANSLATED_ROWS_TO_EXCLUDE = {16, 17, 20, 22, 26, 27};
const std::set<uint32_t> TRANSLATED_COLS_TO_EXCLUDE = {16, 17, 20};

std::vector<uint32_t> payload(uint32_t num_words) {
    std::vector<uint32_t> data(num_words);
    for (uint32_t i = 0; i < num_words; i++) {
        data[i] = 0xB0AD0000 + i;
    }
    return data;
}

// Starts a Wormhole cluster and offers the zero / broadcast / read back steps every broadcast test uses.
class ClusterBroadcastFixture : public ::testing::Test {
protected:
    void start_cluster(uint32_t num_host_mem_channels) {
        cluster_ = test_utils::make_default_test_cluster(
            ClusterOptions{.num_host_mem_ch_per_mmio_device = num_host_mem_channels});
        if (cluster_->get_cluster_description()->get_arch() != tt::ARCH::WORMHOLE_B0) {
            GTEST_SKIP() << "The broadcast exclusion rows and columns are Wormhole NOC0 coordinates.";
        }
        cluster_->set_barrier_address_params(
            {wormhole::L1_BARRIER_BASE, wormhole::ERISC_BARRIER_BASE, /*dram_barrier_base=*/0});
        // Parks every BRISC so that writing L1 cannot disturb a running core, then starts the device.
        test_utils::safe_test_cluster_start(cluster_.get());
        started_ = true;
    }

    void TearDown() override {
        if (started_) {
            cluster_->close_device();
        }
    }

    std::vector<CoreCoord> tensix_cores_noc0(ChipId chip) const {
        return cluster_->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::NOC0);
    }

    std::vector<CoreCoord> dram_cores(ChipId chip) const {
        const SocDescriptor& soc = cluster_->get_soc_descriptor(chip);
        std::vector<CoreCoord> cores;
        cores.reserve(soc.get_num_dram_channels());
        for (int channel = 0; channel < soc.get_num_dram_channels(); channel++) {
            cores.push_back(soc.get_dram_core_for_channel(channel, 0, CoordSystem::NOC0));
        }
        return cores;
    }

    void write(ChipId chip, const std::vector<CoreCoord>& cores, const std::vector<uint32_t>& data) {
        for (const CoreCoord& core : cores) {
            cluster_->write_to_device(data.data(), data.size() * sizeof(uint32_t), chip, core, BROADCAST_ADDRESS);
        }
    }

    // Zeroes the test address on every Tensix and DRAM core of every chip, so that untouched cores read zeros.
    void zero_all_cores(uint32_t num_words) {
        const std::vector<uint32_t> zeros(num_words, 0);
        for (ChipId chip : cluster_->get_target_device_ids()) {
            write(chip, tensix_cores_noc0(chip), zeros);
            write(chip, dram_cores(chip), zeros);
        }
        cluster_->wait_for_non_mmio_flush();
    }

    void broadcast(
        const std::vector<uint32_t>& data,
        const std::set<ChipId>& chips_to_exclude,
        std::set<uint32_t> rows_to_exclude,
        std::set<uint32_t> cols_to_exclude,
        bool use_translated_coords = false) {
        cluster_->broadcast_write_to_cluster(
            data.data(),
            data.size() * sizeof(uint32_t),
            BROADCAST_ADDRESS,
            chips_to_exclude,
            rows_to_exclude,
            cols_to_exclude,
            use_translated_coords);
        cluster_->wait_for_non_mmio_flush();
    }

    void expect_cores_hold(ChipId chip, const std::vector<CoreCoord>& cores, const std::vector<uint32_t>& expected) {
        for (const CoreCoord& core : cores) {
            std::vector<uint32_t> readback(expected.size(), 0);
            cluster_->read_from_device(
                readback.data(), chip, core, BROADCAST_ADDRESS, readback.size() * sizeof(uint32_t));
            EXPECT_EQ(readback, expected) << "chip " << chip << " core " << core.str();
        }
    }

    std::unique_ptr<Cluster> cluster_;
    bool started_ = false;
};

// Parameterized over the payload size in 32-bit words (a single word and a 4 KiB block) and over the host memory
// channels per MMIO chip: the result is the same with or without host memory.
class ClusterBroadcastWrite : public ClusterBroadcastFixture,
                              public ::testing::WithParamInterface<std::tuple<uint32_t, uint32_t>> {
protected:
    void SetUp() override { start_cluster(std::get<1>(GetParam())); }

    uint32_t num_words() const { return std::get<0>(GetParam()); }
};

TEST_P(ClusterBroadcastWrite, TensixBroadcastReachesEveryChip) {
    const std::vector<uint32_t> data = payload(num_words());
    const std::vector<uint32_t> zeros(data.size(), 0);
    zero_all_cores(data.size());

    broadcast(data, {}, NON_TENSIX_ROWS, NON_TENSIX_COLS);

    for (ChipId chip : cluster_->get_target_device_ids()) {
        expect_cores_hold(chip, tensix_cores_noc0(chip), data);
        expect_cores_hold(chip, dram_cores(chip), zeros);
    }
}

TEST_P(ClusterBroadcastWrite, DramBroadcastReachesEveryChip) {
    const std::vector<uint32_t> data = payload(num_words());
    const std::vector<uint32_t> zeros(data.size(), 0);
    zero_all_cores(data.size());

    broadcast(data, {}, {}, TENSIX_COLS);

    for (ChipId chip : cluster_->get_target_device_ids()) {
        expect_cores_hold(chip, dram_cores(chip), data);
        expect_cores_hold(chip, tensix_cores_noc0(chip), zeros);
    }
}

TEST_P(ClusterBroadcastWrite, ExcludedColumnStaysUntouched) {
    const std::vector<uint32_t> data = payload(num_words());
    const std::vector<uint32_t> zeros(data.size(), 0);
    zero_all_cores(data.size());

    std::set<uint32_t> cols_to_exclude = NON_TENSIX_COLS;
    cols_to_exclude.insert(EXCLUDED_TENSIX_COL);
    broadcast(data, {}, NON_TENSIX_ROWS, cols_to_exclude);

    for (ChipId chip : cluster_->get_target_device_ids()) {
        std::vector<CoreCoord> targeted;
        std::vector<CoreCoord> excluded;
        for (const CoreCoord& core : tensix_cores_noc0(chip)) {
            (core.x == EXCLUDED_TENSIX_COL ? excluded : targeted).push_back(core);
        }
        ASSERT_FALSE(excluded.empty()) << "chip " << chip << " has no Tensix core in the excluded column";
        expect_cores_hold(chip, targeted, data);
        expect_cores_hold(chip, excluded, zeros);
    }
}

TEST_P(ClusterBroadcastWrite, TranslatedExcludedRowsAndColumnStayUntouched) {
    const std::optional<SemVer> eth_version = cluster_->get_ethernet_firmware_version();
    const bool translated_broadcast_supported =
        (eth_version >= SemVer(6, 8, 0) || eth_version == SemVer(6, 7, 241)) &&
        cluster_->get_soc_descriptor(*cluster_->get_target_device_ids().begin()).noc_translation_enabled;
    if (!translated_broadcast_supported) {
        GTEST_SKIP() << "Broadcast in translated coordinates needs Ethernet firmware 6.8.0 (or 6.7.241) and NOC "
                        "translation.";
    }
    const std::vector<uint32_t> data = payload(num_words());
    const std::vector<uint32_t> zeros(data.size(), 0);
    zero_all_cores(data.size());

    broadcast(data, {}, TRANSLATED_ROWS_TO_EXCLUDE, TRANSLATED_COLS_TO_EXCLUDE, /*use_translated_coords=*/true);

    for (ChipId chip : cluster_->get_target_device_ids()) {
        std::vector<CoreCoord> targeted;
        std::vector<CoreCoord> excluded;
        for (const CoreCoord& core :
             cluster_->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)) {
            const bool is_excluded =
                TRANSLATED_ROWS_TO_EXCLUDE.count(core.y) != 0 || TRANSLATED_COLS_TO_EXCLUDE.count(core.x) != 0;
            (is_excluded ? excluded : targeted).push_back(core);
        }
        ASSERT_FALSE(excluded.empty()) << "chip " << chip << " has no Tensix core in the excluded rows or column";
        expect_cores_hold(chip, targeted, data);
        expect_cores_hold(chip, excluded, zeros);
    }
}

INSTANTIATE_TEST_SUITE_P(
    PayloadWords,
    ClusterBroadcastWrite,
    ::testing::Combine(::testing::Values(1, 1024), ::testing::Values(0, 1)),
    [](const ::testing::TestParamInfo<std::tuple<uint32_t, uint32_t>>& info) {
        return std::to_string(std::get<0>(info.param)) + "Words_" + std::to_string(std::get<1>(info.param)) +
               "HostMemChannels";
    });

enum class ExcludedChip { FIRST, LAST };

// Parameterized over which chip the broadcast excludes: the first chip id (an MMIO gateway, whose remote chips stay
// targeted) or the last one (a remote chip on an N300 or a TTSim wh_x2 cluster).
class ClusterBroadcastWriteExcludedChip : public ClusterBroadcastFixture,
                                          public ::testing::WithParamInterface<ExcludedChip> {
protected:
    void SetUp() override { start_cluster(/*num_host_mem_channels=*/1); }
};

TEST_P(ClusterBroadcastWriteExcludedChip, ExcludedChipStaysUntouched) {
    const std::set<ChipId> chips = cluster_->get_target_device_ids();
    if (chips.size() < 2) {
        GTEST_SKIP() << "Excluding a chip needs a cluster of at least two chips.";
    }
    const ChipId excluded_chip = GetParam() == ExcludedChip::FIRST ? *chips.begin() : *chips.rbegin();
    const std::vector<uint32_t> data = payload(1024);
    const std::vector<uint32_t> zeros(data.size(), 0);
    zero_all_cores(data.size());

    broadcast(data, {excluded_chip}, NON_TENSIX_ROWS, NON_TENSIX_COLS);

    for (ChipId chip : chips) {
        expect_cores_hold(chip, tensix_cores_noc0(chip), chip == excluded_chip ? zeros : data);
    }
}

INSTANTIATE_TEST_SUITE_P(
    ExcludedChips,
    ClusterBroadcastWriteExcludedChip,
    ::testing::Values(ExcludedChip::FIRST, ExcludedChip::LAST),
    [](const ::testing::TestParamInfo<ExcludedChip>& info) {
        return std::string(info.param == ExcludedChip::FIRST ? "FirstChip" : "LastChip");
    });

// NOC0 exclusions the Ethernet broadcast refuses, whether or not its firmware could deliver the request.
struct UnsupportedExclusion {
    std::string name;
    std::set<uint32_t> rows_to_exclude;
    std::set<uint32_t> cols_to_exclude;
};

class ClusterBroadcastWriteRefusal : public ClusterBroadcastFixture,
                                     public ::testing::WithParamInterface<UnsupportedExclusion> {
protected:
    void SetUp() override { start_cluster(/*num_host_mem_channels=*/1); }
};

TEST_P(ClusterBroadcastWriteRefusal, UnsupportedExclusionThrows) {
    const std::set<ChipId> remote_chips = cluster_->get_target_remote_device_ids();
    if (remote_chips.empty()) {
        GTEST_SKIP() << "The Ethernet broadcast for a single chip needs a remote chip.";
    }
    EthernetBroadcast eth_broadcast(cluster_->get_remote_chip(*remote_chips.begin())->get_remote_communication());
    const uint32_t data = 0xB0AD0000;
    std::set<uint32_t> rows_to_exclude = GetParam().rows_to_exclude;
    std::set<uint32_t> cols_to_exclude = GetParam().cols_to_exclude;

    EXPECT_THROW(
        (void)eth_broadcast.broadcast_write_to_cluster(
            &data,
            sizeof(data),
            BROADCAST_ADDRESS,
            {},
            rows_to_exclude,
            cols_to_exclude,
            /*use_translated_coords=*/false),
        error::UmdException<error::RuntimeError>);
}

INSTANTIATE_TEST_SUITE_P(
    UnsupportedExclusions,
    ClusterBroadcastWriteRefusal,
    ::testing::Values(
        // Only the non-Tensix rows are excluded, so the request targets both Tensix and DRAM cores.
        UnsupportedExclusion{"TensixAndDramMix", {0, 6}, {}},
        // NOC0 row 2 holds Tensix cores: some but not all Tensix rows are excluded.
        UnsupportedExclusion{"PartialNoc0TensixRows", {0, 2, 6}, {0, 5}}),
    [](const ::testing::TestParamInfo<UnsupportedExclusion>& info) { return info.param.name; });

// start_device puts every Tensix RISC of every chip, remote chips included, in soft reset, whatever state a
// previous user left them in.
TEST(ClusterStartDeviceReset, StartDeviceResetsTensixOnEveryChip) {
    std::unique_ptr<Cluster> cluster =
        test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    if (cluster->get_cluster_description()->get_arch() != tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP() << "The BRISC park program used to leave a core running is the Wormhole one.";
    }
    constexpr uint64_t brisc_code_address = 0x20;

    // Leave BRISC running a parked program on one Tensix core per chip.
    for (ChipId chip : cluster->get_target_device_ids()) {
        const CoreCoord core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
        cluster->assert_risc_reset(chip, core, RiscType::ALL_TENSIX);
        cluster->write_to_device(&BRISC_TRAMPOLINE_JMP, sizeof(BRISC_TRAMPOLINE_JMP), chip, core, 0);
        cluster->write_to_device(
            wh_brisc_park_program.data(),
            wh_brisc_park_program.size() * sizeof(uint32_t),
            chip,
            core,
            brisc_code_address);
        cluster->l1_membar(chip, {core});
        cluster->deassert_risc_reset(chip, core, RiscType::BRISC);
        ASSERT_EQ(cluster->get_risc_reset_state(chip, core) & RiscType::BRISC, RiscType::NONE)
            << "chip " << chip << ": BRISC did not leave reset";
    }

    cluster->start_device({});

    for (ChipId chip : cluster->get_target_device_ids()) {
        const CoreCoord core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
        EXPECT_EQ(cluster->get_risc_reset_state(chip, core) & RiscType::ALL_TENSIX, RiscType::ALL_TENSIX)
            << "chip " << chip << ": Tensix RISCs not in reset after start_device";
    }
    cluster->close_device();
}

}  // namespace
