// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/tt_device.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"
#include "umd/device/tt_device/remote_blackhole_tt_device.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/lock_manager.hpp"

// TODO #526: This is a hack to allow UMD to use the NOC1 TLB.
bool umd_use_noc1 = false;

namespace tt::umd {

void TTDevice::use_noc1(bool use_noc1) { umd_use_noc1 = use_noc1; }

TTDevice::TTDevice(
    std::shared_ptr<PCIDevice> pci_device, std::unique_ptr<architecture_implementation> architecture_impl) :
    pci_device_(pci_device),
    communication_device_type_(IODeviceType::PCIe),
    communication_device_id_(pci_device_->get_device_num()),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    lock_manager.initialize_mutex(MutexType::TT_DEVICE_IO, get_communication_device_id());
}

TTDevice::TTDevice(
    std::shared_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    std::unique_ptr<architecture_implementation> architecture_impl) :
    jtag_device_(jtag_device),
    jlink_id_(jlink_id),
    communication_device_type_(IODeviceType::JTAG),
    communication_device_id_(jtag_device_->get_device_id(jlink_id_)),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    lock_manager.initialize_mutex(MutexType::TT_DEVICE_IO, get_communication_device_id(), IODeviceType::JTAG);
}

TTDevice::TTDevice() {}

TTDevice::TTDevice(std::unique_ptr<architecture_implementation> architecture_impl) :
    architecture_impl_(std::move(architecture_impl)), arch(architecture_impl_->get_architecture()) {}

void TTDevice::init_tt_device() {
    pre_init_hook();
    arc_messenger_ = ArcMessenger::create_arc_messenger(this);
    telemetry = ArcTelemetryReader::create_arc_telemetry_reader(this);
    firmware_info_provider = FirmwareInfoProvider::create_firmware_info_provider(this);
    wait_arc_core_start();
    post_init_hook();
}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(int device_number, IODeviceType device_type) {
    // TODO make abstract IO handler inside TTDevice.
    if (device_type == IODeviceType::JTAG) {
        auto jtag_device = JtagDevice::create();

        switch (jtag_device->get_jtag_arch(device_number)) {
            case ARCH::WORMHOLE_B0:
                return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(jtag_device, device_number));
            case ARCH::BLACKHOLE:
                TT_THROW("JTAG is not yet supported on Blackhole architecture.");
            default:
                return nullptr;
        }
    }

    auto pci_device = std::make_shared<PCIDevice>(device_number);

    switch (pci_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(pci_device));
        case ARCH::BLACKHOLE:
            return std::unique_ptr<BlackholeTTDevice>(new BlackholeTTDevice(pci_device));
        default:
            return nullptr;
    }
}

std::unique_ptr<TTDevice> TTDevice::create(
    std::unique_ptr<RemoteCommunication> remote_communication, eth_coord_t target_chip) {
    switch (remote_communication->get_local_device()->get_arch()) {
        case tt::ARCH::WORMHOLE_B0: {
            return std::unique_ptr<RemoteWormholeTTDevice>(
                new RemoteWormholeTTDevice(std::move(remote_communication), target_chip));
        }
        case tt::ARCH::BLACKHOLE: {
            return std::unique_ptr<RemoteBlackholeTTDevice>(
                new RemoteBlackholeTTDevice(std::move(remote_communication)));
        }
        default:
            throw std::runtime_error("Remote TTDevice creation is not supported for this architecture.");
    }
}

architecture_implementation *TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

std::shared_ptr<PCIDevice> TTDevice::get_pci_device() { return pci_device_; }

tt::ARCH TTDevice::get_arch() { return arch; }

void TTDevice::detect_hang_read(std::uint32_t data_read) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        // Jtag protocol uses different communication paths from pci therefore
        // there's no need to check hang which is in this case pci-specific.
        return;
    }
    if (data_read == HANG_READ_VALUE && is_hardware_hung()) {
        std::uint32_t scratch_data =
            *pci_device_->get_register_address<std::uint32_t>(architecture_impl_->get_read_checking_offset());

        throw std::runtime_error("Read 0xffffffff from PCIE: you should reset the board.");
    }
}

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc
void TTDevice::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_regs is not applicable for JTAG communication type.");
    }
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void TTDevice::write_regs(uint32_t byte_addr, uint32_t word_len, const void *data) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_regs is not applicable for JTAG communication type.");
    }
    volatile uint32_t *dest = pci_device_->get_register_address<uint32_t>(byte_addr);
    const uint32_t *src = reinterpret_cast<const uint32_t *>(data);

    write_regs(dest, src, word_len);
}

