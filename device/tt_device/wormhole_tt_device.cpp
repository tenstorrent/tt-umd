// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/wormhole_tt_device.h"

#include "umd/device/wormhole_implementation.h"
#include "timestamp.hpp"

#define DMA_VIA_BAR2_NOT_ARC 1

namespace tt::umd {

WormholeTTDevice::WormholeTTDevice(std::unique_ptr<PCIDevice> pci_device) :
    TTDevice(std::move(pci_device), std::make_unique<wormhole_implementation>()) {}

ChipInfo WormholeTTDevice::get_chip_info() {
    ChipInfo chip_info;

    uint32_t niu_cfg;
    const tt_xy_pair dram_core = {0, 0};
    const uint64_t niu_cfg_addr = 0x1000A0000 + 0x100;
    read_from_device(&niu_cfg, dram_core, niu_cfg_addr, sizeof(uint32_t));

    bool noc_translation_enabled = (niu_cfg & (1 << 14)) != 0;

    chip_info.noc_translation_enabled = noc_translation_enabled;

    std::vector<uint32_t> arc_msg_return_values = {0};
    const uint32_t timeout_ms = 1000;
    uint32_t ret_code = get_arc_messenger()->send_message(
        tt::umd::wormhole::ARC_MSG_COMMON_PREFIX |
            get_architecture_implementation()->get_arc_message_arc_get_harvesting(),
        arc_msg_return_values,
        0,
        0,
        timeout_ms);

    if (ret_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get harvesting masks with exit code {}", ret_code));
    }

    chip_info.harvesting_masks.tensix_harvesting_mask = arc_msg_return_values[0];

    chip_info.board_type = get_board_type();

    return chip_info;
}

void WormholeTTDevice::wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms) {}

uint32_t WormholeTTDevice::get_clock() {
    const uint32_t timeouts_ms = 1000;
    // There is one return value from AICLK ARC message.
    std::vector<uint32_t> arc_msg_return_values = {0};
    auto exit_code = get_arc_messenger()->send_message(
        tt::umd::wormhole::ARC_MSG_COMMON_PREFIX | get_architecture_implementation()->get_arc_message_get_aiclk(),
        arc_msg_return_values,
        0xFFFF,
        0xFFFF,
        timeouts_ms);
    if (exit_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get AICLK value with exit code {}", exit_code));
    }
    return arc_msg_return_values[0];
}

BoardType WormholeTTDevice::get_board_type() {
    ::std::vector<uint32_t> arc_msg_return_values = {0};
    static const uint32_t timeout_ms = 1000;
    uint32_t exit_code = get_arc_messenger()->send_message(
        tt::umd::wormhole::ARC_MSG_COMMON_PREFIX |
            (uint32_t)tt::umd::wormhole::arc_message_type::GET_SMBUS_TELEMETRY_ADDR,
        arc_msg_return_values,
        0,
        0,
        timeout_ms);

    tt_xy_pair arc_core = tt::umd::wormhole::ARC_CORES_NOC0[0];
    static constexpr uint64_t noc_telemetry_offset = 0x810000000;
    uint64_t telemetry_struct_offset = arc_msg_return_values[0] + noc_telemetry_offset;

    uint32_t board_id_lo;
    uint32_t board_id_hi;
    static uint64_t board_id_hi_telemetry_offset = 16;
    static uint64_t board_id_lo_telemetry_offset = 20;
    read_from_device(&board_id_hi, arc_core, telemetry_struct_offset + board_id_hi_telemetry_offset, sizeof(uint32_t));
    read_from_device(&board_id_lo, arc_core, telemetry_struct_offset + board_id_lo_telemetry_offset, sizeof(uint32_t));

    return get_board_type_from_board_id(((uint64_t)board_id_hi << 32) | board_id_lo);
}

typedef struct {
    uint32_t  chip_addr;
    uint32_t  host_phys_addr;
    uint32_t  completion_flag_phys_addr;
    uint32_t  size_bytes                  : 28;
    uint32_t  write                       : 1;
    uint32_t  pcie_msi_on_done            : 1;
    uint32_t  pcie_write_on_done          : 1;
    uint32_t  trigger                     : 1;
    uint32_t  repeat;
} arc_pcie_ctrl_dma_request_t; // 5 * 4 = 20B

