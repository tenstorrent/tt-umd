/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

namespace tt::umd {

namespace wormhole {

enum WormholeDramTrainingStatus : uint8_t {
    TrainingNone,
    TrainingFail,
    TrainingPass,
    TrainingSkip,
    PhyOff,
    ReadEye,
    BistEye,
    CaDebug,
};

}  // namespace wormhole

}  // namespace tt::umd
