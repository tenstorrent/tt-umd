// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/emule_tlb.hpp"

#include <cstring>

#include "umd/device/chip/sw_emule_chip.hpp"
#include "umd/device/soc_descriptor.hpp"

namespace tt::umd {

// Larger than any device L1/DRAM address and a power of two, so TlbWindow's
// alignment (local_offset & ~(size-1)) leaves the full address in the
// sub-alignment remainder — the window never truncates or splits an access.
static constexpr size_t kEmuleTlbWindowSize = size_t(1) << 40;

// --- EmuleTlbHandle ---

EmuleTlbHandle::EmuleTlbHandle(tt::ARCH arch, size_t size, int tlb_id) : arch_(arch) {
    tlb_size_ = size;
    tlb_id_ = tlb_id;
    tlb_mapping_ = TlbMapping::WC;
}

void EmuleTlbHandle::configure(const tlb_data& new_config) { tlb_config_ = new_config; }

tt::ARCH EmuleTlbHandle::get_arch() const { return arch_; }

// --- EmuleTlbWindow ---

EmuleTlbWindow::EmuleTlbWindow(std::unique_ptr<TlbHandle> handle, SWEmuleChip* chip, const tlb_data config) :
    TlbWindow(std::move(handle), config), chip_(chip) {}

CoreCoord EmuleTlbWindow::target_core() const {
    const tlb_data& cfg = handle_ref().get_config();
    // TLB configs carry TRANSLATED core coords (see TLBManager::configure_tlb);
    // SWEmuleChip::write_to_device keys tt_emule::Core storage on that raw (x,y).
    return CoreCoord(
        static_cast<size_t>(cfg.x_end), static_cast<size_t>(cfg.y_end), CoreType::TENSIX, CoordSystem::TRANSLATED);
}

uint64_t EmuleTlbWindow::target_addr(uint64_t offset) const { return get_base_address() + offset; }

void EmuleTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    chip_->write_to_device(target_core(), data, target_addr(offset), size);
}

void EmuleTlbWindow::read_block(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    chip_->read_from_device(target_core(), data, target_addr(offset), size);
}

void EmuleTlbWindow::write32(uint64_t offset, uint32_t value) { write_block(offset, &value, sizeof(value)); }

uint32_t EmuleTlbWindow::read32(uint64_t offset) {
    uint32_t value = 0;
    read_block(offset, &value, sizeof(value));
    return value;
}

void EmuleTlbWindow::write16(uint64_t offset, uint16_t value) { write_block(offset, &value, sizeof(value)); }

uint16_t EmuleTlbWindow::read16(uint64_t offset) {
    uint16_t value = 0;
    read_block(offset, &value, sizeof(value));
    return value;
}

void EmuleTlbWindow::write_register(uint64_t offset, const void* data, size_t size) {
    write_block(offset, data, size);
}

void EmuleTlbWindow::read_register(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void EmuleTlbWindow::safe_write16(uint64_t offset, uint16_t value) { write16(offset, value); }

uint16_t EmuleTlbWindow::safe_read16(uint64_t offset) { return read16(offset); }

// --- EmuleTlbManager ---

EmuleTlbManager::EmuleTlbManager(SWEmuleChip* chip) : TLBManager(nullptr), chip_(chip) {
    const SocDescriptor& soc = chip->get_soc_descriptor();
    for (const CoreCoord& core : soc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)) {
        const int tlb_id = next_tlb_id_++;
        tlb_data config{};
        config.local_offset = 0;
        config.x_end = core.x;
        config.y_end = core.y;

        auto handle = std::make_unique<EmuleTlbHandle>(soc.arch, kEmuleTlbWindowSize, tlb_id);
        auto window = std::make_unique<EmuleTlbWindow>(std::move(handle), chip_, config);

        tlb_config_map_.insert({tlb_id, 0});
        map_core_to_tlb_.insert({tt_xy_pair(core.x, core.y), tlb_id});
        tlb_windows_.insert({tlb_id, std::move(window)});
    }
}

}  // namespace tt::umd
