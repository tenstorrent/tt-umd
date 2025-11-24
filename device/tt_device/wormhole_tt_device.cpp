// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/wormhole_tt_device.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "utils.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

static constexpr uint32_t DMA_COMPLETION_VALUE = 0xfaca;
static constexpr uint32_t DMA_TIMEOUT_MS = 10000;  // 10 seconds

WormholeTTDevice::WormholeTTDevice(std::shared_ptr<PCIDevice> pci_device) :
    TTDevice(pci_device, std::make_unique<wormhole_implementation>()) {
    arc_core = umd_use_noc1 ? tt_xy_pair(
                                  tt::umd::wormhole::NOC0_X_TO_NOC1_X[tt::umd::wormhole::ARC_CORES_NOC0[0].x],
                                  tt::umd::wormhole::NOC0_Y_TO_NOC1_Y[tt::umd::wormhole::ARC_CORES_NOC0[0].y])
                            : wormhole::ARC_CORES_NOC0[0];
}

void WormholeTTDevice::post_init_hook() {
    eth_addresses = WormholeTTDevice::get_eth_addresses(get_firmware_info_provider()->get_eth_fw_version());
}

WormholeTTDevice::WormholeTTDevice(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id) :
    TTDevice(jtag_device, jlink_id, std::make_unique<wormhole_implementation>()) {
    arc_core = umd_use_noc1 ? tt_xy_pair(
                                  tt::umd::wormhole::NOC0_X_TO_NOC1_X[tt::umd::wormhole::ARC_CORES_NOC0[0].x],
                                  tt::umd::wormhole::NOC0_Y_TO_NOC1_Y[tt::umd::wormhole::ARC_CORES_NOC0[0].y])
                            : wormhole::ARC_CORES_NOC0[0];
    init_tt_device();
}

WormholeTTDevice::WormholeTTDevice() : TTDevice(std::make_unique<wormhole_implementation>()) {
    arc_core = umd_use_noc1 ? tt_xy_pair(
                                  tt::umd::wormhole::NOC0_X_TO_NOC1_X[tt::umd::wormhole::ARC_CORES_NOC0[0].x],
                                  tt::umd::wormhole::NOC0_Y_TO_NOC1_Y[tt::umd::wormhole::ARC_CORES_NOC0[0].y])
                            : wormhole::ARC_CORES_NOC0[0];
    log_warning(tt::LogUMD, "Created WormholeTTDevice without an underlying I/O device (PCIe or JTAG).");
}

bool WormholeTTDevice::get_noc_translation_enabled() {
    uint32_t niu_cfg;
    // We read information about NOC translation from DRAM core just be on paar with Luwen implementation.
    // We use DRAM core (0, 0) to read this information, but it can be read from any core.
    // TODO: read this information from PCIE BAR.
    const tt_xy_pair dram_core =
        umd_use_noc1 ? tt_xy_pair(wormhole::NOC0_X_TO_NOC1_X[0], wormhole::NOC0_Y_TO_NOC1_Y[0]) : tt_xy_pair(0, 0);
    const uint64_t niu_cfg_addr = 0x1000A0000 + 0x100;
    read_from_device(&niu_cfg, dram_core, niu_cfg_addr, sizeof(uint32_t));

    return (niu_cfg & (1 << 14)) != 0;
}

ChipInfo WormholeTTDevice::get_chip_info() {
    ChipInfo chip_info = TTDevice::get_chip_info();

    std::vector<uint32_t> arc_msg_return_values = {0};
    uint32_t ret_code = get_arc_messenger()->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | get_architecture_implementation()->get_arc_message_arc_get_harvesting(),
        arc_msg_return_values,
        0,
        0);

    if (ret_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get harvesting masks with exit code {}", ret_code));
    }

    chip_info.harvesting_masks.tensix_harvesting_mask =
        CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, arc_msg_return_values[0]);

    return chip_info;
}

