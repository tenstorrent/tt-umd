/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/remote_protocol.hpp"

namespace tt::umd {

void RemoteProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {}

void RemoteProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {}

}  // namespace tt::umd
