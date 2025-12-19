// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Chip specific API examples.

#include <gtest/gtest.h>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/tt_io.hpp"

using namespace tt::umd;

// TODO: Once default auto TLB setup is in, check it is setup properly.
TEST(ApiTLBManager, ManualTLBConfiguration) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        const size_t tlb_tensix_size = tt_device->get_arch() == tt::ARCH::WORMHOLE_B0 ? (1 << 20) : (1 << 21);
        tt_device->init_tt_device();

        std::unique_ptr<TLBManager> tlb_manager = std::make_unique<TLBManager>(tt_device.get());
        ChipInfo chip_info = tt_device->get_chip_info();

        SocDescriptor soc_desc(tt_device->get_arch(), chip_info);

        std::int32_t c_zero_address = 0;

        for (CoreCoord translated_core : soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)) {
            tlb_manager->configure_tlb(translated_core, tlb_tensix_size, c_zero_address, tlb_data::Relaxed);
        }
        auto core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
        for (int i = 0 ; i < 200; i ++) {
            tlb_manager->configure_tlb(core, tlb_tensix_size, c_zero_address, tlb_data::Relaxed);
        }

        // So now that we have configured TLBs we can use it to interface with the TTDevice.
        auto any_worker_translated_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
        tlb_configuration tlb_description = tlb_manager->get_tlb_configuration(any_worker_translated_core);

        // TODO: Maybe accept tlb_index only?
        uint64_t address_l1_to_write = 0;
        std::vector<uint8_t> buffer_to_write = {0x01, 0x02, 0x03, 0x04};
        // Writing to TLB over Writer class.
        // TODO: This should be converted to AbstractIO writer.
        Writer writer = tlb_manager->get_static_tlb_writer(any_worker_translated_core);
        writer.write(address_l1_to_write, buffer_to_write[0]);
    }
}
