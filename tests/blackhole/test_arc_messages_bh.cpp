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

inline std::unique_ptr<Cluster> get_cluster() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        return nullptr;
    }
    return std::unique_ptr<Cluster>(new Cluster());
}

TEST(BlackholeArcMessages, BlackholeArcMessagesBasic) {
    const uint32_t num_loops = 100;

    std::unique_ptr<Cluster> cluster = get_cluster();

    std::shared_ptr<BlackholeArcMessageQueue> blackhole_arc_msg_queue =
        BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
            cluster.get(), 0, BlackholeArcMessageQueueIndex::APPLICATION);

    for (int i = 0; i < num_loops; i++) {
        uint32_t response = blackhole_arc_msg_queue->send_message(ArcMessageType::TEST);
        ASSERT_EQ(response, 0);
    }
}
