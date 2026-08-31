// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/blackhole_device_firmware.hpp"

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd {

// How this class picks a route: a non-null JtagInterface means the device is reached over JTAG,
// otherwise it is reached over PCIe. Inferring the route from which optional interface is present
// is sound because a TTDevice is built for exactly one communication protocol - reaching the same
// chip over both PCIe and JTAG today requires two TTDevice objects, so the two interfaces are never
// both handed to the same firmware object. If UMD ever supports using both protocols against a
// single device, this has to become an explicit protocol selection rather than a null check.

BlackholeDeviceFirmware::BlackholeDeviceFirmware(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    architecture_impl_(architecture_impl),
    arc_apb_(device_protocol, pcie_interface, jtag_interface) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "BlackholeDeviceFirmware requires a DeviceProtocol.");
    UMD_ASSERT(
        architecture_impl_ != nullptr,
        error::RuntimeError,
        "BlackholeDeviceFirmware requires an ArchitectureImplementation.");
    // The exactly-one-transport invariant that get_io_device_type() and the ARC APB routing rely on
    // is enforced by arc_apb_, which is constructed from the same two interfaces above.

    // Read after the checks above, not in the member initialiser list: that runs first, so a null
    // protocol faulted there before the assert could report it.
    device_id_ = device_protocol_->get_mmio_id();

    // Resolve both ARC coordinates once. The NOC translation state they depend on is fixed for the
    // device's lifetime and is read over BAR/JTAG, so this does not need the firmware to be up.
    const bool noc_translation_enabled = get_noc_translation_enabled();
    arc_core_noc0_ = blackhole::get_arc_core(noc_translation_enabled, /*use_noc1=*/false);
    arc_core_noc1_ = blackhole::get_arc_core(noc_translation_enabled, /*use_noc1=*/true);
}

IODeviceType BlackholeDeviceFirmware::get_io_device_type() const {
    return jtag_interface_ != nullptr ? IODeviceType::JTAG : IODeviceType::PCIe;
}

void BlackholeDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    wait_firmware_ready(timeout_ms, noc_id);
}

void BlackholeDeviceFirmware::wait_firmware_ready(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    // One throwaway read before the poll, so a dead ARC APB path faults on an access that is
    // clearly a probe rather than partway into the boot-status loop. This was
    // TTDevice::probe_arc(), moved here with its only caller.
    uint32_t dummy;
    read_from_arc_apb(&dummy, blackhole::ARC_RESET_SCRATCH_OFFSET, sizeof(dummy), noc_id);

    uint32_t arc_boot_status = 0;
    uint32_t arc_postcode = 0;
    uint32_t arc_error_status0 = 0;

    constexpr auto busy_poll_window = std::chrono::microseconds(1000);
    constexpr auto poll_interval = std::chrono::microseconds(10);
    const bool arc_core_started = utils::poll_until(
        [this, &arc_boot_status, &arc_postcode, &noc_id]() {
            read_from_arc_apb(&arc_boot_status, blackhole::SCRATCH_RAM_2, sizeof arc_boot_status, noc_id);
            read_from_arc_apb(&arc_postcode, blackhole::ARC_RESET_SCRATCH_OFFSET, sizeof arc_postcode, noc_id);
            return (arc_boot_status & 0x7) == 0x5;
        },
        timeout_ms,
        busy_poll_window,
        poll_interval);

    if (!arc_core_started) {
        read_from_arc_apb(&arc_error_status0, blackhole::SCRATCH_RAM_4, sizeof arc_error_status0, noc_id);
        UMD_THROW(
            error::FirmwareStartupError,
            get_io_device_type(),
            device_id_,
            tt::ARCH::BLACKHOLE,
            noc_id,
            get_firmware_noc_coord(noc_id),
            arc_boot_status,
            arc_postcode,
            timeout_ms,
            /*message_id=*/std::nullopt,
            arc_error_status0);
    }
}

bool BlackholeDeviceFirmware::get_noc_translation_enabled() const {
    uint32_t niu_cfg;
    if (get_io_device_type() == IODeviceType::JTAG) {
        niu_cfg = jtag_interface_->mmio_read32(blackhole::NIU_CFG_NOC0_ARC_ADDR);
    } else {
        niu_cfg = pcie_interface_->bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + 0x100);
    }
    return ((niu_cfg >> 14) & 0x1) != 0;
}

tt_xy_pair BlackholeDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1_ : arc_core_noc0_;
}

void BlackholeDeviceFirmware::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_apb_.read(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

}  // namespace tt::umd
