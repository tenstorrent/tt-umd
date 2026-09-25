// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Tests for the simulation multichip core infrastructure:
// - SocDescriptor::is_core_of_type (moved from a local helper in tt_sim_tt_device.cpp)
// - TTSimProtocol's process-qualified MMIO id
// - TTSimCommunicator shared dlopen / select_chip_if_needed patterns
//
// The SocDescriptor and MMIO id tests run on any CI host (no hardware required).
// The communicator tests require TT_UMD_SIMULATOR and are skipped otherwise.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

#ifdef TT_UMD_BUILD_SIMULATION
#include <fmt/format.h>
#include <unistd.h>

#include <exception>
#include <functional>
#include <set>

#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/tt_device/protocol/tt_sim_protocol.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/utils/error.hpp"
#endif

using namespace tt;
using namespace tt::umd;

// ---------------------------------------------------------------------------
// SocDescriptor::is_core_of_type
// ---------------------------------------------------------------------------

// Return a ChipInfo that satisfies per-arch harvesting constraints so that
// SocDescriptor construction does not throw.
//
// Blackhole: BlackholeCoordinateManager requires exactly 2 or NUM_ETH_CHANNELS
// ETH cores harvested on a full grid.  Use the minimal valid case (2 channels).
static ChipInfo make_valid_chip_info(ARCH arch) {
    ChipInfo info{};
    if (arch == ARCH::BLACKHOLE) {
        info.harvesting_masks.eth_harvesting_mask = 0x3;  // harvest channels 0 & 1
    }
    return info;
}

class IsCoreOfTypeTest : public ::testing::TestWithParam<ARCH> {};

TEST_P(IsCoreOfTypeTest, DramCoreIsIdentifiedCorrectly) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path), make_valid_chip_info(arch));

    // Every core returned by get_cores(DRAM, TRANSLATED) must satisfy is_core_of_type.
    auto dram_cores = soc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    ASSERT_FALSE(dram_cores.empty()) << "Expected at least one DRAM core for arch " << arch_to_str(arch);

    for (const auto& core : dram_cores) {
        tt_xy_pair xy{core.x, core.y};
        EXPECT_TRUE(soc.is_core_of_type(xy, CoreType::DRAM, CoordSystem::TRANSLATED))
            << "DRAM core (" << xy.x << ", " << xy.y << ") not recognized by is_core_of_type";
    }
}

TEST_P(IsCoreOfTypeTest, TensixCoreIsNotDram) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path), make_valid_chip_info(arch));

    auto tensix_cores = soc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    if (tensix_cores.empty()) {
        GTEST_SKIP() << "No TENSIX cores for arch " << arch_to_str(arch);
    }

    for (const auto& core : tensix_cores) {
        tt_xy_pair xy{core.x, core.y};
        EXPECT_FALSE(soc.is_core_of_type(xy, CoreType::DRAM, CoordSystem::TRANSLATED))
            << "TENSIX core (" << xy.x << ", " << xy.y << ") wrongly identified as DRAM";
    }
}

TEST_P(IsCoreOfTypeTest, EthCoreIsIdentifiedCorrectly) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path), make_valid_chip_info(arch));

    auto eth_cores = soc.get_cores(CoreType::ETH, CoordSystem::TRANSLATED);
    if (eth_cores.empty()) {
        GTEST_SKIP() << "No ETH cores for arch " << arch_to_str(arch);
    }

    for (const auto& core : eth_cores) {
        tt_xy_pair xy{core.x, core.y};
        EXPECT_TRUE(soc.is_core_of_type(xy, CoreType::ETH, CoordSystem::TRANSLATED))
            << "ETH core (" << xy.x << ", " << xy.y << ") not recognized by is_core_of_type";
    }
}

TEST_P(IsCoreOfTypeTest, GarbageXYIsNotAnyKnownType) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path), make_valid_chip_info(arch));

    // Use an absurdly large coordinate that cannot belong to any real core.
    tt_xy_pair garbage{9999, 9999};
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::DRAM, CoordSystem::TRANSLATED));
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::TENSIX, CoordSystem::TRANSLATED));
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::ETH, CoordSystem::TRANSLATED));
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::ARC, CoordSystem::TRANSLATED));
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::PCIE, CoordSystem::TRANSLATED));
}

