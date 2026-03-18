/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <variant>

#include "umd/device/tt_device/protocol/pcie_dma/blackhole_dma_transfer.hpp"
#include "umd/device/tt_device/protocol/pcie_dma/wormhole_dma_transfer.hpp"

namespace tt::umd {

/**
 * DmaTransferStrategy is a variant-based strategy for chip-specific DMA transfers.
 *
 * Each architecture (Wormhole, Blackhole, ...) provides its own DMA register programming
 * sequence and completion mechanism. The variant dispatches to the correct implementation
 * at runtime.
 */
using DmaTransferStrategy = std::variant<WormholeDmaTransfer, BlackholeDmaTransfer>;

}  // namespace tt::umd
