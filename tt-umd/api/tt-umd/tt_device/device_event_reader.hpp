// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

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
    uint32_t lost = 0;                // Records discarded before this reader read them, saturating at UINT32_MAX.
    bool device_lost = false;         // The device was reset or removed; see DeviceEventReader.
};

// Pulls the event records of one device. Created by TTDevice::create_event_reader().
//
// Each reader sees the records that arrive after it is created, independently of any other reader
// or device handle in this or another process: reading does not take records away from anyone else.
// Only the most recent records are kept, so a reader that falls behind loses the oldest ones and
// learns how many from DeviceEventBatch::lost.
//
// A reset or removal of the device ends the reader. The batch that observes it, and every later one,
// has DeviceEventBatch::device_lost set and is returned without waiting, so the caller must stop
// reading once it sees it. To read the device's events after a reset, create a new reader once the
// device is back.
//
// cancel() may be called from any thread while another one is inside read(). Every other member must
// be used by one thread at a time, and the reader must not be destroyed while a read() is in progress:
// cancel() is the way to make that read() return.
class DeviceEventReader {
public:
    virtual ~DeviceEventReader() = default;

    // Returns up to max_events records this reader has not read yet. When there are none, waits for one
    // to arrive. A lost device is reported without waiting, and cancel() or a signal ends the wait
    // early with an empty batch.
    virtual DeviceEventBatch read(size_t max_events) = 0;

    // Like read(), but waits at most timeout and returns an empty batch if no record arrives. A zero or
    // negative timeout does not wait.
    virtual DeviceEventBatch read_for(size_t max_events, std::chrono::milliseconds timeout) = 0;

    // Whether firmware publishes events. If it does not, only KMD's own records will arrive. False once
    // the device is lost.
    virtual bool is_firmware_attached() = 0;

    // Ends the current read() wait with an empty batch or, if no read() is waiting, the next wait. Each
    // cancel ends one wait, and cancels that arrive before that wait ends count as one. A read() that
    // finds records available returns them without waiting, leaving the cancel for the next wait.
    virtual void cancel() noexcept = 0;
};

}  // namespace tt::umd
