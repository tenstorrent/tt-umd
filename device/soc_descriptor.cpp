// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/soc_descriptor.hpp"

#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// clang-format off
static const std::unordered_map<tt_xy_pair, tt_xy_pair> ROUTER_NOC1_TO_TRANSLATED_BLACKHOLE = {
    {{15, 11}, {15,  0}},
    {{13, 11}, {13,  0}},
    {{12, 11}, {12,  0}},
    {{11, 11}, {11,  0}},
    {{10, 11}, {10,  0}},
    {{ 9, 11}, { 9,  0}},
    {{ 6, 11}, { 6,  0}},
    {{ 4, 11}, { 4,  0}},
    {{ 3, 11}, { 3,  0}},
    {{ 2, 11}, { 2,  0}},
    {{ 1, 11}, { 1,  0}},
    {{ 0, 11}, { 0,  0}},
    {{ 8, 10}, { 8,  1}},
    {{ 8,  1}, { 8, 10}},
    {{ 8,  3}, { 8,  8}},
    {{ 8,  5}, { 8,  6}},
    {{ 8,  7}, { 8,  4}},
    {{ 8,  0}, { 8, 11}},
};

// clang-format on

void SocDescriptor::write_coords(void *out, const CoreCoord &core) const {
    YAML::Emitter *emitter = static_cast<YAML::Emitter *>(out);

    if (core.x < grid_size.x && core.y < grid_size.y) {
        auto coords = translate_coord_to(core, CoordSystem::NOC0);
        *emitter << std::to_string(coords.x) + "-" + std::to_string(coords.y);
    }
}

void SocDescriptor::write_core_locations(void *out, const CoreType &core_type) const {
    for (const auto &core : get_cores(core_type)) {
        write_coords(out, core);
    }
}

void SocDescriptor::serialize_dram_cores(void *out, const std::vector<std::vector<CoreCoord>> &cores) const {
    YAML::Emitter *emitter = static_cast<YAML::Emitter *>(out);

    const uint32_t num_noc_ports = cores.empty() ? 0 : cores[0].size();

    for (const auto &dram_cores : cores) {
        // Insert the dram core if it's within the given grid.
        bool serialize_cores = true;

        for (const auto &dram_core : dram_cores) {
            if ((dram_core.x > grid_size.x) || (dram_core.y > grid_size.y)) {
                serialize_cores = false;
            }
        }
        if (serialize_cores) {
            int dram_count = 0;
            for (const auto &dram_core : dram_cores) {
                if (dram_count % num_noc_ports == 0) {
                    *emitter << YAML::BeginSeq;
                }
                if (dram_core.x < grid_size.x && dram_core.y < grid_size.y) {
                    write_coords(emitter, dram_core);
                }
                if (dram_count % num_noc_ports == num_noc_ports - 1) {
                    *emitter << YAML::EndSeq;
                }
                dram_count++;
            }
        }
    }
}

void SocDescriptor::init_from_arch_descriptor(const ChipInfo &chip_info) {
    // Copy static fields from arch descriptor for backward compatibility.
    arch = arch_desc_->get_arch();
    grid_size = arch_desc_->get_grid_size();
    worker_l1_size = arch_desc_->get_worker_l1_size();
    eth_l1_size = arch_desc_->get_eth_l1_size();
    dram_bank_size = arch_desc_->get_dram_bank_size();
    trisc_sizes = arch_desc_->get_trisc_sizes();
    device_descriptor_file_path = arch_desc_->get_device_descriptor_file_path();
    overlay_version = arch_desc_->get_overlay_version();
    unpacker_version = arch_desc_->get_unpacker_version();
    dst_size_alignment = arch_desc_->get_dst_size_alignment();
    packer_version = arch_desc_->get_packer_version();

    // Set runtime fields.
    noc_translation_enabled = chip_info.noc_translation_enabled;
    harvesting_masks = chip_info.harvesting_masks;

    // Create coordinate manager from static + runtime data.
    create_coordinate_manager(chip_info.board_type, chip_info.asic_location);
}

