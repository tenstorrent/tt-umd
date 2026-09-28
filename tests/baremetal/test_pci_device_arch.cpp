// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "tt-kmd-lib/pci_ids.h"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/types/arch.hpp"

using namespace tt::umd;

// A Quasar package presents one PCIe function, whichever chiplet combination it is built from, so
// the device id is what names the architecture. Keraunos is one such combination and not an
// architecture of its own.
TEST(PciDeviceArch, QuasarDeviceIdResolvesToQuasarArch) {
    PciDeviceInfo info{};
    info.device_id = TT_QUASAR_PCI_DEVICE_ID;

    EXPECT_EQ(info.get_arch(), tt::ARCH::QUASAR);
}

TEST(PciDeviceArch, KnownDeviceIdsResolveToTheirArch) {
    PciDeviceInfo wormhole{};
    wormhole.device_id = TT_WORMHOLE_PCI_DEVICE_ID;
    EXPECT_EQ(wormhole.get_arch(), tt::ARCH::WORMHOLE_B0);

    PciDeviceInfo blackhole{};
    blackhole.device_id = TT_BLACKHOLE_PCI_DEVICE_ID;
    EXPECT_EQ(blackhole.get_arch(), tt::ARCH::BLACKHOLE);
}

// An id UMD does not know must stay Invalid rather than fall through to an arch, so that an
// unexpected function fails at creation instead of being driven as the wrong part.
TEST(PciDeviceArch, UnknownDeviceIdStaysInvalid) {
    PciDeviceInfo info{};
    info.device_id = 0x0000;

    EXPECT_EQ(info.get_arch(), tt::ARCH::Invalid);
}
