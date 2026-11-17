// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/soc_arch_descriptor.hpp"

#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/utils/common.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

YAML::Node load_descriptor(const std::string& soc_descriptor_path) {
    try {
        return YAML::LoadFile(soc_descriptor_path);
    } catch (const YAML::Exception& e) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Failed to load SoC descriptor {}: {}", soc_descriptor_path, e.what()));
    }
}

std::optional<tt::IpVariant> ip_variant_from_str(const std::string& ip_variant) {
    const std::string lowered = to_lower(ip_variant);
    if (lowered == "grendel") {
        return tt::IpVariant::GRENDEL;
    }
    if (lowered == "horizon") {
        return tt::IpVariant::HORIZON;
    }
    if (lowered == "saturn") {
        return tt::IpVariant::SATURN;
    }
    if (lowered == "trinity") {
        return tt::IpVariant::TRINITY;
    }
    UMD_THROW(error::RuntimeError, fmt::format("Unknown ip_variant '{}' in SoC descriptor.", ip_variant));
}

int read_feature_version(const YAML::Node& features, const char* feature, const char* field) {
    if (features && features[feature] && features[feature][field]) {
        return features[feature][field].as<int>();
    }
    return 0;
}

}  // namespace

SocArchDescriptor::SocArchDescriptor(tt::ARCH arch) {
    arch_ = arch;
    SocArchDescriptor::init();
}

SocArchDescriptor::SocArchDescriptor(const std::string& soc_descriptor_path) {
    UMD_ASSERT(!soc_descriptor_path.empty(), error::RuntimeError, "SoC descriptor path is empty.");
    device_descriptor_file_path_ = soc_descriptor_path;
    SocArchDescriptor::init();
}

void SocArchDescriptor::init() {
    if (!device_descriptor_file_path_.empty()) {
        YAML::Node device_descriptor_yaml = load_descriptor(device_descriptor_file_path_);
        load_from_yaml(device_descriptor_yaml);
        build_derived_data();
        return;
    }

    switch (arch_) {
        case tt::ARCH::WORMHOLE_B0:
            grid_size_ = wormhole::GRID_SIZE;
            tensix_cores_ = wormhole::TENSIX_CORES_NOC0;
            dram_cores_ = wormhole::DRAM_CORES_NOC0;
            eth_cores_ = wormhole::ETH_CORES_NOC0;
            firmware_cores_ = wormhole::ARC_CORES_NOC0;
            pcie_cores_ = wormhole::PCIE_CORES_NOC0;
            router_cores_ = wormhole::ROUTER_CORES_NOC0;
            security_cores_ = wormhole::SECURITY_CORES_NOC0;
            l2cpu_cores_ = wormhole::L2CPU_CORES_NOC0;
            dispatch_cores_ = {};
            worker_l1_size_ = wormhole::TENSIX_L1_SIZE;
            eth_l1_size_ = wormhole::ETH_L1_SIZE;
            dram_bank_size_ = wormhole::DRAM_BANK_SIZE;
            noc0_x_to_noc1_x_ = wormhole::NOC0_X_TO_NOC1_X;
            noc0_y_to_noc1_y_ = wormhole::NOC0_Y_TO_NOC1_Y;
            break;
        case tt::ARCH::BLACKHOLE:
            grid_size_ = blackhole::GRID_SIZE;
            tensix_cores_ = blackhole::TENSIX_CORES_NOC0;
            dram_cores_ = blackhole::DRAM_CORES_NOC0;
            eth_cores_ = blackhole::ETH_CORES_NOC0;
            firmware_cores_ = blackhole::ARC_CORES_NOC0;
            pcie_cores_ = blackhole::PCIE_CORES_NOC0;
            router_cores_ = blackhole::ROUTER_CORES_NOC0;
            security_cores_ = blackhole::SECURITY_CORES_NOC0;
            l2cpu_cores_ = blackhole::L2CPU_CORES_NOC0;
            dispatch_cores_ = {};
            worker_l1_size_ = blackhole::TENSIX_L1_SIZE;
            eth_l1_size_ = blackhole::ETH_L1_SIZE;
            dram_bank_size_ = blackhole::DRAM_BANK_SIZE;
            noc0_x_to_noc1_x_ = blackhole::NOC0_X_TO_NOC1_X;
            noc0_y_to_noc1_y_ = blackhole::NOC0_Y_TO_NOC1_Y;
            break;
        case tt::ARCH::QUASAR:
            grid_size_ = grendel::GRID_SIZE;
            tensix_cores_ = grendel::TENSIX_CORES_NOC0;
            dram_cores_ = grendel::DRAM_CORES_NOC0;
            eth_cores_ = grendel::ETH_CORES_NOC0;
            firmware_cores_ = grendel::ARC_CORES_NOC0;
            pcie_cores_ = grendel::PCIE_CORES_NOC0;
            router_cores_ = grendel::ROUTER_CORES_NOC0;
            security_cores_ = grendel::SECURITY_CORES_NOC0;
            l2cpu_cores_ = grendel::L2CPU_CORES_NOC0;
            dispatch_cores_ = grendel::DISPATCH_CORES_NOC0;
            worker_l1_size_ = grendel::TENSIX_L1_SIZE;
            eth_l1_size_ = grendel::ETH_L1_SIZE;
            dram_bank_size_ = grendel::DRAM_BANK_SIZE;
            noc0_x_to_noc1_x_ = grendel::NOC0_X_TO_NOC1_X;
            noc0_y_to_noc1_y_ = grendel::NOC0_Y_TO_NOC1_Y;
            break;
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("No SoC architecture description exists for architecture {}.", arch_to_str(arch_)));
    }
    build_derived_data();
}

