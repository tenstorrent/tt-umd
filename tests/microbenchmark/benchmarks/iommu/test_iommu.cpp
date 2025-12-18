/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>
#include <sys/mman.h>

#include "common/microbenchmark_utils.hpp"
#include "tests/test_utils/device_test_utils.hpp"

using namespace tt::umd;

static void guard_test_iommu() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
}
