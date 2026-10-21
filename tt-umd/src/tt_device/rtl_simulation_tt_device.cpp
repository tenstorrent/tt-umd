// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/rtl_simulation_tt_device.hpp"

#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "pcie/rtl_sim_tlb_handle.hpp"
#include "pcie/rtl_sim_tlb_window.hpp"
#include "simulation/simulation_server_socket.hpp"
#include "simulation/simulator_paths.hpp"
#include "simulation/word_access.hpp"
#include "tt-umd-utils/internal/host.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/chip_helpers/simulation_sysmem_manager.hpp"
#include "tt-umd/chip_helpers/simulation_tlb_allocator.hpp"
#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/coordinates/att/configs/grendel_qsr1_att_map.hpp"
#include "tt-umd/coordinates/att/configs/horizon_2x3_att_map.hpp"
#include "tt-umd/coordinates/att/configs/horizon_x2_att_map.hpp"
#include "tt-umd/coordinates/att/configs/horizon_x2_columns_att_map.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/simulation/rtl_sim_communicator.hpp"
#include "tt-umd/simulation/simulation_client.hpp"
#include "tt-umd/simulation/simulation_device_identity.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/risc_type.hpp"
#include "tt-umd/types/tlb.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device/protocol/simulation_noc_protocol.hpp"
#include "tt_device/reset/rtl_sim_risc_reset.hpp"
#include "tt_device/reset/tsc_risc_reset.hpp"
#include "tt_device_model/simulation_tt_device_model.hpp"

