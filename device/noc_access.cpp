// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "noc_access.hpp"
#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

// NocId that is stored for current thread in TLS.
static thread_local NocId tls_noc_id = NocId::DEFAULT_NOC;

void set_thread_noc_id(NocId noc_id) { tls_noc_id = noc_id; }

NocId get_selected_noc_id() { return tls_noc_id; }

bool is_selected_noc1() { return get_selected_noc_id() == NocId::NOC1; }

NocIdSwitcher::NocIdSwitcher(NocId new_noc_id) : previous_noc_id_(get_selected_noc_id()) {
    set_thread_noc_id(new_noc_id);
}

}  // namespace tt::umd
