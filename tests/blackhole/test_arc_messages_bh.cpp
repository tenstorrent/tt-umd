// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <memory>
#include <thread>

#include "gtest/gtest.h"
#include "umd/device/blackhole_arc_message_queue.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"

using namespace tt::umd;

static std::set<chip_id_t> get_target_devices() {
    std::set<chip_id_t> target_devices;
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc_uniq = tt_ClusterDescriptor::create();
    for (int i = 0; i < cluster_desc_uniq->get_number_of_chips(); i++) {
        target_devices.insert(i);
    }
    return target_devices;
}

TEST(BlackholeArcMessages, BlackholeArcMessagesBasic) {
    Cluster cluster;

    tt_device_params default_params;
    cluster.start_device(default_params);

    std::set<chip_id_t> target_devices = get_target_devices();

    if (target_devices.size() != 1) {
        GTEST_SKIP() << "BlackholeArcMessagesBasic skipped because it can only be run on a single chip";
    }

    std::shared_ptr<BlackholeArcMessageQueue> blackhole_arc_msg_queue =
        BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
            &cluster, *target_devices.begin(), BlackholeArcMessageQueueIndex::APPLICATION);

    uint32_t response = blackhole_arc_msg_queue->send_message(ArcMessageType::NOP);

    std::cout << "response " << response << std::endl;

    cluster.close_device();
}
