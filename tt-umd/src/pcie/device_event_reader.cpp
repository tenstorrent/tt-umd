// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/device_event_reader.hpp"

#include <fcntl.h>  // O_APPEND
#include <fmt/format.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <iterator>
#include <tt-logger/tt-logger.hpp>

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

DeviceEventReader::DeviceEventReader(int pci_device_num) :
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

DeviceEventReader::~DeviceEventReader() noexcept {
    close_device();
    close(wake_fd_);
}

void DeviceEventReader::open_device() {
    // O_APPEND keeps a handle that only reads events from requesting high power on open.
    int result = tt_device_open(device_path_.c_str(), &device_, O_APPEND);
    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_device_open() failed for device event reader {} errno: {}", device_path_, -result));
}

void DeviceEventReader::close_device() noexcept {
    if (device_ == nullptr) {
        return;
    }

    int result = tt_device_close(device_);
    if (result != 0) {
        log_warning(tt::LogUMD, "tt_device_close() failed for device event reader {} errno: {}", device_path_, -result);
    }
    device_ = nullptr;
}

void DeviceEventReader::reopen_after_reset() {
    log_debug(tt::LogUMD, "Device {} was reset; reopening its event reader handle", device_path_);
    close_device();
    open_device();
    reset_pending_ = true;
}

DeviceEventBatch DeviceEventReader::read(size_t max_events, std::chrono::milliseconds timeout) {
    UMD_ASSERT(
        max_events > 0 && max_events <= UINT32_MAX,
        error::RuntimeError,
        fmt::format("DeviceEventReader::read() max_events {} out of range [1, {}]", max_events, UINT32_MAX));

    // tt-kmd-lib takes a negative timeout as no limit.
    const int timeout_ms =
        timeout == WAIT_FOREVER ? -1 : static_cast<int>(std::clamp<int64_t>(timeout.count(), 0, INT_MAX));
    std::vector<tt_event_t> events(max_events);
    uint32_t count = 0;
    uint32_t lost = 0;
    int result = tt_device_read_events(
        device_, events.data(), static_cast<uint32_t>(max_events), 0, timeout_ms, wake_fd_, &count, &lost, nullptr);

    // These leave count at 0. A reset already discarded every record, so there is nothing to read on the new
    // handle yet, while a cancel or a signal interrupted only the wait.
    if (result == -ENODEV) {
        reopen_after_reset();
    } else if (result == -ECANCELED) {
        // Draining the eventfd consumes every cancel made so far, so they end this wait alone.
        uint64_t cancels = 0;
        (void)::read(wake_fd_, &cancels, sizeof(cancels));
    } else if (result != -EINTR) {
        UMD_ASSERT(
            result == 0,
            error::RuntimeError,
            fmt::format("tt_device_read_events() failed for {} errno: {}", device_path_, -result));
    }

    DeviceEventBatch batch;
    batch.events.reserve(count);
    std::transform(events.begin(), events.begin() + count, std::back_inserter(batch.events), to_device_event);
    batch.lost = lost;
    batch.device_reset = reset_pending_;
    reset_pending_ = false;
    return batch;
}

bool DeviceEventReader::is_firmware_attached() {
    uint32_t count = 0;
    int fw_attached = 0;
    int result = tt_device_read_events(device_, nullptr, 0, 0, 0, -1, &count, nullptr, &fw_attached);

    if (result == -ENODEV) {
        reopen_after_reset();
        result = tt_device_read_events(device_, nullptr, 0, 0, 0, -1, &count, nullptr, &fw_attached);
    }

    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_device_read_events() failed for {} errno: {}", device_path_, -result));

    return fw_attached != 0;
}

void DeviceEventReader::cancel() noexcept {
    // Can fail only if the eventfd counter would overflow, which leaves it readable anyway.
    const uint64_t one = 1;
    (void)write(wake_fd_, &one, sizeof(one));
}

}  // namespace tt::umd
