// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/kmd_device_event_listener.hpp"

#include <fmt/format.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <utility>

#include "pcie/kmd_device_event_reader.hpp"
#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<DeviceEventListener> DeviceEventListener::create(
    const std::map<DeviceEventKey, TTDevice*>& devices, std::set<DeviceEventKey>* uncovered_keys) {
    return std::make_unique<KmdDeviceEventListener>(devices, uncovered_keys);
}

KmdDeviceEventListener::KmdDeviceEventListener(
    const std::map<DeviceEventKey, TTDevice*>& devices, std::set<DeviceEventKey>* uncovered_keys) {
    std::set<DeviceEventKey> uncovered;
    for (const auto& [key, device] : devices) {
        UMD_ASSERT(
            device != nullptr, error::RuntimeError, fmt::format("DeviceEventListener device for key {} is null", key));

        std::unique_ptr<DeviceEventReader> reader = device->create_event_reader();
        if (dynamic_cast<KmdDeviceEventReader*>(reader.get()) == nullptr) {
            uncovered.insert(key);
            continue;
        }

        // The type was checked above, so this cast cannot fail and drop the released reader.
        std::unique_ptr<KmdDeviceEventReader> owned_reader(dynamic_cast<KmdDeviceEventReader*>(reader.release()));
        entries_.push_back(Entry{key, std::move(owned_reader)});
    }

    // Created last, since the destructor that closes it does not run when a constructor throws.
    wake_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    UMD_ASSERT(
        wake_fd_ >= 0, error::RuntimeError, fmt::format("eventfd() failed for device event listener errno: {}", errno));

    if (uncovered_keys != nullptr) {
        *uncovered_keys = std::move(uncovered);
    }
}

KmdDeviceEventListener::~KmdDeviceEventListener() noexcept { close(wake_fd_); }

std::vector<KeyedDeviceEventBatch> KmdDeviceEventListener::read(size_t max_events_per_device) {
    return read_events(max_events_per_device, -1);
}

std::vector<KeyedDeviceEventBatch> KmdDeviceEventListener::read_for(
    size_t max_events_per_device, std::chrono::milliseconds timeout) {
    return read_events(max_events_per_device, static_cast<int>(std::clamp<int64_t>(timeout.count(), 0, INT_MAX)));
}

std::vector<KeyedDeviceEventBatch> KmdDeviceEventListener::read_events(size_t max_events_per_device, int timeout_ms) {
    UMD_ASSERT(
        max_events_per_device > 0 && max_events_per_device <= UINT32_MAX,
        error::RuntimeError,
        fmt::format(
            "DeviceEventListener read max_events_per_device {} out of range [1, {}]",
            max_events_per_device,
            UINT32_MAX));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(timeout_ms, 0));
    std::vector<KeyedDeviceEventBatch> batches;

    while (true) {
        int wait_ms = timeout_ms;
        if (timeout_ms > 0) {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            wait_ms = static_cast<int>(std::max<int64_t>(remaining.count(), 0));
        }

        // A lost device's reader has already released its handle, and the device was dropped then.
        std::vector<tt_device_t*> handles;
        handles.reserve(entries_.size());
        for (const Entry& entry : entries_) {
            handles.push_back(entry.reader->kmd_handle());
        }
        std::vector<uint8_t> ready(entries_.size());
        uint32_t ready_count = 0;
        // A read that does not wait leaves any cancel for the next wait.
        const int wake_fd = (timeout_ms == 0) ? -1 : wake_fd_;
        int result = tt_device_wait_events(
            handles.data(), static_cast<uint32_t>(handles.size()), wait_ms, wake_fd, ready.data(), &ready_count);

        if (result == -ECANCELED) {
            // Draining the eventfd consumes every cancel made so far, so they end this wait alone.
            uint64_t cancels = 0;
            [[maybe_unused]] const ssize_t bytes_read = ::read(wake_fd_, &cancels, sizeof(cancels));
            return batches;
        }
        if (result == -EINTR) {
            return batches;
        }
        UMD_ASSERT(result == 0, error::RuntimeError, fmt::format("tt_device_wait_events() failed errno: {}", -result));

        for (size_t i = 0; i < entries_.size(); i++) {
            if (ready[i] == 0) {
                continue;
            }

            DeviceEventBatch batch = entries_[i].reader->read_for(max_events_per_device, std::chrono::milliseconds(0));
            if (!batch.events.empty() || batch.lost > 0 || batch.device_lost) {
                batches.push_back(KeyedDeviceEventBatch{entries_[i].key, std::move(batch)});
            }
        }

        entries_.erase(
            std::remove_if(
                entries_.begin(),
                entries_.end(),
                [](const Entry& entry) { return entry.reader->kmd_handle() == nullptr; }),
            entries_.end());

        // Should the devices reported ready have nothing to read after all, wait again while time remains.
        if (!batches.empty() || ready_count == 0 || timeout_ms == 0 ||
            (timeout_ms > 0 && std::chrono::steady_clock::now() >= deadline)) {
            return batches;
        }
    }
}

void KmdDeviceEventListener::cancel() noexcept {
    // Can fail only if the eventfd counter would overflow, which leaves it readable anyway.
    const uint64_t one = 1;
    [[maybe_unused]] const ssize_t bytes_written = write(wake_fd_, &one, sizeof(one));
}

}  // namespace tt::umd