namespace tt::umd {

namespace {

// Names the ATT map a simulation target implements, mirroring tt-metal's TT_METAL_NOC_ATT. Left
// unset, no coordinate is folded into an address: architecture alone cannot say whether a model
// carries an ATT, and the Quasar models that do not would be sent addresses they cannot route.
constexpr const char* NOC_ATT_MAP_ENV_VAR = "TT_UMD_NOC_ATT";

// The maps this build carries, by the name TT_UMD_NOC_ATT gives them. A map has one table for every
// access point, or one per access point of the build's ip_layout.yaml, in its order, when each of a
// device's access points has its own flat addresses.
struct NamedAttMap {
    const char* name;
    att::Table<const att::MapData*> tables;
};

constexpr const att::MapData* GRENDEL_QSR1_TABLES[] = {&att::GRENDEL_QSR1_MAP};
constexpr const att::MapData* HORIZON_2X3_TABLES[] = {&att::HORIZON_2X3_MAP};
constexpr const att::MapData* HORIZON_X2_TABLES[] = {
    &att::HORIZON_X2_DEV0_NOC2AXI0_MAP,
    &att::HORIZON_X2_DEV0_NOC2AXI1_MAP,
    &att::HORIZON_X2_DEV1_NOC2AXI0_MAP,
    &att::HORIZON_X2_DEV1_NOC2AXI1_MAP};
constexpr const att::MapData* HORIZON_X2_COLUMNS_TABLES[] = {
    &att::HORIZON_X2_DEV0_COLUMN0_MAP,
    &att::HORIZON_X2_DEV0_COLUMN1_MAP,
    &att::HORIZON_X2_DEV1_COLUMN0_MAP,
    &att::HORIZON_X2_DEV1_COLUMN1_MAP};

constexpr NamedAttMap ATT_MAPS[] = {
    {"grendel_qsr1", GRENDEL_QSR1_TABLES},
    {"horizon_2x3", HORIZON_2X3_TABLES},
    {"horizon_x2", HORIZON_X2_TABLES},
    {"horizon_x2_columns", HORIZON_X2_COLUMNS_TABLES},
};

// The IP variants (the SoC descriptor's ip_variant) whose cores UMD resets itself, through each tile's TSC.
struct VariantTileReset {
    tt::IpVariant variant;
    const TscResetLayout* layout;
};

constexpr VariantTileReset TILE_RESETS[] = {
    {tt::IpVariant::HORIZON, &HORIZON_TSC_RESET},
};

// Access point @p access_point's table in @p map.
const att::MapData& table_for(const NamedAttMap& map, AccessPointId access_point) {
    const uint32_t index = map.tables.size() == 1 ? 0 : static_cast<uint32_t>(access_point);
    UMD_ASSERT(
        index < map.tables.size(),
        error::RuntimeError,
        fmt::format(
            "{} names ATT map '{}', which has tables for {} access points; this is access point {}.",
            NOC_ATT_MAP_ENV_VAR,
            map.name,
            map.tables.size(),
            static_cast<uint32_t>(access_point)));
    return *map.tables[index];
}

}  // namespace

static_assert(!std::is_abstract<RtlSimulationTTDevice>(), "RtlSimulationTTDevice must be non-abstract.");

static constexpr ChipId DEFAULT_CHIP_ID = 0;

namespace {

// The access point a device talks over: its first. The others are known but not used yet.
const RtlSimAccessPoint& io_access_point(const IpBlockLayoutDevice& layout_device) {
    UMD_ASSERT(
        !layout_device.access_points.empty(),
        error::RuntimeError,
        fmt::format("Device {} has no access point to talk over.", static_cast<uint32_t>(layout_device.device)));
    return layout_device.access_points.front();
}

void validate_noc_for_arch(NocId noc_id, tt::ARCH arch) {
    if (noc_id == NocId::SYSTEM_NOC && arch != tt::ARCH::QUASAR) {
        UMD_THROW(error::RuntimeError, "System NOC is only supported on Grendel (Quasar) architecture.");
    }
    if (noc_id == NocId::NOC1 && arch == tt::ARCH::QUASAR) {
        UMD_THROW(error::RuntimeError, "NOC1 is not supported on Grendel (Quasar) architecture.");
    }
}
}  // namespace

std::unique_ptr<RtlSimulationTTDevice> RtlSimulationTTDevice::create(
    const std::filesystem::path& simulator_directory, int num_host_mem_channels) {
    auto soc_desc_path = get_simulator_soc_descriptor_path(simulator_directory);
    SocDescriptor soc_descriptor = SocDescriptor(std::make_shared<SocArchDescriptor>(soc_desc_path));
    return std::make_unique<RtlSimulationTTDevice>(
        simulator_directory, soc_descriptor, DEFAULT_CHIP_ID, num_host_mem_channels);
}

std::unique_ptr<RtlSimulationTTDevice> RtlSimulationTTDevice::create_client(
    ChipId chip_id, std::unique_ptr<SimulationClient> client, const SimulationServerDeviceInfo& device_info) {
    UMD_ASSERT(
        client != nullptr,
        error::RuntimeError,
        "Client-mode RtlSimulationTTDevice requires a non-null SimulationClient.");
    UMD_ASSERT(
        device_info.status == 0,
        error::RuntimeError,
        fmt::format("Cannot build a client device from failed device info (status {}).", device_info.status));
    // The connector already fetched the device identity over the socket (it needed the backend type
    // to pick this class); build the SoC descriptor from it -- a client does not read a local build.
    SocDescriptor soc_descriptor = build_soc_descriptor(device_info);
    // make_unique can't reach the private client-mode constructor; this static factory can via new.
    return std::unique_ptr<RtlSimulationTTDevice>(
        new RtlSimulationTTDevice(soc_descriptor, chip_id, std::move(client)));
}

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels) :
    RtlSimulationTTDevice(
        simulator_directory,
        soc_descriptor,
        chip_id,
        num_host_mem_channels,
        std::nullopt,
        std::nullopt,
        std::make_unique<RtlSimCommunicator>(simulator_directory)) {}

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const std::shared_ptr<RtlSimSession>& session,
    const IpBlockLayoutDevice& layout_device,
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels) :
    RtlSimulationTTDevice(
        RtlSimSocket(session, io_access_point(layout_device).host_access),
        layout_device,
        simulator_directory,
        soc_descriptor,
        chip_id,
        num_host_mem_channels) {}

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const RtlSimSocket& socket,
    const IpBlockLayoutDevice& layout_device,
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels) :
    RtlSimulationTTDevice(
        simulator_directory,
        soc_descriptor,
        chip_id,
        num_host_mem_channels,
        socket,
        layout_device,
        std::make_unique<RtlSimCommunicator>(socket)) {}

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels,
    std::optional<RtlSimSocket> socket,
    std::optional<IpBlockLayoutDevice> layout_device,
    std::unique_ptr<RtlSimCommunicator> communicator) :
    SimulationTTDevice(
        ModelHandle(std::make_unique<SimulationTTDeviceModel>(soc_descriptor)),
        simulator_directory,
        std::make_unique<SimulationSysmemManager>(
            num_host_mem_channels, soc_descriptor.arch, static_cast<uint32_t>(chip_id))),
    session_socket_(std::move(socket)),
    communicator_(std::move(communicator)),
    layout_device_(std::move(layout_device)) {
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation TTDevice");
    set_soc_descriptor(soc_descriptor);
    setup_noc_address_resolver();
    get_simulation_model()->set_risc_reset(make_risc_reset(chip_id));

    // Host/local mode: the lifecycle drives the in-process RTL backend (the communicator).
    setup_ = [this, num_host_mem_channels] { initialize_backend(num_host_mem_channels); };
    teardown_ = [this] { communicator_->shutdown(); };
    setup_();
}

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const SocDescriptor& soc_descriptor, ChipId chip_id, std::unique_ptr<SimulationClient> client) :
    SimulationTTDevice(ModelHandle(std::make_unique<SimulationTTDeviceModel>(soc_descriptor)), std::move(client)) {
    set_soc_descriptor(soc_descriptor);
    // A client-mode device drives no backend of its own, so it has no communicator to reset through.
    get_simulation_model()->set_risc_reset(
        std::make_unique<RtlSimRiscReset>(/*communicator=*/nullptr, soc_descriptor.arch, device_lock));

    // Client mode: the lifecycle drives the remote host over the socket. read/write are not wired
    // here -- the SimulationClient has no device I/O yet -- so those throw until the API grows.
    // create_client() has already validated that client_ is non-null.
    setup_ = [this] { attach_client(); };
    teardown_ = [this] { detach_client(); };
    setup_();
}

