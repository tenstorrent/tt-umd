/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <string>

#include "umd/device/types/cluster_descriptor_types.h"

namespace tt::umd {

// Get number of 1GB host hugepages installed.
uint32_t get_num_hugepages();

// Looks for hugetlbfs inside /proc/mounts matching desired pagesize (typically 1G)
std::string find_hugepage_dir(std::size_t pagesize);

// Open a file in <hugepage_dir> for the hugepage mapping.
// All processes operating on the same pipeline must agree on the file name.
// Today we assume there's only one pipeline running within the system.
// One hugepage per device such that each device gets unique memory.
int open_hugepage_file(const std::string &dir, chip_id_t physical_device_id, uint16_t channel);
}  // namespace tt::umd
