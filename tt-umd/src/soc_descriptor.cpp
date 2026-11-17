// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/soc_descriptor.hpp"

#include <fmt/format.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "tt-umd/utils/common.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Every core type a SoC descriptor describes.
constexpr std::array<CoreType, 9> CORE_TYPES = {
    CoreType::TENSIX,
    CoreType::DRAM,
    CoreType::ETH,
    CoreType::ARC,
    CoreType::PCIE,
    CoreType::ROUTER_ONLY,
    CoreType::SECURITY,
    CoreType::L2CPU,
    CoreType::DISPATCH};

std::string ip_variant_to_str(tt::IpVariant ip_variant) {
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

}  // namespace

SocDescriptor::SocDescriptor(std::shared_ptr<const SocArchDescriptor> arch_desc, const ChipInfo chip_info) :
    arch_desc_(std::move(arch_desc)) {
    UMD_ASSERT(arch_desc_ != nullptr, error::RuntimeError, "SocDescriptor needs an architecture descriptor.");
    init_from_arch_descriptor(chip_info);
    create_coordinate_manager(chip_info.board_type, chip_info.asic_location);
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

tt_xy_pair SocDescriptor::translate_chip_coord_to_translated(const CoreCoord core, const NocId noc_id) const {
    return translate_chip_coord_to_translated_coord(core, noc_id).to_pair();
}

CoreCoord SocDescriptor::translate_chip_coord_to_translated_coord(const CoreCoord core, const NocId noc_id) const {
    // LITERAL coordinates are already what the caller wants on the NOC.
    if (core.coord_system == CoordSystem::LITERAL) {
        return core;
    }

    const CoordSystem noc_coord_system = noc_id == NocId::NOC1 ? CoordSystem::NOC1 : CoordSystem::NOC0;
    if (!noc_translation_enabled) {
        return translate_coord_to(core, noc_coord_system);
    }

    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            // These cores sit outside the translated grid, so they are reached by their coordinates on the NOC used.
            if (core.core_type == CoreType::DRAM || core.core_type == CoreType::ARC ||
                core.core_type == CoreType::PCIE || core.core_type == CoreType::ROUTER_ONLY) {
                return translate_coord_to(core, noc_coord_system);
            }
            break;
        case tt::ARCH::BLACKHOLE:
            if (noc_id != NocId::NOC1) {
                break;
            }
            // Outside the Tensix grid, cores the hardware does not translate (routers, harvested ETH and PCIe cores)
            // keep their NOC0 coordinates as translated ones, but NOC1 reads the x of such an address as a NOC1
            // coordinate, while y stays a NOC0 one.
            if (core.core_type != CoreType::TENSIX) {
                const CoreCoord translated_core = translate_coord_to(core, CoordSystem::TRANSLATED);
                const CoreCoord noc0_core = translate_coord_to(core, CoordSystem::NOC0);
                if (translated_core.to_pair() == noc0_core.to_pair()) {
                    const CoreCoord noc1_core = translate_coord_to(core, CoordSystem::NOC1);
                    return CoreCoord(noc1_core.x, noc0_core.y, core.core_type, CoordSystem::LITERAL);
                }
                return translated_core;
            }
            break;
        default:
            break;
    }
    return translate_coord_to(core, CoordSystem::TRANSLATED);
}

