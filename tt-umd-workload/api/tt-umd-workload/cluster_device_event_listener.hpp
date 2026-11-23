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
// It behaves as the DeviceEventListener it reads through: it sees the records that arrive after it is created, a
// chip lost to a reset or removal is reported once with DeviceEventBatch::device_lost set and then left out, and
// cancel() may be called from any thread while another one is inside read(). Every other member must be used by one
// thread at a time, and the listener must not be destroyed while a read() is in progress.
//
// It reads through its own device handles, so it doesn't depend on the Cluster at runtime and may outlive it. Its
// chip IDs are that Cluster's, though, which a later Cluster may number differently, so stop using it together with
// that Cluster. Destroying the Cluster doesn't end a read() waiting at that point: cancel() it.
class ClusterDeviceEventListener {
public:
    // Returns a batch, with up to max_events_per_chip records, for each chip that has records this listener has not
    // read yet or that was lost. When there are none, waits for one. cancel() or a signal ends the wait early with no
    // batches. With no chips, only those end the wait.
    std::vector<ChipEventBatch> read(size_t max_events_per_chip);

    // Like read(), but waits at most timeout and returns no batches if nothing arrives. A zero or negative timeout
    // does not wait.
    std::vector<ChipEventBatch> read_for(size_t max_events_per_chip, std::chrono::milliseconds timeout);

    // Ends the current read() wait with no batches or, if no read() is waiting, the next wait. Each cancel ends one
    // wait, and cancels that arrive before that wait ends count as one.
    void cancel() noexcept;

private:
    friend class Cluster;

    // @param listener  Keyed by ChipId.
    explicit ClusterDeviceEventListener(std::unique_ptr<DeviceEventListener> listener);

    std::unique_ptr<DeviceEventListener> listener_;
};

}  // namespace tt::umd
