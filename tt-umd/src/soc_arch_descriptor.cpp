// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/soc_arch_descriptor.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/utils/common.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Topology of an architecture as its implementation header describes it.
struct ArchConstants {
    tt_xy_pair grid_size;
    std::vector<tt_xy_pair> tensix_cores;
    std::vector<std::vector<tt_xy_pair>> dram_cores;
    std::vector<tt_xy_pair> eth_cores;
    std::vector<tt_xy_pair> arc_cores;
    std::vector<tt_xy_pair> pcie_cores;
    std::vector<tt_xy_pair> router_cores;
    std::vector<tt_xy_pair> security_cores;
    std::vector<tt_xy_pair> l2cpu_cores;
    std::vector<tt_xy_pair> dispatch_cores;
    uint32_t worker_l1_size;
    uint32_t eth_l1_size;
    uint64_t dram_bank_size;
    std::vector<uint32_t> noc0_x_to_noc1_x;
    std::vector<uint32_t> noc0_y_to_noc1_y;
};

ArchConstants get_arch_constants(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return {
                wormhole::GRID_SIZE,
                wormhole::TENSIX_CORES_NOC0,
                wormhole::DRAM_CORES_NOC0,
                wormhole::ETH_CORES_NOC0,
                wormhole::ARC_CORES_NOC0,
                wormhole::PCIE_CORES_NOC0,
                wormhole::ROUTER_CORES_NOC0,
                wormhole::SECURITY_CORES_NOC0,
                wormhole::L2CPU_CORES_NOC0,
                {},
                wormhole::TENSIX_L1_SIZE,
                wormhole::ETH_L1_SIZE,
                wormhole::DRAM_BANK_SIZE,
                wormhole::NOC0_X_TO_NOC1_X,
                wormhole::NOC0_Y_TO_NOC1_Y};
        case tt::ARCH::BLACKHOLE:
            return {
                blackhole::GRID_SIZE,
                blackhole::TENSIX_CORES_NOC0,
                blackhole::DRAM_CORES_NOC0,
                blackhole::ETH_CORES_NOC0,
                blackhole::ARC_CORES_NOC0,
                blackhole::PCIE_CORES_NOC0,
                blackhole::ROUTER_CORES_NOC0,
                blackhole::SECURITY_CORES_NOC0,
                blackhole::L2CPU_CORES_NOC0,
                {},
                blackhole::TENSIX_L1_SIZE,
                blackhole::ETH_L1_SIZE,
                blackhole::DRAM_BANK_SIZE,
                blackhole::NOC0_X_TO_NOC1_X,
                blackhole::NOC0_Y_TO_NOC1_Y};
        case tt::ARCH::QUASAR:
            return {
                grendel::GRID_SIZE,
                grendel::TENSIX_CORES_NOC0,
                grendel::DRAM_CORES_NOC0,
                grendel::ETH_CORES_NOC0,
                grendel::ARC_CORES_NOC0,
                grendel::PCIE_CORES_NOC0,
                grendel::ROUTER_CORES_NOC0,
                grendel::SECURITY_CORES_NOC0,
                grendel::L2CPU_CORES_NOC0,
                grendel::DISPATCH_CORES_NOC0,
                grendel::TENSIX_L1_SIZE,
                grendel::ETH_L1_SIZE,
                grendel::DRAM_BANK_SIZE,
                grendel::NOC0_X_TO_NOC1_X,
                grendel::NOC0_Y_TO_NOC1_Y};
        default:
            UMD_THROW(error::RuntimeError, "No SoC description for architecture " + arch_to_str(arch) + ".");
    }
}

YAML::Node load_soc_descriptor_yaml(const std::string& soc_descriptor_path) {
    UMD_ASSERT(
        std::filesystem::exists(soc_descriptor_path),
        error::RuntimeError,
        "SoC descriptor file " + soc_descriptor_path + " does not exist.");
    return YAML::LoadFile(soc_descriptor_path);
}

tt::ARCH parse_arch(const YAML::Node& device_descriptor_yaml) {
    const std::string arch_name = device_descriptor_yaml["arch_name"].as<std::string>();
    const tt::ARCH arch = arch_from_str(arch_name);
    UMD_ASSERT(arch != tt::ARCH::Invalid, error::RuntimeError, "Unknown arch_name " + arch_name + ".");
    return arch;
}

