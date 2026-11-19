// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "tt-umd/tt_device/device_event_listener.hpp"

namespace tt::umd {

class KmdDeviceEventReader;

// Waits on the KMD handles of several KmdDeviceEventReaders at once through tt_device_wait_events(),
// then reads the ready ones through their readers.
class KmdDeviceEventListener final : public DeviceEventListener {
public:
    KmdDeviceEventListener();
    ~KmdDeviceEventListener() noexcept override;

    KmdDeviceEventListener(const KmdDeviceEventListener&) = delete;
    KmdDeviceEventListener& operator=(const KmdDeviceEventListener&) = delete;
    KmdDeviceEventListener(KmdDeviceEventListener&&) = delete;
    KmdDeviceEventListener& operator=(KmdDeviceEventListener&&) = delete;

    // Accepts the devices whose models create a KmdDeviceEventReader.
    bool add(TTDevice& device, uint64_t key) override;
    std::vector<KeyedDeviceEventBatch> read(size_t max_events_per_device) override;
    std::vector<KeyedDeviceEventBatch> read_for(
        size_t max_events_per_device, std::chrono::milliseconds timeout) override;
    void cancel() noexcept override;

private:
    struct Entry {
        uint64_t key;
        std::unique_ptr<KmdDeviceEventReader> reader;
    };

    // timeout_ms is in tt_device_wait_events() terms: negative for no limit.
    std::vector<KeyedDeviceEventBatch> read_events(size_t max_events_per_device, int timeout_ms);

    std::vector<Entry> entries_;  // Lost devices are dropped.
    int wake_fd_ = -1;            // eventfd that cancel() makes readable to end a wait.
};

}  // namespace tt::umd
