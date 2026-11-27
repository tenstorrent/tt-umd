// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

#include "tt-umd/tt_device/device_event_listener.hpp"
#include "tt-umd/tt_device/device_event_reader.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class Cluster;

// What one ClusterDeviceEventListener::read() got from one chip.
struct ChipEventBatch {
    ChipId chip_id;          // The chip's ID in the Cluster that created the listener.
    DeviceEventBatch batch;  // As a DeviceEventReader of the chip would have returned it.
};

// Pulls the event records of a Cluster's chips through one wait, reporting each chip by its ChipId. Created by
// Cluster::create_event_listener().
//
// It behaves as the DeviceEventListener it reads through, including thread safety, with chips in place of devices
// and ChipIds in place of keys; see that class for each member.
//
// It reads through its own device handles, so it doesn't depend on the Cluster at runtime and may outlive it. Its
// chip IDs are that Cluster's, though, which a later Cluster may number differently, so stop using it together with
// that Cluster. Destroying the Cluster doesn't end a read() waiting at that point: cancel() it.
class ClusterDeviceEventListener {
public:
    std::vector<ChipEventBatch> read(size_t max_events_per_chip);
    std::vector<ChipEventBatch> read_for(size_t max_events_per_chip, std::chrono::milliseconds timeout);
    void cancel() noexcept;

private:
    friend class Cluster;

    // @param listener  Keyed by ChipId.
    explicit ClusterDeviceEventListener(std::unique_ptr<DeviceEventListener> listener);

    std::unique_ptr<DeviceEventListener> listener_;
};

}  // namespace tt::umd
