// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "tt-umd/tt_device/device_event_reader.hpp"

struct tt_device_t;

namespace tt::umd {

// Reads a PCIe device's event records through KMD's TENSTORRENT_IOCTL_READ_EVENTS.
// Requires a KMD with that ioctl (PCIDevice::is_device_events_supported()).
class KmdDeviceEventReader final : public DeviceEventReader {
public:
    // @param pci_device_num  N in /dev/tenstorrent/N.
    explicit KmdDeviceEventReader(int pci_device_num);
    ~KmdDeviceEventReader() noexcept override;

    KmdDeviceEventReader(const KmdDeviceEventReader&) = delete;
    KmdDeviceEventReader& operator=(const KmdDeviceEventReader&) = delete;
    KmdDeviceEventReader(KmdDeviceEventReader&&) = delete;
    KmdDeviceEventReader& operator=(KmdDeviceEventReader&&) = delete;

    DeviceEventBatch read(size_t max_events) override;
    DeviceEventBatch read_for(size_t max_events, std::chrono::milliseconds timeout) override;
    bool is_firmware_attached() override;
    void cancel() noexcept override;

    // The KMD handle, for waiting on this reader together with others through tt_device_wait_events().
    // Null once the device is lost. The reader owns it: don't close it or read through it.
    tt_device_t* kmd_handle() const { return device_; }

private:
    void open_device();
    void close_device() noexcept;

    // timeout_ms is in tt_device_read_events() terms: negative for no limit.
    DeviceEventBatch read_events(size_t max_events, int timeout_ms);

    // A reset invalidates the handle and a removal deletes the device, so neither leaves anything
    // to read from: the handle is released and the reader reports the device as lost from then on.
    void mark_device_lost();

    std::string device_path_;
    tt_device_t* device_ = nullptr;  // Null once the device is lost.
    int wake_fd_ = -1;               // eventfd that cancel() makes readable to end a wait.
};

}  // namespace tt::umd
