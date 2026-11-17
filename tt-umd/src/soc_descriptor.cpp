// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/soc_descriptor.hpp"

#include <fmt/format.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "coordinates/blackhole_coordinate_manager.hpp"
#include "tt-umd/coordinates/coordinate_manager.hpp"
#include "tt-umd/utils/common.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Core types a SoC descriptor lists, with the YAML key each is serialized under.
constexpr std::pair<CoreType, const char*> SERIALIZED_CORE_TYPES[] = {
    {CoreType::ARC, "arc"},
    {CoreType::PCIE, "pcie"},
    {CoreType::ETH, "eth"},
    {CoreType::TENSIX, "functional_workers"},
    {CoreType::ROUTER_ONLY, "router_only"},
    {CoreType::SECURITY, "security"},
    {CoreType::L2CPU, "l2cpu"},
    {CoreType::DISPATCH, "dispatch"},
};

// Every core type a chip is made of, in the order get_all_cores() lists them.
constexpr CoreType ALL_CORE_TYPES[] = {
    CoreType::TENSIX,
    CoreType::DRAM,
    CoreType::ETH,
    CoreType::ARC,
    CoreType::PCIE,
    CoreType::ROUTER_ONLY,
    CoreType::SECURITY,
    CoreType::L2CPU,
    CoreType::DISPATCH,
};

const std::vector<tt_xy_pair>& get_arch_cores(const SocArchDescriptor& arch_desc, CoreType core_type) {
    switch (core_type) {
        case CoreType::ARC:
            return arch_desc.get_firmware_cores();
        case CoreType::PCIE:
            return arch_desc.get_pcie_cores();
        case CoreType::ETH:
            return arch_desc.get_eth_cores();
        case CoreType::TENSIX:
            return arch_desc.get_tensix_cores();
        case CoreType::ROUTER_ONLY:
            return arch_desc.get_router_cores();
        case CoreType::SECURITY:
            return arch_desc.get_security_cores();
        case CoreType::L2CPU:
            return arch_desc.get_l2cpu_cores();
        case CoreType::DISPATCH:
            return arch_desc.get_dispatch_cores();
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Core type {} is not listed by core.", to_str(core_type)));
    }
}

std::string arch_to_yaml_name(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return "WORMHOLE_B0";
        case tt::ARCH::BLACKHOLE:
            return "BLACKHOLE";
        case tt::ARCH::QUASAR:
            return "QUASAR";
        default:
            UMD_THROW(
                error::RuntimeError, fmt::format("Architecture {} has no SoC descriptor name.", arch_to_str(arch)));
    }
}

std::string ip_variant_to_yaml_name(tt::IpVariant ip_variant) {
    switch (ip_variant) {
        case tt::IpVariant::GRENDEL:
            return "grendel";
        case tt::IpVariant::HORIZON:
            return "horizon";
        case tt::IpVariant::SATURN:
            return "saturn";
        case tt::IpVariant::TRINITY:
            return "trinity";
    }
    return "";
}

// A core carries the system its coordinates are in; only a LITERAL one needs to be told.
CoordSystem coord_system_of(const CoreCoord& core, CoordSystem literal_coord_system) {
    return core.coord_system == CoordSystem::LITERAL ? literal_coord_system : core.coord_system;
}

}  // namespace

SocDescriptor::SocDescriptor(std::shared_ptr<const SocArchDescriptor> arch_desc, const ChipInfo chip_info) :
    arch_desc_(std::move(arch_desc)) {
    UMD_ASSERT(arch_desc_ != nullptr, error::RuntimeError, "SocDescriptor needs an architecture descriptor.");
    init_from_arch_descriptor(chip_info);
}

void SocDescriptor::init_from_arch_descriptor(const ChipInfo& chip_info) {
    arch = arch_desc_->get_arch();
    grid_size = arch_desc_->get_grid_size();
    trisc_sizes = arch_desc_->get_trisc_sizes();
    device_descriptor_file_path = arch_desc_->get_device_descriptor_file_path();
    overlay_version = arch_desc_->get_overlay_version();
    unpacker_version = arch_desc_->get_unpacker_version();
    dst_size_alignment = arch_desc_->get_dst_size_alignment();
    packer_version = arch_desc_->get_packer_version();
    worker_l1_size = static_cast<int>(arch_desc_->get_worker_l1_size());
    eth_l1_size = static_cast<int>(arch_desc_->get_eth_l1_size());
    dram_bank_size = arch_desc_->get_dram_bank_size();

    noc_translation_enabled = chip_info.noc_translation_enabled;
    harvesting_masks = chip_info.harvesting_masks;
    create_coordinate_manager(chip_info.board_type, chip_info.asic_location);
}