// QUASAR is included because is_core_of_type's DRAM-teleport path in
// tt_sim_tt_device.cpp special-cases it, and a quasar_simulation_1x1.yaml
// descriptor is available in the repo.
INSTANTIATE_TEST_SUITE_P(
    AllArchs,
    IsCoreOfTypeTest,
    ::testing::Values(ARCH::WORMHOLE_B0, ARCH::BLACKHOLE, ARCH::QUASAR),
    [](const ::testing::TestParamInfo<ARCH>& info) { return arch_to_str(info.param); });

// ---------------------------------------------------------------------------
// TTSimCommunicator: shared dlopen and chip selection
// These tests require a running simulator (TT_UMD_SIMULATOR env var).
// ---------------------------------------------------------------------------

#ifdef TT_UMD_BUILD_SIMULATION

// ---------------------------------------------------------------------------
// TTSimProtocol's MMIO id
// ---------------------------------------------------------------------------

// A simulated endpoint exists only inside the process that brought its image up, so the id UMD
// addresses and locks it by carries the process as well as the endpoint. These are the properties
// the lock names depend on: the endpoint is still in there, two chips of one image stay apart, and
// two processes never agree.
TEST(TTSimProtocolMmioId, CarriesTheEndpointAndTheProcess) {
    const int chip0 = TTSimProtocol::process_local_mmio_id(0);
    const int chip1 = TTSimProtocol::process_local_mmio_id(1);

    // The endpoint survives in the low bits, and the process in the rest.
    EXPECT_EQ(chip0 & 31, 0);
    EXPECT_EQ(chip1 & 31, 1);
    EXPECT_EQ(chip0 >> 5, static_cast<int>(getpid()));

    // Each chip of a multi-endpoint image keeps its own lock, and nothing claims a PCI device number
    // silicon could also be using.
    EXPECT_NE(chip0, chip1);
    EXPECT_GT(chip0, 31);

    // Stable for the process, so a lock initialized under this name is found again when acquired.
    EXPECT_EQ(chip0, TTSimProtocol::process_local_mmio_id(0));
}

// The 5 bits reserved for the endpoint are the same 5 bits the BDF device field has, so a chip id
// that does not fit would silently land in the process part instead of overflowing visibly.
TEST(TTSimProtocolMmioId, RefusesAChipIdThatDoesNotFitTheBdfField) {
    EXPECT_THROW(TTSimProtocol::process_local_mmio_id(32), std::exception);
    EXPECT_THROW(TTSimProtocol::process_local_mmio_id(-1), std::exception);
}

// ---------------------------------------------------------------------------
// TTSimCommunicator::scan_pci_endpoints
// ---------------------------------------------------------------------------

namespace {

// A config space holding endpoints at the given BDFs, recording every BDF it is asked about. An
// empty slot reads all-ones, as on a real bus.
class FakeConfigSpace {
public:
    explicit FakeConfigSpace(std::set<uint32_t> endpoints) : endpoints_(std::move(endpoints)) {}

    std::function<uint32_t(uint32_t, uint32_t)> reader() {
        return [this](uint32_t bdf, uint32_t offset) {
            probed_.push_back(bdf);
            EXPECT_EQ(offset, 0u) << "presence is decided by the vendor/device dword";
            return endpoints_.count(bdf) != 0 ? 0xB1401E52u : 0xFFFFFFFFu;
        };
    }

    const std::vector<uint32_t>& probed() const { return probed_; }

private:
    std::set<uint32_t> endpoints_;
    std::vector<uint32_t> probed_;
};

// Bus in the high nibble and device in the low nibble, the order the BH Galaxy image numbers its
// chips in -- which is not BDF order.
constexpr uint8_t BH_GALAXY_BUS_DEVICE[32] = {
    0x01, 0x02, 0x03, 0x04, 0xC1, 0xC2, 0xC3, 0xC4, 0x05, 0x06, 0x07, 0x08, 0xC5, 0xC6, 0xC7, 0xC8,
    0x45, 0x46, 0x47, 0x48, 0x85, 0x86, 0x87, 0x88, 0x41, 0x42, 0x43, 0x44, 0x81, 0x82, 0x83, 0x84,
};

uint32_t bdf_of(uint32_t bus, uint32_t device) { return (bus << 8) | (device << 3); }

}  // namespace

