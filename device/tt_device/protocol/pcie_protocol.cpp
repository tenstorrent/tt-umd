/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/pcie_protocol.hpp"

#include <tt-logger/tt-logger.hpp>

#include "noc_access.hpp"

namespace tt::umd {

PcieProtocol::PcieProtocol(
    std::shared_ptr<PCIDevice> pci_device, architecture_implementation *architecture_impl, bool use_safe_api) :
    pci_device_(std::move(pci_device)),
    communication_device_id_(pci_device_->get_device_num()),
    architecture_impl_(architecture_impl),
    use_safe_api_(use_safe_api) {}

void PcieProtocol::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(pcie_io_lock);
    if (use_safe_api_) {
        write_to_device_impl<true>(mem_ptr, core, addr, size);
        return;
    }
    write_to_device_impl<false>(mem_ptr, core, addr, size);
}

void PcieProtocol::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(pcie_io_lock);
    if (use_safe_api_) {
        read_from_device_impl<true>(mem_ptr, core, addr, size);
        return;
    }
    read_from_device_impl<false>(mem_ptr, core, addr, size);
}

template <bool safe>
void PcieProtocol::read_from_device_impl(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if constexpr (safe) {
        get_cached_tlb_window()->safe_read_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        get_cached_tlb_window()->read_block_reconfigure(mem_ptr, core, addr, size);
    }
}

template <bool safe>
void PcieProtocol::write_to_device_impl(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if constexpr (safe) {
        get_cached_tlb_window()->safe_write_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        get_cached_tlb_window()->write_block_reconfigure(mem_ptr, core, addr, size);
    }
}

PCIDevice *PcieProtocol::get_pci_device() { return pci_device_.get(); }

TlbWindow *PcieProtocol::get_cached_tlb_window() {
    if (cached_tlb_window == nullptr) {
        cached_tlb_window = std::make_unique<TlbWindow>(
            get_pci_device()->allocate_tlb(architecture_impl_->get_cached_tlb_size(), TlbMapping::UC));
        return cached_tlb_window.get();
    }
    return cached_tlb_window.get();
}

TlbWindow *PcieProtocol::get_cached_pcie_dma_tlb_window(tlb_data config) {
    if (cached_pcie_dma_tlb_window == nullptr) {
        cached_pcie_dma_tlb_window =
            std::make_unique<TlbWindow>(get_pci_device()->allocate_tlb(16 * 1024 * 1024, TlbMapping::WC), config);
        return cached_pcie_dma_tlb_window.get();
    }

    cached_pcie_dma_tlb_window->configure(config);
    return cached_pcie_dma_tlb_window.get();
}

void PcieProtocol::noc_multicast_write(
    void *dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    std::lock_guard<std::mutex> lock(pcie_io_lock);
    get_cached_tlb_window()->noc_multicast_write_reconfigure(dst, size, core_start, core_end, addr, tlb_data::Strict);
}

