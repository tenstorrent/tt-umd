// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd {

// State of an asynchronous DMA transfer, as reported by its check.
enum class DmaTransferState : uint8_t {
    IDLE,         // No transfer of the asked direction is in flight. The device can still be busy with the other one.
    IN_PROGRESS,  // A transfer of the asked direction is in flight.
    FINISHED,     // The calling thread's transfer completed. Reported once; the device is free again.
    ERROR,        // The calling thread's transfer did not complete in time. The device is free again.
};

}  // namespace tt::umd
