/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <cstdint>

typedef std::uint32_t DWORD;

struct TTDevice;

struct PCIdevice  {
	unsigned int id = 0;
	TTDevice *hdev = nullptr;
    unsigned int logical_id;
    std::uint16_t vendor_id;
    std::uint16_t device_id;
    std::uint16_t subsystem_vendor_id;
    std::uint16_t subsystem_id;
    std::uint16_t revision_id;

    // PCI bus identifiers
    DWORD dwDomain;
	DWORD dwBus;
    DWORD dwSlot;
    DWORD dwFunction;

	uint64_t BAR_addr;
	DWORD BAR_size_bytes;
};