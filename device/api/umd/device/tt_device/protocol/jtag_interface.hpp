/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

namespace tt::umd {

class JtagDevice;

/**
 * JtagInterface defines JTAG-specific operations beyond the basic DeviceProtocol.
 */
class JtagInterface {
public:
    virtual ~JtagInterface() = default;

    virtual JtagDevice* get_jtag_device() = 0;
};

}  // namespace tt::umd
