// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd-workload/cluster_device_event_listener.hpp"

#include <utility>

namespace tt::umd {

namespace {

std::vector<ChipEventBatch> to_chip_event_batches(std::vector<KeyedDeviceEventBatch>&& keyed_batches) {
    std::vector<ChipEventBatch> batches;
    batches.reserve(keyed_batches.size());
    for (KeyedDeviceEventBatch& keyed_batch : keyed_batches) {
        batches.push_back(ChipEventBatch{static_cast<ChipId>(keyed_batch.key), std::move(keyed_batch.batch)});
    }
    return batches;
}

}  // namespace

ClusterDeviceEventListener::ClusterDeviceEventListener(std::unique_ptr<DeviceEventListener> listener) :
    listener_(std::move(listener)) {}

std::vector<ChipEventBatch> ClusterDeviceEventListener::read(size_t max_events_per_chip) {
    return to_chip_event_batches(listener_->read(max_events_per_chip));
}

std::vector<ChipEventBatch> ClusterDeviceEventListener::read_for(
    size_t max_events_per_chip, std::chrono::milliseconds timeout) {
    return to_chip_event_batches(listener_->read_for(max_events_per_chip, timeout));
}

void ClusterDeviceEventListener::cancel() noexcept { listener_->cancel(); }

}  // namespace tt::umd