tt::IpVariant parse_ip_variant(const std::string& ip_variant) {
    const std::string name = to_lower(ip_variant);
    if (name == "grendel") {
        return tt::IpVariant::GRENDEL;
    }
    if (name == "horizon") {
        return tt::IpVariant::HORIZON;
    }
    if (name == "saturn") {
        return tt::IpVariant::SATURN;
    }
    if (name == "trinity") {
        return tt::IpVariant::TRINITY;
    }
    UMD_THROW(error::RuntimeError, "Unknown ip_variant " + ip_variant + ".");
}

// A feature value the YAML may leave out, in which case it stays at zero.
int read_feature(const YAML::Node& features, const char* feature, const char* field) {
    const YAML::Node node = features[feature][field];
    return node ? node.as<int>() : 0;
}

}  // namespace

SocArchDescriptor::SocArchDescriptor(tt::ARCH arch) : arch_(arch) { SocArchDescriptor::init(); }

SocArchDescriptor::SocArchDescriptor(const std::string& soc_descriptor_path) :
    device_descriptor_file_path_(soc_descriptor_path) {
    SocArchDescriptor::init();
}

void SocArchDescriptor::init() {
    if (!device_descriptor_file_path_.empty()) {
        YAML::Node device_descriptor_yaml = load_soc_descriptor_yaml(device_descriptor_file_path_);
        load_from_yaml(device_descriptor_yaml);
    } else {
        ArchConstants constants = get_arch_constants(arch_);
        grid_size_ = constants.grid_size;
        tensix_cores_ = std::move(constants.tensix_cores);
        dram_cores_ = std::move(constants.dram_cores);
        eth_cores_ = std::move(constants.eth_cores);
        firmware_cores_ = std::move(constants.arc_cores);
        pcie_cores_ = std::move(constants.pcie_cores);
        router_cores_ = std::move(constants.router_cores);
        security_cores_ = std::move(constants.security_cores);
        l2cpu_cores_ = std::move(constants.l2cpu_cores);
        dispatch_cores_ = std::move(constants.dispatch_cores);
        worker_l1_size_ = constants.worker_l1_size;
        eth_l1_size_ = constants.eth_l1_size;
        dram_bank_size_ = constants.dram_bank_size;
        noc0_x_to_noc1_x_ = std::move(constants.noc0_x_to_noc1_x);
        noc0_y_to_noc1_y_ = std::move(constants.noc0_y_to_noc1_y);
    }
    build_derived_data();
}

tt::ARCH SocArchDescriptor::get_arch_from_path(const std::string& soc_descriptor_path) {
    return parse_arch(load_soc_descriptor_yaml(soc_descriptor_path));
}

tt_xy_pair SocArchDescriptor::get_grid_size_from_path(const std::string& soc_descriptor_path) {
    const YAML::Node device_descriptor_yaml = load_soc_descriptor_yaml(soc_descriptor_path);
    return {
        device_descriptor_yaml["grid"]["x_size"].as<size_t>(), device_descriptor_yaml["grid"]["y_size"].as<size_t>()};
}

tt_xy_pair SocArchDescriptor::calculate_grid_size(const std::vector<tt_xy_pair>& cores) {
    std::set<size_t> xs;
    std::set<size_t> ys;
    for (const tt_xy_pair& core : cores) {
        xs.insert(core.x);
        ys.insert(core.y);
    }
    return {xs.size(), ys.size()};
}

void SocArchDescriptor::build_derived_data() {
    cores_.clear();
    const auto add_cores = [this](const std::vector<tt_xy_pair>& cores, CoreType type, std::size_t l1_size) {
        for (const tt_xy_pair& core : cores) {
            cores_[core] = CoreDescriptor{core, type, l1_size};
        }
    };
    add_cores(firmware_cores_, CoreType::ARC, 0);
    add_cores(pcie_cores_, CoreType::PCIE, 0);
    add_cores(flatten_vector(dram_cores_), CoreType::DRAM, 0);
    add_cores(eth_cores_, CoreType::ETH, eth_l1_size_);
    add_cores(tensix_cores_, CoreType::TENSIX, worker_l1_size_);
    add_cores(router_cores_, CoreType::ROUTER_ONLY, 0);
    add_cores(security_cores_, CoreType::SECURITY, 0);
    add_cores(l2cpu_cores_, CoreType::L2CPU, 0);
    add_cores(dispatch_cores_, CoreType::DISPATCH, 0);

    worker_grid_size_ = calculate_grid_size(tensix_cores_);

    dram_core_channel_map_.clear();
    for (size_t channel = 0; channel < dram_cores_.size(); channel++) {
        for (size_t subchannel = 0; subchannel < dram_cores_[channel].size(); subchannel++) {
            dram_core_channel_map_[dram_cores_[channel][subchannel]] =
                std::make_tuple(static_cast<int>(channel), static_cast<int>(subchannel));
        }
    }

    ethernet_core_channel_map_.clear();
    for (size_t channel = 0; channel < eth_cores_.size(); channel++) {
        ethernet_core_channel_map_[eth_cores_[channel]] = static_cast<int>(channel);
    }
}

