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

protected:
    explicit BlackholeTTDevice(std::unique_ptr<TTDeviceModel> model);

private:
    friend std::unique_ptr<TTDevice> TTDevice::create(
        int device_number,
        IODeviceType device_type,
        bool use_safe_api,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);

    static constexpr uint64_t ATU_OFFSET_IN_BH_BAR2 = 0x1000;
    std::set<size_t> iatu_regions_;
};

}  // namespace tt::umd
