/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"

namespace tt::umd {
class MockChip : public Chip {
public:
    MockChip(tt_SocDescriptor soc_descriptor);
    bool is_mmio_capable() const override;

    void start_device() override;

    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done,
        uint32_t arg0,
        uint32_t arg1,
        uint32_t timeout_ms,
        uint32_t* return_3,
        uint32_t* return_4) override;
};
}  // namespace tt::umd
