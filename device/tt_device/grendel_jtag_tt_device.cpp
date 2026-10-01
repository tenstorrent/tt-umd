// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/grendel_jtag_tt_device.hpp"

#include <fmt/format.h>

#include <utility>
#include <vector>

#include "protocol/grendel_jtag_protocol_impl.hpp"
#include "mimir_chippy_memory.hpp"
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

private:
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

    auto transport = make_grendel_jtag_transport(host, port, chiplet_number, version);
    auto protocol = make_grendel_jtag_protocol(
        [transport](tt_xy_pair) { return transport; }, static_cast<int>(chiplet_number));
    auto memory_map = std::make_unique<MimirChippyMemoryMap>(
        soc_descriptor, std::vector<std::shared_ptr<MimirChippyMemoryMap::Transport>>{transport}, "GrendelJtagTTDevice");
    return std::unique_ptr<GrendelJtagTTDevice>(
        new GrendelJtagTTDevice(soc_descriptor, std::move(protocol), std::move(memory_map)));
}

GrendelJtagTTDevice::GrendelJtagTTDevice(
    const SocDescriptor& soc_descriptor,
    std::unique_ptr<GrendelJtagProtocol> protocol,
    std::unique_ptr<MimirChippyMemoryMap> memory_map) :
    TTDevice(std::make_unique<GrendelJtagTTDeviceModel>(soc_descriptor.arch, std::move(protocol))),
    memory_map_(std::move(memory_map)) {
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
    memory_map_->write(flat_addr, mem_ptr, size);
}

void GrendelJtagTTDevice::read_from_device(
    void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    const uint64_t flat_addr = address_resolver_->to_flat_address(core, addr, noc_id);
    memory_map_->read(flat_addr, mem_ptr, size);
}

}  // namespace tt::umd