// Every current image: chip N at bus 0, device N. The scan has to name exactly those, in that
// order, so chip ids come out as they always have.
TEST(TTSimScanPciEndpoints, LinearLayoutIsBusZeroInDeviceOrder) {
    std::set<uint32_t> endpoints;
    for (uint32_t device = 0; device < 32; ++device) {
        endpoints.insert(bdf_of(0, device));
    }
    FakeConfigSpace config_space(endpoints);

    const std::vector<uint32_t> bdfs = TTSimCommunicator::scan_pci_endpoints(config_space.reader());

    ASSERT_EQ(bdfs.size(), 32u);
    for (uint32_t chip = 0; chip < 32; ++chip) {
        EXPECT_EQ(bdfs[chip], bdf_of(0, chip)) << "chip " << chip;
        EXPECT_EQ(bdfs[chip] >> 3, chip) << "chip id no longer matches the device number";
    }
}

// Images built before sparse layouts exit the process on any BDF off bus 0, and look exactly like
// a linear image built since. A linear layout must therefore never be probed past bus 0.
TEST(TTSimScanPciEndpoints, LinearLayoutNeverProbesPastBusZero) {
    for (const uint32_t num_chips : {1u, 2u, 4u, 32u}) {
        std::set<uint32_t> endpoints;
        for (uint32_t device = 0; device < num_chips; ++device) {
            endpoints.insert(bdf_of(0, device));
        }
        FakeConfigSpace config_space(endpoints);

        EXPECT_EQ(TTSimCommunicator::scan_pci_endpoints(config_space.reader()).size(), num_chips);
        for (const uint32_t bdf : config_space.probed()) {
            EXPECT_EQ(bdf >> 8, 0u) << num_chips << "-chip image probed BDF 0x" << std::hex << bdf;
        }
    }
}

// The BH Galaxy layout: endpoints on buses 0x00, 0x40, 0x80, 0xC0 at devices 1-8, and nothing at
// bus 0 device 0. All 32 are found, in BDF order rather than the image's own chip order.
TEST(TTSimScanPciEndpoints, SparseLayoutIsFoundAcrossBusesInBdfOrder) {
    std::set<uint32_t> endpoints;
    for (const uint8_t bus_device : BH_GALAXY_BUS_DEVICE) {
        endpoints.insert(bdf_of(bus_device & 0xF0, bus_device & 0x0F));
    }
    ASSERT_EQ(endpoints.size(), 32u);
    ASSERT_EQ(endpoints.count(0u), 0u);
    FakeConfigSpace config_space(endpoints);

    const std::vector<uint32_t> bdfs = TTSimCommunicator::scan_pci_endpoints(config_space.reader());

    EXPECT_EQ(bdfs, std::vector<uint32_t>(endpoints.begin(), endpoints.end()));
    ASSERT_EQ(bdfs.size(), 32u);
    // Dense chip ids in BDF order: bus 0x00 first, then 0x40, 0x80, 0xC0, devices 1-8 on each.
    const uint32_t buses[] = {0x00, 0x40, 0x80, 0xC0};
    for (uint32_t chip = 0; chip < 32; ++chip) {
        EXPECT_EQ(bdfs[chip], bdf_of(buses[chip / 8], (chip % 8) + 1)) << "chip " << chip;
    }
}

// Only function 0 of each slot is probed: simulated endpoints are single-function.
TEST(TTSimScanPciEndpoints, ProbesFunctionZeroOnly) {
    FakeConfigSpace config_space({bdf_of(0x40, 1)});

    EXPECT_EQ(TTSimCommunicator::scan_pci_endpoints(config_space.reader()), std::vector<uint32_t>{bdf_of(0x40, 1)});
    for (const uint32_t bdf : config_space.probed()) {
        EXPECT_EQ(bdf & 0x7u, 0u) << "probed BDF 0x" << std::hex << bdf;
    }
}

// Nothing anywhere: every slot on every bus is probed, and none is reported.
TEST(TTSimScanPciEndpoints, EmptyConfigSpaceFindsNothing) {
    FakeConfigSpace config_space({});

    EXPECT_TRUE(TTSimCommunicator::scan_pci_endpoints(config_space.reader()).empty());
    EXPECT_EQ(config_space.probed().size(), 256u * 32u);
    EXPECT_EQ(std::set<uint32_t>(config_space.probed().begin(), config_space.probed().end()).size(), 256u * 32u)
        << "a slot was probed twice";
}