void RtlSimulationTTDevice::setup_noc_address_resolver() {
    const std::optional<std::string> map_name = utils::get_env_var_value(NOC_ATT_MAP_ENV_VAR);
    if (!map_name.has_value()) {
        return;
    }

    const auto* const known = std::find_if(
        std::begin(ATT_MAPS), std::end(ATT_MAPS), [&](const NamedAttMap& map) { return *map_name == map.name; });
    std::vector<std::string> names;
    for (const NamedAttMap& map : ATT_MAPS) {
        names.emplace_back(map.name);
    }
    UMD_ASSERT(
        known != std::end(ATT_MAPS),
        error::RuntimeError,
        fmt::format(
            "{} names ATT map '{}', which this build does not carry. Known maps: {}.",
            NOC_ATT_MAP_ENV_VAR,
            *map_name,
            fmt::join(names, ", ")));

    // Outside an ip_layout.yaml a device has one implicit access point, the map's first.
    if (!layout_device_.has_value()) {
        noc_address_resolver_ = std::make_unique<att::EndpointResolver>(table_for(*known, AccessPointId{0}));
        global_address_mode_ = true;
        return;
    }
    for (const RtlSimAccessPoint& access_point : layout_device_->access_points) {
        access_point_resolvers_.emplace(access_point.id, table_for(*known, access_point.id));
    }
    noc_address_resolver_ =
        std::make_unique<att::EndpointResolver>(table_for(*known, io_access_point(*layout_device_).id));
    global_address_mode_ = true;
}

