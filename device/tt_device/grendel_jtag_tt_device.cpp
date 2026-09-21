// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/grendel_jtag_tt_device.hpp"

#include <fmt/format.h>

#include <utility>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/firmware/simulation_device_firmware.hpp"
#include "umd/device/tt_device_model/tt_device_model.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

class GrendelJtagTTDeviceModel : public TTDeviceModel {
public:
    GrendelJtagTTDeviceModel(tt::ARCH arch, std::unique_ptr<GrendelJtagProtocol> protocol) :
        arch_(arch),
        communication_device_id_(protocol->get_mmio_id()),
        protocol_(std::move(protocol)),
        firmware_(std::make_unique<SimulationDeviceFirmware>(arch)),
        architecture_impl_(ArchitectureImplementation::create(arch)) {}

    tt::ARCH get_arch() const override { return arch_; }
    int get_communication_device_id() const override { return communication_device_id_; }
    DeviceProtocol* get_device_protocol() override { return protocol_.get(); }
    DeviceFirmware* get_device_firmware() override { return firmware_.get(); }
    ArchitectureImplementation* get_architecture_impl() override { return architecture_impl_.get(); }
    SocArchDescriptor* get_soc_arch_descriptor() override { return nullptr; }
    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override { return nullptr; }

private:
    tt::ARCH arch_;
    int communication_device_id_;
    std::unique_ptr<GrendelJtagProtocol> protocol_;
    std::unique_ptr<SimulationDeviceFirmware> firmware_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
};

}  // namespace

std::unique_ptr<GrendelJtagTTDevice> GrendelJtagTTDevice::create(
    const SocDescriptor& soc_descriptor,
    const std::string& host,
    uint16_t port,
    uint32_t chiplet_number,
    GrendelJtagTransportVersion version) {
    UMD_ASSERT(
        soc_descriptor.arch == tt::ARCH::QUASAR || soc_descriptor.arch == tt::ARCH::GRENDEL,
        error::RuntimeError,
        fmt::format(
            "GrendelJtagTTDevice requires a QUASAR (or GRENDEL package) descriptor, got {}.",
            arch_to_str(soc_descriptor.arch)));
    UMD_ASSERT(
        soc_descriptor.get_cores(CoreType::SMC).size() == 1,
        error::RuntimeError,
        "The initial Grendel JTAG attach path supports exactly one Mimir chiplet.");

    return std::unique_ptr<GrendelJtagTTDevice>(new GrendelJtagTTDevice(
        soc_descriptor, GrendelJtagProtocol::create(host, port, chiplet_number, version)));
}

GrendelJtagTTDevice::GrendelJtagTTDevice(
    const SocDescriptor& soc_descriptor, std::unique_ptr<GrendelJtagProtocol> protocol) :
    TTDevice(std::make_unique<GrendelJtagTTDeviceModel>(soc_descriptor.arch, std::move(protocol))) {
    set_soc_descriptor(soc_descriptor);
    address_resolver_ =
        std::make_unique<GrendelNocAddressResolver>(get_soc_descriptor(), mimir_local_address_windows(soc_descriptor));
}

GrendelJtagTTDevice::~GrendelJtagTTDevice() = default;

void GrendelJtagTTDevice::write_to_device(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    if (apply_cce_reset_vector_write(mem_ptr, core, addr, size)) {
        return;
    }

    std::lock_guard<std::mutex> lock(io_mutex_);
    const uint64_t flat_addr = address_resolver_->to_flat_address(core, addr, noc_id);
    const CoreCoord translated = get_soc_descriptor().translate_chip_coord_to_translated(core, noc_id);
    get_device_protocol()->write_data(mem_ptr, translated, flat_addr, size, noc_id);
}

void GrendelJtagTTDevice::read_from_device(
    void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    const uint64_t flat_addr = address_resolver_->to_flat_address(core, addr, noc_id);
    const CoreCoord translated = get_soc_descriptor().translate_chip_coord_to_translated(core, noc_id);
    get_device_protocol()->read_data(mem_ptr, translated, flat_addr, size, noc_id);
}

void GrendelJtagTTDevice::read_from_arc_apb(void*, uint64_t, size_t) {
    UMD_THROW(error::RuntimeError, "ARC APB access is not supported by the Grendel JTAG attach path.");
}

void GrendelJtagTTDevice::write_to_arc_apb(const void*, uint64_t, size_t) {
    UMD_THROW(error::RuntimeError, "ARC APB access is not supported by the Grendel JTAG attach path.");
}

uint32_t GrendelJtagTTDevice::get_clock() { return 0; }

uint32_t GrendelJtagTTDevice::get_min_clock_freq() { return 0; }

}  // namespace tt::umd
