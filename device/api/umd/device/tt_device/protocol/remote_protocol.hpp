/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"

namespace tt::umd {

class RemoteProtocol : public DeviceProtocol {
public:
    RemoteProtocol() = default;
    virtual ~RemoteProtocol() = default;

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
};

}  // namespace tt::umd
