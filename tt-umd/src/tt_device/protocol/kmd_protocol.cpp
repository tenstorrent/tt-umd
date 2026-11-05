// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/kmd_protocol.hpp"

#include <fmt/format.h>

#include <cstring>
#include <utility>

#include "common/noc_address_space.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

KmdProtocol::KmdProtocol(std::unique_ptr<PCIDevice> pci_device, int mmio_id) :
    pci_device_(std::move(pci_device)), mmio_id_(mmio_id) {
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "This protocol needs an open device.");
}

KmdProtocol::~KmdProtocol() = default;

void KmdProtocol::validate(tt_xy_pair core, uint64_t addr, size_t size, uint32_t access_width) {
    UMD_ASSERT(
        core.x == 0 && core.y == 0,
        error::RuntimeError,
        fmt::format(
            "A Quasar address already names its target, so it travels with no coordinate; core ({}, {}) cannot be "
            "honoured.",
            core.x,
            core.y));

    UMD_ASSERT(
        size != 0 && size % access_width == 0,
        error::RuntimeError,
        fmt::format("A {} byte transfer is not a whole number of {} byte accesses.", size, access_width));

    UMD_ASSERT(
        addr % access_width == 0,
        error::RuntimeError,
        fmt::format("Address 0x{:x} is not aligned to a {} byte access.", addr, access_width));

    UMD_ASSERT(
        size <= MAX_TRANSFER_SIZE,
        error::RuntimeError,
        fmt::format(
            "A {} byte transfer is {} driver round trips, past the {} byte limit this path serves.",
            size,
            size / access_width,
            MAX_TRANSFER_SIZE));
}

void KmdProtocol::read_ctrl(
    void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    const NocAddressSpace space = noc_address_space(noc_id);
    const auto access_width = static_cast<uint32_t>(register_access_size(width));
    validate(core, addr, size, access_width);

    auto* out = static_cast<uint8_t*>(dst);
    for (size_t offset = 0; offset < size; offset += access_width) {
        // Zero-extended to 64 bits by the driver, so the low access_width bytes are the register.
        const uint64_t value = pci_device_->noc_read_scalar(addr + offset, access_width, space);
        std::memcpy(out + offset, &value, access_width);
    }
}

void KmdProtocol::write_ctrl(
    const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    const NocAddressSpace space = noc_address_space(noc_id);
    const auto access_width = static_cast<uint32_t>(register_access_size(width));
    validate(core, addr, size, access_width);

    const auto* in = static_cast<const uint8_t*>(src);
    for (size_t offset = 0; offset < size; offset += access_width) {
        uint64_t value = 0;
        std::memcpy(&value, in + offset, access_width);

        pci_device_->noc_write_scalar(addr + offset, value, access_width, space);
    }
}

// The driver serves no bulk transfer yet, so a data transfer is the same sequence of single
// accesses as a control transfer and the two differ only in what the caller means by them. They
// stay separate so that data can take a bulk path of its own as soon as one exists.
void KmdProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    read_ctrl(dst, core, addr, size, noc_id, RegisterAccessWidth::BITS32);
}

void KmdProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    write_ctrl(src, core, addr, size, noc_id, RegisterAccessWidth::BITS32);
}

bool KmdProtocol::write_to_core_range(
    const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) {
    return false;
}

int KmdProtocol::get_mmio_id() { return mmio_id_; }

PCIDevice* KmdProtocol::get_pci_device() { return pci_device_.get(); }

}  // namespace tt::umd
