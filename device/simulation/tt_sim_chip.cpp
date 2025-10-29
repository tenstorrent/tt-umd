// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/tt_sim_chip.hpp"

#include <filesystem>
#include <iostream>
#include <mutex>
#include <unordered_map>

#include "tt_sim_chip_impl.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<TTSimChip>(), "TTSimChip must be non-abstract.");

TTSimChip::TTSimChip(
    const std::filesystem::path& simulator_directory,
// <<<<<<< HEAD
//     const SocDescriptor& soc_descriptor,
//     ChipId chip_id,
//     bool copy_sim_binary,
//     int num_host_mem_channels) :
//     SimulationChip(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels) {
//     tt_device_ = std::make_unique<TTSimTTDevice>(
//         simulator_directory, soc_descriptor, chip_id, copy_sim_binary, num_host_mem_channels);
// }

// TTSimChip::~TTSimChip() = default;

// void TTSimChip::start_device() { tt_device_->start_device(); }

// void TTSimChip::close_device() { tt_device_->close_device(); }

// void TTSimChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
//     std::lock_guard<std::mutex> lock(device_lock);
//     tt_device_->write_to_device(src, soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), l1_dest, size);
// =======
    SocDescriptor soc_descriptor,
    ClusterDescriptor* cluster_desc,
    ChipId chip_id) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id), chips_to_clock_({{chip_id, this}}) {
    impl_ = std::make_unique<TTSimChipImpl>(simulator_directory, cluster_desc, chip_id, true);
}

bool TTSimChip::connect_eth_links() {
    std::lock_guard<std::mutex> lock(device_lock);
    return impl_->connect_eth_links();
}

TTSimChip::~TTSimChip() { impl_.reset(); }

void TTSimChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    impl_->start_device();
}

void TTSimChip::close_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    impl_->close_device();
}

void TTSimChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    impl_->write_to_device(translated_core, src, l1_dest, size);
// >>>>>>> 9e429c7c (#0: Add Active Ethernet connectivity support to ttsim chip)
}

void TTSimChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    {
        std::lock_guard<std::mutex> lock(device_lock);
        tt_xy_pair translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
        impl_->read_from_device(translated_core, dest, l1_src, size);
    }
    for (uint32_t i = 0; i < 10; i++) {
        for (const auto& [chip_id, chip] : chips_to_clock_) {
            chip->clock(1);
        }
    }
}

void TTSimChip::clock(uint32_t clock) {
    std::lock_guard<std::mutex> lock(device_lock);
// <<<<<<< HEAD
//     tt_device_->read_from_device(dest, soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), l1_src, size);
// =======
    impl_->clock(clock);
// >>>>>>> 9e429c7c (#0: Add Active Ethernet connectivity support to ttsim chip)
}

void TTSimChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
// <<<<<<< head
//     tt_device_->send_tensix_risc_reset(translated_core, soft_resets);
// =======
    impl_->send_tensix_risc_reset(translated_core, soft_resets);
// >>>>>>> 9e429c7c (#0: add active ethernet connectivity support to ttsim chip)
}

void TTSimChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    for (const CoreCoord& core : soc_descriptor_.get_cores(CoreType::TENSIX)) {
        tt_xy_pair translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
        impl_->send_tensix_risc_reset(translated_core, soft_resets);
    }
}

void TTSimChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
// <<<<<<< HEAD
//     tt_device_->assert_risc_reset(soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), selected_riscs);
// =======
    tt_xy_pair translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    impl_->assert_risc_reset(translated_core, selected_riscs);
// >>>>>>> 9e429c7c (#0: Add Active Ethernet connectivity support to ttsim chip)
}

void TTSimChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
// <<<<<<< HEAD
//     tt_device_->deassert_risc_reset(
//         soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), selected_riscs, staggered_start);
// =======
    tt_xy_pair translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    impl_->deassert_risc_reset(translated_core, selected_riscs, staggered_start);
}

void TTSimChip::set_chips_to_clock(std::unordered_map<ChipId, TTSimChip*> chips_to_clock) {
    std::lock_guard<std::mutex> lock(device_lock);
    chips_to_clock_ = std::move(chips_to_clock);
// >>>>>>> 9e429c7c (#0: Add Active Ethernet connectivity support to ttsim chip)
}

}  // namespace tt::umd