void TTDevice::read_regs(uint32_t byte_addr, uint32_t word_len, void *data) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("read_regs is not applicable for JTAG communication type.");
    }
    const volatile uint32_t *src = pci_device_->get_register_address<uint32_t>(byte_addr);
    uint32_t *dest = reinterpret_cast<uint32_t *>(data);

    while (word_len-- != 0) {
        uint32_t temp = *src++;
        memcpy(dest++, &temp, sizeof(temp));
    }
}

void TTDevice::memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("memcpy_to_device is not applicable for JTAG communication type.");
    }
    typedef std::uint32_t copy_t;

    // Start by aligning the destination (device) pointer. If needed, do RMW to fix up the
    // first partial word.
    volatile copy_t *dp;

    std::uintptr_t dest_addr = reinterpret_cast<std::uintptr_t>(dest);
    unsigned int dest_misalignment = dest_addr % sizeof(copy_t);

    if (dest_misalignment != 0) {
        // Read-modify-write for the first dest element.
        dp = reinterpret_cast<copy_t *>(dest_addr - dest_misalignment);

        copy_t tmp = *dp;

        auto leading_len = std::min(sizeof(tmp) - dest_misalignment, num_bytes);

        std::memcpy(reinterpret_cast<char *>(&tmp) + dest_misalignment, src, leading_len);
        num_bytes -= leading_len;
        src = static_cast<const char *>(src) + leading_len;

        *dp++ = tmp;

    } else {
        dp = static_cast<copy_t *>(dest);
    }

    // Copy the destination-aligned middle.
    const copy_t *sp = static_cast<const copy_t *>(src);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++) {
        *dp++ = *sp++;
    }

    // Finally copy any sub-word trailer, again RMW on the destination.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *dp;

        std::memcpy(&tmp, sp, trailing_len);

        *dp++ = tmp;
    }
}

void TTDevice::memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("memcpy_from_device is not applicable for JTAG communication type.");
    }
    typedef std::uint32_t copy_t;

    // Start by aligning the source (device) pointer.
    const volatile copy_t *sp;

    std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
    unsigned int src_misalignment = src_addr % sizeof(copy_t);

    if (src_misalignment != 0) {
        sp = reinterpret_cast<copy_t *>(src_addr - src_misalignment);

        copy_t tmp = *sp++;

        auto leading_len = std::min(sizeof(tmp) - src_misalignment, num_bytes);
        std::memcpy(dest, reinterpret_cast<char *>(&tmp) + src_misalignment, leading_len);
        num_bytes -= leading_len;
        dest = static_cast<char *>(dest) + leading_len;

    } else {
        sp = static_cast<const volatile copy_t *>(src);
    }

    // Copy the source-aligned middle.
    copy_t *dp = static_cast<copy_t *>(dest);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++) {
        *dp++ = *sp++;
    }

    // Finally copy any sub-word trailer.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *sp;
        std::memcpy(dp, &tmp, trailing_len);
    }
}

void TTDevice::write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t *buffer_addr) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_block is not applicable for JTAG communication type.");
    }
    void *dest = nullptr;
    if (pci_device_->bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        dest = reinterpret_cast<uint8_t *>(pci_device_->bar4_wc) + byte_addr;
    } else {
        dest = pci_device_->get_register_address<uint8_t>(byte_addr);
    }

    const void *src = reinterpret_cast<const void *>(buffer_addr);
    if (arch == tt::ARCH::WORMHOLE_B0) {
        memcpy_to_device(dest, src, num_bytes);
    } else {
        memcpy(dest, src, num_bytes);
    }
}

