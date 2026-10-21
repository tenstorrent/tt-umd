// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "grendel_flat_address.hpp"

#include <fmt/format.h>

#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/utils/error.hpp"

#include "mimir_chippy_memory.hpp"

namespace tt::umd {

namespace {

constexpr size_t kMinWordSizeBytes = 4;

// One transport. The flat address is the SPA, so nothing is translated before it hits the wire.
class SpaFlatAddressTransport : public FlatAddressTransport {
public:
    explicit SpaFlatAddressTransport(std::shared_ptr<chippy::transport::TransportInterface> transport) :
        transport_(std::move(transport)) {}

    void read(uint64_t flat_address, void* dst, std::size_t size) const override {
        transport_->read(size, kMinWordSizeBytes, flat_address, dst);
    }

    void write(uint64_t flat_address, const void* src, std::size_t size) override {
        // chippy's write() takes a non-const void* even though it only reads the buffer.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        transport_->write(size, kMinWordSizeBytes, flat_address, const_cast<void*>(src));
    }

private:
    std::shared_ptr<chippy::transport::TransportInterface> transport_;
};

}  // namespace

std::unique_ptr<FlatAddressTransport> make_grendel_flat_address_transport(
    const SocDescriptor& soc_descriptor,
    std::vector<std::shared_ptr<chippy::transport::TransportInterface>> transports,
    std::string device_name) {
    if (soc_descriptor.get_arch_descriptor().get_grendel_address_space() == GrendelAddressSpace::MimirLocal) {
        return std::make_unique<MimirChippyMemoryMap>(soc_descriptor, std::move(transports), std::move(device_name));
    }
    UMD_ASSERT(
        transports.size() == 1,
        error::RuntimeError,
        fmt::format(
            "{} full_package addressing uses one transport; the flat address is the SPA. Got {}.",
            device_name,
            transports.size()));
    return std::make_unique<SpaFlatAddressTransport>(std::move(transports.front()));
}

}  // namespace tt::umd
