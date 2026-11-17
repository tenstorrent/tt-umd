// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "noc_access.hpp"

#include "tt-umd/types/noc_id.hpp"

namespace tt::umd {

namespace {

// Each thread picks its NOC independently, so concurrent users of one device never see each other's choice.
thread_local NocId selected_noc_id = NocId::NOC0;

}  // namespace

NocId get_selected_noc_id() { return selected_noc_id; }

void set_thread_noc_id(NocId noc_id) { selected_noc_id = noc_id; }

bool is_selected_noc1() { return get_selected_noc_id() == NocId::NOC1; }

NocIdSwitcher::NocIdSwitcher(NocId new_noc_id) : previous_noc_id_(get_selected_noc_id()) {
    set_thread_noc_id(new_noc_id);
}

}  // namespace tt::umd