// A BDF that could not have come from enumerating an image is refused up front, before anything
// is loaded, rather than misrouting config reads later.
TEST(TTSimCommunicatorBdf, RefusesABdfThatIsNotAFunctionZeroEndpoint) {
    const std::filesystem::path unused = "/nonexistent/libttsim.so";
    EXPECT_THROW(TTSimCommunicator(unused, false, 0, 2, 2, bdf_of(0x40, 1) | 1), std::exception);
    EXPECT_THROW(TTSimCommunicator(unused, false, 0, 2, 2, 0x10000), std::exception);

    TTSimCommunicator linear(unused, false, 0, 2, 2);
    EXPECT_EQ(linear.get_pci_bdf(), std::nullopt);
    TTSimCommunicator sparse(unused, false, 0, 2, 2, bdf_of(0xC0, 8));
    EXPECT_EQ(sparse.get_pci_bdf(), bdf_of(0xC0, 8));
}

class TTSimCommunicatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        simulator_path_ = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path_ == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR is not set. Skipping communicator tests.";
        }
    }

    const char* simulator_path_ = nullptr;
};

// Verify that TTSimTTDevice::create produces a non-null device.
TEST_F(TTSimCommunicatorTest, CreateSimDevice) {
    auto device = TTSimTTDevice::create(simulator_path_);
    ASSERT_NE(device, nullptr);
    // The device should have a valid soc descriptor.
    const auto& soc = device->get_soc_descriptor();
    EXPECT_NE(soc.arch, ARCH::Invalid);
}

// The endpoint count must come from the image itself: it is what tells UMD how many chips a
// multichip build models without a cluster descriptor to declare it.
TEST_F(TTSimCommunicatorTest, EnumerateMmioDeviceBdfs) {
    // Enumeration stands on its own: no device is constructed first, as on silicon.
    const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path_);

    // Every image exposes at least one endpoint. The count and where the endpoints sit are
    // properties of the image, so they are reported rather than asserted.
    ASSERT_FALSE(bdfs.empty());
    std::cout << "simulator exposes " << bdfs.size() << " host-visible PCI endpoint(s):";
    for (const uint32_t bdf : bdfs) {
        std::cout << fmt::format(" {:02x}:{:02x}.{:x}", bdf >> 8, (bdf >> 3) & 0x1F, bdf & 0x7);
    }
    std::cout << std::endl;

    // Endpoints are 16-bit BDFs, function 0, ascending and unique.
    for (size_t i = 0; i < bdfs.size(); ++i) {
        EXPECT_LE(bdfs[i], 0xFFFFu) << "endpoint " << i << " is wider than a BDF";
        EXPECT_EQ(bdfs[i] & 0x7u, 0u) << "endpoint " << i << " is not function 0";
        if (i > 0) {
            EXPECT_GT(bdfs[i], bdfs[i - 1]) << "endpoints are not ascending";
        }
    }

    // A linear image fills bus 0 from device 0 up; one that leaves device 0 empty is sparse.
    if (bdfs.front() == 0) {
        for (size_t i = 0; i < bdfs.size(); ++i) {
            EXPECT_EQ(bdfs[i], i << 3) << "linear image endpoint " << i << " is not at bus 0, device " << i;
        }
    }
}

// Verify that write_to_device after close_device() is a no-op (closed_ guard).
// Also verifies that a second close_device() and the destructor do not
// double-call pfn_libttsim_exit_ (shutdown() honours closed_).
TEST_F(TTSimCommunicatorTest, WriteAfterCloseIsNoOp) {
    auto device = TTSimTTDevice::create(simulator_path_);
    ASSERT_NE(device, nullptr);

    const auto& soc = device->get_soc_descriptor();
    auto dram_cores = soc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        GTEST_SKIP() << "No DRAM cores; cannot run I/O test.";
    }

    tt_xy_pair target{dram_cores[0].x, dram_cores[0].y};

    // Write a sentinel before close.
    uint32_t sentinel = 0xCAFEBABE;
    device->write_to_device(&sentinel, target, 0, sizeof(sentinel));

    device->close_device();

    // write_to_device after close must not crash (closed_ guard makes it a no-op).
    uint32_t after_close = 0xDEADBEEF;
    EXPECT_NO_THROW(device->write_to_device(&after_close, target, 0, sizeof(after_close)));

    // A second close_device() must not crash either (shutdown() checks closed_).
    EXPECT_NO_THROW(device->close_device());
    // Destructor also calls shutdown() — again honoured by the closed_ guard.
}

