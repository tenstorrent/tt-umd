// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/ip_block_layout.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

void validate_ip_block_layout(const IpBlockLayout& layout) {
    const size_t num_access_points = layout.get_num_access_points();
    const std::map<IpDeviceId, std::vector<AccessPointId>> devices = layout.get_devices();
    UMD_ASSERT(!devices.empty(), error::RuntimeError, "IP block layout has no devices.");

    // Owning device of each access point, filled as the devices are walked.
    std::vector<std::optional<uint32_t>> owners(num_access_points);

    uint32_t expected_device_id = 0;
    for (const auto& [device_id, access_points] : devices) {
        const uint32_t device = static_cast<uint32_t>(device_id);
        UMD_ASSERT(
            device == expected_device_id,
            error::RuntimeError,
            fmt::format(
                "IP block layout device ids are not 0..N-1: expected {}, found {}.", expected_device_id, device));
        expected_device_id++;

        UMD_ASSERT(
            !access_points.empty(),
            error::RuntimeError,
            fmt::format("IP block layout device {} has no access points.", device));

        std::set<uint32_t> seen;
        for (const AccessPointId access_point_id : access_points) {
            const uint32_t access_point = static_cast<uint32_t>(access_point_id);
            UMD_ASSERT(
                seen.insert(access_point).second,
                error::RuntimeError,
                fmt::format("IP block layout device {} lists access point {} twice.", device, access_point));
            UMD_ASSERT(
                access_point < num_access_points,
                error::RuntimeError,
                fmt::format(
                    "IP block layout device {} lists access point {}, but there are only {}.",
                    device,
                    access_point,
                    num_access_points));
            UMD_ASSERT(
                !owners[access_point].has_value(),
                error::RuntimeError,
                fmt::format(
                    "IP block layout access point {} is owned by both device {} and device {}.",
                    access_point,
                    owners[access_point].value(),
                    device));
            owners[access_point] = device;
        }
    }

    for (size_t access_point = 0; access_point < num_access_points; access_point++) {
        UMD_ASSERT(
            owners[access_point].has_value(),
            error::RuntimeError,
            fmt::format("IP block layout access point {} is not owned by any device.", access_point));
    }
}

}  // namespace tt::umd