void SocDescriptor::create_coordinate_manager(const BoardType board_type, const uint8_t asic_location) {
    const auto &dram_cores = arch_desc_->get_dram_cores();
    const tt_xy_pair dram_grid_size = tt_xy_pair(dram_cores.size(), dram_cores.empty() ? 0 : dram_cores[0].size());
    tt_xy_pair arc_grid_size = SocArchDescriptor::calculate_grid_size(arch_desc_->get_arc_cores());
    tt_xy_pair pcie_grid_size = SocArchDescriptor::calculate_grid_size(arch_desc_->get_pcie_cores());

    std::vector<tt_xy_pair> dram_cores_unpacked;
    for (const auto &vec : dram_cores) {
        for (const auto &core : vec) {
            dram_cores_unpacked.push_back(core);
        }
    }

    // TODO: P100 has two types of boards, each using different PCI core.
    // Either have two separate enums or completely remove the check here.
    // PCIE harvesting mask 0x1 corresponds to (2, 0) and 0x2 corresponds to (11, 0).
    // if (board_type == BoardType::P100 && harvesting_masks.pcie_harvesting_mask != 0x1) {
    //     UMD_THROW(error::RuntimeError, "P100 card should always have PCIe core (2, 0) harvested.");
    // }

    if (board_type == BoardType::P150 && harvesting_masks.pcie_harvesting_mask != 0x2) {
        UMD_THROW(error::RuntimeError, "P150 card should always have PCIe core (11, 0) harvested.");
    }

    if (board_type == BoardType::P300 && asic_location == 0 && harvesting_masks.pcie_harvesting_mask != 0x2) {
        UMD_THROW(error::RuntimeError, "P300 card left chip should always have PCIe core (11, 0) harvested.");
    }

    if (board_type == BoardType::P300 && asic_location == 1 && harvesting_masks.pcie_harvesting_mask != 0x1) {
        UMD_THROW(error::RuntimeError, "P300 card right chip should always have PCIe core (2, 0) harvested.");
    }

    pcie_grid_size = SocArchDescriptor::calculate_grid_size(arch_desc_->get_pcie_cores());

    coordinate_manager = CoordinateManager::create_coordinate_manager(
        arch,
        noc_translation_enabled,
        harvesting_masks,
        arch_desc_->get_worker_grid_size(),
        arch_desc_->get_tensix_cores(),
        dram_grid_size,
        dram_cores_unpacked,
        arch_desc_->get_eth_cores(),
        arc_grid_size,
        arch_desc_->get_arc_cores(),
        pcie_grid_size,
        arch_desc_->get_pcie_cores(),
        arch_desc_->get_router_cores(),
        arch_desc_->get_security_cores(),
        arch_desc_->get_l2cpu_cores(),
        arch_desc_->get_dispatch_cores(),
        arch_desc_->get_noc0_x_to_noc1_x(),
        arch_desc_->get_noc0_y_to_noc1_y());
}

SocDescriptor::SocDescriptor(std::shared_ptr<const SocArchDescriptor> arch_desc, const ChipInfo chip_info) :
    arch_desc_(std::move(arch_desc)) {
    if (!arch_desc_) {
        throw std::invalid_argument("SocArchDescriptor pointer must not be null.");
    }
    init_from_arch_descriptor(chip_info);
}

tt::ARCH SocDescriptor::get_arch_from_soc_descriptor_path(const std::string &soc_descriptor_path) {
    return SocArchDescriptor::get_arch_from_path(soc_descriptor_path);
}

tt_xy_pair SocDescriptor::get_grid_size_from_soc_descriptor_path(const std::string &soc_descriptor_path) {
    return SocArchDescriptor::get_grid_size_from_path(soc_descriptor_path);
}

const SocArchDescriptor &SocDescriptor::get_arch_descriptor() const { return *arch_desc_; }

CoreCoord SocDescriptor::translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const {
    return coordinate_manager->translate_coord_to(core_coord, coord_system);
}

CoreCoord SocDescriptor::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    return coordinate_manager->get_coord_at(core, coord_system);
}

CoreCoord SocDescriptor::translate_coord_to(
    const tt_xy_pair core_location, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    return coordinate_manager->translate_coord_to(core_location, input_coord_system, target_coord_system);
}

