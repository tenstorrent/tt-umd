// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>

#include "umd/device/arc/blackhole_arc_telemetry_reader.hpp"
#include "umd/device/arch/architecture_registers.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {
class JtagDevice;
class PCIDevice;
enum class IODeviceType;

class BlackholeTTDevice : public TTDevice {
public:
    ~BlackholeTTDevice() override;

    void configure_iatu_region(size_t region, uint64_t target, size_t region_size) override;

    uint32_t get_clock() override;

    uint32_t get_min_clock_freq() override;

    void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    explicit BlackholeTTDevice(std::unique_ptr<TTDeviceModel> model);

protected:
    virtual bool is_arc_available_over_axi();

private:
    const ArchitectureRegisters registers_ = get_architecture_registers(tt::ARCH::BLACKHOLE);

    int get_pcie_x_coordinate();

    static constexpr uint64_t ATU_OFFSET_IN_BH_BAR2 = 0x1000;
    std::set<size_t> iatu_regions_;
};

}  // namespace tt::umd
