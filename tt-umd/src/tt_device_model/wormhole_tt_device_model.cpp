// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Wormhole devices are not supported by this build: TTDevice::create refuses any PCIe device that is not a
// Blackhole, so there is no Wormhole model to compose.
