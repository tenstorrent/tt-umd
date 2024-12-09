// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "eth_l1_address_map.h"
#include "tests/test_utils/stimulus_generators.hpp"
#include "tt_cluster_descriptor.h"
#include "tt_xy_pair.h"
#include "umd/device/cluster.h"

using namespace tt::umd;

namespace tt::umd::test::utils {

static void set_params_for_remote_txn(Cluster& device) {
    // Populate address map and NOC parameters that the driver needs for remote transactions
    device.set_device_l1_address_params(
        {l1_mem::address_map::L1_BARRIER_BASE,
         eth_l1_mem::address_map::ERISC_BARRIER_BASE,
         eth_l1_mem::address_map::FW_VERSION_ADDR});
}

class BlackholeTestFixture : public ::testing::Test {
protected:
    // You can remove any or all of the following functions if their bodies would
    // be empty.

    std::unique_ptr<Cluster> device;

    BlackholeTestFixture() {}

    ~BlackholeTestFixture() override {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    virtual int get_detected_num_chips() = 0;
    virtual bool is_test_skipped() = 0;

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    void SetUp() override {
        // Code here will be called immediately after the constructor (right
        // before each test).

        if (is_test_skipped()) {
            GTEST_SKIP() << "Test is skipped due to incorrect number of chips";
        }

        // std::cout << "Setting Up Test." << std::endl;
        assert(get_detected_num_chips() > 0);
        auto devices = std::vector<chip_id_t>(get_detected_num_chips());
        std::iota(devices.begin(), devices.end(), 0);
        std::set<chip_id_t> target_devices = {devices.begin(), devices.end()};
        uint32_t num_host_mem_ch_per_mmio_device = 1;
        device = std::make_unique<Cluster>(num_host_mem_ch_per_mmio_device, false, true, true);
        assert(device != nullptr);
        assert(device->get_cluster_description()->get_number_of_chips() == get_detected_num_chips());

        set_params_for_remote_txn(*device);

        tt_device_params default_params;
        device->start_device(default_params);

        device->deassert_risc_reset();

        device->wait_for_non_mmio_flush();
    }

    void TearDown() override {
        // Code here will be called immediately after each test (right
        // before the destructor).

        if (!is_test_skipped()) {
            // std::cout << "Tearing Down Test." << std::endl;
            device->close_device();
        }
    }
};

}  // namespace tt::umd::test::utils
