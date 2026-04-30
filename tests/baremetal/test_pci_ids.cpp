// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "umd/device/pcie/pci_ids.h"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

TEST(ArchFromPciDeviceId, Wormhole) {
    EXPECT_EQ(arch_from_pci_device_id(TT_WORMHOLE_PCI_DEVICE_ID), tt::ARCH::WORMHOLE_B0);
}

TEST(ArchFromPciDeviceId, Blackhole) {
    EXPECT_EQ(arch_from_pci_device_id(TT_BLACKHOLE_PCI_DEVICE_ID), tt::ARCH::BLACKHOLE);
}

TEST(ArchFromPciDeviceId, GrendelMapsToQuasar) {
    EXPECT_EQ(arch_from_pci_device_id(TT_GRENDEL_PCI_DEVICE_ID), tt::ARCH::QUASAR);
}

TEST(ArchFromPciDeviceId, UnknownIsInvalid) {
    EXPECT_EQ(arch_from_pci_device_id(0x0000), tt::ARCH::Invalid);
    EXPECT_EQ(arch_from_pci_device_id(0xFFFF), tt::ARCH::Invalid);
    EXPECT_EQ(arch_from_pci_device_id(0x1234), tt::ARCH::Invalid);
}

}  // namespace tt::umd
