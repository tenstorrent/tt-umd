// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "umd/device/topology/ip_layout.hpp"

namespace tt::umd {

/**
 * IpLayout of an RTL simulation/emulation build, read from <simulator_directory>/ip_layout.yaml.
 *
 * An access point's id is its position in the file's access_points list. A device's id is its `id`
 * field, or its position in the devices list when no device has one (either all devices have an id
 * or none do). An access point's host_access names its
 * socket (NNG_SOCKET_ADDR_<host_access>). soc_descriptor paths are relative to simulator_directory.
 */
class RtlSimIpLayout : public IpLayout {
public:
    static constexpr const char* FILE_NAME = "ip_layout.yaml";

    explicit RtlSimIpLayout(const std::filesystem::path& simulator_directory);

    size_t get_num_access_points() const override;

    std::map<IpDeviceId, std::vector<AccessPointId>> get_devices() const override;

    AccessPointLocation get_location(AccessPointId access_point) const override;

    const std::string& get_host_access(AccessPointId access_point) const;

    const std::filesystem::path& get_soc_descriptor(IpDeviceId device) const;

private:
    struct AccessPoint {
        std::string host_access;
        AccessPointLocation location;
    };

    std::vector<AccessPoint> access_points_;
    std::map<IpDeviceId, std::vector<AccessPointId>> devices_;
    std::map<IpDeviceId, std::filesystem::path> soc_descriptors_;
};

}  // namespace tt::umd