void PcieProtocol::dma_multicast_write(
    void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    if (get_pci_device()->get_dma_buffer().buffer == nullptr) {
        log_warning(
            LogUMD,
            "DMA buffer was not allocated for PCI device {}, falling back to non-DMA (regular MMIO TLB) multicast "
            "write.",
            communication_device_id_);

        noc_multicast_write(src, size, core_start, core_end, addr);
        return;
    }

    auto pcie_dma_lock = lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, IODeviceType::PCIe);

    const uint8_t *buffer = static_cast<const uint8_t *>(src);
    PCIDevice *pci_device = get_pci_device();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    tlb_data config{};
    config.local_offset = addr;
    config.x_start = core_start.x;
    config.y_start = core_start.y;
    config.x_end = core_end.x;
    config.y_end = core_end.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = architecture_impl_->get_static_vc();
    config.mcast = true;
    TlbWindow *tlb_window = get_cached_pcie_dma_tlb_window(config);

    auto axi_address_base = architecture_impl_->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id()).tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    while (size > 0) {
        auto tlb_size = tlb_window->get_size();

        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        dma_h2d(axi_address, buffer, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

void PcieProtocol::dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr) {
    if (get_pci_device()->get_dma_buffer().buffer == nullptr) {
        log_warning(
            LogUMD,
            "DMA buffer was not allocated for PCI device {}, falling back to non-DMA (regular MMIO TLB) write.",
            communication_device_id_);
        write_to_device(src, core, addr, size);
        return;
    }

    auto pcie_dma_lock = lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, IODeviceType::PCIe);

    const uint8_t *buffer = static_cast<const uint8_t *>(src);
    PCIDevice *pci_device = get_pci_device();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = architecture_impl_->get_static_vc();
    TlbWindow *tlb_window = get_cached_pcie_dma_tlb_window(config);

    auto axi_address_base = architecture_impl_->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id()).tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    while (size > 0) {
        auto tlb_size = tlb_window->get_size();

        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        dma_h2d(axi_address, buffer, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

void PcieProtocol::dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr) {
    if (get_pci_device()->get_dma_buffer().buffer == nullptr) {
        log_warning(
            LogUMD,
            "DMA buffer was not allocated for PCI device {}, falling back to non-DMA (regular MMIO TLB) read.",
            communication_device_id_);
        read_from_device(dst, core, addr, size);
        return;
    }

    auto pcie_dma_lock = lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, IODeviceType::PCIe);

    uint8_t *buffer = static_cast<uint8_t *>(dst);
    PCIDevice *pci_device = get_pci_device();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = architecture_impl_->get_static_vc();
    TlbWindow *tlb_window = get_cached_pcie_dma_tlb_window(config);

    auto axi_address_base = architecture_impl_->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id()).tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));

    while (size > 0) {
        auto tlb_size = tlb_window->get_size();
        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        dma_d2h(buffer, axi_address, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

void PcieProtocol::bar_write32(uint32_t addr, uint32_t data) {
    const uint32_t bar0_offset = 0x1FD00000;
    if (addr < bar0_offset) {
        throw std::runtime_error("Write Invalid BAR address for this device.");
    }
    addr -= bar0_offset;
    *reinterpret_cast<volatile uint32_t *>(static_cast<uint8_t *>(get_pci_device()->bar0) + addr) = data;
}

uint32_t PcieProtocol::bar_read32(uint32_t addr) {
    const uint32_t bar0_offset = 0x1FD00000;
    if (addr < bar0_offset) {
        throw std::runtime_error("Read Invalid BAR address for this device.");
    }
    addr -= bar0_offset;
    return *reinterpret_cast<volatile uint32_t *>(static_cast<uint8_t *>(get_pci_device()->bar0) + addr);
}

void PcieProtocol::dma_h2d(uint32_t dst, const void *src, size_t size) {
    if (architecture_impl_->get_architecture() == ARCH::BLACKHOLE) {
        throw std::runtime_error("H2D DMA is not supported on Blackhole.");
    }

    DmaBuffer &dma_buffer = pci_device_->get_dma_buffer();

    if (size > dma_buffer.size) {
        throw std::runtime_error("DMA size exceeds buffer size");
    }

    memcpy(dma_buffer.buffer, src, size);
    dma_h2d_transfer(dst, dma_buffer.buffer_pa, size);
}

void PcieProtocol::dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) {
    if (architecture_impl_->get_architecture() == ARCH::BLACKHOLE) {
        throw std::runtime_error("H2D DMA is not supported on Blackhole.");
    }
    dma_h2d_transfer(dst, reinterpret_cast<uint64_t>(src), size);
}

// Private helper function.
void PcieProtocol::dma_h2d_transfer(const uint32_t dst, const uint64_t src, const size_t size) {
    static constexpr uint32_t DMA_COMPLETION_VALUE = 0xfaca;
    static constexpr uint32_t DMA_TIMEOUT_MS = 10000;  // 10 seconds

    static constexpr uint64_t DMA_READ_ENGINE_EN_OFF = 0x2c;
    static constexpr uint64_t DMA_READ_INT_MASK_OFF = 0xa8;
    static constexpr uint64_t DMA_CH_CONTROL1_OFF_RDCH_0 = 0x300;
    static constexpr uint64_t DMA_READ_DONE_IMWR_LOW_OFF = 0xcc;
    static constexpr uint64_t DMA_READ_CH01_IMWR_DATA_OFF = 0xdc;
    static constexpr uint64_t DMA_READ_DONE_IMWR_HIGH_OFF = 0xd0;
    static constexpr uint64_t DMA_READ_ABORT_IMWR_LOW_OFF = 0xd4;
    static constexpr uint64_t DMA_READ_ABORT_IMWR_HIGH_OFF = 0xd8;
    static constexpr uint64_t DMA_TRANSFER_SIZE_OFF_RDCH_0 = 0x308;
    static constexpr uint64_t DMA_SAR_LOW_OFF_RDCH_0 = 0x30c;
    static constexpr uint64_t DMA_SAR_HIGH_OFF_RDCH_0 = 0x310;
    static constexpr uint64_t DMA_DAR_LOW_OFF_RDCH_0 = 0x314;
    static constexpr uint64_t DMA_DAR_HIGH_OFF_RDCH_0 = 0x318;
    static constexpr uint64_t DMA_READ_DOORBELL_OFF = 0x30;

    std::scoped_lock lock(dma_mutex_);
    DmaBuffer &dma_buffer = pci_device_->get_dma_buffer();
    volatile uint8_t *bar2 = reinterpret_cast<volatile uint8_t *>(pci_device_->bar2_uc);
    volatile uint32_t *completion = reinterpret_cast<volatile uint32_t *>(dma_buffer.completion);

    if (!completion || !dma_buffer.buffer) {
        throw std::runtime_error("DMA buffer is not initialized");
    }

    if (dst % 4 != 0) {
        throw std::runtime_error("DMA destination address must be aligned to 4 bytes");
    }

    if (size % 4 != 0) {
        throw std::runtime_error("DMA size must be a multiple of 4");
    }

    if (!bar2) {
        throw std::runtime_error("BAR2 is not mapped");
    }

    // Reset completion flag.
    *completion = 0;

    auto write_reg = [&](uint32_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t *>(bar2 + offset) = value;
    };

    write_reg(DMA_READ_ENGINE_EN_OFF, 0x1);
    write_reg(DMA_READ_INT_MASK_OFF, 0);
    write_reg(DMA_CH_CONTROL1_OFF_RDCH_0, 0x10);  // Remote interrupt enable (for completion)
    write_reg(
        DMA_READ_DONE_IMWR_LOW_OFF, (uint32_t)(dma_buffer.completion_pa & 0xFFFFFFFF));  // Read completion address
    write_reg(DMA_READ_DONE_IMWR_HIGH_OFF, (uint32_t)((dma_buffer.completion_pa >> 32) & 0xFFFFFFFF));
    write_reg(DMA_READ_CH01_IMWR_DATA_OFF, DMA_COMPLETION_VALUE);  // Read completion value
    write_reg(DMA_READ_ABORT_IMWR_LOW_OFF, 0);
    write_reg(DMA_READ_ABORT_IMWR_HIGH_OFF, 0);
    write_reg(DMA_TRANSFER_SIZE_OFF_RDCH_0, size);
    write_reg(DMA_SAR_LOW_OFF_RDCH_0, (uint32_t)(src & 0xFFFFFFFF));
    write_reg(DMA_SAR_HIGH_OFF_RDCH_0, (uint32_t)((src >> 32) & 0xFFFFFFFF));
    write_reg(DMA_DAR_LOW_OFF_RDCH_0, dst);
    write_reg(DMA_DAR_HIGH_OFF_RDCH_0, 0);
    write_reg(DMA_READ_DOORBELL_OFF, 0);

    auto start = std::chrono::steady_clock::now();
    for (;;) {
        if (*completion == DMA_COMPLETION_VALUE) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        if (elapsed_ms > DMA_TIMEOUT_MS) {
            throw std::runtime_error("DMA timeout");
        }
    }
}

// TODO: This is a temporary implementation, and ought to be replaced with a
// driver-based technique that can take advantage of multiple channels and
// interrupts.  With a driver-based implementation we can also avoid the need to
// memcpy into/out of a buffer, although exposing zero-copy DMA functionality to
// the application will require IOMMU support.  One day...
void PcieProtocol::dma_d2h(void *dst, uint32_t src, size_t size) {
    if (architecture_impl_->get_architecture() == ARCH::BLACKHOLE) {
        throw std::runtime_error("D2H DMA is not supported on Blackhole.");
    }

    DmaBuffer &dma_buffer = pci_device_->get_dma_buffer();

    if (size > dma_buffer.size) {
        throw std::runtime_error("DMA size exceeds buffer size");
    }

    dma_d2h_transfer(dma_buffer.buffer_pa, src, size);
    memcpy(dst, dma_buffer.buffer, size);
}

void PcieProtocol::dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) {
    if (architecture_impl_->get_architecture() == ARCH::BLACKHOLE) {
        throw std::runtime_error("D2H DMA is not supported on Blackhole.");
    }
    dma_d2h_transfer(reinterpret_cast<uint64_t>(dst), src, size);
}

