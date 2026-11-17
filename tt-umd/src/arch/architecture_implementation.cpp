// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/architecture_implementation.hpp"

#include <memory>

#include "common/unsupported.hpp"

namespace tt::umd {

std::unique_ptr<ArchitectureImplementation> ArchitectureImplementation::create(tt::ARCH architecture) {
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