// Translates a chip coordinate to the correct device coordinates, returning a CoreCoord.
// Returns the correct pre-translation coordinates for the given architecture. Note that
// the returned CoordSystem is not necessarily TRANSLATED — architecture-specific fixups
// (e.g., Wormhole DRAM/ARC/PCIe cores) may produce NOC0/NOC1 coordinates instead.
// The key guarantee is that the returned coordinates will be correct for device access
// on the given architecture.
CoreCoord SocDescriptor::translate_chip_coord_to_translated_coord(const CoreCoord core) const {
    if (core.coord_system == CoordSystem::LITERAL) {
        return xy_pair(core.x, core.y);
    }
    if (!noc_translation_enabled) {
        return translate_coord_to(core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0);
    }

    // For ROUTER_ONLY cores, the translated coordinate space differs depending on
    // whether the NOC0 or NOC1 network is used. Use the NOC1 -> TRANSLATED mapping
    // from ROUTER_NOC1_TO_TRANSLATED_BLACKHOLE so that accesses over the NOC1
    // network resolve to the correct tile.
    if ((arch == tt::ARCH::BLACKHOLE) && (core.core_type == CoreType::ROUTER_ONLY) && is_selected_noc1()) {
        CoreCoord noc1_core = translate_coord_to(core, CoordSystem::NOC1);
        CoreCoord translated_noc1_core = CoreCoord(
            ROUTER_NOC1_TO_TRANSLATED_BLACKHOLE.at(static_cast<tt_xy_pair>(noc1_core)),
            CoreType::ROUTER_ONLY,
            CoordSystem::TRANSLATED);
        return translated_noc1_core;
    }

    // Wormhole-specific workaround: For DRAM, ARC, and PCIe cores, the translated coordinate system
    // is not used (for now), and UMD is using NOC0/NOC1 (depending on the selected NOC).
    // Task to address this: https://github.com/tenstorrent/tt-umd/issues/2176.
    if ((arch == tt::ARCH::WORMHOLE_B0) &&
        (core.core_type == CoreType::DRAM || core.core_type == CoreType::ARC || core.core_type == CoreType::PCIE)) {
        return translate_coord_to(core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0);
    }

    return translate_coord_to(core, CoordSystem::TRANSLATED);
}

// Convenience wrapper returning tt_xy_pair; the actual logic lives in
// translate_chip_coord_to_translated_coord.
tt_xy_pair SocDescriptor::translate_chip_coord_to_translated(const CoreCoord core) const {
    return translate_chip_coord_to_translated_coord(core);
}

int SocDescriptor::get_num_dram_channels() const { return get_grid_size(CoreType::DRAM).x; }

CoreCoord SocDescriptor::get_dram_core_for_channel(
    int dram_chan, int subchannel, const CoordSystem coord_system) const {
    const CoreCoord logical_dram_coord = CoreCoord(dram_chan, subchannel, CoreType::DRAM, CoordSystem::LOGICAL);
    return translate_coord_to(logical_dram_coord, coord_system);
}

std::unordered_set<CoreCoord> SocDescriptor::translate_coords_to(
    const std::unordered_set<CoreCoord> &core_coords, const CoordSystem coord_system) const {
    std::unordered_set<CoreCoord> translated_cores;
    for (const auto &core : core_coords) {
        translated_cores.insert(translate_coord_to(core, coord_system));
    }
    return translated_cores;
}

std::unordered_set<tt_xy_pair> SocDescriptor::translate_coords_to_xy_pair(
    const std::unordered_set<CoreCoord> &core_coords, const CoordSystem coord_system) const {
    std::unordered_set<tt_xy_pair> translated_xy_pairs;
    for (const auto &core : core_coords) {
        CoreCoord translated_core = translate_coord_to(core, coord_system);
        translated_xy_pairs.insert({translated_core.x, translated_core.y});
    }
    return translated_xy_pairs;
}

std::unordered_set<CoreCoord> SocDescriptor::get_eth_cores_for_channels(
    const std::set<uint32_t> &eth_channels, const CoordSystem coord_system) const {
    std::unordered_set<CoreCoord> eth_cores;
    for (uint32_t channel : eth_channels) {
        eth_cores.insert(get_eth_core_for_channel(channel, coord_system));
    }
    return eth_cores;
}

std::unordered_set<tt_xy_pair> SocDescriptor::get_eth_xy_pairs_for_channels(
    const std::set<uint32_t> &eth_channels, const CoordSystem coord_system) const {
    std::unordered_set<tt_xy_pair> eth_xy_pairs;
    for (uint32_t channel : eth_channels) {
        CoreCoord eth_core = get_eth_core_for_channel(channel, coord_system);
        eth_xy_pairs.insert({eth_core.x, eth_core.y});
    }
    return eth_xy_pairs;
}