void SocDescriptor::create_coordinate_manager(const BoardType board_type, const uint8_t asic_location) {
    // Blackhole boards connect only one of the two PCIe cores to the host. When harvesting doesn't say
    // which, the board type does.
    if (arch == tt::ARCH::BLACKHOLE && harvesting_masks.pcie_harvesting_mask == 0 &&
        arch_desc_->get_pcie_cores().size() > 1) {
        harvesting_masks.pcie_harvesting_mask =
            BlackholeCoordinateManager::get_pcie_harvesting_mask(board_type, asic_location);
    }

    const std::vector<std::vector<tt_xy_pair>>& dram_channels = arch_desc_->get_dram_cores();
    const tt_xy_pair dram_grid_size(dram_channels.size(), dram_channels.empty() ? 0 : dram_channels.front().size());

    coordinate_manager = CoordinateManager::create_coordinate_manager(
        arch,
        noc_translation_enabled,
        harvesting_masks,
        arch_desc_->get_worker_grid_size(),
        arch_desc_->get_tensix_cores(),
        dram_grid_size,
        flatten_vector(dram_channels),
        arch_desc_->get_eth_cores(),
        SocArchDescriptor::calculate_grid_size(arch_desc_->get_firmware_cores()),
        arch_desc_->get_firmware_cores(),
        SocArchDescriptor::calculate_grid_size(arch_desc_->get_pcie_cores()),
        arch_desc_->get_pcie_cores(),
        arch_desc_->get_router_cores(),
        arch_desc_->get_security_cores(),
        arch_desc_->get_l2cpu_cores(),
        arch_desc_->get_dispatch_cores(),
        arch_desc_->get_noc0_x_to_noc1_x(),
        arch_desc_->get_noc0_y_to_noc1_y());
}

tt::ARCH SocDescriptor::get_arch_from_soc_descriptor_path(const std::string& soc_descriptor_path) {
    return SocArchDescriptor::get_arch_from_path(soc_descriptor_path);
}

tt_xy_pair SocDescriptor::get_grid_size_from_soc_descriptor_path(const std::string& soc_descriptor_path) {
    return SocArchDescriptor::get_grid_size_from_path(soc_descriptor_path);
}

const SocArchDescriptor& SocDescriptor::get_arch_descriptor() const { return *arch_desc_; }

CoreCoord SocDescriptor::translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const {
    return coordinate_manager->translate_coord_to(core_coord, coord_system);
}

std::unordered_set<CoreCoord> SocDescriptor::translate_coords_to(
    const std::unordered_set<CoreCoord>& core_coord, const CoordSystem coord_system) const {
    std::unordered_set<CoreCoord> translated;
    for (const CoreCoord& core : core_coord) {
        translated.insert(translate_coord_to(core, coord_system));
    }
    return translated;
}

std::unordered_set<tt_xy_pair> SocDescriptor::translate_coords_to_xy_pair(
    const std::unordered_set<CoreCoord>& core_coord, const CoordSystem coord_system) const {
    std::unordered_set<tt_xy_pair> translated;
    for (const CoreCoord& core : core_coord) {
        translated.insert(translate_coord_to(core, coord_system).to_pair());
    }
    return translated;
}

CoreCoord SocDescriptor::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    return coordinate_manager->get_coord_at(core, coord_system);
}

CoreCoord SocDescriptor::translate_coord_to(
    const tt_xy_pair core_location, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    return coordinate_manager->translate_coord_to(core_location, input_coord_system, target_coord_system);
}

