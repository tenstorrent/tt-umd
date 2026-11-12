// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// tt-kmd-lib device handle. Forward declared so this public header does not require tt-kmd-lib's
// include path, which UMD links privately.
struct tt_device_t;

namespace tt::umd {

// One device event record, raised by firmware or by KMD.
struct DeviceEvent {
    uint64_t seq;                  // KMD sequence number; consecutive per device, starting at 0 when KMD probes it.
    uint64_t host_time_ns;         // CLOCK_MONOTONIC when KMD received the record.
    uint64_t device_time;          // ARC reference clock since firmware boot, in 20 ns ticks; 0 for KMD types.
    uint16_t type;                 // Firmware type (below 0x8000) or a KMD type (0x8000 and up).
    uint16_t source;               // For an event raised by a tile, its translated NOC coordinates x | y << 6.
    uint32_t cause;                // Type-specific subcode.
    std::array<uint32_t, 4> data;  // Type-specific data; firmware records use data[0..2].
};

// The outcome of one DeviceEventReader::read().
struct DeviceEventBatch {
    std::vector<DeviceEvent> events;  // Oldest first.
    uint32_t lost = 0;                // Records KMD discarded before this reader read them, saturating at UINT32_MAX.
    bool device_reset = false;        // The device was reset since the previous read, which discards every record.
};

// Pulls the event records of one device from KMD.
//
// Each reader sees the records that arrive after it is constructed, independently of any other reader
// or device handle in this or another process: reading does not take records away from anyone else.
// KMD keeps only the most recent records, so a reader that falls behind loses the oldest ones and
// learns how many from DeviceEventBatch::lost.
//
// A device reset invalidates the reader's device handle. The reader then opens a new one and reports
// the reset in the next batch, so it keeps working across resets without being reconstructed.
//
// cancel() may be called from any thread while another one is inside read(). Every other member must
// be used by one thread at a time, and the reader must not be destroyed while a read() is in progress:
// cancel() is the way to make that read() return.
//
// Requires a KMD with TENSTORRENT_IOCTL_READ_EVENTS; otherwise every call throws.
class DeviceEventReader {
public:
    // @param pci_device_num  N in /dev/tenstorrent/N.
    explicit DeviceEventReader(int pci_device_num);
    ~DeviceEventReader() noexcept;

    DeviceEventReader(const DeviceEventReader&) = delete;
    DeviceEventReader& operator=(const DeviceEventReader&) = delete;
    DeviceEventReader(DeviceEventReader&&) = delete;
    DeviceEventReader& operator=(DeviceEventReader&&) = delete;

    // Returns up to max_events records this reader has not read yet. When there are none, waits for one
    // to arrive. A reset is reported without waiting, and cancel() or a signal ends the wait early with
    // an empty batch.
    DeviceEventBatch read(size_t max_events);

    // Like read(), but waits at most timeout and returns an empty batch if no record arrives. A zero or
    // negative timeout does not wait.
    DeviceEventBatch read_for(size_t max_events, std::chrono::milliseconds timeout);

    // Whether firmware publishes events. If it does not, only KMD's own records will arrive.
    bool is_firmware_attached();

    // Ends the current read() wait with an empty batch or, if no read() is waiting, the next wait. Each
    // cancel ends one wait, and cancels that arrive before that wait ends count as one. A read() that
    // finds records available returns them without waiting, leaving the cancel for the next wait.
    void cancel() noexcept;

private:
    void open_device();
    void close_device() noexcept;

    // timeout_ms is in tt_device_read_events() terms: negative for no limit.
    DeviceEventBatch read_events(size_t max_events, int timeout_ms);

    // Replaces the handle a reset invalidated, and records the reset for the next read() to report.
    void reopen_after_reset();

    std::string device_path_;
    tt_device_t* device_ = nullptr;
    bool post_reset_recovery_pending_ = false;
    int wake_fd_ = -1;  // eventfd that cancel() makes readable to end a wait.
};

}  // namespace tt::umd
