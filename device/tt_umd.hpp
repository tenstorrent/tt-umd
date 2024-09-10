/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <set>

#include "tt_soc_descriptor.h"
#include "tt_xy_pair.h"
#include "tt_silicon_driver_common.hpp"
#include "device/tt_cluster_descriptor_types.h"
#include "device/tlb.h"
#include "device/tt_io.hpp"

#include "pci_device.hpp"

void write_to_device(const void *mem_ptr, uint32_t size, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb);
void write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb);
void read_from_device(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
void read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
void write_to_sysmem(const void* mem_ptr, std::uint32_t size,  uint64_t addr, uint16_t channel, chip_id_t src_device_id);
void write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
void read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
void read_from_sysmem(std::vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);

void wait_for_non_mmio_flush();
void l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});
void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels);
void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});

void assert_risc_reset();
void assert_risc_reset_at_core(tt_cxy_pair core);
void deassert_risc_reset();
void deassert_risc_reset_at_core(tt_cxy_pair core);

std::map<int, int> get_clocks();
std::set<chip_id_t> get_target_remote_device_ids();
std::uint32_t get_num_host_channels(std::uint32_t device_id);
std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);
void *host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel); // should prob be a get?
std::uint64_t get_pcie_base_addr_from_device();
std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id);
std::vector<chip_id_t> detect_available_device_ids(); // should move all calls of this completely into umd

// Fast-dispatch workaround :(
std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable(int device_id);
tt::Writer get_static_tlb_writer(tt_cxy_pair target);

