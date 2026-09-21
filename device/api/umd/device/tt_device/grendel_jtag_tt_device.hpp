// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "umd/device/coordinates/grendel_noc_address_resolver.hpp"
#include "umd/device/tt_device/protocol/grendel_jtag_protocol.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

class SocDescriptor;

/**
 * A pre-initialized Grendel package reached through chippy JTAG2AXI/OpenOCD.
 *
 * The OpenOCD process and fabric bring-up are owned by the caller. This device only attaches to
 * the TCL RPC endpoint and performs local-AXI reads/writes; it never resets or tears down the part.
 */
class GrendelJtagTTDevice : public TTDevice {
public:
    static std::unique_ptr<GrendelJtagTTDevice> create(
        const SocDescriptor& soc_descriptor,
        const std::string& host,
        uint16_t port = 6666,
        uint32_t chiplet_number = 0,
        GrendelJtagTransportVersion version = GrendelJtagTransportVersion::V2);

    ~GrendelJtagTTDevice() override;

    void read_from_device(
        void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id = NocId::DEFAULT_NOC) override;
    void write_to_device(
        const void* mem_ptr,
        CoreCoord core,
        uint64_t addr,
        size_t size,
        NocId noc_id = NocId::DEFAULT_NOC) override;

    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;
    void write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;
    uint32_t get_clock() override;
    uint32_t get_min_clock_freq() override;

private:
    GrendelJtagTTDevice(
        const SocDescriptor& soc_descriptor, std::unique_ptr<GrendelJtagProtocol> protocol);

    std::unique_ptr<GrendelNocAddressResolver> address_resolver_;
    std::mutex io_mutex_;
};

}  // namespace tt::umd
