// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "tt-umd/utils/error.hpp"

// Body of a function this build does not support: Wormhole, Quasar, JTAG, remote (ETH-tunnelled) devices and
// the simulation transport. Callers get an error naming the feature instead of a link failure.
#define UMD_NOT_SUPPORTED(feature) \
    UMD_THROW(tt::umd::error::RuntimeError, std::string(feature) + " is not supported by this build.")

// Body of a function whose implementation has not been written yet.
// TODO: temporary - remove once no function uses it.
#define UMD_NOT_IMPLEMENTED() \
    UMD_THROW(tt::umd::error::RuntimeError, std::string(__PRETTY_FUNCTION__) + " is not implemented yet.")
