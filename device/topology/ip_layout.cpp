// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/topology/ip_layout.hpp"

#include <fmt/format.h>

#include <map>
#include <set>
#include <vector>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

void validate_ip_layout(const IpLayout& layout) {
    const std::map<IpDeviceId, std::vector<AccessPointId>> devices = layout.get_devices();
    UMD_ASSERT(!devices.empty(), error::RuntimeError, "IP layout has no devices.");

    // Device ids are ordinals: the map is ordered, so they are 0..N-1 exactly when the last is N-1.
    UMD_ASSERT(
        static_cast<uint32_t>(devices.rbegin()->first) == devices.size() - 1,
        error::RuntimeError,
        fmt::format("IP layout device ids must be 0..{}.", devices.size() - 1));

    // Access point -> number of devices that own it.
    const size_t num_access_points = layout.get_num_access_points();
    std::vector<int> owners(num_access_points, 0);
    for (const auto& [device_id, access_points] : devices) {
        const uint32_t device = static_cast<uint32_t>(device_id);
        UMD_ASSERT(
            !access_points.empty(),
            error::RuntimeError,
            fmt::format("IP layout device {} has no access points.", device));
        std::set<uint32_t> listed;
        for (AccessPointId access_point : access_points) {
            const uint32_t id = static_cast<uint32_t>(access_point);
            UMD_ASSERT(
                id < num_access_points,
                error::RuntimeError,
                fmt::format("IP layout device {} uses unknown access point {}.", device, id));
            UMD_ASSERT(
                listed.insert(id).second,
                error::RuntimeError,
                fmt::format("IP layout device {} lists access point {} twice.", device, id));
            owners[id]++;
        }
    }

    for (uint32_t id = 0; id < num_access_points; id++) {
        UMD_ASSERT(
            owners[id] == 1,
            error::RuntimeError,
            fmt::format("IP layout access point {} is owned by {} devices, expected 1.", id, owners[id]));
    }
}

}  // namespace tt::umd