uint32_t WormholeTTDevice::get_clock() {
    // There is one return value from AICLK ARC message.
    std::vector<uint32_t> arc_msg_return_values = {0};
    auto exit_code = get_arc_messenger()->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | get_architecture_implementation()->get_arc_message_get_aiclk(),
        arc_msg_return_values,
        0xFFFF,
        0xFFFF);
    if (exit_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get AICLK value with exit code {}", exit_code));
    }
    return arc_msg_return_values[0];
}

uint32_t WormholeTTDevice::get_min_clock_freq() { return wormhole::AICLK_IDLE_VAL; }

void WormholeTTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    uint32_t dest_bar_lo = target & 0xffffffff;
    uint32_t dest_bar_hi = (target >> 32) & 0xffffffff;
    std::uint32_t region_id_to_use = region;

    // TODO: stop doing this.  It's related to HUGEPAGE_CHANNEL_3_SIZE_LIMIT.
    if (region == 3) {
        region_id_to_use = 4;  // Hack use region 4 for channel 3..this ensures that we have a smaller chan 3 address
                               // space with the correct start offset
    }

    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("configure_iatu_region is redundant for JTAG communication type.");
    }

    bar_write32(architecture_impl_->get_arc_csm_bar0_mailbox_offset() + 0 * 4, region_id_to_use);
    bar_write32(architecture_impl_->get_arc_csm_bar0_mailbox_offset() + 1 * 4, dest_bar_lo);
    bar_write32(architecture_impl_->get_arc_csm_bar0_mailbox_offset() + 2 * 4, dest_bar_hi);
    bar_write32(architecture_impl_->get_arc_csm_bar0_mailbox_offset() + 3 * 4, region_size);
    arc_messenger_->send_message(
        wormhole::ARC_MSG_COMMON_PREFIX | architecture_impl_->get_arc_message_setup_iatu_for_peer_to_peer(), 0, 0);

    // Print what just happened.
    uint32_t peer_region_start = region_id_to_use * region_size;
    uint32_t peer_region_end = (region_id_to_use + 1) * region_size - 1;
    log_debug(
        LogUMD,
        "    [region id {}] NOC to PCI address range 0x{:x}-0x{:x} mapped to addr 0x{:x}",
        region,
        peer_region_start,
        peer_region_end,
        target);
}

void WormholeTTDevice::dma_d2h_transfer(const uint64_t dst, const uint32_t src, const size_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("dma_d2h_transfer is not applicable for JTAG communication type.");
    }

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

void WormholeTTDevice::dma_h2d_transfer(const uint32_t dst, const uint64_t src, const size_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("dma_h2d_transfer is not applicable for JTAG communication type.");
    }
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
void WormholeTTDevice::dma_d2h(void *dst, uint32_t src, size_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("dma_d2h is not applicable for JTAG communication type.");
    }
    DmaBuffer &dma_buffer = pci_device_->get_dma_buffer();

    if (size > dma_buffer.size) {
        throw std::runtime_error("DMA size exceeds buffer size");
    }

    dma_d2h_transfer(dma_buffer.buffer_pa, src, size);
    memcpy(dst, dma_buffer.buffer, size);
}

void WormholeTTDevice::dma_h2d(uint32_t dst, const void *src, size_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("dma_h2d is not applicable for JTAG communication type.");
    }
    DmaBuffer &dma_buffer = pci_device_->get_dma_buffer();

    if (size > dma_buffer.size) {
        throw std::runtime_error("DMA size exceeds buffer size");
    }

    memcpy(dma_buffer.buffer, src, size);
    dma_h2d_transfer(dst, dma_buffer.buffer_pa, size);
}

void WormholeTTDevice::dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("dma_h2d_zero_copy is not applicable for JTAG communication type.");
    }
    dma_h2d_transfer(dst, reinterpret_cast<uint64_t>(src), size);
}

void WormholeTTDevice::dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("dma_d2h_zero_copy is not applicable for JTAG communication type.");
    }
    dma_d2h_transfer(reinterpret_cast<uint64_t>(dst), src, size);
}