// Private helper function.
void PcieProtocol::dma_d2h_transfer(const uint64_t dst, const uint32_t src, const size_t size) {
    static constexpr uint32_t DMA_COMPLETION_VALUE = 0xfaca;
    static constexpr uint32_t DMA_TIMEOUT_MS = 10000;  // 10 seconds

    static constexpr uint64_t DMA_WRITE_ENGINE_EN_OFF = 0xc;
    static constexpr uint64_t DMA_WRITE_INT_MASK_OFF = 0x54;
    static constexpr uint64_t DMA_CH_CONTROL1_OFF_WRCH_0 = 0x200;
    static constexpr uint64_t DMA_WRITE_DONE_IMWR_LOW_OFF = 0x60;
    static constexpr uint64_t DMA_WRITE_CH01_IMWR_DATA_OFF = 0x70;
    static constexpr uint64_t DMA_WRITE_DONE_IMWR_HIGH_OFF = 0x64;
    static constexpr uint64_t DMA_WRITE_ABORT_IMWR_LOW_OFF = 0x68;
    static constexpr uint64_t DMA_WRITE_ABORT_IMWR_HIGH_OFF = 0x6c;
    static constexpr uint64_t DMA_TRANSFER_SIZE_OFF_WRCH_0 = 0x208;
    static constexpr uint64_t DMA_SAR_LOW_OFF_WRCH_0 = 0x20c;
    static constexpr uint64_t DMA_SAR_HIGH_OFF_WRCH_0 = 0x210;
    static constexpr uint64_t DMA_DAR_LOW_OFF_WRCH_0 = 0x214;
    static constexpr uint64_t DMA_DAR_HIGH_OFF_WRCH_0 = 0x218;
    static constexpr uint64_t DMA_WRITE_DOORBELL_OFF = 0x10;

    std::scoped_lock lock(dma_mutex_);
    DmaBuffer &dma_buffer = pci_device_->get_dma_buffer();
    volatile uint8_t *bar2 = reinterpret_cast<volatile uint8_t *>(pci_device_->bar2_uc);
    volatile uint32_t *completion = reinterpret_cast<volatile uint32_t *>(dma_buffer.completion);

    if (!completion || !dma_buffer.buffer) {
        throw std::runtime_error("DMA buffer is not initialized");
    }

    if (src % 4 != 0) {
        throw std::runtime_error("DMA source address must be aligned to 4 bytes");
    }

    if (size % 4 != 0) {
        throw std::runtime_error("DMA size must be a multiple of 4");
    }

    if (!bar2) {
        throw std::runtime_error("BAR2 is not mapped");
    }

    // Reset completion flag.
    *completion = 0;

    auto write_reg = [&](uint32_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t *>(bar2 + offset) = value;
    };

    write_reg(DMA_WRITE_ENGINE_EN_OFF, 0x1);
    write_reg(DMA_WRITE_INT_MASK_OFF, 0);
    write_reg(DMA_CH_CONTROL1_OFF_WRCH_0, 0x00000010);  // Remote interrupt enable (for completion)
    write_reg(
        DMA_WRITE_DONE_IMWR_LOW_OFF, (uint32_t)(dma_buffer.completion_pa & 0xFFFFFFFF));  // Write completion address
    write_reg(DMA_WRITE_DONE_IMWR_HIGH_OFF, (uint32_t)((dma_buffer.completion_pa >> 32) & 0xFFFFFFFF));
    write_reg(DMA_WRITE_CH01_IMWR_DATA_OFF, DMA_COMPLETION_VALUE);  // Write completion value
    write_reg(DMA_WRITE_ABORT_IMWR_LOW_OFF, 0);
    write_reg(DMA_WRITE_ABORT_IMWR_HIGH_OFF, 0);
    write_reg(DMA_TRANSFER_SIZE_OFF_WRCH_0, size);
    write_reg(DMA_SAR_LOW_OFF_WRCH_0, src);
    write_reg(DMA_SAR_HIGH_OFF_WRCH_0, 0);
    write_reg(DMA_DAR_LOW_OFF_WRCH_0, (uint32_t)(dst & 0xFFFFFFFF));
    write_reg(DMA_DAR_HIGH_OFF_WRCH_0, (uint32_t)((dst >> 32) & 0xFFFFFFFF));
    write_reg(DMA_WRITE_DOORBELL_OFF, 0);

    auto start = std::chrono::steady_clock::now();
    for (;;) {
        if (*completion == DMA_COMPLETION_VALUE) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        if (elapsed_ms > DMA_TIMEOUT_MS) {
            throw std::runtime_error("DMA timeout");
        }
    }
}

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc.
void PcieProtocol::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

}  // namespace tt::umd
