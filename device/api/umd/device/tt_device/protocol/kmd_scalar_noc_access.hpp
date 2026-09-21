// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/tt_device/protocol/scalar_noc_access.hpp"

struct tt_device_t;

namespace tt::umd {

/** Scalar access performed by the kernel driver, over the character device this handle names. */
class KmdScalarNocAccess : public ScalarNocAccess {
public:
    /** @param handle Driver handle, owned by the caller and outliving this object. */
    explicit KmdScalarNocAccess(tt_device_t* handle);

    void read(uint64_t addr, uint64_t* value, uint32_t width, uint32_t flags) override;
    void write(uint64_t addr, uint64_t value, uint32_t width, uint32_t flags) override;

private:
    tt_device_t* handle_;
};

}  // namespace tt::umd