// Two TTSimTTDevice instances with distinct chip_ids writing and reading back
// independently. Exercises select_chip_if_needed for each I/O call.
TEST_F(TTSimCommunicatorTest, TwoDevicesIndependentIO) {
    // Enumerate before bringing anything up: the simulator aborts the process rather than throwing
    // when asked to start twice, so a single-endpoint image has to be detected up front.
    const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path_);
    if (bdfs.size() < 2) {
        GTEST_SKIP() << "Simulator image exposes " << bdfs.size()
                     << " endpoint(s); multi-device I/O needs at least two.";
    }
    const size_t num_chips = bdfs.size();
    const auto endpoint_count = static_cast<uint32_t>(bdfs.size());

    // The endpoint count selects shared-BDF addressing: the two devices share one image, each
    // reaching its own endpoint by the BDF enumeration found it at.
    auto dev_0 = TTSimTTDevice::create_for_chip(
        simulator_path_,
        static_cast<ChipId>(0),
        /*num_host_mem_channels=*/0,
        /*copy_sim_binary=*/false,
        num_chips,
        endpoint_count,
        bdfs[0]);
    ASSERT_NE(dev_0, nullptr);
    auto dev_1 = TTSimTTDevice::create_for_chip(
        simulator_path_,
        static_cast<ChipId>(1),
        /*num_host_mem_channels=*/0,
        /*copy_sim_binary=*/false,
        num_chips,
        endpoint_count,
        bdfs[1]);
    ASSERT_NE(dev_1, nullptr);

    const auto& soc_0 = dev_0->get_soc_descriptor();
    auto dram_cores = soc_0.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        GTEST_SKIP() << "No DRAM cores; cannot run I/O test.";
    }

    tt_xy_pair target{dram_cores[0].x, dram_cores[0].y};

    // Write a pattern to device 0.
    uint32_t pattern_0 = 0xDEAD0000;
    dev_0->write_to_device(&pattern_0, target, 0, sizeof(pattern_0));

    // Write a different pattern to device 1 at the same address.
    uint32_t pattern_1 = 0xBEEF0001;
    dev_1->write_to_device(&pattern_1, target, 0, sizeof(pattern_1));

    // Read back from device 0 — should still see pattern_0.
    uint32_t readback_0 = 0;
    dev_0->read_from_device(&readback_0, target, 0, sizeof(readback_0));
    EXPECT_EQ(readback_0, pattern_0);

    // Read back from device 1 — should see pattern_1.
    uint32_t readback_1 = 0;
    dev_1->read_from_device(&readback_1, target, 0, sizeof(readback_1));
    EXPECT_EQ(readback_1, pattern_1);

    dev_0->close_device();
    dev_1->close_device();
}

// ---------------------------------------------------------------------------
// Topology discovery against a simulator image
// ---------------------------------------------------------------------------

// A simulator with no cluster_descriptor.yaml beside it has its topology discovered rather than
// declared, so what the descriptor says is a property of the image. These are the invariants.
class TTSimDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR is not set. Skipping discovery tests.";
        }
        simulator_path_ = simulator_path;

        if (std::filesystem::exists(SimulationChip::get_cluster_descriptor_path_from_simulator_path(simulator_path_))) {
            GTEST_SKIP() << "A cluster_descriptor.yaml sits beside this simulator, so its topology is declared "
                            "rather than discovered.";
        }

        arch_ = SocDescriptor::get_arch_from_soc_descriptor_path(
            SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_path_));
        if (arch_ == ARCH::QUASAR) {
            GTEST_SKIP() << "TTSim models neither ARC nor Ethernet for Quasar, so there is no firmware to discover "
                            "a topology from.";
        }

        // How many chips this image models, which only whoever staged it knows -- the assertions
        // below are otherwise satisfied by a topology that lost a chip, since a discovery that
        // returns the MMIO chip alone has nothing for the remote half of the test to look at. CI
        // names it per image; a developer running against an image by hand need not.
        if (const char* expected_chips = std::getenv("TT_UMD_SIM_EXPECTED_CHIPS"); expected_chips != nullptr) {
            expected_chips_ = static_cast<size_t>(std::stoul(expected_chips));
        }
    }

    std::string simulator_path_;
    ARCH arch_ = ARCH::Invalid;
    std::optional<size_t> expected_chips_;
};

