// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <tt-logger/tt-logger.hpp>

#include "tt-umd/utils/error.hpp"

// Body of a function this build does not support: Wormhole, Quasar, JTAG, remote (ETH-tunnelled) devices and
// the simulation transport. Callers get an error naming the feature instead of a link failure.
#define UMD_NOT_SUPPORTED(feature) \
    UMD_THROW(tt::umd::error::RuntimeError, std::string(feature) + " is not supported by this build.")

namespace tt::umd {

// Thrown by a function whose implementation has not been written yet. Deliberately not a std::exception, so that
// neither a negative test assertion nor a catch (const std::exception &) fallback mistakes a stub for real behaviour.
struct NotImplementedYet {
    const char *function;
};

}  // namespace tt::umd

// Body of a function whose implementation has not been written yet. The function is logged because gtest reports
// only "unknown C++ exception" for a type it does not know.
// TODO: temporary - remove once no function uses it.
#define UMD_NOT_IMPLEMENTED()                                                     \
    do {                                                                          \
        log_error(tt::LogUMD, "{} is not implemented yet.", __PRETTY_FUNCTION__); \
        throw ::tt::umd::NotImplementedYet{__PRETTY_FUNCTION__};                  \
    } while (false)
