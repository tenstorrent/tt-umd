// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

namespace tt::umd {

// Hugepages must be 1GB in size
// It's important that this is 64 bits, so that it doesn't overflow when multiplied to 4.
inline constexpr uint64_t HUGEPAGE_REGION_SIZE = 1ULL << 30;  // 1GB

inline constexpr uint32_t MAX_HOST_MEM_CHANNELS = 4;

// Get number of 1GB host hugepages installed.
uint32_t get_num_hugepages();

// Dynamically figure out how many host memory channels (based on hugepages installed) for each device, based on arch.
uint32_t get_available_num_host_mem_channels(
    const uint32_t num_channels_per_device_target, const uint16_t device_id, const uint8_t revision_id);

// Describes the 1GB hugepage pool for an allocation failure: how many pages a new mapping can still get out of the
// total, and which hugetlbfs mounts hold files that keep pages out of the pool. Never throws.
std::string describe_hugepage_pool();

}  // namespace tt::umd
