// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/tlb_window.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "pcie/io_window_reconfigure.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/io_window_config.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// IoOrdering carries the TLB hardware encoding directly, so translating between the two is a cast.
// If these ever diverge, the casts in configure()/get_io_ordering() below become silently wrong.
// TEMP: these live exactly as long as those casts do, i.e. until tlb_data is gone from the
// window API and IoOrdering is what callers pass all the way down. tlb_data itself stays either way --
// it is the hardware register descriptor (see tlb_data::apply_offset) and remains internal to the
// handle/PCI layer.
static_assert(static_cast<uint64_t>(IoOrdering::Relaxed) == tlb_data::Relaxed);
static_assert(static_cast<uint64_t>(IoOrdering::Strict) == tlb_data::Strict);
static_assert(static_cast<uint64_t>(IoOrdering::Posted) == tlb_data::Posted);

TlbWindow::TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config) : tlb_handle(std::move(handle)) {
    tlb_data aligned_config = config;
    aligned_config.local_offset = config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = config.local_offset - (config.local_offset & ~(tlb_handle->get_size() - 1));
}

tlb_data TlbWindow::make_tlb_config(
    uint64_t addr, tt_xy_pair core_end, NocId noc_id, uint64_t ordering, bool mcast, tt_xy_pair core_start) const {
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core_end.x;
    config.y_end = core_end.y;
    config.noc_sel = static_cast<uint64_t>(noc_id);
    config.ordering = ordering;
    config.static_vc = handle_ref().get_arch() != tt::ARCH::BLACKHOLE;
    if (mcast) {
        config.mcast = true;
        config.x_start = core_start.x;
        config.y_start = core_start.y;
    }
    return config;
}

// Thin forwarders onto the free-function family in io_window_reconfigure.hpp, kept as virtual
// members solely so SiliconTlbWindow's safe_* variants (execute_safe, taking a pointer-to-member)
// keep working unchanged. Callers with no safe/unsafe distinction to make should call the free
// functions directly instead of going through a TlbWindow.
void TlbWindow::read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    tt::umd::read_block_reconfigure(*this, mem_ptr, core, addr, size, noc_id, static_cast<IoOrdering>(ordering));
}

void TlbWindow::read_register_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    tt::umd::read_register_reconfigure(*this, mem_ptr, core, addr, size, noc_id, static_cast<IoOrdering>(ordering));
}

void TlbWindow::write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    tt::umd::write_block_reconfigure(*this, mem_ptr, core, addr, size, noc_id, static_cast<IoOrdering>(ordering));
}

void TlbWindow::write_register_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    tt::umd::write_register_reconfigure(*this, mem_ptr, core, addr, size, noc_id, static_cast<IoOrdering>(ordering));
}

void TlbWindow::noc_multicast_write_reconfigure(
    const void* src,
    size_t size,
    tt_xy_pair core_start,
    tt_xy_pair core_end,
    uint64_t addr,
    NocId noc_id,
    uint64_t ordering) {
    tt::umd::noc_multicast_write_reconfigure(
        *this, src, size, core_start, core_end, addr, noc_id, static_cast<IoOrdering>(ordering));
}

TlbHandle& TlbWindow::handle_ref() const { return *tlb_handle; }

size_t TlbWindow::get_size() const { return tlb_handle->get_size() - offset_from_aligned_addr; }

void TlbWindow::validate(uint64_t offset, size_t size) const {
    if ((offset + size) > get_size()) {
        throw std::out_of_range("Out of bounds access");
    }
}

void TlbWindow::configure(const tlb_data& new_config) {
    tlb_data aligned_config = new_config;
    aligned_config.local_offset = new_config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = new_config.local_offset - (new_config.local_offset & ~(tlb_handle->get_size() - 1));
}

void TlbWindow::write_aligned(uint64_t offset, const void* data, size_t size) {
    UMD_ASSERT(
        offset % sizeof(uint32_t) == 0 && size % sizeof(uint32_t) == 0,
        error::RuntimeError,
        "write_aligned offset and size must be 4-byte aligned.");
    write_register(offset, data, size);
}

