// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/simulation_tt_device.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <tt-logger/tt-logger.hpp>

#include "noc_access.hpp"
#include "simulation/simulation_server_socket.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_device_identity.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/hang_detection/hang_detector.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

class SimulationHangDetector final : public HangDetector {
protected:
    uint32_t read_hang_check_reg_via_bar() override { return 0; }

    uint32_t read_hang_check_reg_via_noc(NocId /*noc*/) override { return 0; }

    bool is_bus_available() override { return true; }

    bool is_noc_available() override { return true; }
};

// Simulation reaches the device through SimulationTTDevice's own I/O overrides rather than one of
// the transports (PCIe, JTAG, Ethernet) that implement DeviceProtocol. Components which were
// rewritten to take a DeviceProtocol* instead of a TTDevice* -- ArcTelemetryReader -- would
// otherwise get a null pointer here, so this forwards those calls back to the device.
//
// Coordinates arrive already resolved to NOC coordinates, because protocols sit below coordinate
// translation, so they are passed down as LITERAL rather than translated a second time.
class SimulationProtocol final : public DeviceProtocol {
public:
    explicit SimulationProtocol(SimulationTTDevice* device) : device_(device) {}

    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override {
        device_->read_from_device(dst, CoreCoord(core), addr, size, noc_id);
    }

    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override {
        device_->write_to_device(src, CoreCoord(core), addr, size, noc_id);
    }

    void read_ctrl(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override {
        device_->read_from_device_reg(dst, CoreCoord(core), addr, size, noc_id);
    }

    void write_ctrl(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override {
        device_->write_to_device_reg(src, CoreCoord(core), addr, size, noc_id);
    }

    // No hardware multicast engine behind this path; report that the caller must fall back to
    // software unicast, which is what the device's own multicast helpers already do.
    bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id)
        override {
        return false;
    }

    int get_mmio_id() override { return device_->get_communication_device_id(); }

private:
    SimulationTTDevice* device_;
};

}  // namespace

// The constructor and destructor are defined out-of-line so that socket_
// (unique_ptr<SimulationServerSocket>) is constructed/destroyed where the type is complete; the
// public header only forward-declares SimulationServerSocket.
SimulationTTDevice::SimulationTTDevice(
    const std::filesystem::path& simulator_directory, std::unique_ptr<SimulationSysmemManager> sysmem_manager) :
    simulator_directory_(simulator_directory), sysmem_manager_(std::move(sysmem_manager)) {
    set_hang_detector(std::make_unique<SimulationHangDetector>());
    set_device_protocol(std::make_unique<SimulationProtocol>(this));
}

SimulationTTDevice::SimulationTTDevice(std::unique_ptr<SimulationClient> client) : client_(std::move(client)) {
    set_hang_detector(std::make_unique<SimulationHangDetector>());
    set_device_protocol(std::make_unique<SimulationProtocol>(this));
}

SimulationTTDevice::~SimulationTTDevice() = default;

void SimulationTTDevice::attach_client() { client_->attach(); }

void SimulationTTDevice::detach_client() {
    if (client_) {
        client_->detach();
    }
}

void SimulationTTDevice::adopt_socket(
    std::unique_ptr<SimulationServerSocket> socket, std::function<void()> shutdown_handler) {
    socket_ = std::move(socket);
    // Begin serving remote clients now that the backend is up. The shutdown handler is captured into
    // the request handler here, before serving starts, so it is fixed for the socket's lifetime and
    // the serving threads read it without synchronization.
    socket_->serve([this, shutdown_handler = std::move(shutdown_handler)](const std::vector<uint8_t>& request_bytes) {
        return handle_request(request_bytes, shutdown_handler);
    });
}