TEST_F(TTSimDiscoveryTest, ChipCountMatchesEnumeratedEndpoints) {
    const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path_);
    ASSERT_FALSE(bdfs.empty());

    // target_devices is deliberately left unset, so every chip discovery finds stays visible.
    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_path_;
    options.num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster(options);

    ClusterDescriptor* cluster_desc = cluster.get_cluster_description();
    ASSERT_NE(cluster_desc, nullptr);

    // One MMIO chip per host-visible endpoint: every chip an image exposes to the host is reached
    // over its own BDF, so a different count means discovery either missed one or invented one.
    EXPECT_EQ(cluster_desc->get_chips_with_mmio().size(), bdfs.size());

    // Chips beyond those are reached over ethernet -- wh_x2's second chip has no endpoint of its
    // own -- so the totals coincide only where every chip is MMIO. Where the image's chip count is
    // known, that is what the total has to be: >= bdfs.size() alone passes at 1 == 1 for wh_x2,
    // which is the image whose remote chip this exists to notice.
    EXPECT_GE(cluster_desc->get_number_of_chips(), bdfs.size());
    if (expected_chips_.has_value()) {
        EXPECT_EQ(cluster_desc->get_number_of_chips(), *expected_chips_)
            << "discovery found " << cluster_desc->get_number_of_chips() << " chip(s) in an image modelling "
            << *expected_chips_;
    }
    for (const ChipId chip : cluster_desc->get_all_chips()) {
        const bool is_mmio = cluster_desc->get_chips_with_mmio().count(chip) != 0;
        EXPECT_EQ(cluster_desc->is_chip_mmio_capable(chip), is_mmio)
            << "chip " << chip << " disagrees with the MMIO set it is or is not in";
    }
}

// A chip with no PCI endpoint of its own is reached over ethernet, so discovery finding it at all
// means it walked the links. wh_x2 models exactly that: one endpoint, two chips.
TEST_F(TTSimDiscoveryTest, RemoteChipsAreReachedOverEthernet) {
    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_path_;
    options.num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster(options);

    ClusterDescriptor* cluster_desc = cluster.get_cluster_description();
    ASSERT_NE(cluster_desc, nullptr);

    const auto& mmio_chips = cluster_desc->get_chips_with_mmio();
    const auto& eth_connections = cluster_desc->get_ethernet_connections();

    // Every chip the image models beyond its endpoints was reached over ethernet. Asserting that
    // count before walking the links is what makes the walk mean something: a discovery that missed
    // the remote chip entirely leaves nothing to iterate over, and every expectation inside the
    // loop below then holds vacuously.
    if (expected_chips_.has_value()) {
        ASSERT_GE(*expected_chips_, mmio_chips.size());
        const size_t expected_remote = *expected_chips_ - mmio_chips.size();
        ASSERT_EQ(cluster_desc->get_number_of_chips() - mmio_chips.size(), expected_remote)
            << "discovery reached " << cluster_desc->get_number_of_chips() - mmio_chips.size()
            << " chip(s) over ethernet in an image modelling " << expected_remote;
    }

    for (const ChipId chip : cluster_desc->get_all_chips()) {
        if (mmio_chips.count(chip) != 0) {
            continue;
        }

        // Every link this chip reports has to land on a chip in the same cluster, and at least one
        // of them is what discovery arrived over.
        const auto links = eth_connections.find(chip);
        ASSERT_NE(links, eth_connections.end()) << "remote chip " << chip << " reports no ethernet links";
        EXPECT_FALSE(links->second.empty()) << "remote chip " << chip << " reports no ethernet links";
        for (const auto& [channel, remote] : links->second) {
            const ChipId peer = std::get<0>(remote);
            EXPECT_NE(cluster_desc->get_all_chips().count(peer), 0u)
                << "chip " << chip << " channel " << channel << " links to unknown chip " << peer;
        }
    }
}

