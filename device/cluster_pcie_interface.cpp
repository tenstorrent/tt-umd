/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/cluster_pcie_interface.hpp"

namespace tt::umd {

IClusterPcie::IClusterPcie(
    std::set<chip_id_t>& local_chip_ids, std::unordered_map<chip_id_t, std::unique_ptr<Chip>>& chips) :
    local_chip_ids_(local_chip_ids), chips_(chips) {}

void IClusterPcie::initialize_pcie_chips() {
    for (auto& local_chip_id : local_chip_ids_) {
        pcie_chips_.insert(dynamic_cast<PCIeConnection>(chips_.at(local_chip_id)));
    }
}

std::function<void(uint32_t, uint32_t, const uint8_t*)> IClusterPcie::get_fast_pcie_static_tlb_write_callable(
    int device_id) {
    return chips_.at(device_id)->get_fast_pcie_static_tlb_write_callable();
}

Writer IClusterPcie::get_static_tlb_writer(const chip_id_t chip, const CoreCoord core) {
    tt_xy_pair translated_core = get_chip(chip)->translate_chip_coord_to_translated(core);
    return get_tlb_manager(chip)->get_static_tlb_writer(translated_core);
}

tlb_configuration IClusterPcie::get_tlb_configuration(const chip_id_t chip, CoreCoord core) {
    tt_xy_pair translated_core = get_chip(chip)->translate_chip_coord_to_translated(core);
    return get_tlb_manager(chip)->get_tlb_configuration(translated_core);
}

// TODO: These configure_tlb APIs are soon going away.
void IClusterPcie::configure_tlb(
    chip_id_t logical_device_id, tt_xy_pair core, int32_t tlb_index, uint64_t address, uint64_t ordering) {
    configure_tlb(
        logical_device_id,
        get_soc_descriptor(logical_device_id).get_coord_at(core, CoordSystem::VIRTUAL),
        tlb_index,
        address,
        ordering);
}

void IClusterPcie::configure_tlb(
    chip_id_t logical_device_id, CoreCoord core, int32_t tlb_index, uint64_t address, uint64_t ordering) {
    tt_xy_pair translated_core = get_chip(logical_device_id)->translate_chip_coord_to_translated(core);
    get_tlb_manager(logical_device_id)->configure_tlb(translated_core, tlb_index, address, ordering);
}

void* IClusterPcie::host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
    hugepage_mapping hugepage_map = get_chip(src_device_id)->get_sysmem_manager()->get_hugepage_mapping(channel);
    if (hugepage_map.mapping != nullptr) {
        return static_cast<std::byte*>(hugepage_map.mapping) + offset;
    } else {
        return nullptr;
    }
}

void IClusterPcie::write_to_sysmem(
    const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
    get_chip(src_device_id)->write_to_sysmem(channel, mem_ptr, addr, size);
}

void IClusterPcie::read_from_sysmem(
    void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
    get_chip(src_device_id)->read_from_sysmem(channel, mem_ptr, addr, size);
}

void IClusterPcie::dma_write_to_device(const void* src, size_t size, chip_id_t chip, CoreCoord core, uint64_t addr) {
    get_chip(chip)->dma_write_to_device(src, size, core, addr);
}

void IClusterPcie::dma_read_from_device(void* dst, size_t size, chip_id_t chip, CoreCoord core, uint64_t addr) {
    get_chip(chip)->dma_read_from_device(dst, size, core, addr);
}

std::uint32_t IClusterPcie::get_num_host_channels(std::uint32_t device_id) {
    return chips_.at(device_id)->get_num_host_channels();
}

std::uint32_t IClusterPcie::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    return chips_.at(device_id)->get_host_channel_size(channel);
}

std::uint32_t IClusterPcie::get_numa_node_for_pcie_device(std::uint32_t device_id) {
    return chips_.at(device_id)->get_numa_node();
}

std::uint64_t IClusterPcie::get_pcie_base_addr_from_device(const chip_id_t chip_id) const {
    // TODO: Should probably be lowered to TTDevice.
    tt::ARCH arch = get_soc_descriptor(chip_id).arch;
    if (arch == tt::ARCH::WORMHOLE_B0) {
        return 0x800000000;
    } else if (arch == tt::ARCH::BLACKHOLE) {
        // Enable 4th ATU window.
        return 1ULL << 60;
    } else {
        return 0;
    }
}

TLBManager* IClusterPcie::get_tlb_manager(chip_id_t device_id) const { return get_chip(device_id)->get_tlb_manager(); }

bool IClusterPcie::verify_sysmem_initialized(chip_id_t chip_id) {
    // return (get_chip(chip_id)->get_sysmem_manager()->get_hugepage_mapping(0).mapping != nullptr);
    return false;
}

}  // namespace tt::umd