std::unique_ptr<RiscReset> RtlSimulationTTDevice::make_risc_reset(ChipId chip_id) {
    const std::optional<tt::IpVariant> variant = get_soc_descriptor().get_arch_descriptor().get_ip_variant();
    const auto* const known =
        std::find_if(std::begin(TILE_RESETS), std::end(TILE_RESETS), [&](const VariantTileReset& reset) {
            return variant == reset.variant;
        });
    if (known == std::end(TILE_RESETS)) {
        return std::make_unique<RtlSimRiscReset>(communicator_.get(), get_soc_descriptor().arch, device_lock);
    }
    noc_protocol_ = std::make_unique<SimulationNocProtocol>(this, static_cast<int>(chip_id));
    return std::make_unique<TscRiscReset>(noc_protocol_.get(), *known->layout);
}

std::vector<AccessPointId> RtlSimulationTTDevice::get_access_points() const {
    std::vector<AccessPointId> access_points;
    if (layout_device_.has_value()) {
        for (const RtlSimAccessPoint& access_point : layout_device_->access_points) {
            access_points.push_back(access_point.id);
        }
    }
    return access_points;
}

std::optional<uint64_t> RtlSimulationTTDevice::get_flat_address(
    AccessPointId access_point, CoreCoord core, WindowFlags flags, IoOrdering ordering, uint64_t offset) const {
    const std::vector<AccessPointId> access_points = get_access_points();
    UMD_ASSERT(
        std::find(access_points.begin(), access_points.end(), access_point) != access_points.end(),
        error::RuntimeError,
        fmt::format("Access point {} is not one of this device's.", static_cast<uint32_t>(access_point)));
    // Without a map (TT_UMD_NOC_ATT) the device has no flat addresses.
    const auto resolver = access_point_resolvers_.find(access_point);
    if (resolver == access_point_resolvers_.end()) {
        return std::nullopt;
    }
    return att::resolve_io_window(resolver->second, get_soc_descriptor(), core, flags, ordering, offset);
}

bool RtlSimulationTTDevice::should_use_cached_tlb_window() {
    // A global address already names its destination, so the dummy window Quasar would allocate to
    // carry that coordinate in tlb_data has nothing left to carry.
    return !global_address_mode_ && cached_tlb_window_ != nullptr;
}

void RtlSimulationTTDevice::initialize_backend(int num_host_mem_channels) {
    // Register sysmem callbacks so the simulator can read/write host memory.
    if (num_host_mem_channels > 0) {
        SimulationSysmemManager* mgr = sysmem_manager_.get();
        size_t num_channels = mgr->get_num_host_mem_channels();
        communicator_->set_ram_callbacks(
            // Write callback: simulator writes data into host sysmem.
            [mgr, num_channels](uint64_t address, const void* data, uint32_t size) {
                uint64_t pcie_base = mgr->get_pcie_base();
                UMD_ASSERT(address >= pcie_base, error::RuntimeError, "RAM callback address underflow.");
                if (mgr->write_mapped_buffer(address, data, size)) {
                    return;
                }
                UMD_ASSERT(
                    address < pcie_base + mgr->get_mapped_arena_offset(),
                    error::RuntimeError,
                    "RAM callback mapped-buffer address is not registered.");
                uint64_t offset = address - pcie_base;
                uint16_t channel = static_cast<uint16_t>(offset / (1ULL << 30));
                UMD_ASSERT(channel < num_channels, error::RuntimeError, "RAM callback channel out of range.");
                uint64_t offset_in_channel = offset % (1ULL << 30);
                mgr->write_to_sysmem(channel, data, offset_in_channel, size);
            },
            // Read callback: simulator reads data from host sysmem.
            [mgr, num_channels](uint64_t address, void* data_out, uint32_t size) {
                uint64_t pcie_base = mgr->get_pcie_base();
                UMD_ASSERT(address >= pcie_base, error::RuntimeError, "RAM callback address underflow.");
                if (mgr->read_mapped_buffer(address, data_out, size)) {
                    return;
                }
                UMD_ASSERT(
                    address < pcie_base + mgr->get_mapped_arena_offset(),
                    error::RuntimeError,
                    "RAM callback mapped-buffer address is not registered.");
                uint64_t offset = address - pcie_base;
                uint16_t channel = static_cast<uint16_t>(offset / (1ULL << 30));
                UMD_ASSERT(channel < num_channels, error::RuntimeError, "RAM callback channel out of range.");
                uint64_t offset_in_channel = offset % (1ULL << 30);
                mgr->read_from_sysmem(channel, data_out, offset_in_channel, size);
            });
    }

    communicator_->initialize();

    init_tlb_allocator(/*bar0_base=*/0, /*bar4_base=*/0);
    setup_cached_tlb_window();
}