void WormholeTTDevice::dma_d2h(void *dst, uint32_t src, size_t size) {
    std::scoped_lock lock(dma_mutex_);
    DmaBuffer &dma_buffer = pci_device_->get_dma_buffer();

    if (size > dma_buffer.size) {
        throw std::runtime_error("DMA size exceeds buffer size");
    }

    // Reset completion flag
    *reinterpret_cast<volatile uint32_t*>(dma_buffer.completion) = 0;

#ifdef DMA_VIA_BAR2_NOT_ARC
    volatile uint8_t *bar2 = reinterpret_cast<volatile uint8_t*>(pci_device_->bar2_uc);

    auto write_dma_reg = [&](uint32_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t*>(bar2 + offset) = value;
    };

    const uint64_t DMA_WRITE_ENGINE_EN_OFF = 0xc;
    const uint64_t DMA_WRITE_INT_MASK_OFF = 0x54;
    const uint64_t DMA_CH_CONTROL1_OFF_WRCH_0 = 0x200;
    const uint64_t DMA_WRITE_DONE_IMWR_LOW_OFF = 0x60;
    const uint64_t DMA_WRITE_CH01_IMWR_DATA_OFF = 0x70;
    const uint64_t DMA_WRITE_DONE_IMWR_HIGH_OFF = 0x64;
    const uint64_t DMA_WRITE_ABORT_IMWR_LOW_OFF = 0x68;
    const uint64_t DMA_WRITE_ABORT_IMWR_HIGH_OFF = 0x6c;
    const uint64_t DMA_TRANSFER_SIZE_OFF_WRCH_0 = 0x208;
    const uint64_t DMA_SAR_LOW_OFF_WRCH_0 = 0x20c;
    const uint64_t DMA_SAR_HIGH_OFF_WRCH_0 = 0x210;
    const uint64_t DMA_DAR_LOW_OFF_WRCH_0 = 0x214;
    const uint64_t DMA_DAR_HIGH_OFF_WRCH_0 = 0x218;
    const uint64_t DMA_WRITE_DOORBELL_OFF = 0x10;

    write_dma_reg(DMA_WRITE_ENGINE_EN_OFF, 0x1);
    write_dma_reg(DMA_WRITE_INT_MASK_OFF, 0);
    write_dma_reg(DMA_CH_CONTROL1_OFF_WRCH_0, 0x04000010);
    write_dma_reg(DMA_WRITE_DONE_IMWR_LOW_OFF, dma_buffer.completion_pa);
    write_dma_reg(DMA_WRITE_CH01_IMWR_DATA_OFF, 0xfaca);
    write_dma_reg(DMA_WRITE_DONE_IMWR_HIGH_OFF, 0);
    write_dma_reg(DMA_WRITE_ABORT_IMWR_LOW_OFF, 0);
    write_dma_reg(DMA_WRITE_ABORT_IMWR_HIGH_OFF, 0);
    write_dma_reg(DMA_TRANSFER_SIZE_OFF_WRCH_0, size);
    write_dma_reg(DMA_SAR_LOW_OFF_WRCH_0, src);
    write_dma_reg(DMA_SAR_HIGH_OFF_WRCH_0, 0);
    write_dma_reg(DMA_DAR_LOW_OFF_WRCH_0, dma_buffer.buffer_pa);
    write_dma_reg(DMA_DAR_HIGH_OFF_WRCH_0, 0);
    write_dma_reg(DMA_WRITE_DOORBELL_OFF, 0);

#else // the ARC way

    arc_pcie_ctrl_dma_request_t req = {
        .chip_addr           = src,
        .host_phys_addr      = dma_buffer.buffer_pa,
        .completion_flag_phys_addr = dma_buffer.completion_pa,
        .size_bytes          = (uint32_t)size,
        .write               = 0,
        .pcie_msi_on_done    = 0,
        .pcie_write_on_done  = 1,
        .trigger             = 1,
        .repeat              = 1
    };

    static constexpr uint64_t CSM_PCIE_CTRL_DMA_REQUEST_OFFSET = 0x1fef84c8;
    static constexpr uint64_t ARC_MISC_CNTL_ADDRESS = 0x1ff30100;
    write_regs(CSM_PCIE_CTRL_DMA_REQUEST_OFFSET, sizeof(req) / sizeof(uint32_t), &req);

    uint32_t go = 1 << 16;
    write_regs(ARC_MISC_CNTL_ADDRESS, 1, &go);

#endif

    util::Timestamp ts;
    for (;;) {
        if (*(volatile uint32_t*)dma_buffer.completion == 0xfaca) {
            break;
        }

        if (ts.seconds() > 1) {
            throw std::runtime_error("DMA timeout");
        }
    }

    memcpy(dst, dma_buffer.buffer, size);
}