uint32_t SocDescriptor::get_eth_channel_for_core(const CoreCoord &core_coord, const CoordSystem coord_system) const {
    return translate_coord_to(core_coord, CoordSystem::LOGICAL).y;
}

std::pair<int, int> SocDescriptor::get_dram_channel_for_core(
    const CoreCoord &core_coord, const CoordSystem coord_system) const {
    auto logical_core = translate_coord_to(core_coord, CoordSystem::LOGICAL);
    return std::make_pair(logical_core.x, logical_core.y);
}

CoreCoord SocDescriptor::get_eth_core_for_channel(uint32_t eth_chan, const CoordSystem coord_system) const {
    const CoreCoord logical_eth_coord = CoreCoord(0, eth_chan, CoreType::ETH, CoordSystem::LOGICAL);
    return translate_coord_to(logical_eth_coord, coord_system);
}

std::string SocDescriptor::serialize() const {
    YAML::Emitter out;

    out << YAML::BeginMap;

    out << YAML::Key << "grid" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "x_size" << YAML::Value << grid_size.x;
    out << YAML::Key << "y_size" << YAML::Value << grid_size.y;
    out << YAML::EndMap;

    out << YAML::Key << "arc" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::ARC);
    out << YAML::EndSeq;

    out << YAML::Key << "pcie" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::PCIE);
    out << YAML::EndSeq;

    out << YAML::Key << "dram" << YAML::Value << YAML::BeginSeq;
    serialize_dram_cores(&out, get_dram_cores());
    out << YAML::EndSeq;

    out << YAML::Key << "eth" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::ETH);
    out << YAML::EndSeq;

    out << YAML::Key << "functional_workers" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::TENSIX);
    out << YAML::EndSeq;

    out << YAML::Key << "router_only" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::ROUTER_ONLY);
    out << YAML::EndSeq;

    out << YAML::Key << "security" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::SECURITY);
    out << YAML::EndSeq;

    out << YAML::Key << "l2cpu" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::L2CPU);
    out << YAML::EndSeq;

    out << YAML::Key << "dispatch" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::DISPATCH);
    out << YAML::EndSeq;

    // Fill in the rest that are static to our device.
    out << YAML::Key << "worker_l1_size" << YAML::Value << worker_l1_size;
    out << YAML::Key << "dram_bank_size" << YAML::Value << dram_bank_size;
    out << YAML::Key << "eth_l1_size" << YAML::Value << eth_l1_size;
    out << YAML::Key << "arch_name" << YAML::Value << tt::arch_to_str(arch);

    out << YAML::Key << "features" << YAML::Value << YAML::BeginMap;

    out << YAML::Key << "noc" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "translation_id_enabled" << YAML::Value << true;
    out << YAML::EndMap;

    out << YAML::Key << "unpacker" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << unpacker_version;
    out << YAML::Key << "inline_srca_trans_without_srca_trans_instr" << YAML::Value << true;
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

    return out.c_str();
}

std::filesystem::path SocDescriptor::serialize_to_file(const std::filesystem::path &dest_file) const {
    std::filesystem::path file_path = dest_file;
    if (file_path.empty()) {
        file_path = get_default_soc_descriptor_file_path();
    }
    std::ofstream file(file_path);
    file << serialize();
    file.close();
    return file_path;
}

std::filesystem::path SocDescriptor::get_default_soc_descriptor_file_path() {
    std::filesystem::path temp_path = std::filesystem::temp_directory_path();
    std::string soc_path_dir_template = temp_path / "umd_XXXXXX";
    std::filesystem::path soc_path_dir = mkdtemp(soc_path_dir_template.data());
    std::filesystem::path soc_path = soc_path_dir / "soc_descriptor.yaml";

    return soc_path;
}

std::vector<CoreCoord> SocDescriptor::translate_coordinates(
    const std::vector<CoreCoord> &noc0_cores, const CoordSystem coord_system) const {
    std::vector<CoreCoord> translated_cores;
    translated_cores.reserve(noc0_cores.size());
    for (const auto &core : noc0_cores) {
        translated_cores.push_back(translate_coord_to(core, coord_system));
    }
    return translated_cores;
}

bool SocDescriptor::is_core_of_type(const tt_xy_pair &core, CoreType core_type, CoordSystem coord_system) const {
    const auto &cores = get_cores(core_type, coord_system);
    return std::any_of(cores.begin(), cores.end(), [&core](const auto &c) { return c.x == core.x && c.y == core.y; });
}