void WormholeTTDevice::read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_APB_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC APB address range");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->read(
            communication_device_id_,
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0].x,
            wormhole::ARC_CORES_NOC0[0].y,
            architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    auto result = bar_read32(wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset);
    *(reinterpret_cast<uint32_t *>(mem_ptr)) = result;
}

void WormholeTTDevice::write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_APB_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC APB address range");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(
            communication_device_id_,
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0].x,
            wormhole::ARC_CORES_NOC0[0].y,
            architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    bar_write32(
        wormhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset, *(reinterpret_cast<const uint32_t *>(mem_ptr)));
}

void WormholeTTDevice::read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_CSM_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC CSM address range");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->read(
            communication_device_id_,
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0].x,
            wormhole::ARC_CORES_NOC0[0].y,
            architecture_impl_->get_arc_csm_noc_base_address() + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    auto result = bar_read32(wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START + arc_addr_offset);
    *(reinterpret_cast<uint32_t *>(mem_ptr)) = result;
}

void WormholeTTDevice::write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_CSM_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC CSM address range");
    }
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(
            communication_device_id_,
            mem_ptr,
            wormhole::ARC_CORES_NOC0[0].x,
            wormhole::ARC_CORES_NOC0[0].y,
            architecture_impl_->get_arc_csm_noc_base_address() + arc_addr_offset,
            sizeof(uint32_t));
        return;
    }
    bar_write32(
        wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START + arc_addr_offset, *(reinterpret_cast<const uint32_t *>(mem_ptr)));
}

std::chrono::milliseconds WormholeTTDevice::wait_eth_core_training(
    const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms) {
    constexpr uint64_t eth_core_heartbeat_addr = 0x1C;
    auto time_taken_heartbeat = std::chrono::milliseconds(0);
    auto time_taken_port = std::chrono::milliseconds(0);
    auto start = std::chrono::steady_clock::now();
    uint32_t heartbeat_val;

    tt_xy_pair actual_eth_core = eth_core;
    if (umd_use_noc1) {
        actual_eth_core = tt_xy_pair(wormhole::NOC0_X_TO_NOC1_X[eth_core.x], wormhole::NOC0_Y_TO_NOC1_Y[eth_core.y]);
    }

    read_from_device(&heartbeat_val, actual_eth_core, eth_core_heartbeat_addr, sizeof(heartbeat_val));

    uint32_t new_heartbeat_val = heartbeat_val;
    while (new_heartbeat_val != heartbeat_val) {
        read_from_device(&new_heartbeat_val, actual_eth_core, eth_core_heartbeat_addr, sizeof(heartbeat_val));
        utils::check_timeout(start, timeout_ms, fmt::format("ETH training timed out after {} ms", timeout_ms));
    }

    start = std::chrono::steady_clock::now();
    while (read_training_status(eth_core) == LINK_TRAIN_TRAINING) {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        time_taken_port = duration;
        if (time_taken_port > timeout_ms) {
            if (get_board_type() != BoardType::UBB) {
                throw std::runtime_error(fmt::format(
                    "ETH training timed out after {} ms, on eth core {}, {}",
                    timeout_ms.count(),
                    eth_core.x,
                    eth_core.y));
            }
            break;
        }
    }
    return time_taken_heartbeat + time_taken_port;
}

uint32_t WormholeTTDevice::read_training_status(tt_xy_pair eth_core) {
    uint32_t training_status;
    read_from_device(
        &training_status,
        umd_use_noc1 ? tt_xy_pair(wormhole::NOC0_X_TO_NOC1_X[eth_core.x], wormhole::NOC0_Y_TO_NOC1_Y[eth_core.y])
                     : eth_core,
        0x1104,
        sizeof(uint32_t));
    return training_status;
}