// A simulated chip has no PCIDevice, so its bus id and BDF come from the endpoint the image
// enumerated -- tray and ASIC positions are derived from the bus id downstream. Chips are numbered
// densely in BDF order, so MMIO chip i is the i-th endpoint.
TEST_F(TTSimDiscoveryTest, MmioChipsReportTheirEnumeratedBusAndBdf) {
    const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path_);
    ASSERT_FALSE(bdfs.empty());

    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_path_;
    options.num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster(options);

    ClusterDescriptor* cluster_desc = cluster.get_cluster_description();
    ASSERT_NE(cluster_desc, nullptr);
    ASSERT_EQ(cluster_desc->get_chips_with_mmio().size(), bdfs.size());

    for (size_t index = 0; index < bdfs.size(); ++index) {
        const auto chip = static_cast<ChipId>(index);
        const uint32_t bdf = bdfs[index];
        ASSERT_NE(cluster_desc->get_chips_with_mmio().count(chip), 0u) << "chip " << chip << " is not MMIO";

        const auto& bus_ids = cluster_desc->get_chip_to_bus_id();
        ASSERT_NE(bus_ids.find(chip), bus_ids.end()) << "chip " << chip << " has no bus id";
        EXPECT_EQ(bus_ids.at(chip), (bdf >> 8) & 0xFF) << "chip " << chip;
        EXPECT_EQ(cluster_desc->get_bus_id(chip), (bdf >> 8) & 0xFF) << "chip " << chip;

        const auto& pci_bdfs = cluster_desc->get_chip_pci_bdfs();
        ASSERT_NE(pci_bdfs.find(chip), pci_bdfs.end()) << "chip " << chip << " has no BDF";
        EXPECT_EQ(
            pci_bdfs.at(chip), fmt::format("0000:{:02x}:{:02x}.{:x}", (bdf >> 8) & 0xFF, (bdf >> 3) & 0x1F, bdf & 0x7))
            << "chip " << chip;
    }
}

TEST_F(TTSimDiscoveryTest, HarvestingComesFromTheDevice) {
    if (arch_ != ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Harvesting expectations below are Blackhole's.";
    }

    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_path_;
    options.num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster(options);

    ClusterDescriptor* cluster_desc = cluster.get_cluster_description();
    ASSERT_NE(cluster_desc, nullptr);
    ASSERT_FALSE(cluster_desc->get_all_chips().empty());

    for (const ChipId chip : cluster_desc->get_all_chips()) {
        // TTSim models ETH tiles 12 and 13 as harvested, so its ENABLED_ETH telemetry reads 0x0FFF
        // and the mask UMD derives from it is 0x3000. Asserting the derived value is what keeps
        // discovery honest about reading harvesting from the device instead of assuming it.
        EXPECT_EQ(cluster_desc->get_harvesting_masks(chip).eth_harvesting_mask, 0x3000u)
            << "chip " << chip << " ETH harvesting did not come from telemetry";

        // Whatever the image harvests, the descriptor and the SoC descriptor built from it have to
        // tell the same story about how many Tensix columns survived. The mask is what discovery
        // read off the device; the harvested grid is what the SoC descriptor built from it actually
        // took out, one column per set bit on Blackhole. A regression that reads the mask and then
        // drops it leaves a full grid behind a nonzero mask, which a nonempty core set cannot see.
        const SocDescriptor& soc_desc = cluster.get_soc_descriptor(chip);
        const size_t harvested_columns =
            CoordinateManager::get_num_harvested(cluster_desc->get_harvesting_masks(chip).tensix_harvesting_mask);
        const tt_xy_pair harvested_grid = soc_desc.get_harvested_grid_size(CoreType::TENSIX);
        EXPECT_EQ(harvested_grid.x, harvested_columns)
            << "chip " << chip << ": SoC descriptor harvested " << harvested_grid.x
            << " Tensix column(s), discovered mask names " << harvested_columns;

        // And the cores that survived are exactly the grid that survived.
        const tt_xy_pair live_grid = soc_desc.get_grid_size(CoreType::TENSIX);
        EXPECT_EQ(soc_desc.get_cores(CoreType::TENSIX).size(), live_grid.x * live_grid.y)
            << "chip " << chip << " Tensix core count disagrees with its " << live_grid.x << "x" << live_grid.y
            << " grid";
    }
}

#endif  // TT_UMD_BUILD_SIMULATION
