// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/emu_tt_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "emu_axi_transport.h"  // chippy
#include "grendel_flat_address.hpp"
#include "mimir_chippy_memory.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd {

namespace {

// A multi-chiplet server answers GET_CHIPLET with a chiplet id ("k0", "m0"). The single-chiplet
// SiVal server answers "GET_CHIPLET called in single-chiplet model." and logs every SELECT_CHIPLET
// it ignores. Only an id means there is a chiplet to select. A failed probe keeps selecting: the
// MMK default chiplet is k0, and skipping the select there would program Keraunos.
bool looks_like_chiplet_id(std::string chiplet) {
    const auto begin = chiplet.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return false;
    }
    const auto end = chiplet.find_last_not_of(" \t");
    chiplet = chiplet.substr(begin, end - begin + 1);
    if (chiplet.size() < 2 || chiplet.size() > 3) {
        return false;
    }
    const char kind = static_cast<char>(std::tolower(static_cast<unsigned char>(chiplet.front())));
    if (kind != 'k' && kind != 'm' && kind != 'q') {
        return false;
    }
    return std::all_of(chiplet.begin() + 1, chiplet.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool emu_server_selects_chiplets(const std::shared_ptr<chippy::transport::emu_axi::EmuAxiTransport>& root) {
    try {
        return looks_like_chiplet_id(root->get_chiplet());
    } catch (const std::exception&) {
        return true;
    }
}

// Ready at construction. The chippy sockets already exist, and TTDevice calls this protocol
// without a separate attach step.
class EmuAxiProtocol : public DeviceProtocol {
public:
    EmuAxiProtocol(
        const SocDescriptor& soc_descriptor,
        const std::vector<std::shared_ptr<chippy::transport::TransportInterface>>& transports) :
        soc_(soc_descriptor),
        resolver_(soc_, address_windows_for(soc_)),
        memory_(soc_, transports, "EmuAxiProtocol") {}

    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override {
        memory_.read(flat_address(core, addr, noc_id), dst, size);
    }

    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override {
        memory_.write(flat_address(core, addr, noc_id), src, size);
    }

    void read_ctrl(
        void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override {
        validate_register_access(addr, size, width);
        read_data(dst, core, addr, size, noc_id);
    }

    void write_ctrl(
        const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width)
        override {
        validate_register_access(addr, size, width);
        write_data(src, core, addr, size, noc_id);
    }

    bool write_to_core_range(
        const void* /*src*/,
        tt_xy_pair /*core_start*/,
        tt_xy_pair /*core_end*/,
        uint64_t /*addr*/,
        size_t /*size*/,
        NocId /*noc_id*/) override {
        return false;
    }

    int get_mmio_id() override { return 0; }

private:
    uint64_t flat_address(tt_xy_pair core, uint64_t addr, NocId noc_id) const {
        // DEFAULT_NOC and NOC0 are the same value. Chippy's AXI map is not a second NOC.
        UMD_ASSERT(
            noc_id == NocId::NOC0,
            error::RuntimeError,
            fmt::format("EmuAxiProtocol supports only NOC0 (got {}).", noc_to_str(noc_id)));
        // The caller already translated the coordinate. LITERAL keeps the resolver from translating
        // it again, and the descriptor says which core type sits there.
        return resolver_.to_flat_address(CoreCoord(core), addr, noc_id);
    }

    // Declared in this order: the resolver borrows soc_.
    SocDescriptor soc_;
    GrendelNocAddressResolver resolver_;
    MimirChippyMemoryMap memory_;
};

}  // namespace

GrendelAddressWindows EmuTTDevice::mimir_address_windows(const SocDescriptor& soc_descriptor) {
    return mimir_local_address_windows(soc_descriptor);
}

/* static */ std::unique_ptr<EmuTTDevice> EmuTTDevice::create(
    const SocDescriptor& soc_descriptor, const std::string& host, uint32_t port, bool send_init) {
    UMD_ASSERT(
        soc_descriptor.arch == tt::ARCH::QUASAR || soc_descriptor.arch == tt::ARCH::GRENDEL,
        error::RuntimeError,
        fmt::format(
            "EmuTTDevice requires a QUASAR (or GRENDEL package) descriptor, got {}.",
            arch_to_str(soc_descriptor.arch)));
    // These sockets carry a chiplet-local AXI map. A package ATT address would miss every window.
    UMD_ASSERT(
        soc_descriptor.get_arch_descriptor().get_grendel_address_space() == GrendelAddressSpace::MimirLocal,
        error::RuntimeError,
        "EmuTTDevice talks to a chiplet-local AXI map. The descriptor must set address_space: mimir_local.");

    auto root_transport = std::make_shared<chippy::transport::emu_axi::EmuAxiTransport>(host, port);
    const auto& channels = soc_descriptor.get_arch_descriptor().get_mimir_channels();
    const std::size_t mimir_count = soc_descriptor.get_cores(CoreType::SMC).size();
    UMD_ASSERT(
        channels.size() == mimir_count,
        error::RuntimeError,
        fmt::format(
            "EmuTTDevice expected one mimir_channels entry per Mimir, found {} entries for {} SMC cores.",
            channels.size(),
            mimir_count));
    const bool select_chiplet = emu_server_selects_chiplets(root_transport);
    std::vector<std::shared_ptr<chippy::transport::TransportInterface>> chiplet_transports;
    chiplet_transports.reserve(channels.size());
    for (const auto& channel : channels) {
        // A one-Mimir descriptor still selects on MMK: the server's default chiplet is k0, and
        // a naked socket would program Keraunos. A single-chiplet server has nothing to select,
        // and each SELECT_CHIPLET there is logged and ignored. The id comes from the descriptor,
        // so channel 0 is not assumed to be m0.
        std::shared_ptr<chippy::transport::TransportInterface> chiplet_transport = root_transport;
        if (select_chiplet) {
            chiplet_transport = std::make_shared<chippy::transport::emu_axi::MultiChipletEmuAxiTransport>(
                root_transport, channel.chiplet);
        }
        chiplet_transports.push_back(std::move(chiplet_transport));
    }
    // The protocol and the flat-address map share the sockets. Data and registers still move
    // through the map. The protocol is what TTDevice uses when a call is not on that path.
    auto protocol = std::make_unique<EmuAxiProtocol>(soc_descriptor, chiplet_transports);
    auto address_transport =
        make_grendel_flat_address_transport(soc_descriptor, std::move(chiplet_transports), "EmuTTDevice");
    auto device = std::unique_ptr<EmuTTDevice>(
        new EmuTTDevice(soc_descriptor, std::move(protocol), std::move(address_transport)));

    // INIT resets/initializes the model; send it once on the root socket, never once per chiplet.
    // On the SiVal servers INIT re-runs the reset phase, which drops any bring-up an earlier client
    // performed, so it is suppressed when the caller attaches to an already-brought-up model.
    if (send_init) {
        root_transport->initialize();
    }
    return device;
}

EmuTTDevice::EmuTTDevice(
    const SocDescriptor& soc_descriptor,
    std::unique_ptr<DeviceProtocol> protocol,
    std::unique_ptr<FlatAddressTransport> address_transport) :
    GrendelTTDevice(std::move(protocol), soc_descriptor, std::move(address_transport)) {}

// Deliberately does NOT tear the transport down. chippy's teardown() sends QUIT, and QUIT ends the
// session for whoever owns the server: the mock server sets its shutdown event, and on a real run
// it ends the emulation job. Ownership sits with the orchestrator, which sends QUIT itself once the
// test process has exited (run_validation_test.py's _send_quit_command) -- so a device that sent it
// would kill the server under any later device, or under a second test in the same process.
//
// chippy's transport exposes no close-without-QUIT, and ~EmuAxiTransport is defaulted, so the
// socket is released when the process exits rather than here. Worth a small chippy addition.
EmuTTDevice::~EmuTTDevice() = default;

}  // namespace tt::umd
