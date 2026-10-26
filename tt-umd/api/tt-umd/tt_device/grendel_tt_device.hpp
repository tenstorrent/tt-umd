// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"

namespace tt::umd {

class DeviceProtocol;
class FlatAddressTransport;
class GrendelNocAddressResolver;

/**
 * One Grendel device: a protocol, a resolver, and a flat-address transport chosen from the
 * descriptor's address space.
 *
 * Emulation, JTAG, and a later PCIe or FPGA protocol construct this. They do not implement
 * reads and writes themselves. A new way to reach the package adds a protocol and, when that
 * protocol does not already speak the address space, a FlatAddressTransport.
 */
class GrendelTTDevice : public TTDevice {
public:
    ~GrendelTTDevice() override;

    void read_from_device(
        void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id = NocId::DEFAULT_NOC) override;
    void write_to_device(
        const void* mem_ptr,
        CoreCoord core,
        uint64_t addr,
        size_t size,
        NocId noc_id = NocId::DEFAULT_NOC) override;
    void read_from_device_reg(
        void* mem_ptr,
        CoreCoord core,
        uint64_t addr,
        size_t size,
        NocId noc_id = NocId::DEFAULT_NOC,
        RegisterAccessWidth width = RegisterAccessWidth::BITS32) override;
    void write_to_device_reg(
        const void* mem_ptr,
        CoreCoord core,
        uint64_t addr,
        size_t size,
        NocId noc_id = NocId::DEFAULT_NOC,
        RegisterAccessWidth width = RegisterAccessWidth::BITS32) override;

    void noc_multicast_write(
        const void* src,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        uint64_t addr,
        NocId noc_id = NocId::DEFAULT_NOC) override;
    void noc_multicast_write(const void* src, size_t size, uint64_t addr, NocId noc_id = NocId::DEFAULT_NOC) override;

    void dma_write_to_core_range(
        const void* src,
        uint64_t dst_addr,
        size_t size,
        CoreCoord core_start,
        CoreCoord core_end,
        NocId noc_id = NocId::DEFAULT_NOC) override;

protected:
    GrendelTTDevice(
        std::unique_ptr<DeviceProtocol> protocol,
        const SocDescriptor& soc_descriptor,
        std::unique_ptr<FlatAddressTransport> address_transport);

    void read_flat_address(uint64_t addr, void* dst, size_t size) override;
    void write_flat_address(uint64_t addr, const void* src, size_t size) override;

private:
    // Tests read a flat address the resolver already produced. read_from_device would translate it again.
    friend struct GrendelTTDeviceTestAccess;

    uint64_t flat_address(CoreCoord core, uint64_t addr, NocId noc_id) const;

    std::unique_ptr<GrendelNocAddressResolver> address_resolver_;
    std::unique_ptr<FlatAddressTransport> address_transport_;
    std::mutex io_mutex_;
};

}  // namespace tt::umd
