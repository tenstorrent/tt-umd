// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace tt::umd::wormhole {

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

}  // namespace tt::umd::wormhole
