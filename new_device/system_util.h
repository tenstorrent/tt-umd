/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "common_types.h"

#include <string>
#include <vector>

#include <dirent.h>


namespace tt::umd {

std::string find_hugepage_dir(std::size_t pagesize);

bool is_char_dev(const dirent *ent, const char *parent_dir);

// brosko rename
std::vector<chip_id_t> ttkmd_scan();

// brosko move other system stuff here as well, all the headers in pci_device.h and cpp 
int find_device(const uint16_t device_id);

void print_file_contents(std::string filename, std::string hint = "");

uint32_t get_num_hugepages();

}  // namespace tt::umd