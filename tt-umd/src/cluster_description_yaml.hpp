// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

#include "tt-umd/cluster_description.hpp"

namespace tt::umd {

// The cluster described by the cluster descriptor YAML file at path, before it is built into a ClusterDescriptor, so
// that a caller can adjust it first. ClusterDescriptor::create_from_yaml builds exactly this description.
ClusterDescription load_cluster_description_yaml(const std::filesystem::path &path);

// The cluster described by cluster descriptor YAML content, as load_cluster_description_yaml reads it from a file.
ClusterDescription parse_cluster_description_yaml(const std::string &cluster_descriptor_file_content);

}  // namespace tt::umd