tt::ARCH SocArchDescriptor::get_arch_from_path(const std::string& soc_descriptor_path) {
    const YAML::Node device_descriptor_yaml = load_descriptor(soc_descriptor_path);
    UMD_ASSERT(
        device_descriptor_yaml["arch_name"],
        error::RuntimeError,
        fmt::format("SoC descriptor {} has no arch_name.", soc_descriptor_path));
    return arch_from_str(device_descriptor_yaml["arch_name"].as<std::string>());
}

tt_xy_pair SocArchDescriptor::get_grid_size_from_path(const std::string& soc_descriptor_path) {
    const YAML::Node device_descriptor_yaml = load_descriptor(soc_descriptor_path);
    UMD_ASSERT(
        device_descriptor_yaml["grid"],
        error::RuntimeError,
        fmt::format("SoC descriptor {} has no grid.", soc_descriptor_path));
    return {
        device_descriptor_yaml["grid"]["x_size"].as<std::size_t>(),
        device_descriptor_yaml["grid"]["y_size"].as<std::size_t>()};
}

tt_xy_pair SocArchDescriptor::calculate_grid_size(const std::vector<tt_xy_pair>& cores) {
    // Cores of one type form a grid of distinct columns by distinct rows, which need not be adjacent
    // on the NOC (Tensix columns are split by the DRAM and CPU columns).
    std::set<std::size_t> columns;
    std::set<std::size_t> rows;
    for (const tt_xy_pair& core : cores) {
        columns.insert(core.x);
        rows.insert(core.y);
    }
    return {columns.size(), rows.size()};
}

std::vector<tt_xy_pair> SocArchDescriptor::convert_to_tt_xy_pair(const std::vector<std::string>& core_strings) {
    std::vector<tt_xy_pair> cores;
    cores.reserve(core_strings.size());
    for (const std::string& core_string : core_strings) {
        const size_t delimiter = core_string.find('-');
        UMD_ASSERT(
            delimiter != std::string::npos,
            error::RuntimeError,
            fmt::format("SoC descriptor core '{}' is not in x-y format.", core_string));
        cores.emplace_back(std::stoul(core_string.substr(0, delimiter)), std::stoul(core_string.substr(delimiter + 1)));
    }
    return cores;
}

std::vector<std::vector<tt_xy_pair>> SocArchDescriptor::convert_dram_cores_from_yaml(
    YAML::Node& device_descriptor_yaml, const std::string& dram_core) {
    std::vector<std::vector<tt_xy_pair>> dram_cores;
    const YAML::Node channels = device_descriptor_yaml[dram_core];
    if (!channels) {
        return dram_cores;
    }
    // A channel is either a list of the cores reaching the same bank, or a single core.
    for (const YAML::Node& channel : channels) {
        if (channel.IsSequence()) {
            dram_cores.push_back(convert_to_tt_xy_pair(channel.as<std::vector<std::string>>()));
        } else {
            dram_cores.push_back(convert_to_tt_xy_pair({channel.as<std::string>()}));
        }
    }
    return dram_cores;
}