void SocArchDescriptor::load_from_yaml(YAML::Node& device_descriptor_yaml) {
    // Core lists other than the mandatory ones may be left out of the YAML, meaning there are none.
    const auto read_cores = [&device_descriptor_yaml](const char* key) -> std::vector<tt_xy_pair> {
        const YAML::Node node = device_descriptor_yaml[key];
        return node ? convert_to_tt_xy_pair(node.as<std::vector<std::string>>()) : std::vector<tt_xy_pair>{};
    };
    const auto read_noc1_map = [&device_descriptor_yaml](const char* key) -> std::vector<uint32_t> {
        const YAML::Node node = device_descriptor_yaml[key];
        return node ? node.as<std::vector<uint32_t>>() : std::vector<uint32_t>{};
    };

    arch_ = parse_arch(device_descriptor_yaml);
    grid_size_ = {
        device_descriptor_yaml["grid"]["x_size"].as<size_t>(), device_descriptor_yaml["grid"]["y_size"].as<size_t>()};

    tensix_cores_ = read_cores("functional_workers");
    dram_cores_ = convert_dram_cores_from_yaml(device_descriptor_yaml);
    eth_cores_ = read_cores("eth");
    firmware_cores_ = read_cores("arc");
    pcie_cores_ = read_cores("pcie");
    router_cores_ = read_cores("router_only");
    security_cores_ = read_cores("security");
    l2cpu_cores_ = read_cores("l2cpu");
    dispatch_cores_ = read_cores("dispatch");

    worker_l1_size_ = device_descriptor_yaml["worker_l1_size"].as<uint32_t>();
    eth_l1_size_ = device_descriptor_yaml["eth_l1_size"].as<uint32_t>();
    dram_bank_size_ = device_descriptor_yaml["dram_bank_size"].as<uint64_t>();

    noc0_x_to_noc1_x_ = read_noc1_map("noc0_x_to_noc1_x");
    noc0_y_to_noc1_y_ = read_noc1_map("noc0_y_to_noc1_y");

    const YAML::Node features = device_descriptor_yaml["features"];
    overlay_version_ = read_feature(features, "overlay", "version");
    unpacker_version_ = read_feature(features, "unpacker", "version");
    dst_size_alignment_ = read_feature(features, "math", "dst_size_alignment");
    packer_version_ = read_feature(features, "packer", "version");

    const YAML::Node ip_variant = device_descriptor_yaml["ip_variant"];
    ip_variant_ = ip_variant ? std::optional(parse_ip_variant(ip_variant.as<std::string>())) : std::nullopt;
}

std::vector<tt_xy_pair> SocArchDescriptor::convert_to_tt_xy_pair(const std::vector<std::string>& core_strings) {
    std::vector<tt_xy_pair> cores;
    cores.reserve(core_strings.size());
    for (const std::string& core_string : core_strings) {
        const size_t delimiter = core_string.find('-');
        UMD_ASSERT(
            delimiter != std::string::npos,
            error::RuntimeError,
            "Core location " + core_string + " is not in x-y format.");
        cores.emplace_back(std::stoul(core_string.substr(0, delimiter)), std::stoul(core_string.substr(delimiter + 1)));
    }
    return cores;
}

std::vector<std::vector<tt_xy_pair>> SocArchDescriptor::convert_dram_cores_from_yaml(
    YAML::Node& device_descriptor_yaml, const std::string& dram_core) {
    std::vector<std::vector<tt_xy_pair>> dram_cores;
    for (const YAML::Node& channel : device_descriptor_yaml[dram_core]) {
        // A channel is either a list of its subchannels' cores or a single core.
        const std::vector<std::string> channel_cores = channel.IsSequence()
                                                           ? channel.as<std::vector<std::string>>()
                                                           : std::vector<std::string>{channel.as<std::string>()};
        dram_cores.push_back(convert_to_tt_xy_pair(channel_cores));
    }
    return dram_cores;
}

}  // namespace tt::umd