CoreCoord SocDescriptor::translate_chip_coord_to_translated_coord(const CoreCoord core, const NocId noc_id) const {
    if (core.coord_system == CoordSystem::LITERAL) {
        return core;
    }

    const bool use_noc1 = noc_id == NocId::NOC1;
    if (!noc_translation_enabled) {
        return translate_coord_to(core, use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
    }

    const CoreCoord translated = translate_coord_to(core, CoordSystem::TRANSLATED);
    if (!use_noc1) {
        return translated;
    }

    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            // Coordinates below 16 bypass the Wormhole translation tables and are raw coordinates of
            // whichever NOC carries the request; translated coordinates start at 16.
            constexpr size_t FIRST_TRANSLATED_COORDINATE = 16;
            if (translated.x < FIRST_TRANSLATED_COORDINATE && translated.y < FIRST_TRANSLATED_COORDINATE) {
                return translate_coord_to(core, CoordSystem::NOC1);
            }
            return translated;
        }
        case tt::ARCH::BLACKHOLE: {
            // Blackhole leaves X untranslated in rows 0 and 1 (router, ARC, PCIe and ETH rows), so there
            // X is the raw X of the NOC carrying the request while Y still goes through the table.
            constexpr size_t FIRST_TRANSLATED_ROW = 2;
            if (translated.y < FIRST_TRANSLATED_ROW) {
                const CoreCoord noc1 = translate_coord_to(core, CoordSystem::NOC1);
                return CoreCoord(noc1.x, translated.y, translated.core_type, CoordSystem::NOC1);
            }
            return translated;
        }
        default:
            return translated;
    }
}

tt_xy_pair SocDescriptor::translate_chip_coord_to_translated(const CoreCoord core, const NocId noc_id) const {
    return translate_chip_coord_to_translated_coord(core, noc_id).to_pair();
}

