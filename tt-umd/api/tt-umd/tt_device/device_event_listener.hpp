// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "tt-umd/tt_device/device_event_reader.hpp"

namespace tt::umd {

class TTDevice;

// The caller's label for a device in a DeviceEventListener, such as an index or a chip ID. It is a plain
// value rather than a reference to the device, since the listener may outlive the device.
using DeviceEventKey = uint64_t;

// What one DeviceEventListener::read() got from one device.
struct KeyedDeviceEventBatch {
    DeviceEventKey key;      // The device's key, as passed to DeviceEventListener::create().
    DeviceEventBatch batch;  // As a DeviceEventReader of the device would have returned it.
};

// Pulls the event records of several devices through one wait. Created by DeviceEventListener::create().
//
// The devices are set at creation. Each is read as by its own DeviceEventReader: the listener sees the
// records that arrive after it is created, independently of any other reader or device handle, and
// learns from DeviceEventBatch::lost how many it missed by falling behind.
//
// A reset or removal of a device ends the reading of that device alone. The device's batch with
// DeviceEventBatch::device_lost set is returned once, after which the listener drops the device and
// carries on with the others. To read its events after a reset, create a new listener once it is back.
//
// cancel() may be called from any thread while another one is inside read(). Every other member must be
// used by one thread at a time, and the listener must not be destroyed while a read() is in progress:
// cancel() is the way to make that read() return.
class DeviceEventListener {
public:
    // Creates a listener for the devices, each reported under the key the caller gives it. A device may
    // appear under several keys. The listener keeps no reference to the devices.
    //
    // @param uncovered_keys  If not null, receives the keys of the devices whose model or KMD version does not
    //                        support reading events through this listener. Those devices are left out.
    static std::unique_ptr<DeviceEventListener> create(
        const std::map<DeviceEventKey, TTDevice*>& devices, std::set<DeviceEventKey>* uncovered_keys = nullptr);

    virtual ~DeviceEventListener() = default;

    // Returns a batch, with up to max_events_per_device records, for each device that has records this
    // listener has not read yet or that was lost. When there are none, waits for one. cancel() or a signal
    // ends the wait early with no batches. With no devices, only those end the wait.
    virtual std::vector<KeyedDeviceEventBatch> read(size_t max_events_per_device) = 0;

    // Like read(), but waits at most timeout and returns no batches if nothing arrives. A zero or negative
    // timeout does not wait.
    virtual std::vector<KeyedDeviceEventBatch> read_for(
        size_t max_events_per_device, std::chrono::milliseconds timeout) = 0;

    // Ends the current read() wait with no batches or, if no read() is waiting, the next wait. Each cancel
    // ends one wait, and cancels that arrive before that wait ends count as one. A read() that finds
    // records available returns them without waiting, leaving the cancel for the next wait.
    virtual void cancel() noexcept = 0;
};

}  // namespace tt::umd