void TlbWindow::read_aligned(uint64_t offset, void* data, size_t size) {
    UMD_ASSERT(
        offset % sizeof(uint32_t) == 0 && size % sizeof(uint32_t) == 0,
        error::RuntimeError,
        "read_aligned offset and size must be 4-byte aligned.");
    read_register(offset, data, size);
}

void TlbWindow::configure(const TargetIoWindowConfig& config) { configure(config, IoOrdering::Strict); }

void TlbWindow::configure(const TargetIoWindowConfig& config, IoOrdering ordering) {
    // A TLB mapping has no way to express these; failing loudly beats silently dropping them.
    UMD_ASSERT(
        config.flags == WindowFlags::None,
        error::RuntimeError,
        "WindowFlags are not supported by TLB-backed IoWindows.");

    UMD_ASSERT(config.noc.has_value(), error::RuntimeError, "TLB-backed IoWindows must specify a NOC.");

    const bool mcast = config.core_end.has_value();
    configure(make_tlb_config(
        config.addr,
        mcast ? config.core_end.value() : config.core_start,
        config.noc.value(),
        static_cast<uint64_t>(ordering),
        mcast,
        config.core_start));
}

TargetIoWindowConfig TlbWindow::get_target_config() const {
    const tlb_data& config = handle_ref().get_config();
    const tt_xy_pair end_core(config.x_end, config.y_end);

    TargetIoWindowConfig target;
    if (config.mcast) {
        target.core_start = tt_xy_pair(config.x_start, config.y_start);
        target.core_end = end_core;
    } else {
        target.core_start = end_core;
    }
    // The handle holds the aligned base; the remainder lives in offset_from_aligned_addr.
    target.addr = get_base_address();
    target.noc = static_cast<NocId>(config.noc_sel);
    return target;
}

IoOrdering TlbWindow::get_io_ordering() const { return static_cast<IoOrdering>(handle_ref().get_config().ordering); }

HostMemoryCaching TlbWindow::get_memory_caching_type() const {
    return handle_ref().get_tlb_mapping() == TlbMapping::WC ? HostMemoryCaching::WC : HostMemoryCaching::UC;
}

uint64_t TlbWindow::get_total_offset(uint64_t offset) const { return offset + offset_from_aligned_addr; }

uint64_t TlbWindow::get_base_address() const {
    return handle_ref().get_config().local_offset + offset_from_aligned_addr;
}

void TlbWindow::safe_write32(uint64_t offset, uint32_t value) { write32(offset, value); }

uint32_t TlbWindow::safe_read32(uint64_t offset) { return read32(offset); }

void TlbWindow::safe_write_register(uint64_t offset, const void* data, size_t size) {
    write_register(offset, data, size);
}

void TlbWindow::safe_read_register(uint64_t offset, void* data, size_t size) { read_register(offset, data, size); }

void TlbWindow::safe_write_block(uint64_t offset, const void* data, size_t size) { write_block(offset, data, size); }

void TlbWindow::safe_read_block(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void TlbWindow::safe_write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    write_block_reconfigure(mem_ptr, core, addr, size, noc_id, ordering);
}

void TlbWindow::safe_read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    read_block_reconfigure(mem_ptr, core, addr, size, noc_id, ordering);
}

void TlbWindow::safe_read_register_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    read_register_reconfigure(mem_ptr, core, addr, size, noc_id, ordering);
}

void TlbWindow::safe_write_register_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    write_register_reconfigure(mem_ptr, core, addr, size, noc_id, ordering);
}

void TlbWindow::safe_noc_multicast_write_reconfigure(
    const void* src,
    size_t size,
    tt_xy_pair core_start,
    tt_xy_pair core_end,
    uint64_t addr,
    NocId noc_id,
    uint64_t ordering) {
    noc_multicast_write_reconfigure(src, size, core_start, core_end, addr, noc_id, ordering);
}

}  // namespace tt::umd
