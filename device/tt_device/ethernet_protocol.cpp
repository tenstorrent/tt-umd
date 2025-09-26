/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/ethernet_protocol.hpp"

namespace tt::umd {

void EthernetProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {}

void EthernetProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {}

void EthernetProtocol::wait_for_non_mmio_flush() {}

bool EthernetProtocol::is_remote() { return false; }

}  // namespace tt::umd
