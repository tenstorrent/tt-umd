// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Ordinal of a host access resource over which the IP is reached: 0..N-1 within one IP.
 * Opaque: it does not correspond to a location in the IP.
 */
enum class AccessPointId : uint32_t {};

/**
 * Ordinal of a device (one ATT view) within one IP, 0..N-1. Not an mmio id, not a ChipId: Cluster assigns
 * a ChipId per (IP, IpDeviceId).
 */
enum class IpDeviceId : uint32_t {};

/**
 * Location of an access point. Each field is set once known.
 *
 * coord: the access point's tile, in the frame of the device it belongs to.
 * flat_address: the global address at which the host reaches the access point through the ATT;
 * distinct for every access point. Unset when the ATT is off (gasket mode) or when the host is
 * coupled to the access point directly.
 */
struct AccessPointLocation {
    std::optional<tt_xy_pair> coord;
    std::optional<uint64_t> flat_address;
};

/**
 * How an IP's access points split it into devices.
 *
 * A device is one view of the IP set by its Address Translation Table (ATT). The ATT is a table in
 * each NOC endpoint (tile and host bridge) that maps an address to a destination tile and a local
 * address: its mask entries match address ranges, and its endpoint table gives the tile each range
 * reaches. Programming the ATTs so a device's ranges reach only its own tiles splits one IP into
 * several devices; with the ATT off (gasket mode) the host addresses tiles directly by coordinate.
 *
 * The access points may be reached over one or more host connections (e.g. /dev/tenstorrent/<N>
 * fds, or one simulator run); mapping an AccessPointId to its connection is up to the implementation.
 * Current consumers assume one connection per IP. An implementation with several would need rework
 * of components tied to the fd, e.g. DMA pinning and sysmem, which are per connection.
 */
class IpLayout {
public:
    virtual ~IpLayout() = default;

    /** The access points are AccessPointId 0..get_num_access_points()-1. */
    virtual size_t get_num_access_points() const = 0;

    /** Device -> the access points that reach it. */
    virtual std::map<IpDeviceId, std::vector<AccessPointId>> get_devices() const = 0;

    virtual AccessPointLocation get_location(AccessPointId access_point) const = 0;
};

/**
 * Throws if @p layout has no devices, device ids that aren't 0..N-1, a device with no access points
 * or one listing an access point twice, an unknown access point, or an access point not owned by
 * exactly one device.
 */
void validate_ip_layout(const IpLayout& layout);

}  // namespace tt::umd
