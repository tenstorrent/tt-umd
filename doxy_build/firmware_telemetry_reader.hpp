// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class FirmwareTelemetryReader {
public:
    virtual ~FirmwareTelemetryReader() = default;
    virtual uint32_t read_entry(uint8_t tag) = 0;
    virtual bool is_entry_available(uint8_t tag) = 0;
};

}  // namespace tt::umd
