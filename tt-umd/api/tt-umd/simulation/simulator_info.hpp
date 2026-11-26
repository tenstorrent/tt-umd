// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

#include "tt-umd/types/arch.hpp"

namespace tt::umd {

/**
 * The architecture the simulator at simulator_path models: a TTSim .so, or an RTL simulator directory.
 *
 * It is the architecture the simulator's soc_descriptor.yaml declares when it ships one, else the one its libttsim
 * image reports. Answering for an image without a soc_descriptor.yaml runs the image, so the caller must make the first
 * call for such an image before any simulator is brought up in this process; the answer is kept for later calls.
 *
 * @throws error::RuntimeError if UMD was built without simulation support, if an RTL simulator ships no
 * soc_descriptor.yaml, if the image cannot be loaded or probed, or if the image reports a device that has no built-in
 * SoC descriptor.
 */
tt::ARCH get_simulator_arch(const std::filesystem::path& simulator_path);

/**
 * The soc_descriptor.yaml the simulator at simulator_path ships (beside a TTSim .so, or inside an RTL simulator
 * directory), or an empty string when it ships none. A Wormhole or Blackhole TTSim image needs none: it is described by
 * its architecture's built-in SoC descriptor.
 *
 * Only looks at the filesystem, so it answers the same in every build of UMD.
 */
std::string find_simulator_soc_descriptor_path(const std::filesystem::path& simulator_path);

}  // namespace tt::umd
