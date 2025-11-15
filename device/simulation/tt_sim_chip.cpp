/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/simulation/tt_sim_chip.hpp"

#include <filesystem>
#include <mutex>
#include <unordered_map>

#include "tt_sim_chip_impl.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<TTSimChip>(), "TTSimChip must be non-abstract.");

TTSimChip::TTSimChip(
    const std::filesystem::path& simulator_directory,
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
    impl_->clock(clock);
}

void TTSimChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    impl_->send_tensix_risc_reset(translated_core, soft_resets);
}

void TTSimChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    Chip::send_tensix_risc_reset(soft_resets);
}

void TTSimChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    impl_->assert_risc_reset(translated_core, selected_riscs);
}

void TTSimChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translated_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    impl_->deassert_risc_reset(translated_core, selected_riscs, staggered_start);
}

void TTSimChip::set_chips_to_clock(std::unordered_map<ChipId, TTSimChip*> chips_to_clock) {
    std::lock_guard<std::mutex> lock(device_lock);
    chips_to_clock_ = chips_to_clock;
}

}  // namespace tt::umd