void TTDevice::read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t *buffer_addr) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("read_block is not applicable for JTAG communication type.");
    }
    void *src = nullptr;
    if (pci_device_->bar4_wc != nullptr && byte_addr >= BAR0_BH_SIZE) {
        byte_addr -= BAR0_BH_SIZE;
        src = reinterpret_cast<uint8_t *>(pci_device_->bar4_wc) + byte_addr;
    } else {
        src = pci_device_->get_register_address<uint8_t>(byte_addr);
    }

    void *dest = reinterpret_cast<void *>(buffer_addr);
    if (arch == tt::ARCH::WORMHOLE_B0) {
        memcpy_from_device(dest, src, num_bytes);
    } else {
        memcpy(dest, src, num_bytes);
    }

    if (num_bytes >= sizeof(std::uint32_t)) {
        detect_hang_read(*reinterpret_cast<std::uint32_t *>(dest));
    }
}

void TTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->read(jlink_id_, mem_ptr, core.x, core.y, addr, size, umd_use_noc1 ? 1 : 0);
        return;
    }
    auto lock = lock_manager.acquire_mutex(MutexType::TT_DEVICE_IO, get_pci_device()->get_device_num());
    uint8_t *buffer_addr = static_cast<uint8_t *>(mem_ptr);
    const uint32_t tlb_index = get_architecture_implementation()->get_reg_tlb();
    while (size > 0) {
        auto [mapped_address, tlb_size] = set_dynamic_tlb(tlb_index, core, addr, tlb_data::Strict);
        uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
        read_block(mapped_address, transfer_size, buffer_addr);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

void TTDevice::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(jlink_id_, mem_ptr, core.x, core.y, addr, size, umd_use_noc1 ? 1 : 0);
        return;
    }
    auto lock = lock_manager.acquire_mutex(MutexType::TT_DEVICE_IO, get_pci_device()->get_device_num());
    uint8_t *buffer_addr = (uint8_t *)(uintptr_t)(mem_ptr);
    const uint32_t tlb_index = get_architecture_implementation()->get_reg_tlb();

    while (size > 0) {
        auto [mapped_address, tlb_size] = set_dynamic_tlb(tlb_index, core, addr, tlb_data::Strict);
        uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
        write_block(mapped_address, transfer_size, buffer_addr);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

void TTDevice::write_tlb_reg(
    uint32_t byte_addr, uint64_t value_lower, uint64_t value_upper, uint32_t tlb_cfg_reg_size) {
    TT_ASSERT(
        (tlb_cfg_reg_size == 8) or (tlb_cfg_reg_size == 12),
        "Tenstorrent hardware supports only 64bit or 96bit TLB config regs");

    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_tlb_reg is not applicable for JTAG communication type.");
    }

    volatile uint64_t *dest_qw = pci_device_->get_register_address<uint64_t>(byte_addr);
    volatile uint32_t *dest_extra_dw = pci_device_->get_register_address<uint32_t>(byte_addr + 8);
#if defined(__ARM_ARCH) || defined(__riscv)
    // The store below goes through UC memory on x86, which has implicit ordering constraints with WC accesses.
    // ARM has no concept of UC memory. This will not allow for implicit ordering of this store wrt other memory
    // accesses. Insert an explicit full memory barrier for ARM. Do the same for RISC-V.
    tt_driver_atomics::mfence();
#endif
    *dest_qw = value_lower;
    if (tlb_cfg_reg_size > 8) {
        uint32_t *p_value_upper = reinterpret_cast<uint32_t *>(&value_upper);
        *dest_extra_dw = p_value_upper[0];
    }
    tt_driver_atomics::mfence();  // Otherwise subsequent WC loads move earlier than the above UC store to the TLB
                                  // register.
}