WormholeTTDevice::EthAddresses WormholeTTDevice::get_eth_addresses(const uint32_t eth_fw_version) {
    uint32_t masked_version = eth_fw_version & 0x00FFFFFF;

    uint64_t node_info;
    uint64_t eth_conn_info;
    uint64_t results_buf;
    uint64_t erisc_remote_board_type_offset;
    uint64_t erisc_local_board_type_offset;
    uint64_t erisc_local_board_id_lo_offset;
    uint64_t erisc_remote_board_id_lo_offset;
    uint64_t erisc_remote_eth_id_offset;

    if (masked_version >= 0x060000) {
        node_info = 0x1100;
        eth_conn_info = 0x1200;
        results_buf = 0x1ec0;
    } else {
        throw std::runtime_error(
            fmt::format("Unsupported ETH version {:#x}. ETH version should always be at least 6.0.0.", eth_fw_version));
    }

    if (masked_version >= 0x06C000) {
        erisc_remote_board_type_offset = 77;
        erisc_local_board_type_offset = 69;
        erisc_remote_board_id_lo_offset = 72;
        erisc_local_board_id_lo_offset = 64;
        erisc_remote_eth_id_offset = 76;
    } else {
        erisc_remote_board_type_offset = 72;
        erisc_local_board_type_offset = 64;
        erisc_remote_board_id_lo_offset = 73;
        erisc_local_board_id_lo_offset = 65;
        erisc_remote_eth_id_offset = 77;
    }

    return WormholeTTDevice::EthAddresses{
        masked_version,
        node_info,
        eth_conn_info,
        results_buf,
        erisc_remote_board_type_offset,
        erisc_local_board_type_offset,
        erisc_local_board_id_lo_offset,
        erisc_remote_board_id_lo_offset,
        erisc_remote_eth_id_offset};
}

