// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/wormhole_arc_window.hpp"

#include <fmt/format.h>

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// How this class picks a route, mirroring WormholeTTDevice::read_from_arc_apb: a non-null
// RemoteInterface means the device is reached over ethernet through a gateway, a non-null
// JtagInterface means it is reached over JTAG, and otherwise it is reached over PCIe. Inferring the
// route from which optional interface is present is sound because a TTDevice is built for exactly
// one communication protocol. The constructor enforces that exactly one of them is given, so the
// route the null checks pick always has an interface behind it.

/* static */ WormholeArcWindow WormholeArcWindow::arc_apb(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) {
    static constexpr Config config{
        .name = "ARC APB",
        .noc_base_address = wormhole::ARC_APB_NOC_BASE_ADDRESS,
        .bar0_offset_start = wormhole::ARC_APB_BAR0_XBAR_OFFSET_START,
        // ARC_APB_ADDRESS_RANGE is END - START with an inclusive END, so it is the last valid
        // offset rather than the window size. The bound below is a size, hence the + 1.
        .size_bytes = wormhole::ARC_APB_ADDRESS_RANGE + 1,
        .content = Content::REGISTERS,
    };
    return WormholeArcWindow(config, device_protocol, pcie_interface, jtag_interface, remote_interface);
}

/* static */ WormholeArcWindow WormholeArcWindow::arc_csm(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) {
    static constexpr Config config{
        .name = "ARC CSM",
        .noc_base_address = wormhole::ARC_CSM_NOC_BASE_ADDRESS,
        .bar0_offset_start = wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START,
        .size_bytes = wormhole::ARC_CSM_ADDRESS_RANGE,
        .content = Content::MEMORY,
    };
    return WormholeArcWindow(config, device_protocol, pcie_interface, jtag_interface, remote_interface);
}

WormholeArcWindow::WormholeArcWindow(
    const Config& config,
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) :
    config_(config),
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    remote_interface_(remote_interface) {
    UMD_ASSERT(
        device_protocol_ != nullptr,
        error::RuntimeError,
        fmt::format("The {} window requires a DeviceProtocol.", config_.name));
    const int transports = (pcie_interface_ != nullptr) + (jtag_interface_ != nullptr) + (remote_interface_ != nullptr);
    UMD_ASSERT(
        transports == 1,
        error::RuntimeError,
        fmt::format(
            "The {} window requires exactly one of a PcieInterface, a JtagInterface or a RemoteInterface, since "
            "which one is present is how it picks the route for an access.",
            config_.name));
}

void WormholeArcWindow::check_access(uint64_t arc_addr_offset, size_t size) const {
    UMD_ASSERT(size != 0, error::RuntimeError, fmt::format("Zero-length {} access.", config_.name));

    // The JTAG and BAR routes move exactly one word whatever size the caller asked for, so anything
    // else is a caller error: a smaller size overruns mem_ptr, a larger one leaves it short. Both
    // were silent in WormholeTTDevice, where the accessors were private and every caller passed a
    // word.
    UMD_ASSERT(
        remote_interface_ != nullptr || size == sizeof(uint32_t),
        error::RuntimeError,
        fmt::format(
            "{} access over JTAG or the PCIe BAR must be {} bytes, got {}.", config_.name, sizeof(uint32_t), size));

    // size_bytes is the size of the window, not its last valid offset, so the whole transfer has to
    // fit: the last access that fits starts size bytes before the end. Checking only the first byte,
    // as WormholeTTDevice did, let a word access at the very end run past the window. The bound is a
    // subtraction rather than an addition so it cannot wrap.
    UMD_ASSERT(
        size <= config_.size_bytes && arc_addr_offset <= config_.size_bytes - size,
        error::RuntimeError,
        fmt::format(
            "{} access of {} bytes at offset {:#x} does not fit in the {:#x} byte window.",
            config_.name,
            size,
            arc_addr_offset,
            config_.size_bytes));
}

void WormholeArcWindow::read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    check_access(arc_addr_offset, size);

    const uint64_t noc_address = config_.noc_base_address + arc_addr_offset;
    if (remote_interface_ != nullptr) {
        if (config_.content == Content::MEMORY) {
            device_protocol_->read_data(mem_ptr, arc_core, noc_address, size, noc_id);
        } else {
            device_protocol_->read_ctrl(mem_ptr, arc_core, noc_address, size, noc_id);
        }
        return;
    }
    if (jtag_interface_ != nullptr) {
        device_protocol_->read_ctrl(mem_ptr, arc_core, noc_address, sizeof(uint32_t), noc_id);
        return;
    }
    auto result = pcie_interface_->bar_read32(config_.bar0_offset_start + arc_addr_offset);
    *(reinterpret_cast<uint32_t*>(mem_ptr)) = result;
}

void WormholeArcWindow::write(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    check_access(arc_addr_offset, size);

    const uint64_t noc_address = config_.noc_base_address + arc_addr_offset;
    if (remote_interface_ != nullptr) {
        if (config_.content == Content::MEMORY) {
            device_protocol_->write_data(mem_ptr, arc_core, noc_address, size, noc_id);
        } else {
            device_protocol_->write_ctrl(mem_ptr, arc_core, noc_address, size, noc_id);
        }
        return;
    }
    if (jtag_interface_ != nullptr) {
        device_protocol_->write_ctrl(mem_ptr, arc_core, noc_address, sizeof(uint32_t), noc_id);
        return;
    }
    pcie_interface_->bar_write32(
        config_.bar0_offset_start + arc_addr_offset, *(reinterpret_cast<const uint32_t*>(mem_ptr)));
}

}  // namespace tt::umd
