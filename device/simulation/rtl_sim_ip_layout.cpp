// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/rtl_sim_ip_layout.hpp"

#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <cstdint>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

RtlSimIpLayout::RtlSimIpLayout(const std::filesystem::path& simulator_directory) {
    const std::filesystem::path path = simulator_directory / FILE_NAME;
    UMD_ASSERT(std::filesystem::exists(path), error::RuntimeError, fmt::format("{} not found.", path.string()));

    const YAML::Node yaml = YAML::LoadFile(path.string());

    size_t index = 0;
    for (const YAML::Node& node : yaml["access_points"]) {
        UMD_ASSERT(
            node["host_access"],
            error::RuntimeError,
            fmt::format("{}: access point {} has no host_access.", path.string(), index));
        index++;
        AccessPoint access_point;
        access_point.host_access = node["host_access"].as<std::string>();
        if (node["coord"]) {
            access_point.location.coord = tt_xy_pair(node["coord"][0].as<size_t>(), node["coord"][1].as<size_t>());
        }
        if (node["flat_address"]) {
            access_point.location.flat_address = node["flat_address"].as<uint64_t>();
        }
        access_points_.push_back(access_point);
    }

    // Either every device states its id or none does; mixing them would let a positional id
    // collide with an explicit one.
    size_t with_id = 0;
    for (const YAML::Node& node : yaml["devices"]) {
        with_id += node["id"] ? 1 : 0;
    }
    UMD_ASSERT(
        with_id == 0 || with_id == yaml["devices"].size(),
        error::RuntimeError,
        fmt::format("{}: give every device an id, or none.", path.string()));

    uint32_t position = 0;
    for (const YAML::Node& node : yaml["devices"]) {
        const IpDeviceId device = static_cast<IpDeviceId>(node["id"] ? node["id"].as<uint32_t>() : position);
        UMD_ASSERT(
            node["soc_descriptor"],
            error::RuntimeError,
            fmt::format("{}: device {} has no soc_descriptor.", path.string(), static_cast<uint32_t>(device)));
        const std::filesystem::path soc_descriptor = simulator_directory / node["soc_descriptor"].as<std::string>();
        UMD_ASSERT(
            std::filesystem::exists(soc_descriptor),
            error::RuntimeError,
            fmt::format(
                "{}: device {}'s soc_descriptor {} not found.",
                path.string(),
                static_cast<uint32_t>(device),
                soc_descriptor.string()));
        position++;

        std::vector<AccessPointId> access_points;
        for (const YAML::Node& id : node["access_points"]) {
            access_points.push_back(static_cast<AccessPointId>(id.as<uint32_t>()));
        }
        UMD_ASSERT(
            devices_.emplace(device, access_points).second,
            error::RuntimeError,
            fmt::format("{} lists device {} twice.", path.string(), static_cast<uint32_t>(device)));
        soc_descriptors_.emplace(device, soc_descriptor);
    }

    validate_ip_layout(*this);
}

size_t RtlSimIpLayout::get_num_access_points() const { return access_points_.size(); }

std::map<IpDeviceId, std::vector<AccessPointId>> RtlSimIpLayout::get_devices() const { return devices_; }

AccessPointLocation RtlSimIpLayout::get_location(AccessPointId access_point) const {
    return access_points_.at(static_cast<uint32_t>(access_point)).location;
}

const std::string& RtlSimIpLayout::get_host_access(AccessPointId access_point) const {
    return access_points_.at(static_cast<uint32_t>(access_point)).host_access;
}

const std::filesystem::path& RtlSimIpLayout::get_soc_descriptor(IpDeviceId device) const {
    return soc_descriptors_.at(device);
}

}  // namespace tt::umd
