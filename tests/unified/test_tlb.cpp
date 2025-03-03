/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>

#include "umd/device/cluster.h"
#include "umd/device/tt_device/tlb_allocator.h"
#include "umd/device/types/tlb.h"

using namespace tt::umd;

TEST(TestTlb, TestTlbWindowAllocateNew) {
    const uint64_t tensix_addr = 0;
    const chip_id_t chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    uint32_t val = 0;
    std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    for (CoreCoord core : tensix_cores) {
        cluster->write_to_device(&val, sizeof(uint32_t), chip, core, tensix_addr, "LARGE_WRITE_TLB");
        val++;
    }

    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();
    std::unique_ptr<TlbAllocator> tlb_allocator = std::make_unique<TlbAllocator>(pci_device);

    uint32_t value_check = 0;

    for (CoreCoord core : tensix_cores) {
        tenstorrent_noc_tlb_config config;
        config.addr = 0;
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Relaxed;
        config.linked = 0;
        config.static_vc = 1;

        std::unique_ptr<TlbWindow> tlb_window = tlb_allocator->get_tlb(two_mb_size, config);

        uint32_t readback_value = tlb_window->read32(0);

        EXPECT_EQ(readback_value, value_check);

        value_check++;
    }
}

TEST(TestTlb, TestTlbWindowReuse) {
    const uint64_t tensix_addr = 0;
    const chip_id_t chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    uint32_t val = 0;
    std::vector<CoreCoord> tensix_cores =
        cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    for (CoreCoord core : tensix_cores) {
        cluster->write_to_device(&val, sizeof(uint32_t), chip, core, tensix_addr, "LARGE_WRITE_TLB");
        val++;
    }

    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();
    std::unique_ptr<TlbAllocator> tlb_allocator = std::make_unique<TlbAllocator>(pci_device);

    uint32_t value_check = 0;

    // Here it's not important how we have configured the TLB. For every read we will
    // do the reconfigure of the TLB window.
    tenstorrent_noc_tlb_config config;
    std::unique_ptr<TlbWindow> tlb_window = tlb_allocator->get_tlb(two_mb_size, config);

    for (CoreCoord core : tensix_cores) {
        tenstorrent_noc_tlb_config config;
        config.addr = 0;
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Relaxed;
        config.linked = 0;
        config.static_vc = 1;

        tlb_window->handle_ref().configure(config);

        uint32_t readback_value = tlb_window->read32(0);

        EXPECT_EQ(readback_value, value_check);

        value_check++;
    }
}

TEST(TestTlb, TestTlbWindowReadRegister) {
    const uint64_t tensix_addr = 0;
    const chip_id_t chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    // Point of the test is to read NOC0 node id register.
    // TLB needs to be aligned to 2MB so these base and offset values are
    // how TLB should be programmed in order to get to addr 0xFFB2002C.
    const uint64_t tlb_base = 0xFFA00000;
    const uint64_t noc_node_id_tlb_offset = 0x12002C;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    PCIDevice* pci_device = cluster->get_tt_device(0)->get_pci_device();
    std::unique_ptr<TlbAllocator> tlb_allocator = std::make_unique<TlbAllocator>(pci_device);

    const std::vector<CoreCoord> tensix_cores = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX);
    for (CoreCoord core : tensix_cores) {
        tenstorrent_noc_tlb_config config;
        config.addr = tlb_base & ~(two_mb_size - 1);
        config.x_end = core.x;
        config.y_end = core.y;
        config.x_start = 0;
        config.y_start = 0;
        config.noc = 0;
        config.mcast = 0;
        config.ordering = tlb_data::Strict;
        config.linked = 0;
        config.static_vc = 1;

        std::unique_ptr<TlbWindow> tlb_window = tlb_allocator->get_tlb(two_mb_size, config);

        tlb_window->handle_ref().configure(config);

        uint32_t noc_node_id_val = tlb_window->read_register(noc_node_id_tlb_offset & (two_mb_size - 1));

        uint32_t x = noc_node_id_val & 0x3F;
        uint32_t y = (noc_node_id_val >> 6) & 0x3F;

        EXPECT_EQ(core.x, x);
        EXPECT_EQ(core.y, y);
    }
}

TEST(TestTlb, TestTlbWindowReadWrite) {
    const uint64_t tensix_addr = 0;
    const chip_id_t chip = 0;
    const uint64_t two_mb_size = 1 << 21;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    const std::vector<CoreCoord> tensix_cores = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX);
    PCIDevice* pci_device = cluster->get_tt_device(chip)->get_pci_device();
    std::unique_ptr<TlbAllocator> tlb_allocator = std::make_unique<TlbAllocator>(pci_device);

    for (CoreCoord core : tensix_cores) {
        tenstorrent_noc_tlb_config config_write;
        config_write.addr = 0;
        config_write.x_end = core.x;
        config_write.y_end = core.y;
        config_write.x_start = 0;
        config_write.y_start = 0;
        config_write.noc = 0;
        config_write.mcast = 0;
        config_write.ordering = tlb_data::Relaxed;
        config_write.linked = 0;
        config_write.static_vc = 1;

        std::unique_ptr<TlbWindow> tlb_window_write = tlb_allocator->get_tlb(two_mb_size, config_write);

        tlb_window_write->write32(0, 4);
        tlb_window_write->write32(4, 0);

        tenstorrent_noc_tlb_config config_read = config_write;
        std::unique_ptr<TlbWindow> tlb_window_read = tlb_allocator->get_tlb(two_mb_size, config_read);

        uint32_t expect4 = tlb_window_read->read32(0);
        uint32_t expect0 = tlb_window_read->read32(4);

        EXPECT_EQ(expect4, 4);
        EXPECT_EQ(expect0, 0);
    }
}
