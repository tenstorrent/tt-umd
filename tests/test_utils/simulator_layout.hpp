// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdlib.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "tests/test_utils/fetch_local_files.hpp"

namespace tt::umd::test_utils {

// A scratch directory laid out the way a simulator ships: a TTSim build is a libttsim.so with an optional
// soc_descriptor.yaml beside it, an RTL build is a directory with an optional soc_descriptor.yaml inside. Removed with
// the object.
class SimulatorLayout {
public:
    SimulatorLayout() {
        std::string pattern = (std::filesystem::temp_directory_path() / "tt-umd-simulator-layout-XXXXXX").string();
        if (mkdtemp(pattern.data()) == nullptr) {
            throw std::filesystem::filesystem_error(
                "Cannot create a simulator layout directory", pattern, std::error_code(errno, std::generic_category()));
        }
        directory_ = pattern;
    }

    ~SimulatorLayout() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    SimulatorLayout(const SimulatorLayout&) = delete;
    SimulatorLayout& operator=(const SimulatorLayout&) = delete;

    // The directory, as an RTL simulator build would be passed.
    const std::filesystem::path& directory() const { return directory_; }

    // A placeholder libttsim.so in the directory, as a TTSim build would be passed; it is no loadable image.
    std::filesystem::path add_ttsim_library() const {
        const std::filesystem::path library = directory_ / "libttsim.so";
        std::ofstream(library).put('\0');
        return library;
    }

    // A copy of the TTSim image at image in the directory, as libttsim.so, without the files shipped beside image.
    std::filesystem::path add_ttsim_library(const std::filesystem::path& image) const {
        const std::filesystem::path library = directory_ / "libttsim.so";
        std::filesystem::copy_file(image, library);
        return library;
    }

    // Ships the repo's descriptors/soc_descs/<soc_descriptor_name> as the directory's soc_descriptor.yaml.
    std::filesystem::path add_soc_descriptor(const std::string& soc_descriptor_name) const {
        const std::filesystem::path soc_descriptor = directory_ / "soc_descriptor.yaml";
        std::filesystem::copy_file(get_soc_desc_path(soc_descriptor_name), soc_descriptor);
        return soc_descriptor;
    }

    // Ships yaml as the directory's cluster_descriptor.yaml, which declares the topology the simulator models.
    std::filesystem::path add_cluster_descriptor(const std::string& yaml) const {
        const std::filesystem::path cluster_descriptor = directory_ / "cluster_descriptor.yaml";
        std::ofstream(cluster_descriptor) << yaml;
        return cluster_descriptor;
    }

private:
    std::filesystem::path directory_;
};

}  // namespace tt::umd::test_utils