// Get TLB index (from zero), check if it's in 16MB, 2MB or 1MB TLB range, and dynamically program it.
dynamic_tlb TTDevice::set_dynamic_tlb(
    unsigned int tlb_index,
    tt_xy_pair start,
    tt_xy_pair end,
    std::uint64_t address,
    bool multicast,
    std::uint64_t ordering) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("set_dynamic_tlb is not applicable for JTAG communication type.");
    }
    if (multicast) {
        std::tie(start, end) = architecture_impl_->multicast_workaround(start, end);
    }

    log_trace(
        LogUMD,
        "set_dynamic_tlb with arguments: tlb_index = {}, start = ({}, {}), end = ({}, {}), address = 0x{:x}, "
        "multicast "
        "= {}, ordering = {}",
        tlb_index,
        start.x,
        start.y,
        end.x,
        end.y,
        address,
        multicast,
        (int)ordering);

    tlb_configuration tlb_config = architecture_impl_->get_tlb_configuration(tlb_index);
    std::uint32_t TLB_CFG_REG_SIZE_BYTES = architecture_impl_->get_tlb_cfg_reg_size_bytes();
    uint64_t tlb_address = address / tlb_config.size;
    uint32_t local_address = address % tlb_config.size;
    uint64_t tlb_base = tlb_config.base + (tlb_config.size * tlb_config.index_offset);
    uint32_t tlb_cfg_reg = tlb_config.cfg_addr + (TLB_CFG_REG_SIZE_BYTES * tlb_config.index_offset);

    std::pair<std::uint64_t, std::uint64_t> tlb_reg_config =
        tlb_data{
            .local_offset = tlb_address,
            .x_end = static_cast<uint64_t>(end.x),
            .y_end = static_cast<uint64_t>(end.y),
            .x_start = static_cast<uint64_t>(start.x),
            .y_start = static_cast<uint64_t>(start.y),
            .noc_sel = umd_use_noc1 ? 1U : 0,
            .mcast = multicast,
            .ordering = ordering,
            // TODO #2715: hack for Blackhole A0, will potentially be fixed in B0.
            // Using the same static vc for reads and writes through TLBs can hang the card. It doesn't even have to
            // be the same TLB. Dynamic vc should not have this issue. There might be a perf impact with using
            // dynamic vc.
            .static_vc = (arch == tt::ARCH::BLACKHOLE) ? false : true,
        }
            .apply_offset(tlb_config.offset);

    log_trace(
        LogUMD,
        "set_dynamic_tlb() with tlb_index: {} tlb_index_offset: {} dynamic_tlb_size: {}MB tlb_base: 0x{:x} "
        "tlb_cfg_reg: 0x{:x} to core ({},{})",
        tlb_index,
        tlb_config.index_offset,
        tlb_config.size / (1024 * 1024),
        tlb_base,
        tlb_cfg_reg,
        end.x,
        end.y);
    write_tlb_reg(tlb_cfg_reg, tlb_reg_config.first, tlb_reg_config.second, TLB_CFG_REG_SIZE_BYTES);

    return {tlb_base + local_address, tlb_config.size - local_address};
}

dynamic_tlb TTDevice::set_dynamic_tlb(
    unsigned int tlb_index, tt_xy_pair target, std::uint64_t address, std::uint64_t ordering) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("set_dynamic_tlb is not applicable for JTAG communication type.");
    }
    return set_dynamic_tlb(tlb_index, tt_xy_pair(0, 0), target, address, false, ordering);
}

dynamic_tlb TTDevice::set_dynamic_tlb_broadcast(
    unsigned int tlb_index, std::uint64_t address, tt_xy_pair start, tt_xy_pair end, std::uint64_t ordering) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("set_dynamic_tlb_broadcast is not applicable for JTAG communication type.");
    }
    // Issue a broadcast to cores included in the start (top left) and end (bottom right) grid
    return set_dynamic_tlb(tlb_index, start, end, address, true, ordering);
}

void TTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    throw std::runtime_error("configure_iatu_region is not implemented for this device");
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const uint32_t timeout_ms) {
    if (dram_channel >= architecture_impl_->get_dram_banks_number()) {
        throw std::runtime_error(fmt::format(
            "Invalid DRAM channel index {}, maximum index for given architecture is {}",
            dram_channel,
            architecture_impl_->get_dram_banks_number() - 1));
    }
    auto start = std::chrono::system_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status = get_dram_training_status();

        if (dram_training_status.empty()) {
            log_warning(LogUMD, "DRAM training status is not available, breaking the wait for DRAM training.");
            return;
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::FAIL) {
            throw std::runtime_error("DRAM training failed");
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::SUCCESS) {
            return;
        }

        auto end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms) {
            throw std::runtime_error(
                fmt::format("DRAM training for channel {} timed out after {} ms", dram_channel, timeout_ms));
            break;
        }
    }
}

void TTDevice::bar_write32(uint32_t addr, uint32_t data) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("bar_write32 is not applicable for JTAG communication type.");
    }
    if (addr < get_pci_device()->bar0_uc_offset) {
        write_block(addr, sizeof(data), reinterpret_cast<const uint8_t *>(&data));  // do we have to reinterpret_cast?
    } else {
        write_regs(addr, 1, &data);
    }
}

