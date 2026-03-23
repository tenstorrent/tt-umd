// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd {

// NOC Identifiers that can be selected when communicating with the device.
enum class NocId : uint8_t { DEFAULT_NOC = 0, NOC0 = 0, NOC1 = 1, SYSTEM_NOC = 2 };

// Set the NocId for the current thread.
// All subsequent device communications from this thread will use the selected NocId.
void set_thread_noc_id(NocId noc_id);

// Helper RAII class to switch NocId for the current thread within a scope.
class NocIdSwitcher {
public:
    NocIdSwitcher(NocId new_noc_id);

    ~NocIdSwitcher() { set_thread_noc_id(previous_noc_id_); }

private:
    NocId previous_noc_id_;
};

}  // namespace tt::umd
