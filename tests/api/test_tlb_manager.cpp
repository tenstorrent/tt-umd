// // SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
// //
// // SPDX-License-Identifier: Apache-2.0

// // This file holds Chip specific API examples.

// #include <gtest/gtest.h>

// #include "umd/device/tt_device/tt_device.h"
// #include "umd/device/tt_io.hpp"
// #include "umd/device/tt_soc_descriptor.h"

// using namespace tt::umd;

// std::unique_ptr<TTDevice> get_tt_device() {
//     std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
//     if (pci_device_ids.empty()) {
//         return nullptr;
//     }
//     return TTDevice::create(pci_device_ids[0]);
// }

// // TODO: Once default auto TLB setup is in, check it is setup properly.
// TEST(ApiTLBManager, ManualTLBConfiguration) {
//     std::unique_ptr<TTDevice> tt_device = get_tt_device();

//     if (tt_device == nullptr) {
//         GTEST_SKIP() << "No chips present on the system. Skipping test.";
//     }

//     TLBManager* tlb_manager = tt_device->get_tlb_manager();
//     tt_SocDescriptor soc_desc = tt_SocDescriptor(
//         tt_SocDescriptor::get_soc_descriptor_path(tt_device->get_arch()), tt_device->get_arch() !=
//         tt::ARCH::GRAYSKULL);

//     // TODO: This should be part of TTDevice interface, not Cluster or Chip.
//     // Configure TLBs.
//     std::function<int(tt_xy_pair)> get_static_tlb_index = [&](tt_xy_pair core) -> int {
//         // TODO: Make this per arch.
//         bool is_worker_core = soc_desc.is_worker_core(core);
//         if (!is_worker_core) {
//             return -1;
//         }

//         auto tlb_index = core.x + core.y * tt_device->get_architecture_implementation()->get_grid_size_x();

//         auto tlb_1m_base_and_count = tt_device->get_architecture_implementation()->get_tlb_1m_base_and_count();
//         auto tlb_2m_base_and_count = tt_device->get_architecture_implementation()->get_tlb_2m_base_and_count();

//         // Use either 1mb or 2mb tlbs.
//         if (tlb_1m_base_and_count.second > 0) {
//             // Expect that tlb index is within the number of 1mb TLBs.
//             EXPECT_TRUE(tlb_index < tlb_1m_base_and_count.second);
//             tlb_index += tlb_1m_base_and_count.first;
//         } else {
//             // Expect that tlb index is within the number of 1mb TLBs.
//             EXPECT_TRUE(tlb_index < tlb_2m_base_and_count.second);
//             tlb_index += tlb_2m_base_and_count.first;
//         }

//         return tlb_index;
//     };

//     std::int32_t c_zero_address = 0;

//     for (tt_xy_pair core : soc_desc.get_cores(CoreType::TENSIX)) {
//         tlb_manager->configure_tlb(core, core, get_static_tlb_index(core), c_zero_address, tlb_data::Relaxed);
//     }

//     // So now that we have configured TLBs we can use it to interface with the TTDevice.
//     auto any_worker_core = soc_desc.get_cores(CoreType::TENSIX)[0];
//     tlb_configuration tlb_description = tlb_manager->get_tlb_configuration(any_worker_core);

//     // TODO: Maybe accept tlb_index only?
//     uint64_t address_l1_to_write = 0;
//     std::vector<uint8_t> buffer_to_write = {0x01, 0x02, 0x03, 0x04};
//     tt_device->write_block(
//         tlb_description.tlb_offset + address_l1_to_write, buffer_to_write.size(), buffer_to_write.data());

//     // Another way to write to the TLB.
//     // TODO: This should be converted to AbstractIO writer.
//     tt::Writer writer = tlb_manager->get_static_tlb_writer(any_worker_core);
//     writer.write(address_l1_to_write, buffer_to_write[0]);
// }