bool WormholeTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) {
    // Status codes.
    constexpr uint32_t STATUS_NO_ACCESS = 0xFFFFFFFF;
    constexpr uint32_t STATUS_WATCHDOG_TRIGGERED = 0xDEADC0DE;
    constexpr uint32_t STATUS_BOOT_INCOMPLETE_1 = 0x00000060;
    constexpr uint32_t STATUS_BOOT_INCOMPLETE_2 = 0x11110000;
    constexpr uint32_t STATUS_ASLEEP_1 = 0x0000AA00;
    constexpr uint32_t STATUS_ASLEEP_2 = 0x55;
    constexpr uint32_t STATUS_INIT_DONE_1 = 0x00000001;
    constexpr uint32_t STATUS_INIT_DONE_2 = 0xFFFFDEAD;
    constexpr uint32_t STATUS_OLD_POST_CODE = 0;
    constexpr uint32_t STATUS_MESSAGE_QUEUED_MASK = 0xFFFFFF00;
    constexpr uint32_t STATUS_MESSAGE_QUEUED_VAL = 0x0000AA00;
    constexpr uint32_t STATUS_HANDLING_MESSAGE_MASK = 0xFF00FFFF;
    constexpr uint32_t STATUS_HANDLING_MESSAGE_VAL = 0xAA000000;
    constexpr uint32_t STATUS_MESSAGE_COMPLETE_MASK = 0x0000FFFF;
    constexpr uint32_t STATUS_MESSAGE_COMPLETE_MIN = 0x00000001;

    // Post codes.
    constexpr uint32_t POST_CODE_INIT_DONE = 0xC0DE0001;
    constexpr uint32_t POST_CODE_ARC_MSG_HANDLE_START = 0xC0DE0030;
    constexpr uint32_t POST_CODE_ARC_MSG_HANDLE_DONE = 0xC0DE003F;
    constexpr uint32_t POST_CODE_ARC_TIME_LAST = 0xC0DE007F;

    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (timeout_ms.count() != 0 && elapsed_ms > timeout_ms.count()) {
            log_debug(LogUMD, "Post reset wait for ARC timed out after: {}", timeout_ms.count());
            return false;
        }

        uint32_t bar_read_arc_reset_scratch_status;

        read_from_arc_apb(
            &bar_read_arc_reset_scratch_status,
            wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET,
            sizeof(bar_read_arc_reset_scratch_status));

        uint32_t bar_read_arc_post_code;

        read_from_arc_apb(
            &bar_read_arc_post_code,
            architecture_impl_->get_arc_reset_scratch_offset(),
            sizeof(bar_read_arc_post_code));

        uint32_t bar_read_arc_csm_pcie_dma_request;

        read_from_arc_csm(
            &bar_read_arc_csm_pcie_dma_request,
            wormhole::ARC_CSM_ARC_PCIE_DMA_REQUEST,
            sizeof(bar_read_arc_csm_pcie_dma_request));

        // Handle known error/status codes.
        switch (bar_read_arc_reset_scratch_status) {
            case STATUS_NO_ACCESS:
                log_debug(LogUMD, "NoAccess error");
                return false;
            case STATUS_WATCHDOG_TRIGGERED:
                log_debug(LogUMD, "WatchdogTriggered error");
                return false;
            case STATUS_BOOT_INCOMPLETE_1:
            case STATUS_BOOT_INCOMPLETE_2:
                log_debug(LogUMD, "BootIncomplete error");
                continue;
            case STATUS_ASLEEP_1:
            case STATUS_ASLEEP_2:
                log_debug(LogUMD, "Asleep error");
                continue;
            case STATUS_INIT_DONE_1:
            case STATUS_INIT_DONE_2:
                return true;
            case STATUS_OLD_POST_CODE: {
                bool pc_idle = (bar_read_arc_post_code == POST_CODE_INIT_DONE) ||
                               (bar_read_arc_post_code >= POST_CODE_ARC_MSG_HANDLE_DONE &&
                                bar_read_arc_post_code <= POST_CODE_ARC_TIME_LAST);
                if (pc_idle) {
                    return true;
                }
                log_debug(LogUMD, "OldPostCode error, post_code: {}", bar_read_arc_post_code);
                continue;
            }
        }

        // Check for outstanding DMA request.
        if (bar_read_arc_csm_pcie_dma_request != 0) {
            log_debug(LogUMD, "OutstandingPcieDMA error");
            continue;
        }
        // Check for queued message.
        if ((bar_read_arc_reset_scratch_status & STATUS_MESSAGE_QUEUED_MASK) == STATUS_MESSAGE_QUEUED_VAL) {
            uint32_t message_id = bar_read_arc_reset_scratch_status & 0xFF;
            log_debug(LogUMD, "MessageQueued error, message_id: {}", message_id);
            continue;
        }
        // Check for message being handled.
        if ((bar_read_arc_reset_scratch_status & STATUS_HANDLING_MESSAGE_MASK) == STATUS_HANDLING_MESSAGE_VAL) {
            uint32_t message_id = (bar_read_arc_reset_scratch_status >> 16) & 0xFF;
            log_debug(LogUMD, "HandlingMessage error, message_id: {}", message_id);
            continue;
        }
        // Message complete, response written into bar_read_arc_reset_scratch_status.
        if ((bar_read_arc_reset_scratch_status & STATUS_MESSAGE_COMPLETE_MASK) > STATUS_MESSAGE_COMPLETE_MIN) {
            return true;
        }
    }
}

bool WormholeTTDevice::is_hardware_hung() {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("is_hardware_hung is not applicable for JTAG communication type.");
    }

    volatile const void *addr = reinterpret_cast<const char *>(pci_device_->bar0_uc) +
                                (architecture_impl_->get_arc_axi_apb_peripheral_offset() +
                                 architecture_impl_->get_arc_reset_scratch_offset() + 6 * 4) -
                                pci_device_->bar0_uc_offset;
    std::uint32_t scratch_data = *reinterpret_cast<const volatile std::uint32_t *>(addr);

    return (scratch_data == HANG_READ_VALUE);
}

}  // namespace tt::umd
