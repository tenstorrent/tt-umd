// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Chip specific API examples.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/mmio_timeout_config.hpp"
#include "umd/device/utils/timeouts.hpp"

using namespace tt;
using namespace tt::umd;

// The TLBManager window here comes from get_io_window, which carries no hang-detector veto, so a single
// MMIO op that stalls on a contended host would trip the tight default per-op budget and throw
// DeviceTimeoutError. Widen the budget for the test and restore the default afterwards.
class ApiTLBManager : public ::testing::Test {
protected:
    void SetUp() override { MmioTimeoutConfig::set_op_timeout(std::chrono::milliseconds(100)); }

    void TearDown() override { MmioTimeoutConfig::set_op_timeout(timeout::MMIO_OP_TIMEOUT); }
};

// TODO: Once default auto TLB setup is in, check it is setup properly.
TEST_F(ApiTLBManager, ManualTLBConfiguration) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        const size_t tlb_tensix_size = tt_device->get_arch() == tt::ARCH::WORMHOLE_B0 ? (1 << 20) : (1 << 21);
        tt_device->set_power_state(true);
        tt_device->init_tt_device();

        std::unique_ptr<TLBManager> tlb_manager = std::make_unique<TLBManager>(tt_device.get());

        const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();

        std::int32_t c_zero_address = SAFE_IO_L1_ADDRESS;

        for (CoreCoord translated_core : soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)) {
            tlb_manager->configure_tlb(translated_core, tlb_tensix_size, c_zero_address, tlb_data::Relaxed);
        }

        // So now that we have configured TLBs we can use it to interface with the TTDevice.
        auto any_worker_translated_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

        // TODO: Maybe accept tlb_index only?
        std::vector<uint8_t> buffer_to_write = {0x01, 0x02, 0x03, 0x04};
        TlbWindow* window = tlb_manager->get_tlb_window(any_worker_translated_core);
        window->write_register(SAFE_IO_L1_ADDRESS, buffer_to_write.data(), buffer_to_write.size());

        tt_device->set_power_state(false);
    }
}