uint32_t TTDevice::bar_read32(uint32_t addr) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("bar_read32 is not applicable for JTAG communication type.");
    }
    uint32_t data;
    if (addr < get_pci_device()->bar0_uc_offset) {
        read_block(addr, sizeof(data), reinterpret_cast<uint8_t *>(&data));
    } else {
        read_regs(addr, 1, &data);
    }
    return data;
}

ArcMessenger *TTDevice::get_arc_messenger() const { return arc_messenger_.get(); }

ArcTelemetryReader *TTDevice::get_arc_telemetry_reader() const { return telemetry.get(); }

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { return firmware_info_provider.get(); }

semver_t TTDevice::get_firmware_version() { return get_firmware_info_provider()->get_firmware_version(); }

TTDevice::~TTDevice() {
    // Remote TTDevices actually use TTDevice from remote_communication object.
    // Even though they extend TTDevices, they don't actually act as TTDevices but
    // as wrappers around remote communication which contain TTDevices.
    // Therefore, they don't need to clear mutexes since they don't use them directly.
    // Moreover, if they would try to clear mutexes, it would cause errors since Remote TTDevice
    // didn't initialize its own PCIDevice which is needed to identify the right mutex.
    if (is_remote_tt_device) {
        return;
    }
    lock_manager.clear_mutex(MutexType::TT_DEVICE_IO, get_communication_device_id(), communication_device_type_);
}

void TTDevice::wait_for_non_mmio_flush() {}

bool TTDevice::is_remote() { return is_remote_tt_device; }

int TTDevice::get_communication_device_id() const { return communication_device_id_; }

IODeviceType TTDevice::get_communication_device_type() const { return communication_device_type_; }

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

uint64_t TTDevice::get_refclk_counter() {
    uint32_t high1_addr = 0, high2_addr = 0, low_addr = 0;
    read_from_arc(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    read_from_arc(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    read_from_arc(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    if (high2_addr > high1_addr) {
        read_from_arc(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    }
    return (static_cast<uint64_t>(high2_addr) << 32) | low_addr;
}

uint64_t TTDevice::get_board_id() { return get_firmware_info_provider()->get_board_id(); }

std::vector<DramTrainingStatus> TTDevice::get_dram_training_status() {
    if (!telemetry->is_entry_available(TelemetryTag::DDR_STATUS)) {
        return {};
    }

    std::vector<DramTrainingStatus> dram_training_status;
    const uint32_t num_dram_channels = architecture_impl_->get_dram_banks_number();
    // Format of the dram training status is as follows:
    // Each channel gets two bits in the 32-bit value (16 bits used). The lower bits are for lower channels.
    // Lower of the two bits is for training error and higher of the two bits is for training status.
    // Example: 0b 00 00 00 00 00 00 01 10
    // would mean that only channel 0 is trained, channel 1 has the error and other are not trained and don't have
    // errors. If some channel is harvested the bits are always going to be zero.
    for (uint32_t dram_channel = 0; dram_channel < num_dram_channels; dram_channel++) {
        dram_training_status.push_back(get_firmware_info_provider()->get_dram_training_status(dram_channel));
    }

    return dram_training_status;
}

double TTDevice::get_asic_temperature() { return get_firmware_info_provider()->get_asic_temperature(); }

uint8_t TTDevice::get_asic_location() { return get_firmware_info_provider()->get_asic_location(); }

ChipInfo TTDevice::get_chip_info() {
    ChipInfo chip_info;

    chip_info.noc_translation_enabled = get_noc_translation_enabled();
    chip_info.chip_uid.board_id = get_board_id();
    chip_info.board_type = get_board_type();
    chip_info.asic_location = get_asic_location();

    return chip_info;
}

uint32_t TTDevice::get_max_clock_freq() { return get_firmware_info_provider()->get_max_clock_freq(); }

uint32_t TTDevice::get_risc_reset_state(tt_xy_pair core) {
    uint32_t tensix_risc_state;
    read_from_device(&tensix_risc_state, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(uint32_t));

    return tensix_risc_state;
}

void TTDevice::set_risc_reset_state(tt_xy_pair core, const uint32_t risc_flags) {
    write_to_device(&risc_flags, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(uint32_t));
    tt_driver_atomics::sfence();
}

tt_xy_pair TTDevice::get_arc_core() const { return arc_core; }

}  // namespace tt::umd