void SocArchDescriptor::load_from_yaml(YAML::Node& device_descriptor_yaml) {
    auto read_cores = [&](const char* key) {
        const YAML::Node node = device_descriptor_yaml[key];
        return node ? convert_to_tt_xy_pair(node.as<std::vector<std::string>>()) : std::vector<tt_xy_pair>{};
    };
    auto read_table = [&](const char* key) {
        const YAML::Node node = device_descriptor_yaml[key];
        return node ? node.as<std::vector<uint32_t>>() : std::vector<uint32_t>{};
    };

    UMD_ASSERT(
        device_descriptor_yaml["arch_name"] && device_descriptor_yaml["grid"],
        error::RuntimeError,
        fmt::format("SoC descriptor {} needs arch_name and grid.", device_descriptor_file_path_));

    arch_ = arch_from_str(device_descriptor_yaml["arch_name"].as<std::string>());
    grid_size_ = tt_xy_pair(
        device_descriptor_yaml["grid"]["x_size"].as<std::size_t>(),
        device_descriptor_yaml["grid"]["y_size"].as<std::size_t>());

    tensix_cores_ = read_cores("functional_workers");
    dram_cores_ = convert_dram_cores_from_yaml(device_descriptor_yaml);
    eth_cores_ = read_cores("eth");
    firmware_cores_ = read_cores("arc");
    pcie_cores_ = read_cores("pcie");
    router_cores_ = read_cores("router_only");
    security_cores_ = read_cores("security");
    l2cpu_cores_ = read_cores("l2cpu");
    dispatch_cores_ = read_cores("dispatch");

    worker_l1_size_ =
        device_descriptor_yaml["worker_l1_size"] ? device_descriptor_yaml["worker_l1_size"].as<uint32_t>() : 0;
    eth_l1_size_ = device_descriptor_yaml["eth_l1_size"] ? device_descriptor_yaml["eth_l1_size"].as<uint32_t>() : 0;
    dram_bank_size_ =
        device_descriptor_yaml["dram_bank_size"] ? device_descriptor_yaml["dram_bank_size"].as<uint64_t>() : 0;

    noc0_x_to_noc1_x_ = read_table("noc0_x_to_noc1_x");
    noc0_y_to_noc1_y_ = read_table("noc0_y_to_noc1_y");

    const YAML::Node features = device_descriptor_yaml["features"];
    overlay_version_ = read_feature_version(features, "overlay", "version");
    unpacker_version_ = read_feature_version(features, "unpacker", "version");
    dst_size_alignment_ = read_feature_version(features, "math", "dst_size_alignment");
    packer_version_ = read_feature_version(features, "packer", "version");

    if (device_descriptor_yaml["ip_variant"]) {
        ip_variant_ = ip_variant_from_str(device_descriptor_yaml["ip_variant"].as<std::string>());
    }
}

void SocArchDescriptor::build_derived_data() {
    cores_.clear();
    dram_core_channel_map_.clear();
    ethernet_core_channel_map_.clear();

    auto add_cores = [&](const std::vector<tt_xy_pair>& cores, CoreType type, std::size_t l1_size) {
        for (const tt_xy_pair& core : cores) {
            cores_[core] = CoreDescriptor{core, type, l1_size};
        }
    };

    add_cores(tensix_cores_, CoreType::TENSIX, worker_l1_size_);
    add_cores(eth_cores_, CoreType::ETH, eth_l1_size_);
    add_cores(firmware_cores_, CoreType::ARC, 0);
    add_cores(pcie_cores_, CoreType::PCIE, 0);
    add_cores(router_cores_, CoreType::ROUTER_ONLY, 0);
    add_cores(security_cores_, CoreType::SECURITY, 0);
    add_cores(l2cpu_cores_, CoreType::L2CPU, 0);
    add_cores(dispatch_cores_, CoreType::DISPATCH, 0);

    for (std::size_t channel = 0; channel < dram_cores_.size(); channel++) {
        add_cores(dram_cores_[channel], CoreType::DRAM, 0);
        for (std::size_t subchannel = 0; subchannel < dram_cores_[channel].size(); subchannel++) {
            dram_core_channel_map_[dram_cores_[channel][subchannel]] =
                std::make_tuple(static_cast<int>(channel), static_cast<int>(subchannel));
        }
    }

    for (std::size_t channel = 0; channel < eth_cores_.size(); channel++) {
        ethernet_core_channel_map_[eth_cores_[channel]] = static_cast<int>(channel);
    }

    worker_grid_size_ = calculate_grid_size(tensix_cores_);
}

}  // namespace tt::umd
