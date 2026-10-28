// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace tt::umd {

/**
 * Open device `device_id` with `open_device`. When `skip_failed` is false an open that throws propagates, as one
 * unusable device makes a cluster that needs every device unusable. When it is true, a failed open returns
 * std::nullopt and records its error message in `failed_opens` (device id -> message), so the caller can go on
 * with the other devices and name this one.
 */
template <typename Device>
std::optional<Device> open_device_or_skip(
    int device_id,
    bool skip_failed,
    const std::function<Device(int)>& open_device,
    std::map<int, std::string>& failed_opens) {
    try {
        return open_device(device_id);
    } catch (const std::exception& e) {
        if (!skip_failed) {
            throw;
        }
        failed_opens[device_id] = e.what();
        return std::nullopt;
    }
}

}  // namespace tt::umd