std::unique_ptr<TlbWindow> RtlSimulationTTDevice::create_tlb_window(
    int tlb_index, size_t size, TlbMapping mapping, tlb_data config) {
    auto handle = RtlSimTlbHandle::create(tlb_allocator_, tlb_index, size, mapping);
    // Static TLB windows resolve addresses like every other host access: through the resolver in
    // flat-address mode, as raw coordinates otherwise.
    return std::make_unique<RtlSimTlbWindow>(
        std::move(handle),
        communicator_.get(),
        global_address_mode_ ? noc_address_resolver_.get() : nullptr,
        &get_soc_descriptor(),
        config);
}

RtlSimulationTTDevice::~RtlSimulationTTDevice() {
    // Stop serving (and remove the socket) before tearing the backend down.
    socket_.reset();
    // teardown_ is communicator_->shutdown() (host) or client_->detach() (client). The
    // destructor is implicitly noexcept, so this is best-effort.
    if (teardown_) {
        try {
            teardown_();
        } catch (const std::exception& e) {
            log_warning(tt::LogEmulationDriver, "RtlSimulationTTDevice teardown failed: {}", e.what());
        }
    }
}

void RtlSimulationTTDevice::tile_read_bytes(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) {
    if (global_address_mode_) {
        // The simulator moves whole words; see word_access.hpp. Callers hold device_lock.
        read_bytes_as_words(addr, mem_ptr, static_cast<uint32_t>(size), [this](uint64_t a, void* d, uint32_t n) {
            communicator_->global_read_words(a, d, n);
        });
        return;
    }
    communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
}

void RtlSimulationTTDevice::tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) {
    // In this mode addr already names the destination, so the coordinate is not sent: the
    // simulator has nothing to translate and cannot resolve a resolved address again.
    if (global_address_mode_) {
        // The simulator moves whole words, so a partial first or last word is read, merged and
        // written back; see word_access.hpp. Callers hold device_lock, so no other host access to
        // this device runs in between.
        write_bytes_as_words(
            addr,
            mem_ptr,
            static_cast<uint32_t>(size),
            [this](uint64_t a, void* d, uint32_t n) { communicator_->global_read_words(a, d, n); },
            [this](uint64_t a, const void* d, uint32_t n) { communicator_->global_write_words(a, d, n); });
        return;
    }
    communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
}

bool RtlSimulationTTDevice::handle_special_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    return smn_read(mem_ptr, core, addr, size);
}

bool RtlSimulationTTDevice::handle_special_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    return smn_write(mem_ptr, core, addr, size);
}

bool RtlSimulationTTDevice::smn_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    NocId noc_id = get_selected_noc_id();
    validate_noc_for_arch(noc_id, get_soc_descriptor().arch);
    if (noc_id == NocId::SYSTEM_NOC) {
        communicator_->smn_tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
        return true;
    }
    return false;
}

bool RtlSimulationTTDevice::smn_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, addr, core.str());
    NocId noc_id = get_selected_noc_id();
    validate_noc_for_arch(noc_id, get_soc_descriptor().arch);
    if (noc_id == NocId::SYSTEM_NOC) {
        communicator_->smn_tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
        return true;
    }
    return false;
}

}  // namespace tt::umd