bool SocDescriptor::is_core_of_type(const tt_xy_pair& core, CoreType core_type, CoordSystem coord_system) const {
    if (coord_system == CoordSystem::LOGICAL) {
        // Logical coordinates don't identify a core by themselves, only together with the core type.
        try {
            translate_coord_to(CoreCoord(core, core_type, CoordSystem::LOGICAL), CoordSystem::NOC0);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    try {
        return get_coord_at(core, coord_system).core_type == core_type;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<CoreCoord> SocDescriptor::translate_coordinates(
    const std::vector<CoreCoord>& noc0_cores, const CoordSystem coord_system) const {
    std::vector<CoreCoord> translated;
    translated.reserve(noc0_cores.size());
    for (const CoreCoord& core : noc0_cores) {
        translated.push_back(translate_coord_to(core, coord_system));
    }
    return translated;
}

std::vector<CoreCoord> SocDescriptor::get_cores(
    const CoreType core_type, const CoordSystem coord_system, std::optional<uint32_t> channel) const {
    std::vector<CoreCoord> noc0_cores = coordinate_manager->get_cores(core_type);
    if (channel.has_value()) {
        // Only DRAM and ETH cores belong to channels: a DRAM channel has several cores, an ETH one a
        // single core.
        UMD_ASSERT(
            core_type == CoreType::DRAM || core_type == CoreType::ETH,
            error::RuntimeError,
            fmt::format("Core type {} has no channels.", to_str(core_type)));
        std::vector<CoreCoord> channel_cores;
        for (const CoreCoord& core : noc0_cores) {
            const uint32_t core_channel = core_type == CoreType::DRAM
                                              ? static_cast<uint32_t>(get_dram_channel_for_core(core).first)
                                              : get_eth_channel_for_core(core);
            if (core_channel == channel.value()) {
                channel_cores.push_back(core);
            }
        }
        noc0_cores = std::move(channel_cores);
    }
    return translate_coordinates(noc0_cores, coord_system);
}

std::vector<CoreCoord> SocDescriptor::get_harvested_cores(
    const CoreType core_type, const CoordSystem coord_system) const {
    return translate_coordinates(coordinate_manager->get_harvested_cores(core_type), coord_system);
}

std::vector<CoreCoord> SocDescriptor::get_all_cores(const CoordSystem coord_system) const {
    std::vector<CoreCoord> cores;
    for (const CoreType core_type : ALL_CORE_TYPES) {
        const std::vector<CoreCoord> type_cores = get_cores(core_type, coord_system);
        cores.insert(cores.end(), type_cores.begin(), type_cores.end());
    }
    return cores;
}

std::vector<CoreCoord> SocDescriptor::get_all_harvested_cores(const CoordSystem coord_system) const {
    std::vector<CoreCoord> cores;
    for (const CoreType core_type : ALL_CORE_TYPES) {
        const std::vector<CoreCoord> type_cores = get_harvested_cores(core_type, coord_system);
        cores.insert(cores.end(), type_cores.begin(), type_cores.end());
    }
    return cores;
}

tt_xy_pair SocDescriptor::get_grid_size(const CoreType core_type) const {
    return coordinate_manager->get_grid_size(core_type);
}

tt_xy_pair SocDescriptor::get_harvested_grid_size(const CoreType core_type) const {
    return coordinate_manager->get_harvested_grid_size(core_type);
}

std::pair<CoreCoord, CoreCoord> SocDescriptor::get_bounding_rectangle(
    CoordSystem coord_system, CoreType core_type) const {
    const std::vector<CoreCoord> cores = get_cores(core_type, coord_system);
    UMD_ASSERT(!cores.empty(), error::RuntimeError, fmt::format("There are no {} cores.", to_str(core_type)));

    size_t min_x = cores.front().x;
    size_t min_y = cores.front().y;
    size_t max_x = min_x;
    size_t max_y = min_y;
    for (const CoreCoord& core : cores) {
        min_x = std::min(min_x, core.x);
        min_y = std::min(min_y, core.y);
        max_x = std::max(max_x, core.x);
        max_y = std::max(max_y, core.y);
    }
    return {CoreCoord(min_x, min_y, core_type, coord_system), CoreCoord(max_x, max_y, core_type, coord_system)};
}

std::vector<std::vector<CoreCoord>> SocDescriptor::get_dram_cores() const {
    std::vector<std::vector<CoreCoord>> dram_cores(get_num_dram_channels());
    for (const CoreCoord& core : coordinate_manager->get_cores(CoreType::DRAM)) {
        dram_cores.at(get_dram_channel_for_core(core).first).push_back(core);
    }
    return dram_cores;
}

int SocDescriptor::get_num_dram_channels() const {
    return static_cast<int>(coordinate_manager->get_grid_size(CoreType::DRAM).x);
}

uint32_t SocDescriptor::get_num_eth_channels() const { return coordinate_manager->get_num_eth_channels(); }

uint32_t SocDescriptor::get_num_harvested_eth_channels() const {
    return coordinate_manager->get_num_harvested_eth_channels();
}

CoreCoord SocDescriptor::get_dram_core_for_channel(
    int dram_chan, int subchannel, const CoordSystem coord_system) const {
    return translate_coord_to(CoreCoord(dram_chan, subchannel, CoreType::DRAM, CoordSystem::LOGICAL), coord_system);
}

CoreCoord SocDescriptor::get_eth_core_for_channel(uint32_t eth_chan, const CoordSystem coord_system) const {
    return translate_coord_to(CoreCoord(0, eth_chan, CoreType::ETH, CoordSystem::LOGICAL), coord_system);
}

std::unordered_set<CoreCoord> SocDescriptor::get_eth_cores_for_channels(
    const std::set<uint32_t>& eth_channels, const CoordSystem coord_system) const {
    std::unordered_set<CoreCoord> cores;
    for (const uint32_t channel : eth_channels) {
        cores.insert(get_eth_core_for_channel(channel, coord_system));
    }
    return cores;
}

std::unordered_set<tt_xy_pair> SocDescriptor::get_eth_xy_pairs_for_channels(
    const std::set<uint32_t>& eth_channels, const CoordSystem coord_system) const {
    std::unordered_set<tt_xy_pair> cores;
    for (const uint32_t channel : eth_channels) {
        cores.insert(get_eth_core_for_channel(channel, coord_system).to_pair());
    }
    return cores;
}

uint32_t SocDescriptor::get_eth_channel_for_core(const CoreCoord& core_coord, const CoordSystem coord_system) const {
    // Its channel is the core's logical Y.
    const CoreCoord core(core_coord.to_pair(), CoreType::ETH, coord_system_of(core_coord, coord_system));
    return static_cast<uint32_t>(translate_coord_to(core, CoordSystem::LOGICAL).y);
}

std::pair<int, int> SocDescriptor::get_dram_channel_for_core(
    const CoreCoord& core_coord, const CoordSystem coord_system) const {
    // The core's logical coordinates are (channel, subchannel).
    const CoreCoord core(core_coord.to_pair(), CoreType::DRAM, coord_system_of(core_coord, coord_system));
    const CoreCoord logical = translate_coord_to(core, CoordSystem::LOGICAL);
    return {static_cast<int>(logical.x), static_cast<int>(logical.y)};
}

void SocDescriptor::write_coords(void* out, const CoreCoord& core) const {
    *static_cast<YAML::Emitter*>(out) << fmt::format("{}-{}", core.x, core.y);
}

void SocDescriptor::write_core_locations(void* out, const CoreType& core_type) const {
    YAML::Emitter& emitter = *static_cast<YAML::Emitter*>(out);
    emitter << YAML::BeginSeq;
    for (const tt_xy_pair& core : get_arch_cores(*arch_desc_, core_type)) {
        write_coords(out, CoreCoord(core, core_type, CoordSystem::NOC0));
    }
    emitter << YAML::EndSeq;
}

void SocDescriptor::serialize_dram_cores(void* out, const std::vector<std::vector<CoreCoord>>& cores) const {
    YAML::Emitter& emitter = *static_cast<YAML::Emitter*>(out);
    emitter << YAML::BeginSeq;
    for (const std::vector<CoreCoord>& channel : cores) {
        emitter << YAML::Flow << YAML::BeginSeq;
        for (const CoreCoord& core : channel) {
            write_coords(out, core);
        }
        emitter << YAML::EndSeq;
    }
    emitter << YAML::EndSeq;
}

std::string SocDescriptor::serialize() const {
    // The descriptor is written as the unharvested floorplan, so it reads back into the same
    // SocArchDescriptor; harvesting stays per chip.
    YAML::Emitter out;
    out << YAML::BeginMap;

    out << YAML::Key << "grid" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "x_size" << YAML::Value << grid_size.x;
    out << YAML::Key << "y_size" << YAML::Value << grid_size.y;
    out << YAML::EndMap;

    for (const auto& [core_type, key] : SERIALIZED_CORE_TYPES) {
        out << YAML::Key << key << YAML::Value << YAML::Flow;
        write_core_locations(&out, core_type);
    }

    std::vector<std::vector<CoreCoord>> dram_cores;
    for (const std::vector<tt_xy_pair>& channel : arch_desc_->get_dram_cores()) {
        std::vector<CoreCoord>& channel_cores = dram_cores.emplace_back();
        for (const tt_xy_pair& core : channel) {
            channel_cores.emplace_back(core, CoreType::DRAM, CoordSystem::NOC0);
        }
    }
    out << YAML::Key << "dram" << YAML::Value;
    serialize_dram_cores(&out, dram_cores);

    if (!arch_desc_->get_noc0_x_to_noc1_x().empty() && !arch_desc_->get_noc0_y_to_noc1_y().empty()) {
        out << YAML::Key << "noc0_x_to_noc1_x" << YAML::Value << YAML::Flow << arch_desc_->get_noc0_x_to_noc1_x();
        out << YAML::Key << "noc0_y_to_noc1_y" << YAML::Value << YAML::Flow << arch_desc_->get_noc0_y_to_noc1_y();
    }

    out << YAML::Key << "worker_l1_size" << YAML::Value << worker_l1_size;
    out << YAML::Key << "dram_bank_size" << YAML::Value << dram_bank_size;
    out << YAML::Key << "eth_l1_size" << YAML::Value << eth_l1_size;
    out << YAML::Key << "arch_name" << YAML::Value << arch_to_yaml_name(arch);
    if (arch_desc_->get_ip_variant().has_value()) {
        out << YAML::Key << "ip_variant" << YAML::Value
            << ip_variant_to_yaml_name(arch_desc_->get_ip_variant().value());
    }

    out << YAML::Key << "features" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "unpacker" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << unpacker_version;
    out << YAML::EndMap;
    out << YAML::Key << "math" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "dst_size_alignment" << YAML::Value << dst_size_alignment;
    out << YAML::EndMap;
    out << YAML::Key << "packer" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << packer_version;
    out << YAML::EndMap;
    out << YAML::Key << "overlay" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << overlay_version;
    out << YAML::EndMap;
    out << YAML::EndMap;

    out << YAML::EndMap;
    return out.c_str();
}

std::filesystem::path SocDescriptor::get_default_soc_descriptor_file_path() {
    // Unique per process and per call, so concurrent serializations never write the same file.
    static std::atomic<uint64_t> file_counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           fmt::format("umd_soc_descriptor_{}_{}_{}.yaml", getpid(), timestamp, file_counter++);
}

std::filesystem::path SocDescriptor::serialize_to_file(const std::filesystem::path& dest_file) const {
    const std::filesystem::path file_path = dest_file.empty() ? get_default_soc_descriptor_file_path() : dest_file;
    if (file_path.has_parent_path()) {
        std::filesystem::create_directories(file_path.parent_path());
    }

    std::ofstream file(file_path);
    UMD_ASSERT(
        file.is_open(),
        error::RuntimeError,
        fmt::format("Failed to open {} to write the SoC descriptor.", file_path.string()));
    file << serialize();
    UMD_ASSERT(
        file.good(), error::RuntimeError, fmt::format("Failed to write the SoC descriptor to {}.", file_path.string()));
    return file_path;
}

}  // namespace tt::umd
