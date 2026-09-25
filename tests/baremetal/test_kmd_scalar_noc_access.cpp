// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/protocol/kmd_scalar_noc_access.hpp"

using namespace tt::umd;

// The device's driver handle is what performs the access, so an access path without a device has
// nothing to forward to and must say so at construction rather than on first use.
TEST(KmdScalarNocAccessTest, RequiresADevice) { EXPECT_THROW(KmdScalarNocAccess(nullptr), std::exception); }