std::vector<uint8_t> SimulationTTDevice::handle_request(
    const std::vector<uint8_t>& request_bytes, const std::function<void()>& shutdown_handler) {
    const SimulationServerRequest request = decode_request(request_bytes);

    // GetDeviceInfo returns a different wire message (SimulationServerDeviceInfo) than the
    // read/write skeleton below (SimulationServerResponse), so it is handled up front.
    if (request.command == SimulationServerCommand::GET_DEVICE_INFO) {
        try {
            return encode(describe_device(get_soc_descriptor(), backend_type()));
        } catch (const std::exception& e) {
            log_warning(tt::LogUMD, "Simulation host failed to serve device info: {}", e.what());
            SimulationServerDeviceInfo info;
            info.status = -1;
            return encode(info);
        }
    }

    // GetClusterDescriptor also returns its own wire message; serve the build's cluster-descriptor
    // YAML (empty when the build ships none) so a client can rebuild the full topology.
    if (request.command == SimulationServerCommand::GET_CLUSTER_DESCRIPTOR) {
        try {
            return encode(describe_cluster(simulator_directory_));
        } catch (const std::exception& e) {
            log_warning(tt::LogUMD, "Simulation host failed to serve cluster descriptor: {}", e.what());
            SimulationServerClusterDescriptor cluster_descriptor;
            cluster_descriptor.status = -1;
            return encode(cluster_descriptor);
        }
    }

    // Shutdown: invoke the opt-in handler (a dedicated server passes one to adopt_socket() to signal
    // its main thread to exit and tear down; an embedded host passes none, making this a no-op) and
    // ack. The handler was fixed before serving started, so it is read here without locking; it must
    // only signal -- it must not tear down from this serving thread. The ack is sent before any
    // teardown, and this serving thread hits EOF and is joined during that teardown.
    if (request.command == SimulationServerCommand::SHUTDOWN) {
        if (shutdown_handler) {
            shutdown_handler();
        }
        return encode(SimulationServerResponse{});  // status 0
    }

    // The client already translated the coordinate (translation is stateless and client-side), so
    // pass it through verbatim as LITERAL -- the shared read/write skeleton must not translate
    // again. CoreCoord defaults to CoreType::UNSPECIFIED + CoordSystem::LITERAL.
    const CoreCoord core{request.x, request.y};

    SimulationServerResponse response;
    try {
        switch (request.command) {
            case SimulationServerCommand::READ:
                response.data.resize(request.size);
                read_from_device(response.data.data(), core, request.address, request.size);
                break;
            case SimulationServerCommand::WRITE:
                UMD_ASSERT(
                    request.data.size() >= request.size,
                    error::RuntimeError,
                    fmt::format(
                        "Write request payload ({} bytes) is smaller than its size field ({})",
                        request.data.size(),
                        request.size));
                write_to_device(request.data.data(), core, request.address, request.size);
                break;
            default:
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format("Unknown simulation server command {}", static_cast<int>(request.command)));
        }
    } catch (const std::exception& e) {
        // A failed op surfaces to the client as a nonzero status rather than dropping the
        // connection, mirroring how a silicon access error is reported rather than fatal.
        log_warning(tt::LogUMD, "Simulation host failed to serve a client request: {}", e.what());
        response.status = -1;
        response.data.clear();
    }
    return encode(response);
}

void SimulationTTDevice::write_to_device(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    // Client and host are disjoint paths with nothing shared; dispatch on the named role rather
    // than a bare client_ null-check. Each helper takes device_lock itself (the client path always;
    // the host path after its is_device_closed() gate), so the lock is never dropped on either.
    if (client_mode()) {
        client_write(core, addr, mem_ptr, size);
    } else {
        host_write(core, addr, mem_ptr, size);
    }
}

void SimulationTTDevice::read_from_device(void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    if (client_mode()) {
        client_read(core, addr, mem_ptr, size);
    } else {
        host_read(core, addr, mem_ptr, size);
    }
}

void SimulationTTDevice::read_from_device_reg(void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    read_from_device(mem_ptr, core, addr, size, noc_id);
}

void SimulationTTDevice::write_to_device_reg(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    write_to_device(mem_ptr, core, addr, size, noc_id);
}

void SimulationTTDevice::host_write(CoreCoord core, uint64_t addr, const void* mem_ptr, size_t size) {
    if (is_device_closed()) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());
    if (handle_special_write(mem_ptr, translated_core, addr, size)) {
        return;
    }
    if (should_use_cached_tlb_window()) {
        cached_tlb_window_->write_block_reconfigure(mem_ptr, translated_core, addr, size, get_selected_noc_id());
    } else {
        tile_write_bytes(translated_core, addr, mem_ptr, size);
    }
}

void SimulationTTDevice::host_read(CoreCoord core, uint64_t addr, void* mem_ptr, size_t size) {
    if (is_device_closed()) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());
    if (handle_special_read(mem_ptr, translated_core, addr, size)) {
        return;
    }
    if (should_use_cached_tlb_window()) {
        cached_tlb_window_->read_block_reconfigure(mem_ptr, translated_core, addr, size, get_selected_noc_id());
    } else {
        tile_read_bytes(translated_core, addr, mem_ptr, size);
    }
    after_read();
}

