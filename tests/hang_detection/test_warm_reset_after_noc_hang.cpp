// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "device/api/umd/device/warm_reset_with_recovery.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "utils.hpp"

using namespace tt;
using namespace tt::umd;

class WarmResetAfterNocHangTest : public ::testing::Test {
protected:
    bool noc_hung_ = false;

    void TearDown() override {
        if (noc_hung_) {
            WarmResetWithRecovery::warm_reset();
        }
    }
};

// Hangs a NOC by writing to core (15, 15), then warm resets and verifies the device recovers and I/O
// works again afterwards. This is primarily a warm-reset recovery test (the NOC hang is just the way to
// put the device in a bad state), which is why it lives in its own file rather than alongside the
// is_noc_hung / MMIO-timeout API tests. It is still destructive, so it stays in the on-demand
// hang_detection_tests target. Skipped on Wormhole (a warm reset may not recover the device there,
// needing a watchdog reset) and on ARM64 (can hang the whole host).
TEST_F(WarmResetAfterNocHangTest, TTDeviceWarmResetAfterNocHang) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    auto arch = PCIDevice(pci_device_ids[0]).get_arch();
    if (arch == tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP()
            << "This test intentionally hangs the NOC. On Wormhole, this can cause a severe failure where even a warm "
               "reset does not recover the device, requiring a watchdog-triggered reset for recovery.";
    }

    if (utils::is_arm_platform()) {
        // Reset isn't supported in this situation (ARM64 host), and it turns out that this doesn't just hang the NOC.
        // It hangs my whole system (Blackhole p100, ALTRAD8UD-1L2T) and requires a reboot to recover.
        GTEST_SKIP() << "Skipping test on ARM64 due to instability.";
    }

    auto cluster = test_utils::make_default_test_cluster();
    if (is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test calling warm_reset() on Galaxy configurations.";
    }

    uint64_t address = 0x0;
    std::vector<uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> zero_data(data.size(), 0);
    std::vector<uint8_t> readback_data(data.size(), 0);

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->set_power_state(true);
    tt_device->init_tt_device();

    const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();

    tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    // send to core 15, 15 which will hang the NOC
    noc_hung_ = true;
    tt_device->write_to_device(data.data(), xy_pair{15, 15}, address, data.size());

    // TODO: Remove this check when it is figured out why there is no hang detected on Blackhole.
    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        EXPECT_THROW(tt_device->is_pcie_hung(), std::runtime_error);
    }

    WarmResetWithRecovery::warm_reset();
    noc_hung_ = false;

    // After a warm reset, topology discovery must be performed to detect available chips.
    // Creating a Cluster triggers this discovery process, which is why a Cluster is instantiated here,
    // even though this is a TTDevice test.
    cluster = test_utils::make_default_test_cluster();

    EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after reset.";

    // TODO: Comment this out after finding out how to detect hang reads on BH.
    // EXPECT_NO_THROW(cluster->get_chip(0)->get_tt_device()->is_pcie_hung());.

    tt_device.reset();

    tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->set_power_state(true);
    tt_device->init_tt_device();

    tt_device->write_to_device(zero_data.data(), tensix_core, SAFE_IO_L1_ADDRESS, zero_data.size());

    tt_device->write_to_device(data.data(), tensix_core, SAFE_IO_L1_ADDRESS, data.size());

    tt_device->read_from_device(readback_data.data(), tensix_core, SAFE_IO_L1_ADDRESS, readback_data.size());

    ASSERT_EQ(data, readback_data);

    tt_device->set_power_state(false);
}
