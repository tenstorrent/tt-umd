/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

class JtagDevice;

namespace tt::umd {

/**
 * JtagInterface defines JTAG-specific operations beyond the basic DeviceProtocol.
 */
class JtagInterface {
public:
    virtual ~JtagInterface() = default;

    virtual JtagDevice* get_jtag_device() = 0;
};

}  // namespace tt::umd
