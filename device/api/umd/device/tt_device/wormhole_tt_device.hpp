// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/arch/architecture_registers.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {
class JtagDevice;
class PCIDevice;
class RemoteCommunication;
enum class IODeviceType;

class WormholeTTDevice : public TTDevice {
public:
    void configure_iatu_region(size_t region, uint64_t target, size_t region_size) override;

    uint32_t get_clock() override;

    uint32_t get_min_clock_freq() override;

    void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    ~WormholeTTDevice() override = default;

    explicit WormholeTTDevice(std::unique_ptr<TTDeviceModel> model);

private:
    const ArchitectureRegisters registers_ = get_architecture_registers(tt::ARCH::WORMHOLE_B0);
};
}  // namespace tt::umd
