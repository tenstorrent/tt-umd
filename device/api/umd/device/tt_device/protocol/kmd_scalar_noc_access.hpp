// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>

#include "umd/device/tt_device/protocol/scalar_noc_access.hpp"

struct tt_device_t;

namespace tt::umd {

class PCIDevice;

/** Scalar access performed by the kernel driver, over the character device this handle names. */
class KmdScalarNocAccess : public ScalarNocAccess {
public:
    /**
     * @param pci_device The device whose driver handle the accesses are issued on. Held here
     * rather than borrowed: closing the device frees the handle, and nothing else outlives these
     * accesses to keep it open.
     */
    explicit KmdScalarNocAccess(std::unique_ptr<PCIDevice> pci_device);
    ~KmdScalarNocAccess() override;

    void read(uint64_t addr, uint64_t* value, uint32_t width, uint32_t flags) override;
    void write(uint64_t addr, uint64_t value, uint32_t width, uint32_t flags) override;

private:
    std::unique_ptr<PCIDevice> pci_device_;
    tt_device_t* handle_;
};

}  // namespace tt::umd
