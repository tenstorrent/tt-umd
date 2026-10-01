// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Backward-compat forwarding header: kept so existing consumers that still
// #include "umd/device/pcie/pci_device.hpp" keep building after the tt-umd naming rename
// (see #2751). New code should include "tt-umd/pcie/pci_device.hpp" directly.
#include "tt-umd/pcie/pci_device.hpp"