void WormholeTTDevice::dma_h2d(uint32_t dst, const void *src, size_t size) {
    std::scoped_lock lock(dma_mutex_);
    DmaBuffer &dma_buffer = pci_device_->get_dma_buffer();

    if (size > dma_buffer.size) {
        throw std::runtime_error("DMA size exceeds buffer size");
    }

    memcpy(dma_buffer.buffer, src, size);

    // Reset completion flag
    *reinterpret_cast<volatile uint32_t*>(dma_buffer.completion) = 0;

#ifdef DMA_VIA_BAR2_NOT_ARC
    volatile uint8_t *bar2 = reinterpret_cast<volatile uint8_t*>(pci_device_->bar2_uc);

    auto write_dma_reg = [&](uint32_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t*>(bar2 + offset) = value;
    };

    const uint64_t DMA_READ_ENGINE_EN_OFF = 0x2c;
    const uint64_t DMA_READ_INT_MASK_OFF = 0xa8;
    const uint64_t DMA_CH_CONTROL1_OFF_RDCH_0 = 0x300;
    const uint64_t DMA_READ_DONE_IMWR_LOW_OFF = 0xcc;
    const uint64_t DMA_READ_CH01_IMWR_DATA_OFF = 0xdc;
    const uint64_t DMA_READ_DONE_IMWR_HIGH_OFF = 0xd0;
    const uint64_t DMA_READ_ABORT_IMWR_LOW_OFF = 0xd4;
    const uint64_t DMA_READ_ABORT_IMWR_HIGH_OFF = 0xd8;
    const uint64_t DMA_TRANSFER_SIZE_OFF_RDCH_0 = 0x308;
    const uint64_t DMA_SAR_LOW_OFF_RDCH_0 = 0x30c;
    const uint64_t DMA_SAR_HIGH_OFF_RDCH_0 = 0x310;
    const uint64_t DMA_DAR_LOW_OFF_RDCH_0 = 0x314;
    const uint64_t DMA_DAR_HIGH_OFF_RDCH_0 = 0x318;
    const uint64_t DMA_READ_DOORBELL_OFF = 0x30;

    write_dma_reg(DMA_READ_ENGINE_EN_OFF, 0x1);
    write_dma_reg(DMA_READ_INT_MASK_OFF, 0);
    write_dma_reg(DMA_CH_CONTROL1_OFF_RDCH_0, 0x04000010);
    write_dma_reg(DMA_READ_DONE_IMWR_LOW_OFF, dma_buffer.completion_pa);
    write_dma_reg(DMA_READ_CH01_IMWR_DATA_OFF, 0xfaca);
    write_dma_reg(DMA_READ_DONE_IMWR_HIGH_OFF, 0);
    write_dma_reg(DMA_READ_ABORT_IMWR_LOW_OFF, 0);
    write_dma_reg(DMA_READ_ABORT_IMWR_HIGH_OFF, 0);
    write_dma_reg(DMA_TRANSFER_SIZE_OFF_RDCH_0, size);
    write_dma_reg(DMA_SAR_LOW_OFF_RDCH_0, dma_buffer.buffer_pa);
    write_dma_reg(DMA_SAR_HIGH_OFF_RDCH_0, 0);
    write_dma_reg(DMA_DAR_LOW_OFF_RDCH_0, dst);
    write_dma_reg(DMA_DAR_HIGH_OFF_RDCH_0, 0);
    write_dma_reg(DMA_READ_DOORBELL_OFF, 0);
#else // the ARC way
    arc_pcie_ctrl_dma_request_t req = {
        .chip_addr           = dst,
        .host_phys_addr      = dma_buffer.buffer_pa,
        .completion_flag_phys_addr = dma_buffer.completion_pa,
        .size_bytes          = (uint32_t)size,
        .write               = 1,
        .pcie_msi_on_done    = 0,
        .pcie_write_on_done  = 1,
        .trigger             = 1,
        .repeat              = 1
    };

    static constexpr uint64_t CSM_PCIE_CTRL_DMA_REQUEST_OFFSET = 0x1fef84c8;
    static constexpr uint64_t ARC_MISC_CNTL_ADDRESS = 0x1ff30100;
    write_regs(CSM_PCIE_CTRL_DMA_REQUEST_OFFSET, sizeof(req) / sizeof(uint32_t), &req);

    uint32_t go = 1 << 16;
    write_regs(ARC_MISC_CNTL_ADDRESS, 1, &go);
#endif

    util::Timestamp ts;
    for (;;) {
        if (*(volatile uint32_t*)dma_buffer.completion == 0xfaca) {
            break;
        }

        if (ts.seconds() > 1) {
            throw std::runtime_error("DMA timeout");
        }
    }
}

}  // namespace tt::umd
