// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace tt::umd {

// Per-transaction options applied to TLB-window IO. Currently only the AXI
// snoop bit (user_bits[8] / cce_cmd_snoop) is exposed; on silicon this struct
// is a no-op placeholder until more fields are wired up.
struct IoOptions {
    bool snoop = false;
};

}  // namespace tt::umd
