// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <thread>

#include "umd/device/blackhole_arc_message_queue.h"
#include "umd/device/cluster.h"

using namespace tt::umd;

std::set<chip_id_t> get_target_devices() {
    std::set<chip_id_t> target_devices;
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc_uniq = tt_ClusterDescriptor::create();
    for (int i = 0; i < cluster_desc_uniq->get_number_of_chips(); i++) {
        target_devices.insert(i);
    }
    return target_devices;
}

TEST(BlackholeArcMessages, BlackholeArcMessagesBasic) {
    Cluster cluster;

    std::set<chip_id_t> target_devices = get_target_devices();

    if (target_devices.size() != 1) {
        GTEST_SKIP() << "BlackholeArcMessagesBasic skipped because it can only be run on a single chip";
    }

    std::unique_ptr<BlackholeArcMessageQueue> blackhole_arc_msg_queue =
        cluster.get_blackhole_arc_message_queue(*target_devices.begin());
}
