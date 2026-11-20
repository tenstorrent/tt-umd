// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/kmd_device_event_reader.hpp"

#include <fcntl.h>  // O_APPEND
#include <fmt/format.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <iterator>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

DeviceEvent to_device_event(const tt_event_t& event) {
    return DeviceEvent{
        event.seq,
        event.host_time_ns,
        event.device_time,
        event.type,
        event.source,
        event.cause,
        {event.data[0], event.data[1], event.data[2], event.data[3]}};
}

}  // namespace

KmdDeviceEventReader::KmdDeviceEventReader(int pci_device_num) :
    device_path_(fmt::format("/dev/tenstorrent/{}", pci_device_num)), wake_fd_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
    UMD_ASSERT(
        wake_fd_ >= 0, error::RuntimeError, fmt::format("eventfd() failed for device event reader errno: {}", errno));

    // The destructor does not run when a constructor throws.
    try {
        open_device();
    } catch (...) {
        close(wake_fd_);
        throw;
    }
}

KmdDeviceEventReader::~KmdDeviceEventReader() noexcept {
    close_device();
    close(wake_fd_);
}

void KmdDeviceEventReader::open_device() {
    // O_APPEND keeps a handle that only reads events from requesting high power on open.
    int result = tt_device_open(device_path_.c_str(), &device_, O_APPEND);
    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_device_open() failed for device event reader {} errno: {}", device_path_, -result));
}

void KmdDeviceEventReader::close_device() noexcept {
    if (device_ == nullptr) {
        return;
    }

    int result = tt_device_close(device_);
    if (result != 0) {
        log_warning(tt::LogUMD, "tt_device_close() failed for device event reader {} errno: {}", device_path_, -result);
    }
    device_ = nullptr;
}

void KmdDeviceEventReader::mark_device_lost() {
    log_debug(tt::LogUMD, "Device {} was reset or removed; its event reader stops reading", device_path_);
    close_device();
}

DeviceEventBatch KmdDeviceEventReader::read(size_t max_events) { return read_events(max_events, -1); }

DeviceEventBatch KmdDeviceEventReader::read_for(size_t max_events, std::chrono::milliseconds timeout) {
    return read_events(max_events, static_cast<int>(std::clamp<int64_t>(timeout.count(), 0, INT_MAX)));
}

DeviceEventBatch KmdDeviceEventReader::read_events(size_t max_events, int timeout_ms) {
    UMD_ASSERT(
        max_events > 0 && max_events <= UINT32_MAX,
        error::RuntimeError,
        fmt::format("DeviceEventReader read max_events {} out of range [1, {}]", max_events, UINT32_MAX));

    DeviceEventBatch batch;
    if (device_ == nullptr) {
        batch.device_lost = true;
        return batch;
    }

    std::vector<tt_event_t> events(max_events);
    uint32_t count = 0;
    uint32_t lost = 0;
    int result = tt_device_read_events(
        device_, events.data(), static_cast<uint32_t>(max_events), 0, timeout_ms, wake_fd_, &count, &lost, nullptr);

    // These leave count at 0. A reset or removal leaves nothing more to read, while a cancel or a signal
    // interrupted only the wait.
    if (result == -ENODEV) {
        mark_device_lost();
        batch.device_lost = true;
    } else if (result == -ECANCELED) {
        // Draining the eventfd consumes every cancel made so far, so they end this wait alone.
        uint64_t cancels = 0;
        [[maybe_unused]] const ssize_t bytes_read = ::read(wake_fd_, &cancels, sizeof(cancels));
    } else if (result != -EINTR) {
        UMD_ASSERT(
            result == 0,
            error::RuntimeError,
            fmt::format("tt_device_read_events() failed for {} errno: {}", device_path_, -result));
    }

    batch.events.reserve(count);
    std::transform(events.begin(), events.begin() + count, std::back_inserter(batch.events), to_device_event);
    batch.lost = lost;
    return batch;
}

bool KmdDeviceEventReader::is_firmware_attached() {
    if (device_ == nullptr) {
        return false;
    }

    uint32_t count = 0;
    int fw_attached = 0;
    int result = tt_device_read_events(device_, nullptr, 0, 0, 0, -1, &count, nullptr, &fw_attached);

    if (result == -ENODEV) {
        mark_device_lost();
        return false;
    }

    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_device_read_events() failed for {} errno: {}", device_path_, -result));

    return fw_attached != 0;
}

void KmdDeviceEventReader::cancel() noexcept {
    // Can fail only if the eventfd counter would overflow, which leaves it readable anyway.
    const uint64_t one = 1;
    [[maybe_unused]] const ssize_t bytes_written = write(wake_fd_, &one, sizeof(one));
}

}  // namespace tt::umd