std::string SocDescriptor::serialize() const {
    YAML::Emitter out;
    out << YAML::BeginMap;

    out << YAML::Key << "grid" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "x_size" << YAML::Value << grid_size.x;
    out << YAML::Key << "y_size" << YAML::Value << grid_size.y;
    out << YAML::EndMap;

    // Core lists hold every core, harvested ones included, so that the same harvesting masks applied to the
    // serialized descriptor describe the same chip.
    const std::array<std::pair<const char*, CoreType>, 8> core_lists = {{
        {"arc", CoreType::ARC},
        {"pcie", CoreType::PCIE},
        {"eth", CoreType::ETH},
        {"functional_workers", CoreType::TENSIX},
        {"router_only", CoreType::ROUTER_ONLY},
        {"security", CoreType::SECURITY},
        {"l2cpu", CoreType::L2CPU},
        {"dispatch", CoreType::DISPATCH},
    }};
    for (const auto& [key, core_type] : core_lists) {
        out << YAML::Key << key << YAML::Value;
        write_core_locations(&out, core_type);
    }
    out << YAML::Key << "dram" << YAML::Value;
    serialize_dram_cores(&out, get_dram_cores());

    if (!arch_desc_->get_noc0_x_to_noc1_x().empty() && !arch_desc_->get_noc0_y_to_noc1_y().empty()) {
        out << YAML::Key << "noc0_x_to_noc1_x" << YAML::Value << YAML::Flow << arch_desc_->get_noc0_x_to_noc1_x();
        out << YAML::Key << "noc0_y_to_noc1_y" << YAML::Value << YAML::Flow << arch_desc_->get_noc0_y_to_noc1_y();
    }

    out << YAML::Key << "worker_l1_size" << YAML::Value << worker_l1_size;
    out << YAML::Key << "dram_bank_size" << YAML::Value << dram_bank_size;
    out << YAML::Key << "eth_l1_size" << YAML::Value << eth_l1_size;

    std::string arch_name = arch_to_str(arch);
    std::transform(arch_name.begin(), arch_name.end(), arch_name.begin(), ::toupper);
    out << YAML::Key << "arch_name" << YAML::Value << arch_name;

    if (const std::optional<tt::IpVariant> ip_variant = arch_desc_->get_ip_variant()) {
        out << YAML::Key << "ip_variant" << YAML::Value << ip_variant_to_str(*ip_variant);
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

std::filesystem::path SocDescriptor::serialize_to_file(const std::filesystem::path& dest_file) const {
    const std::filesystem::path file_path = dest_file.empty() ? get_default_soc_descriptor_file_path() : dest_file;
    if (file_path.has_parent_path()) {
        std::filesystem::create_directories(file_path.parent_path());
    }
    std::ofstream file(file_path);
    UMD_ASSERT(file.is_open(), error::RuntimeError, "Cannot open " + file_path.string() + " for writing.");
    file << serialize();
    return file_path;
}

bool SocDescriptor::is_core_of_type(const tt_xy_pair& core, CoreType core_type, CoordSystem coord_system) const {
    const std::vector<CoreCoord> cores = get_cores(core_type, coord_system);
    return std::any_of(
        cores.begin(), cores.end(), [&core](const CoreCoord& candidate) { return candidate.to_pair() == core; });
}

std::vector<CoreCoord> SocDescriptor::get_cores(
    const CoreType core_type, const CoordSystem coord_system, std::optional<uint32_t> channel) const {
    if (!channel.has_value()) {
        return translate_coordinates(coordinate_manager->get_cores(core_type), coord_system);
    }
    switch (core_type) {
        case CoreType::DRAM: {
            std::vector<CoreCoord> cores;
            for (size_t subchannel = 0; subchannel < get_grid_size(CoreType::DRAM).y; subchannel++) {
                cores.push_back(get_dram_core_for_channel(*channel, subchannel, coord_system));
            }
            return cores;
        }
        case CoreType::ETH:
            return {get_eth_core_for_channel(*channel, coord_system)};
        default:
            UMD_THROW(error::RuntimeError, "Core type " + to_str(core_type) + " has no channels.");
    }
}

std::vector<CoreCoord> SocDescriptor::get_harvested_cores(
    const CoreType core_type, const CoordSystem coord_system) const {
    return translate_coordinates(coordinate_manager->get_harvested_cores(core_type), coord_system);
}

std::vector<CoreCoord> SocDescriptor::get_all_cores(const CoordSystem coord_system) const {
    std::vector<CoreCoord> cores;
    for (const CoreType core_type : CORE_TYPES) {
        const std::vector<CoreCoord> cores_of_type = get_cores(core_type, coord_system);
        cores.insert(cores.end(), cores_of_type.begin(), cores_of_type.end());
    }
    return cores;
}

std::vector<CoreCoord> SocDescriptor::get_all_harvested_cores(const CoordSystem coord_system) const {
    std::vector<CoreCoord> cores;
    for (const CoreType core_type : CORE_TYPES) {
        const std::vector<CoreCoord> cores_of_type = get_harvested_cores(core_type, coord_system);
        cores.insert(cores.end(), cores_of_type.begin(), cores_of_type.end());
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
    UMD_ASSERT(!cores.empty(), error::RuntimeError, "There are no " + to_str(core_type) + " cores to bound.");

    size_t min_x = cores.front().x;
    size_t min_y = cores.front().y;
    size_t max_x = cores.front().x;
    size_t max_y = cores.front().y;
    for (const CoreCoord& core : cores) {
        min_x = std::min(min_x, core.x);
        min_y = std::min(min_y, core.y);
        max_x = std::max(max_x, core.x);
        max_y = std::max(max_y, core.y);
    }
    return {CoreCoord(min_x, min_y, core_type, coord_system), CoreCoord(max_x, max_y, core_type, coord_system)};
}

std::vector<std::vector<CoreCoord>> SocDescriptor::get_dram_cores() const {
    // Every bank, harvested ones included, indexed by its physical channel.
    std::vector<std::vector<CoreCoord>> dram_cores;
    for (const std::vector<tt_xy_pair>& channel : arch_desc_->get_dram_cores()) {
        std::vector<CoreCoord>& channel_cores = dram_cores.emplace_back();
        for (const tt_xy_pair& core : channel) {
            channel_cores.emplace_back(core, CoreType::DRAM, CoordSystem::NOC0);
        }
    }
    return dram_cores;
}

int SocDescriptor::get_num_dram_channels() const { return get_grid_size(CoreType::DRAM).x; }

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
    std::unordered_set<CoreCoord> eth_cores;
    for (const uint32_t eth_channel : eth_channels) {
        eth_cores.insert(get_eth_core_for_channel(eth_channel, coord_system));
    }
    return eth_cores;
}

std::unordered_set<tt_xy_pair> SocDescriptor::get_eth_xy_pairs_for_channels(
    const std::set<uint32_t>& eth_channels, const CoordSystem coord_system) const {
    std::unordered_set<tt_xy_pair> eth_cores;
    for (const uint32_t eth_channel : eth_channels) {
        eth_cores.insert(get_eth_core_for_channel(eth_channel, coord_system).to_pair());
    }
    return eth_cores;
}

uint32_t SocDescriptor::get_eth_channel_for_core(const CoreCoord& core_coord, const CoordSystem coord_system) const {
    // A LITERAL core is read in coord_system; any other core carries its own coordinate system.
    const CoreCoord eth_core = core_coord.coord_system == CoordSystem::LITERAL
                                   ? CoreCoord(core_coord.to_pair(), CoreType::ETH, coord_system)
                                   : core_coord;
    return translate_coord_to(eth_core, CoordSystem::LOGICAL).y;
}

std::pair<int, int> SocDescriptor::get_dram_channel_for_core(
    const CoreCoord& core_coord, const CoordSystem coord_system) const {
    // A LITERAL core is read in coord_system; any other core carries its own coordinate system.
    const CoreCoord dram_core = core_coord.coord_system == CoordSystem::LITERAL
                                    ? CoreCoord(core_coord.to_pair(), CoreType::DRAM, coord_system)
                                    : core_coord;
    const CoreCoord logical_core = translate_coord_to(dram_core, CoordSystem::LOGICAL);
    return {static_cast<int>(logical_core.x), static_cast<int>(logical_core.y)};
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
    worker_l1_size = arch_desc_->get_worker_l1_size();
    eth_l1_size = arch_desc_->get_eth_l1_size();
    dram_bank_size = arch_desc_->get_dram_bank_size();
    noc_translation_enabled = chip_info.noc_translation_enabled;
    harvesting_masks = chip_info.harvesting_masks;
}

void SocDescriptor::create_coordinate_manager(const BoardType board_type, const uint8_t asic_location) {
    // The board needs no special handling: which PCIe core it uses is carried by the PCIe harvesting mask.
    const std::vector<std::vector<tt_xy_pair>>& dram_cores = arch_desc_->get_dram_cores();
    coordinate_manager = CoordinateManager::create_coordinate_manager(
        arch,
        noc_translation_enabled,
        harvesting_masks,
        arch_desc_->get_worker_grid_size(),
        arch_desc_->get_tensix_cores(),
        {dram_cores.size(), dram_cores.empty() ? 0 : dram_cores.front().size()},
        flatten_vector(dram_cores),
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

std::filesystem::path SocDescriptor::get_default_soc_descriptor_file_path() {
    // mkstemps creates the file under a name no other process or thread can also get.
    std::string file_template = (std::filesystem::temp_directory_path() / "umd_soc_descriptor_XXXXXX.yaml").string();
    const int fd = mkstemps(file_template.data(), static_cast<int>(std::string(".yaml").size()));
    UMD_ASSERT(fd >= 0, error::RuntimeError, "Cannot create a file for the SoC descriptor in the temp directory.");
    close(fd);
    return file_template;
}

void SocDescriptor::write_coords(void* out, const CoreCoord& core) const {
    *static_cast<YAML::Emitter*>(out) << fmt::format("{}-{}", core.x, core.y);
}

void SocDescriptor::write_core_locations(void* out, const CoreType& core_type) const {
    std::vector<tt_xy_pair> cores;
    switch (core_type) {
        case CoreType::ARC:
            cores = arch_desc_->get_firmware_cores();
            break;
        case CoreType::PCIE:
            cores = arch_desc_->get_pcie_cores();
            break;
        case CoreType::ETH:
            cores = arch_desc_->get_eth_cores();
            break;
        case CoreType::TENSIX:
            cores = arch_desc_->get_tensix_cores();
            break;
        case CoreType::ROUTER_ONLY:
            cores = arch_desc_->get_router_cores();
            break;
        case CoreType::SECURITY:
            cores = arch_desc_->get_security_cores();
            break;
        case CoreType::L2CPU:
            cores = arch_desc_->get_l2cpu_cores();
            break;
        case CoreType::DISPATCH:
            cores = arch_desc_->get_dispatch_cores();
            break;
        default:
            UMD_THROW(error::RuntimeError, "Core type " + to_str(core_type) + " is not serialized as a list.");
    }

    YAML::Emitter& emitter = *static_cast<YAML::Emitter*>(out);
    emitter << YAML::Flow << YAML::BeginSeq;
    for (const tt_xy_pair& core : cores) {
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

std::vector<CoreCoord> SocDescriptor::translate_coordinates(
    const std::vector<CoreCoord>& noc0_cores, const CoordSystem coord_system) const {
    if (coord_system == CoordSystem::NOC0) {
        return noc0_cores;
    }
    std::vector<CoreCoord> translated_cores;
    translated_cores.reserve(noc0_cores.size());
    for (const CoreCoord& core : noc0_cores) {
        translated_cores.push_back(translate_coord_to(core, coord_system));
    }
    return translated_cores;
}

}  // namespace tt::umd
