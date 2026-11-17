// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/soc_arch_descriptor.hpp"

#include <string>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

SocArchDescriptor::SocArchDescriptor(tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

SocArchDescriptor::SocArchDescriptor(const std::string& soc_descriptor_path) { UMD_NOT_IMPLEMENTED(); }

void SocArchDescriptor::init() { UMD_NOT_IMPLEMENTED(); }

tt::ARCH SocArchDescriptor::get_arch_from_path(const std::string& soc_descriptor_path) { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair SocArchDescriptor::get_grid_size_from_path(const std::string& soc_descriptor_path) { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair SocArchDescriptor::calculate_grid_size(const std::vector<tt_xy_pair>& cores) { UMD_NOT_IMPLEMENTED(); }

void SocArchDescriptor::build_derived_data() { UMD_NOT_IMPLEMENTED(); }

void SocArchDescriptor::load_from_yaml(YAML::Node& device_descriptor_yaml) { UMD_NOT_IMPLEMENTED(); }

std::vector<tt_xy_pair> SocArchDescriptor::convert_to_tt_xy_pair(const std::vector<std::string>& core_strings) {
    UMD_NOT_IMPLEMENTED();
}

std::vector<std::vector<tt_xy_pair>> SocArchDescriptor::convert_dram_cores_from_yaml(
    YAML::Node& device_descriptor_yaml, const std::string& dram_core) {
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
