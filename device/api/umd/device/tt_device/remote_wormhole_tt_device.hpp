/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/chip/local_chip.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"

namespace tt::umd {

class RemoteWormholeTTDevice : public WormholeTTDevice {
public:
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_csm(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void noc_multicast_write(
        void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;

    void wait_for_non_mmio_flush() override;

    RemoteCommunication* get_remote_communication();

    /*
     * RemoteWormholeTTDevice uses RemoteCommunication and doesn't have an underlying I/O device,
     * so hang detection is done via the local TTDevice used by RemoteCommunication.
     */
    void detect_hang_read(std::uint32_t data_read) override;

    /*
     * RemoteWormholeTTDevice uses RemoteCommunication and doesn't have an underlying I/O device,
     * so hang detection is done via the local TTDevice used by RemoteCommunication.
     */
    bool is_hardware_hung() override;

private:
    RemoteWormholeTTDevice(std::unique_ptr<RemoteCommunication> remote_communication, bool allow_spi = false);

    /*
     * This is a constructor primarily used for JTAG to create a RemoteWormholeTTDevice
     * without an underlying communication device (pcie_device or jtag_device).
     * It was created as a workaround to allow RemoteWormholeTTDevice creation over JTAG.
     * It should not be used for PCIe as certain functionalities from base class rely on the presence of an underlying
     * communication device. Creating a RemoteWormholeTTDevice without an underlying communication device over PCIe
     * would require overriding several methods from the base class.
     * TODO: In the future, either remove this constructor or refactor the class hierarchy to better support PCIe use
     * case.
     */
    RemoteWormholeTTDevice(
        std::unique_ptr<RemoteCommunication> remote_communication, IODeviceType device_type, bool allow_spi = false);

    friend std::unique_ptr<TTDevice> TTDevice::create(
        std::unique_ptr<RemoteCommunication> remote_communication, bool allow_spi);

    std::unique_ptr<RemoteCommunication> remote_communication_;
};

}  // namespace tt::umd
