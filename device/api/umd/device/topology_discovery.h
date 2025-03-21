/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "cluster.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/chip/chip.h"
#include "umd/device/tt_cluster_descriptor.h"

namespace tt::umd {

class TopologyDiscovery {

public:

    std::unique_ptr<tt_ClusterDescriptor> create_ethernet_map();

private:

    

};

}