std::vector<CoreCoord> SocDescriptor::get_cores(
    const CoreType core_type, const CoordSystem coord_system, std::optional<uint32_t> channel) const {
    std::vector<CoreCoord> cores = coordinate_manager->get_cores(core_type);

    // Filter cores by channel if specified.
    // At this time, only applicable for DRAM cores.
    if (channel.has_value()) {
        UMD_ASSERT(core_type == CoreType::DRAM, error::RuntimeError, "Core type must be DRAM when setting channel.");
        UMD_ASSERT(
            channel.value() < get_num_dram_channels(),
            error::RuntimeError,
            "Channel value exceeds number of DRAM channels.");
        std::vector<CoreCoord> filtered_cores;
        for (const auto &core : cores) {
            auto logical_core = translate_coord_to(core, CoordSystem::LOGICAL);
            if (logical_core.y == channel.value()) {
                filtered_cores.push_back(core);
            }
        }
        cores = filtered_cores;
    }

    if (coord_system != CoordSystem::NOC0) {
        return translate_coordinates(cores, coord_system);
    }
    return cores;
}

std::vector<CoreCoord> SocDescriptor::get_harvested_cores(
    const CoreType core_type, const CoordSystem coord_system) const {
    if (coord_system == CoordSystem::LOGICAL) {
        UMD_THROW(error::RuntimeError, "Harvested cores are not supported for logical coordinates.");
    }
    std::vector<CoreCoord> harvested_cores = coordinate_manager->get_harvested_cores(core_type);
    if (coord_system != CoordSystem::NOC0) {
        return translate_coordinates(harvested_cores, coord_system);
    }
    return harvested_cores;
}

std::vector<CoreCoord> SocDescriptor::get_all_cores(const CoordSystem coord_system) const {
    std::vector<CoreCoord> all_cores;
    for (const auto &core_type :
         {CoreType::TENSIX,
          CoreType::DRAM,
          CoreType::ETH,
          CoreType::ARC,
          CoreType::PCIE,
          CoreType::ROUTER_ONLY,
          CoreType::SECURITY,
          CoreType::L2CPU,
          CoreType::DISPATCH}) {
        auto cores = get_cores(core_type, coord_system);
        all_cores.insert(all_cores.end(), cores.begin(), cores.end());
    }
    return all_cores;
}

std::vector<CoreCoord> SocDescriptor::get_all_harvested_cores(const CoordSystem coord_system) const {
    std::vector<CoreCoord> all_harvested_cores;
    for (const auto &core_type :
         {CoreType::TENSIX,
          CoreType::DRAM,
          CoreType::ETH,
          CoreType::ARC,
          CoreType::PCIE,
          CoreType::ROUTER_ONLY,
          CoreType::SECURITY,
          CoreType::L2CPU,
          CoreType::DISPATCH}) {
        auto harvested_cores = get_harvested_cores(core_type, coord_system);
        all_harvested_cores.insert(all_harvested_cores.end(), harvested_cores.begin(), harvested_cores.end());
    }
    return all_harvested_cores;
}

tt_xy_pair SocDescriptor::get_grid_size(const CoreType core_type) const {
    return coordinate_manager->get_grid_size(core_type);
}

tt_xy_pair SocDescriptor::get_harvested_grid_size(const CoreType core_type) const {
    return coordinate_manager->get_harvested_grid_size(core_type);
}

std::vector<std::vector<CoreCoord>> SocDescriptor::get_dram_cores() const {
    const std::vector<CoreCoord> dram_cores = coordinate_manager->get_cores(CoreType::DRAM);
    const tt_xy_pair dram_grid_size = coordinate_manager->get_grid_size(CoreType::DRAM);

    std::vector<std::vector<CoreCoord>> dram_cores_per_bank(dram_grid_size.x);
    for (size_t bank = 0; bank < dram_grid_size.x; bank++) {
        for (size_t noc_port = 0; noc_port < dram_grid_size.y; noc_port++) {
            dram_cores_per_bank[bank].push_back(dram_cores[bank * dram_grid_size.y + noc_port]);
        }
    }
    return dram_cores_per_bank;
}

uint32_t SocDescriptor::get_num_eth_channels() const { return coordinate_manager->get_num_eth_channels(); }

uint32_t SocDescriptor::get_num_harvested_eth_channels() const {
    return coordinate_manager->get_num_harvested_eth_channels();
}

}  // namespace tt::umd