void SimulationTTDevice::client_write(CoreCoord core, uint64_t addr, const void* mem_ptr, size_t size) {
    // Serialized against the host's own access and other clients sharing the one socket.
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    // Translation is client-side and stateless (same SoC descriptor the host used); the host
    // receives an already-translated coordinate and passes it through verbatim.
    // The wire protocol carries size as uint32; reject a transfer that would truncate rather than
    // silently corrupt it. (Device transfers are far below this bound in practice.)
    UMD_ASSERT(
        size <= std::numeric_limits<uint32_t>::max(),
        error::RuntimeError,
        fmt::format("Remote write size {} exceeds the protocol maximum of {} bytes", size, UINT32_MAX));
    const xy_pair translated_core =
        get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());
    SimulationServerRequest request;
    request.command = SimulationServerCommand::WRITE;
    request.x = static_cast<uint32_t>(translated_core.x);
    request.y = static_cast<uint32_t>(translated_core.y);
    request.address = addr;
    request.size = static_cast<uint32_t>(size);
    const auto* bytes = static_cast<const uint8_t*>(mem_ptr);
    request.data.assign(bytes, bytes + size);

    const SimulationServerResponse response = decode_response(client_->transact(encode(request)));
    UMD_ASSERT(
        response.status == 0,
        error::RuntimeError,
        fmt::format("Remote simulation host failed the write (status {})", response.status));
}

void SimulationTTDevice::client_read(CoreCoord core, uint64_t addr, void* mem_ptr, size_t size) {
    // Serialized against the host's own access and other clients sharing the one socket.
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    // The wire protocol carries size as uint32; reject a read that would truncate it.
    UMD_ASSERT(
        size <= std::numeric_limits<uint32_t>::max(),
        error::RuntimeError,
        fmt::format("Remote read size {} exceeds the protocol maximum of {} bytes", size, UINT32_MAX));
    const xy_pair translated_core =
        get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());
    SimulationServerRequest request;
    request.command = SimulationServerCommand::READ;
    request.x = static_cast<uint32_t>(translated_core.x);
    request.y = static_cast<uint32_t>(translated_core.y);
    request.address = addr;
    request.size = static_cast<uint32_t>(size);

    const SimulationServerResponse response = decode_response(client_->transact(encode(request)));
    UMD_ASSERT(
        response.status == 0,
        error::RuntimeError,
        fmt::format("Remote simulation host failed the read (status {})", response.status));
    UMD_ASSERT(
        response.data.size() == size,
        error::RuntimeError,
        fmt::format("Remote read returned {} bytes, expected {}", response.data.size(), size));
    if (size > 0) {
        std::memcpy(mem_ptr, response.data.data(), size);
    }
}

void SimulationTTDevice::init_tlb_allocator(uint64_t bar0_base) {
    tlb_allocator_ = std::make_shared<SimulationTlbAllocator>(bar0_base, arch);
}

void SimulationTTDevice::setup_cached_tlb_window() {
    // Quasar has no real TLBs; the communicator handles all I/O underneath. The 4GB size for Quasar is
    // a dummy value -- it just needs to be large enough so that TlbWindow::validate doesn't reject any
    // valid access (size 0 would cause division by zero in the TLB handle configure).
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    static constexpr size_t SIZE_4GB = 4ULL * 1024 * 1024 * 1024;
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            cached_tlb_window_ = get_io_window({}, TlbMapping::WC, SIZE_2MB);
            break;
        case tt::ARCH::WORMHOLE_B0:
            cached_tlb_window_ = get_io_window({}, TlbMapping::WC, SIZE_16MB);
            break;
        case tt::ARCH::QUASAR:
            cached_tlb_window_ = get_io_window({}, TlbMapping::WC, SIZE_4GB);
            break;
        default:
            log_debug(
                LogUMD,
                "Architecture {} does not support TLB allocation, leaving cached_tlb_window_ null.",
                tt::arch_to_str(arch));
            break;
    }
}

std::unique_ptr<TlbWindow> SimulationTTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    // tlb_allocator_ is only built by init_tlb_allocator(), which the client-mode path never calls
    // (it runs no local backend). Fail loudly instead of dereferencing a null allocator.
    UMD_ASSERT(
        tlb_allocator_ != nullptr,
        error::RuntimeError,
        "TLB allocator is not initialized; get_io_window is unavailable (e.g. in client mode).");
    int tlb_index = tlb_allocator_->allocate_tlb_index(size);
    if (tlb_index == -1) {
        UMD_THROW(error::RuntimeError, "No available TLB of requested size.");
    }
    // QUASAR bypasses the bitmap allocator (pools are empty by design); pass the requested size
    // through, since get_tlb_size_from_index has no pool to look up for the bypass index.
    size_t actual_size = (get_arch() == tt::ARCH::QUASAR) ? size : tlb_allocator_->get_tlb_size_from_index(tlb_index);
    return create_tlb_window(tlb_index, actual_size, mapping, config);
}

bool SimulationTTDevice::get_noc_translation_enabled() {
    // Simulation backends operate on logical/virtual coordinates end-to-end; NOC translation is never
    // applied.
    return false;
}

