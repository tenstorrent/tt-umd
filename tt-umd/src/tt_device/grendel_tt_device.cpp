// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/grendel_tt_device.hpp"

#include "flat_address_transport.hpp"
#include "noc_access.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/coordinates/grendel_noc_address_resolver.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device_model/tt_device_model.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device/firmware/simulation_device_firmware.hpp"
#include "utils.hpp"

namespace tt::umd {

namespace {

class GrendelTTDeviceModel : public TTDeviceModel {
public:
    GrendelTTDeviceModel(tt::ARCH arch, std::unique_ptr<DeviceProtocol> protocol) :
        protocol_(std::move(protocol)),
        firmware_(std::make_unique<SimulationDeviceFirmware>(arch)),
        architecture_impl_(ArchitectureImplementation::create(arch)) {}

    // TTDevice answers get_arch() and get_communication_device_id() from these two, so the model
    // stores neither: the architecture comes from the implementation, the device id from the
    // protocol's mmio id.
    DeviceProtocol* get_device_protocol() override { return protocol_.get(); }

    DeviceFirmware* get_device_firmware() override { return firmware_.get(); }

    ArchitectureImplementation* get_architecture_impl() override { return architecture_impl_.get(); }

    SocArchDescriptor* get_soc_arch_descriptor() override { return nullptr; }

    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override { return nullptr; }

    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig, HostIoWindowConfig) override { return nullptr; }

private:
    std::unique_ptr<DeviceProtocol> protocol_;
    std::unique_ptr<SimulationDeviceFirmware> firmware_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
};

}  // namespace

GrendelTTDevice::GrendelTTDevice(
    std::unique_ptr<DeviceProtocol> protocol,
    const SocDescriptor& soc_descriptor,
    std::unique_ptr<FlatAddressTransport> address_transport) :
    TTDevice(std::make_unique<GrendelTTDeviceModel>(soc_descriptor.arch, std::move(protocol))),
    address_transport_(std::move(address_transport)) {
    set_soc_descriptor(soc_descriptor);
    // The resolver borrows the descriptor stored on this device, not the caller's copy.
    address_resolver_ =
        std::make_unique<GrendelNocAddressResolver>(get_soc_descriptor(), address_windows_for(soc_descriptor));
    set_grendel_address_resolver(address_resolver_.get());
}

GrendelTTDevice::~GrendelTTDevice() = default;

uint64_t GrendelTTDevice::flat_address(CoreCoord core, uint64_t addr, NocId noc_id) const {
    // Chips hand this a translated xy stored as LITERAL. A caller that still has a NOC or logical
    // coordinate is translated here, once, and the resolver then treats that xy as device-ready.
    const tt_xy_pair translated = get_soc_descriptor().translate_chip_coord_to_translated(core, noc_id);
    return address_resolver_->to_flat_address(CoreCoord(translated), addr, noc_id);
}

void GrendelTTDevice::read_from_device(void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    address_transport_->read(flat_address(core, addr, noc_id), mem_ptr, size);
}

void GrendelTTDevice::write_to_device(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    if (apply_cce_reset_vector_write(mem_ptr, core, addr, size)) {
        return;
    }
    std::lock_guard<std::mutex> lock(io_mutex_);
    address_transport_->write(flat_address(core, addr, noc_id), mem_ptr, size);
}

void GrendelTTDevice::read_from_device_reg(
    void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(get_arch(), width);
    validate_register_access(addr, size, width);
    const size_t access_size = register_access_size(width);
    for (size_t offset = 0; offset < size; offset += access_size) {
        read_from_device(static_cast<uint8_t*>(mem_ptr) + offset, core, addr + offset, access_size, noc_id);
    }
}

void GrendelTTDevice::write_to_device_reg(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(get_arch(), width);
    validate_register_access(addr, size, width);
    const size_t access_size = register_access_size(width);
    for (size_t offset = 0; offset < size; offset += access_size) {
        write_to_device(static_cast<const uint8_t*>(mem_ptr) + offset, core, addr + offset, access_size, noc_id);
    }
}

void GrendelTTDevice::read_flat_address(uint64_t addr, void* dst, size_t size) {
    address_transport_->read(addr, dst, size);
}

void GrendelTTDevice::write_flat_address(uint64_t addr, const void* src, size_t size) {
    address_transport_->write(addr, src, size);
}

void GrendelTTDevice::noc_multicast_write(
    const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    // No hardware multicast on this path. The unicast fallback only touches Tensix cores.
    multicast_write_via_unicast(src, size, core_start, core_end, addr, noc_id);
}

void GrendelTTDevice::noc_multicast_write(const void* src, size_t size, uint64_t addr, NocId noc_id) {
    auto [start, end] =
        get_soc_descriptor().get_bounding_rectangle(is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0);
    noc_multicast_write(src, size, start, end, addr, noc_id);
}

void GrendelTTDevice::dma_write_to_core_range(
    const void* /*src*/,
    uint64_t /*dst_addr*/,
    size_t /*size*/,
    CoreCoord /*core_start*/,
    CoreCoord /*core_end*/,
    NocId /*noc_id*/) {
    UMD_THROW(error::RuntimeError, "DMA write to core range is not supported for GrendelTTDevice.");
}

}  // namespace tt::umd