void SimulationTTDevice::noc_multicast_write(
    const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    multicast_write_via_unicast(src, size, core_start, core_end, addr, noc_id);
}

void SimulationTTDevice::noc_multicast_write(const void* src, size_t size, uint64_t addr, NocId noc_id) {
    auto [start, end] =
        get_soc_descriptor().get_bounding_rectangle(is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0);
    noc_multicast_write(src, size, start, end, addr, noc_id);
}

void SimulationTTDevice::dma_multicast_write(
    void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "DMA multicast write is not supported for simulation devices.");
}

void SimulationTTDevice::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    const std::vector<CoreCoord> arc_cores = get_soc_descriptor().get_cores(CoreType::ARC);
    UMD_ASSERT(!arc_cores.empty(), error::RuntimeError, "Simulation SoC descriptor has no ARC core.");
    read_from_device(
        mem_ptr, arc_cores.front(), get_architecture_registers(arch).arc_apb_noc_base_address + arc_addr_offset, size);
}

void SimulationTTDevice::write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    const std::vector<CoreCoord> arc_cores = get_soc_descriptor().get_cores(CoreType::ARC);
    UMD_ASSERT(!arc_cores.empty(), error::RuntimeError, "Simulation SoC descriptor has no ARC core.");
    write_to_device(
        mem_ptr, arc_cores.front(), get_architecture_registers(arch).arc_apb_noc_base_address + arc_addr_offset, size);
}

void SimulationTTDevice::set_arc_coordinate() {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            arc_core_noc0 = wormhole::ARC_CORES_NOC0[0];
            arc_core_noc1 = tt_xy_pair(
                wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
                wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y]);
            return;
        case tt::ARCH::BLACKHOLE:
            arc_core_noc0 = blackhole::get_arc_core(get_noc_translation_enabled(), /*use_noc1=*/false);
            arc_core_noc1 = blackhole::get_arc_core(get_noc_translation_enabled(), /*use_noc1=*/true);
            return;
        case tt::ARCH::QUASAR:
            arc_core_noc0 = grendel::get_arc_core(get_noc_translation_enabled(), /*use_noc1=*/false);
            arc_core_noc1 = grendel::get_arc_core(get_noc_translation_enabled(), /*use_noc1=*/true);
            return;
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("No ARC core coordinates defined for {} architecture.", arch_to_str(arch)));
    }
}

// The simulation backend reaches ARC over the NOC, so CSM access uses the CSM NOC base address, not
// the BAR0 mailbox offset. Architectures which expose no CSM window over the NOC leave that base at
// zero; reject those rather than issuing a read at a bogus address.
uint64_t SimulationTTDevice::get_arc_csm_noc_base_address() const {
    const uint64_t base = get_architecture_registers(arch).arc_csm_noc_base_address;
    UMD_ASSERT(
        base != 0,
        error::RuntimeError,
        fmt::format("ARC CSM access over NOC is not supported for {} architecture.", arch_to_str(arch)));
    return base;
}

void SimulationTTDevice::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    const std::vector<CoreCoord> arc_cores = get_soc_descriptor().get_cores(CoreType::ARC);
    UMD_ASSERT(!arc_cores.empty(), error::RuntimeError, "Simulation SoC descriptor has no ARC core.");
    read_from_device(mem_ptr, arc_cores.front(), get_arc_csm_noc_base_address() + arc_addr_offset, size);
}

void SimulationTTDevice::write_to_arc_csm(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    const std::vector<CoreCoord> arc_cores = get_soc_descriptor().get_cores(CoreType::ARC);
    UMD_ASSERT(!arc_cores.empty(), error::RuntimeError, "Simulation SoC descriptor has no ARC core.");
    write_to_device(mem_ptr, arc_cores.front(), get_arc_csm_noc_base_address() + arc_addr_offset, size);
}

uint32_t SimulationTTDevice::get_clock() {
    UMD_THROW(error::RuntimeError, "Getting clock is not supported for simulation devices.");
}

uint32_t SimulationTTDevice::get_min_clock_freq() {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return wormhole::AICLK_IDLE_VAL;
        case tt::ARCH::BLACKHOLE:
            return blackhole::AICLK_IDLE_VAL;
        case tt::ARCH::QUASAR:
            return grendel::AICLK_IDLE_VAL;
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Getting minimum clock frequency is not supported for simulation arch {}.", arch_to_str(arch)));
    }
}

void SimulationTTDevice::retrain_dram_core(const uint32_t dram_channel) {
    UMD_THROW(error::RuntimeError, "DRAM retraining is not supported for simulation devices.");
}

}  // namespace tt::umd
