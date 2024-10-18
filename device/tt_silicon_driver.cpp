// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <boost/interprocess/permissions.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <ratio>
#include <algorithm>
#include <filesystem>
#include <stdarg.h>
#include <optional>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <errno.h>

#include "yaml-cpp/yaml.h"
#include "common/logger.hpp"

#include "device/cpuset_lib.hpp"
#include "device/driver_atomics.h"
#include "device/architecture.h"
#include "device/architecture_implementation.h"
#include "device/tlb.h"
#include "device/tt_arch_types.h"
#include "tt_device.h"
#include "ioctl.h"

using namespace boost::interprocess;
using namespace tt;

const uint32_t g_MAX_HOST_MEM_CHANNELS = 4;

const uint32_t HUGEPAGE_REGION_SIZE = 1 << 30; // 1GB
const uint32_t HUGEPAGE_MAP_MASK = HUGEPAGE_REGION_SIZE - 1;

static const uint32_t MSG_ERROR_REPLY = 0xFFFFFFFF;

// Hardcode (but allow override) of path now, to support environments with other 1GB hugepage mounts not for runtime.
const char* hugepage_dir_env = std::getenv("TT_BACKEND_HUGEPAGE_DIR");
std::string hugepage_dir = hugepage_dir_env ? hugepage_dir_env : "/dev/hugepages-1G";

// TLB size for DRAM on blackhole - 4GB
const uint64_t BH_4GB_TLB_SIZE = 4ULL * 1024 * 1024 * 1024;

// TODO: Remove in favor of cluster descriptor method, when it becomes available.
// Metal uses this function to determine the architecture of the first PCIe chip
// and then verifies that all subsequent chips are of the same architecture.  It
// looks like Metal is doing this because we don't provide any other way... When
// we are further along in our refactoring efforts and `tt_device` is more of a
// Cluster abstraction, we should provide Metal with interfaces for:
//      1. Checking that all chips are of the same architecture (we may not care
//         about this, but the application might).
//      2. Getting the architecture of a specific chip.
// Until then... I'm putting this function back so that Metal will still build
// next time someone bumps its UMD submodule version.
tt::ARCH detect_arch(int pci_device_num) {
    const auto devices_info = PCIDevice::enumerate_devices_info();
    const auto it = devices_info.find(pci_device_num);
    if (it == devices_info.end()) {
        return tt::ARCH::Invalid;
    }

    const auto info = it->second;
    return info.get_arch();
}

// TODO: Remove in favor of cluster descriptor method, when it becomes available.
// There is also a function which just wants to get any architecture, since it
// presumably already checked that all archs are the same.
tt::ARCH detect_arch() {
    const auto devices_info = PCIDevice::enumerate_devices_info();
    if (devices_info.empty()) {
        return tt::ARCH::Invalid;
    }
    return devices_info.begin()->second.get_arch();
}

template <typename T>
void size_buffer_to_capacity(std::vector<T> &data_buf, std::size_t size_in_bytes) {
    std::size_t target_size = 0;
    if (size_in_bytes > 0) {
        target_size = ((size_in_bytes - 1) / sizeof(T)) + 1;
    }
    data_buf.resize(target_size);
}

// Get number of 1GB host hugepages installed. They are used for host queues.
uint32_t get_num_hugepages(){

    log_assert(HUGEPAGE_REGION_SIZE == 1 << 30, "Hugepages must be 1GB in size");
    std::string nr_hugepages_path = "/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages";
    std::ifstream hugepages_file(nr_hugepages_path);
    uint32_t num_hugepages = 0;

    if(hugepages_file.is_open()) {
        std::string value;
        std::getline(hugepages_file, value);
        num_hugepages = std::stoi(value);
        log_debug(LogSiliconDriver, "Parsed num_hugepages: {} from {}", num_hugepages, nr_hugepages_path);
    } else {
        log_fatal("{} - Cannot open {}. errno: {}", __FUNCTION__, nr_hugepages_path, std::strerror(errno));
    }

    return num_hugepages;

}

// Dynamically figure out how many host memory channels (based on hugepages installed) for each device, based on arch.
uint32_t get_available_num_host_mem_channels(const uint32_t num_channels_per_device_target, const uint16_t device_id, const uint16_t revision_id) {

    // To minimally support hybrid dev systems with mix of ARCH, get only devices matching current ARCH's device_id.
    uint32_t total_num_tt_mmio_devices      = tt::cpuset::tt_cpuset_allocator::get_num_tt_pci_devices();
    uint32_t num_tt_mmio_devices_for_arch   = tt::cpuset::tt_cpuset_allocator::get_num_tt_pci_devices_by_pci_device_id(device_id, revision_id);
    uint32_t total_hugepages                = get_num_hugepages();

    // This shouldn't happen on silicon machines.
    if (num_tt_mmio_devices_for_arch == 0) {
        log_warning(LogSiliconDriver,
            "No TT devices found that match PCI device_id: 0x{:x} revision: {}, returning NumHostMemChannels:0",
            device_id, revision_id);
        return 0;
    }

    // GS will use P2P + 1 channel, others may support 4 host channels. Apply min of 1 to not completely break setups that were incomplete
    // ie fewer hugepages than devices, which would partially work previously for some devices.
    uint32_t num_channels_per_device_available = std::min(num_channels_per_device_target, std::max((uint32_t) 1, total_hugepages / num_tt_mmio_devices_for_arch));

    // Perform some helpful assertion checks to guard against common pitfalls that would show up as runtime issues later on.
    if (total_num_tt_mmio_devices > num_tt_mmio_devices_for_arch) {
        log_warning(LogSiliconDriver,
            "Hybrid system mixing different TTDevices - this is not well supported. Ensure sufficient Hugepages/HostMemChannels per device.");
    }

    if (total_hugepages < num_tt_mmio_devices_for_arch) {
        log_warning(LogSiliconDriver,
            "Insufficient NumHugepages: {} should be at least NumMMIODevices: {} for device_id: 0x{:x} revision: {}. NumHostMemChannels would be 0, bumping to 1.",
            total_hugepages, num_tt_mmio_devices_for_arch, device_id, revision_id);
    }

    if (num_channels_per_device_available < num_channels_per_device_target) {
        log_warning(LogSiliconDriver,
            "NumHostMemChannels: {} used for device_id: 0x{:x} less than target: {}. Workload will fail if it exceeds NumHostMemChannels. Increase Number of Hugepages.",
            num_channels_per_device_available, device_id, num_channels_per_device_target);
    }

    log_assert(num_channels_per_device_available <= g_MAX_HOST_MEM_CHANNELS,
        "NumHostMemChannels: {} exceeds supported maximum: {}, this is unexpected.",
        num_channels_per_device_available, g_MAX_HOST_MEM_CHANNELS);

    return num_channels_per_device_available;

}

bool is_char_dev(const dirent *ent, const char *parent_dir) {
    if (ent->d_type == DT_UNKNOWN || ent->d_type == DT_LNK) {
        char name[2 * NAME_MAX + 2];
        strcpy(name, parent_dir);
        strcat(name, "/");
        strcat(name, ent->d_name);

        struct stat stat_result;
        if (stat(name, &stat_result) == -1) {
            return false;
        }

        return ((stat_result.st_mode & S_IFMT) == S_IFCHR);
    } else {
        return (ent->d_type == DT_CHR);
    }
}



// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------

#include "tt_silicon_driver_common.hpp"
#include "tt_xy_pair.h"
#include <thread>
#include <fstream>
#include <iomanip>


struct routing_cmd_t {
    uint64_t sys_addr;
    uint32_t data;
    uint32_t flags;
    uint16_t rack;
    uint16_t src_resp_buf_index;
    uint32_t local_buf_index;
    uint8_t  src_resp_q_id;
    uint8_t  host_mem_txn_id;
    uint16_t padding;
    uint32_t src_addr_tag; //upper 32-bits of request source address.
};

struct remote_update_ptr_t{
  uint32_t ptr;
  uint32_t pad[3];
};

namespace {
    struct tt_4_byte_aligned_buffer {
        // Stores a 4 byte aligned buffer
        // If the input buffer is already 4 byte aligned, this is a nop
        std::uint32_t* local_storage = nullptr;
        std::uint32_t input_size = 0;
        std::uint32_t block_size = 0;

        tt_4_byte_aligned_buffer(const void* mem_ptr, uint32_t size_in_bytes) {
            input_size = size_in_bytes;
            local_storage = (uint32_t*)mem_ptr;
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            uint32_t aligned_size = (size_in_bytes + alignment_mask) & ~alignment_mask;

            if(size_in_bytes < aligned_size) {
                local_storage = new uint32_t[aligned_size / sizeof(uint32_t)];
            }
            block_size = aligned_size;
        }

        ~tt_4_byte_aligned_buffer() {
            if(block_size > input_size) {
                delete [] local_storage;
            }
        }
    };
}

bool tt_SiliconDevice::address_in_tlb_space(uint32_t address, uint32_t size_in_bytes, int32_t tlb_index, uint64_t tlb_size, std::uint32_t chip) {
    return ((tlb_config_map.at(chip).find(tlb_index) != tlb_config_map.at(chip).end()) && address >= tlb_config_map.at(chip).at(tlb_index) && (address + size_in_bytes <= tlb_config_map.at(chip).at(tlb_index) + tlb_size));
}

std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_SiliconDevice::get_virtual_soc_descriptors() {
    return soc_descriptor_per_chip;
}

void tt_SiliconDevice::initialize_interprocess_mutexes(int pci_interface_id, bool cleanup_mutexes_in_shm) {
    // These mutexes are intended to be based on physical devices/pci-intf not logical. Set these up ahead of time here (during device init)
    // since its unsafe to modify shared state during multithreaded runtime.
    // cleanup_mutexes_in_shm is tied to clean_system_resources from the constructor. The main process is responsible for initializing the driver with this
    // field set to cleanup after an aborted process.

    // Store old mask and clear processes umask
    auto old_umask = umask(0);
    permissions unrestricted_permissions;
    unrestricted_permissions.set_unrestricted();
    std::string mutex_name = "";

    // Initialize Dynamic TLB mutexes
    for(auto &tlb : dynamic_tlb_config) {
        mutex_name = tlb.first + std::to_string(pci_interface_id);
        if (cleanup_mutexes_in_shm) named_mutex::remove(mutex_name.c_str());
        hardware_resource_mutex_map[mutex_name] = std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);
    }

    // Initialize ARC core mutex
    mutex_name = fmt::format("ARC_MSG{}", pci_interface_id);
    if (cleanup_mutexes_in_shm) named_mutex::remove(mutex_name.c_str());
    hardware_resource_mutex_map[mutex_name] = std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);

    if (arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0) {
        mutex_name = NON_MMIO_MUTEX_NAME + std::to_string(pci_interface_id);
        // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for ethernet broadcast
        if (cleanup_mutexes_in_shm) named_mutex::remove(mutex_name.c_str());
        hardware_resource_mutex_map[mutex_name] = std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic
    mutex_name = MEM_BARRIER_MUTEX_NAME + std::to_string(pci_interface_id);
    if (cleanup_mutexes_in_shm) named_mutex::remove(mutex_name.c_str());
    hardware_resource_mutex_map[mutex_name] = std::make_shared<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions);
    
    // Restore old mask
    umask(old_umask);
}

void tt_SiliconDevice::create_device(const std::unordered_set<chip_id_t> &target_mmio_device_ids, const uint32_t &num_host_mem_ch_per_mmio_device, const bool skip_driver_allocs, const bool clean_system_resources) {
    log_debug(LogSiliconDriver, "tt_SiliconDevice::tt_SiliconDevice");

    // Don't buffer stdout.
    setbuf(stdout, NULL);

    // Just use PCI interface id from physical_device_id given by cluster desc mmio map. For GS, already virtualized to use available devices.
    auto logical_to_physical_device_id_map = ndesc->get_chips_with_mmio();

    log_assert(target_mmio_device_ids.size() > 0, "Must provide set of target_mmio_device_ids to tt_SiliconDevice constructor now.");

    for (const chip_id_t &logical_device_id : target_mmio_device_ids) {
        log_assert(logical_to_physical_device_id_map.count(logical_device_id) != 0, "Cannot find logical mmio device_id: {} in cluster desc / logical-to-physical-map", logical_device_id);
        int pci_interface_id = logical_to_physical_device_id_map.at(logical_device_id);

        if (!m_pci_device_map.count(logical_device_id)) {
            log_debug(LogSiliconDriver, "Opening TT_PCI_INTERFACE_ID {} for netlist target_device_id: {}", pci_interface_id, logical_device_id);
            m_pci_device_map.insert({logical_device_id, std::make_unique<PCIDevice>(pci_interface_id, logical_device_id)});
        }
        auto dev = m_pci_device_map.at(logical_device_id).get();

        uint16_t pcie_device_id = dev->get_pci_device_id();
        uint32_t pcie_revision = dev->get_pci_revision();
        // TODO: get rid of this, it doesn't make any sense.
        m_num_host_mem_channels = get_available_num_host_mem_channels(num_host_mem_ch_per_mmio_device, pcie_device_id, pcie_revision);
        if (dev->get_arch() == tt::ARCH::BLACKHOLE && m_num_host_mem_channels > 1) {
            // TODO: Implement support for multiple host channels on BLACKHOLE.
            log_warning(LogSiliconDriver, "Forcing a single channel for Blackhole device. Multiple host channels not supported.");
            m_num_host_mem_channels = 1;
        }

        log_debug(LogSiliconDriver, "Using {} Hugepages/NumHostMemChannels for PCIDevice (logical_device_id: {} pci_interface_id: {} device_id: 0x{:x} revision: {})",
            m_num_host_mem_channels, logical_device_id, pci_interface_id, pci_device->get_device_num(), pci_device->revision_id);

        // Initialize these. Used to be in header file.
        for (int ch = 0; ch < g_MAX_HOST_MEM_CHANNELS; ch ++) {
            hugepage_mapping[logical_device_id][ch]= nullptr;
            hugepage_mapping_size[logical_device_id][ch] = 0;
            hugepage_physical_address[logical_device_id][ch] = 0;
        }

        initialize_interprocess_mutexes(pci_interface_id, clean_system_resources);

        // MT: Initial BH - hugepages will fail init
        // For using silicon driver without workload to query mission mode params, no need for hugepage.
        if (!skip_driver_allocs){
            bool hugepages_initialized = init_hugepage(logical_device_id);
            // Large writes to remote chips require hugepages to be initialized.
            // Conservative assert - end workload if remote chips present but hugepages not initialized (failures caused if using remote only for small transactions)
            if(target_remote_chips.size()) {
                log_assert(hugepages_initialized, "Hugepages must be successfully initialized if workload contains remote chips!");
            }
            if (not hugepage_mapping.at(logical_device_id).at(0)) {
                log_warning(LogSiliconDriver, "No hugepage mapping at device {}.", logical_device_id);
            }
        }
        harvested_coord_translation.insert({logical_device_id, create_harvested_coord_translation(arch_name, true)}); //translation layer for harvested coords. Default is identity map
    }

    for(const chip_id_t& chip : target_devices_in_cluster) {
        // Initialize identity mapping for Non-MMIO chips as well
        if(!ndesc -> is_chip_mmio_capable(chip)) {
            harvested_coord_translation.insert({chip, create_harvested_coord_translation(arch_name, true)});
        }
    }
}

bool tt_SiliconDevice::using_harvested_soc_descriptors() {
    return perform_harvesting_on_sdesc && performed_harvesting;
}

std::unordered_map<tt_xy_pair, tt_xy_pair> tt_SiliconDevice::get_harvested_coord_translation_map(chip_id_t logical_device_id) {
    return harvested_coord_translation.at(logical_device_id);
}

std::unordered_map<chip_id_t, uint32_t> tt_SiliconDevice::get_harvesting_masks_for_soc_descriptors() {
    if(using_harvested_soc_descriptors()) {
        return harvested_rows_per_target;
    }
    std::unordered_map<chip_id_t, uint32_t> default_harvesting_masks = {};
    for(const auto chip : target_devices_in_cluster) default_harvesting_masks.insert({chip, 0});
    return default_harvesting_masks;
}

tt_SiliconDevice::tt_SiliconDevice(const std::string &sdesc_path, const std::string &ndesc_path, const std::set<chip_id_t> &target_devices, 
                                   const uint32_t &num_host_mem_ch_per_mmio_device, const bool skip_driver_allocs,
                                   const bool clean_system_resources, bool perform_harvesting, std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks) : tt_device(sdesc_path) {
    std::unordered_set<chip_id_t> target_mmio_device_ids;
    target_devices_in_cluster = target_devices;
    arch_name = tt_SocDescriptor(sdesc_path).arch;
    perform_harvesting_on_sdesc = perform_harvesting;

    auto available_device_ids = detect_available_device_ids();
    m_num_pci_devices = available_device_ids.size();

    if (!skip_driver_allocs) {
        log_info(LogSiliconDriver, "Detected {} PCI device{} : {}", m_num_pci_devices, (m_num_pci_devices > 1) ? "s":"", available_device_ids);
        log_debug(LogSiliconDriver, "Passed target devices: {}", target_devices);
    }

    if (ndesc_path == "") {
        ndesc = tt_ClusterDescriptor::create_for_grayskull_cluster(target_devices, available_device_ids);
    }
    else {
        ndesc = tt_ClusterDescriptor::create_from_yaml(ndesc_path);
    }

    for (auto &d: target_devices){
        if (ndesc->is_chip_mmio_capable(d)){
            target_mmio_device_ids.insert(d);
        }
        else {
            target_remote_chips.insert(d);
        }
    }

    // It is mandatory for all devices to have these TLBs set aside, as the driver needs them to issue remote reads and writes.
    auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
    dynamic_tlb_config["LARGE_READ_TLB"] =  architecture_implementation->get_mem_large_read_tlb();
    dynamic_tlb_config["LARGE_WRITE_TLB"] = architecture_implementation->get_mem_large_write_tlb();
    dynamic_tlb_config["REG_TLB"] = architecture_implementation->get_reg_tlb();
    dynamic_tlb_config["SMALL_READ_WRITE_TLB"] = architecture_implementation->get_small_read_write_tlb();

    for(const auto& tlb : dynamic_tlb_config) {
        dynamic_tlb_ordering_modes.insert({tlb.first, TLB_DATA::Relaxed}); // All dynamic TLBs use Relaxed Ordering by default; MT: Good for BH
    }
    create_device(target_mmio_device_ids, num_host_mem_ch_per_mmio_device, skip_driver_allocs, clean_system_resources);

    // MT: Initial BH - Disable dependency to ethernet firmware
    if(arch_name == tt::ARCH::BLACKHOLE) {
        use_ethernet_ordered_writes = false;
        use_ethernet_broadcast = false;
        use_virtual_coords_for_eth_broadcast = false;
    }

    if(arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0) {
        const auto& harvesting_masks = ndesc -> get_harvesting_info();
        const auto& noc_translation_enabled = ndesc -> get_noc_translation_table_en();

        translation_tables_en = false;
        for(auto& masks : harvesting_masks) {
            if(target_devices.find(masks.first) != target_devices.end()) {
                harvested_rows_per_target[masks.first] = get_harvested_noc_rows(masks.second);
                noc_translation_enabled_for_chip[masks.first] = noc_translation_enabled.at(masks.first);
                num_rows_harvested.insert({masks.first, std::bitset<32>(masks.second).count()});
                if(harvested_rows_per_target[masks.first]) {
                    performed_harvesting = true;
                }
            }
        }
        if(noc_translation_enabled_for_chip.size() > 0) {
            auto const consistent_translation_table_state = [&] (std::pair<chip_id_t, bool> const& i) {
                return noc_translation_enabled_for_chip.begin() -> second == i.second;
            };

            bool translation_tables_match_on_all_chips = std::all_of(noc_translation_enabled_for_chip.begin(), noc_translation_enabled_for_chip.end(), consistent_translation_table_state);
            log_assert(translation_tables_match_on_all_chips, "Cluster uses NOC translation tables inconsistently across chips.");
            translation_tables_en = noc_translation_enabled_for_chip.begin() -> second;
        }

        if(translation_tables_en) {
            harvested_coord_translation.clear();
            for(const chip_id_t& chip : target_devices_in_cluster) {
                harvested_coord_translation.insert({chip, create_harvested_coord_translation(arch_name, false)});
            }
        }
        log_assert(performed_harvesting ? translation_tables_en : true, "Using a harvested WH cluster with NOC translation disabled.");
    }
    else if(arch_name == tt::ARCH::BLACKHOLE) {
        // Default harvesting info for Blackhole, describing no harvesting
        for(auto chip_id = target_devices.begin(); chip_id != target_devices.end(); chip_id++){
            harvested_rows_per_target[*chip_id] =  0; //get_harvested_noc_rows_for_chip(*chip_id);
            num_rows_harvested.insert({*chip_id, 0}); // Only set for broadcast TLB to get RISCS out of reset. We want all rows to have a reset signal sent.
            if(harvested_rows_per_target[*chip_id]) {
                performed_harvesting = true;
            }
        }
    }
    else if(arch_name == tt::ARCH::GRAYSKULL) {
        // Multichip harvesting is supported for GS.
        for(auto chip_id = target_devices.begin(); chip_id != target_devices.end(); chip_id++){
            harvested_rows_per_target[*chip_id] =  get_harvested_noc_rows_for_chip(*chip_id);
            num_rows_harvested.insert({*chip_id, 0}); // Only set for broadcast TLB to get RISCS out of reset. We want all rows to have a reset signal sent.
            if(harvested_rows_per_target[*chip_id]) {
                performed_harvesting = true;
            }
        }
    }

    if(simulated_harvesting_masks.size()) {
        performed_harvesting = true;
        for (auto device_id = target_devices.begin(); device_id != target_devices.end(); device_id++) {
            log_assert(simulated_harvesting_masks.find(*device_id) != simulated_harvesting_masks.end(), "Could not find harvesting mask for device_id {}", *device_id);
            if(arch_name == tt::ARCH::GRAYSKULL) {
                if ((simulated_harvesting_masks.at(*device_id) & harvested_rows_per_target[*device_id]) != harvested_rows_per_target[*device_id]) {
                    log_warning(LogSiliconDriver,
                                "Simulated harvesting config for device {} does not include the actual harvesting config. Simulated harvesting mask will be added to the real harvesting mask. Actual Harvested Rows : {}    Simulated Harvested Rows : {}",
                                *device_id,  harvested_rows_per_target[*device_id], simulated_harvesting_masks.at(*device_id));
                }
                simulated_harvesting_masks.at(*device_id) |= harvested_rows_per_target[*device_id];
            }
            else if(arch_name == tt::ARCH::WORMHOLE_B0 || arch_name == tt::ARCH::WORMHOLE) {
                log_assert(std::bitset<32>(simulated_harvesting_masks.at(*device_id)).count() >= std::bitset<32>(harvested_rows_per_target[*device_id]).count(),
                            "Simulated Harvesting for WH must contain at least as many rows as the actual harvesting config. Actual Harvested Rows : {}  Simulated Harvested Rows : {}",
                            harvested_rows_per_target[*device_id], simulated_harvesting_masks.at(*device_id));
                            num_rows_harvested.at(*device_id) = std::bitset<32>(simulated_harvesting_masks.at(*device_id)).count();
                log_assert(performed_harvesting ? translation_tables_en : true, "Using a harvested WH cluster with NOC translation disabled.");
            }
            harvested_rows_per_target[*device_id] = simulated_harvesting_masks.at(*device_id);
        }
    }

    perform_harvesting_and_populate_soc_descriptors(sdesc_path, perform_harvesting);
    populate_cores();

    // MT: Initial BH - skip this for BH
    if(arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0) {
        remote_transfer_ethernet_cores.resize(target_mmio_device_ids.size());
        for (const auto &logical_mmio_chip_id : target_mmio_device_ids) {
            const tt_SocDescriptor& soc_desc = get_soc_descriptor(logical_mmio_chip_id);
            // 4-5 is for send_epoch_commands, 0-3 are for everything else
            for (std::uint32_t i = 0; i < NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS; i++) {
                if(remote_transfer_ethernet_cores.size() <= logical_mmio_chip_id) {
                    remote_transfer_ethernet_cores.resize(logical_mmio_chip_id + 1);
                }
                remote_transfer_ethernet_cores.at(logical_mmio_chip_id).push_back(
                    tt_cxy_pair(logical_mmio_chip_id, soc_desc.ethernet_cores.at(i).x, soc_desc.ethernet_cores.at(i).y)
                );
            }
        }
    }
}

void tt_SiliconDevice::configure_active_ethernet_cores_for_mmio_device(chip_id_t mmio_chip, const std::unordered_set<tt_xy_pair>& active_eth_cores_per_chip) {
    // Makes UMD aware of which ethernet cores have active links.
    // Based on this information, UMD determines which ethernet cores can be used for host->cluster non-MMIO transfers.
    // This overrides the default ethernet cores tagged for host to cluster routing in the constructor and must be called for all MMIO devices, if default behaviour
    // is not desired.
    log_assert(get_soc_descriptor(mmio_chip).arch == tt::ARCH::WORMHOLE_B0, "{} can only be called for Wormhole arch", __FUNCTION__);
    auto& eth_cores = get_soc_descriptor(mmio_chip).ethernet_cores;
    // Cores 0, 1, 6, 7 are only available if in the active set
    static std::unordered_set<tt_xy_pair> eth_cores_available_if_active = {eth_cores.at(0), eth_cores.at(1), eth_cores.at(6), eth_cores.at(7)};
    // Eth cores 8 and 9 are always available
    std::vector<tt_cxy_pair> non_mmio_access_cores_for_chip = {tt_cxy_pair(mmio_chip, eth_cores.at(8)), tt_cxy_pair(mmio_chip, eth_cores.at(9))};
    for (const auto& active_eth_core : active_eth_cores_per_chip) {
        if (eth_cores_available_if_active.find(active_eth_core) != eth_cores_available_if_active.end()) {
            non_mmio_access_cores_for_chip.push_back(tt_cxy_pair(mmio_chip, active_eth_core));
        }
    }

    remote_transfer_ethernet_cores[mmio_chip] = non_mmio_access_cores_for_chip;
    active_eth_core_idx_per_chip.insert({mmio_chip, 0});
    non_mmio_transfer_cores_customized = true;
}

void tt_SiliconDevice::populate_cores() {
    std::uint32_t count = 0;
    for(const auto chip : soc_descriptor_per_chip) {
        workers_per_chip.insert({chip.first, std::unordered_set<tt_xy_pair>(chip.second.workers.begin(), chip.second.workers.end())});
        if(count == 0) {
            eth_cores = std::unordered_set<tt_xy_pair>(chip.second.ethernet_cores.begin(), chip.second.ethernet_cores.end());
            for(std::uint32_t dram_idx = 0; dram_idx < chip.second.get_num_dram_channels(); dram_idx++) {
                dram_cores.insert(chip.second.get_core_for_dram_channel(dram_idx, 0)) ;
            }
        }
        count++;
    }
}

std::vector<int> tt_SiliconDevice::extract_rows_to_remove(const tt::ARCH &arch, const int worker_grid_rows, const int harvested_rows) {
    // Check if harvesting config is legal for GS and WH
    log_assert(!((harvested_rows & 1) || (harvested_rows & 64) || (harvested_rows & 0xFFFFF000)), "For grayskull and wormhole, only rows 1-5 and 7-11 can be harvested");
    std::vector<int> row_coordinates_to_remove;
    int row_coordinate = 0;
    int tmp = harvested_rows;
    while (tmp) {
        if (tmp & 1)
            row_coordinates_to_remove.push_back(row_coordinate);

        tmp = tmp >> 1;
        row_coordinate++;
    }
    if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        // For Wormhole, we always remove the last few rows in the SOC descriptor in case of harvesting
        for (int i = 0; i < row_coordinates_to_remove.size(); i++) {
            row_coordinates_to_remove[i] = worker_grid_rows - i;
        }
    }
    return row_coordinates_to_remove;
}

void tt_SiliconDevice::remove_worker_row_from_descriptor(tt_SocDescriptor& full_soc_descriptor, const std::vector<int>& row_coordinates_to_remove) {
    std::vector<tt_xy_pair> workers_to_keep;
    for(auto worker = (full_soc_descriptor.workers).begin(); worker != (full_soc_descriptor.workers).end(); worker++){
        if(find(row_coordinates_to_remove.begin(), row_coordinates_to_remove.end(), (*worker).y) == row_coordinates_to_remove.end()){
            workers_to_keep.push_back(*worker);
        }
        else{
            (full_soc_descriptor.harvested_workers).push_back(*worker);
            full_soc_descriptor.cores.at(*worker).type = CoreType::HARVESTED;
        }
    }
    full_soc_descriptor.workers = workers_to_keep;
    (full_soc_descriptor.worker_grid_size).y -= row_coordinates_to_remove.size();
    full_soc_descriptor.routing_y_to_worker_y = {};
    full_soc_descriptor.worker_log_to_routing_y = {};

    std::set<int> modified_y_coords = {};

    for(const auto& core : full_soc_descriptor.workers) {
        modified_y_coords.insert(core.y);
    }
    int logical_y_coord = 0;
    for(const auto& y_coord : modified_y_coords) {
        full_soc_descriptor.routing_y_to_worker_y.insert({y_coord, logical_y_coord});
        full_soc_descriptor.worker_log_to_routing_y.insert({logical_y_coord,  y_coord});
        logical_y_coord++;
    }
}

void tt_SiliconDevice::harvest_rows_in_soc_descriptor(tt::ARCH arch, tt_SocDescriptor& sdesc, uint32_t harvested_rows) {
    std::uint32_t max_row_to_remove = (*std::max_element((sdesc.workers).begin(), (sdesc.workers).end(), [] (const auto& a, const auto& b) { return a.y < b.y; })).y;
    std::vector<int> row_coordinates_to_remove = extract_rows_to_remove(arch, max_row_to_remove, harvested_rows);
    remove_worker_row_from_descriptor(sdesc, row_coordinates_to_remove);
}

void tt_SiliconDevice::perform_harvesting_and_populate_soc_descriptors(const std::string& sdesc_path, const bool perform_harvesting) {
    const auto default_sdesc = tt_SocDescriptor(sdesc_path);
    for(const auto& chip : harvested_rows_per_target) {
        auto temp_sdesc = default_sdesc;
        if(perform_harvesting) {
            harvest_rows_in_soc_descriptor(arch_name, temp_sdesc, chip.second);
        }
        soc_descriptor_per_chip.insert({chip.first, temp_sdesc});
    }
}

void tt_SiliconDevice::check_pcie_device_initialized(int device_id) {

    PCIDevice *pci_device = get_pci_device(device_id);
    tt::ARCH device_arch = pci_device->get_arch();
    if (arch_name == tt::ARCH::GRAYSKULL) {
        if (device_arch != tt::ARCH::GRAYSKULL) {
            throw std::runtime_error(fmt::format("Attempted to run grayskull configured tt_device on {}", get_arch_str(device_arch)));
        }
    }
    else if (arch_name == tt::ARCH::WORMHOLE || arch_name == tt::ARCH::WORMHOLE_B0) {
        if (device_arch != tt::ARCH::WORMHOLE && device_arch != tt::ARCH::WORMHOLE_B0) {
            throw std::runtime_error(fmt::format("Attempted to run wormhole configured tt_device on {}", get_arch_str(device_arch)));
        }
    }
    else if (arch_name == tt::ARCH::BLACKHOLE) {
        if (device_arch != tt::ARCH::BLACKHOLE) {
            throw std::runtime_error(fmt::format("Attempted to run blackhole configured tt_device on {}", get_arch_str(device_arch)));
        }
    }
    else {
        throw std::runtime_error(fmt::format("Unsupported architecture: {}", get_arch_str(arch_name)));
    }
    auto architecture_implementation = pci_device->get_architecture_implementation();

    // MT Initial BH - Add check for blackhole once access to ARC registers is setup through TLBs
    if (arch_name != tt::ARCH::BLACKHOLE) {
        log_debug(LogSiliconDriver, "== Check if device_id: {} is initialized", device_id);
        uint32_t bar_read_initial = bar_read32(device_id, architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4);
        uint32_t arg = bar_read_initial == 500 ? 325 : 500;
        uint32_t bar_read_again;
        uint32_t arc_msg_return = arc_msg(device_id, 0xaa00 | architecture_implementation->get_arc_message_test(), true, arg, 0, 1, &bar_read_again);
        if (arc_msg_return != 0 || bar_read_again != arg + 1) {
            auto postcode = bar_read32(device_id, architecture_implementation->get_arc_reset_scratch_offset());
            throw std::runtime_error(fmt::format("Device is not initialized: arc_fw postcode: {} arc_msg_return: {} arg: {} bar_read_initial: {} bar_read_again: {}",
                                                 postcode,
                                                 arc_msg_return,
                                                 arg,
                                                 bar_read_initial,
                                                 bar_read_again));
        }
    }


    if (test_setup_interface()) {
        throw std::runtime_error("Device is incorrectly initialized. If this is a harvested Wormhole machine, it is likely that NOC Translation Tables are not enabled on device. These need to be enabled for the silicon driver to run.");
    }
}

std::unordered_map<tt_xy_pair, tt_xy_pair> tt_SiliconDevice::create_harvested_coord_translation(const tt::ARCH arch, bool identity_map) {
    log_assert(identity_map ? true : (arch != tt::ARCH::GRAYSKULL), "NOC Translation can only be performed for WH devices");
    std::unordered_map<tt_xy_pair, tt_xy_pair> translation_table = {};

    tt_xy_pair grid_size;
    std::vector<uint32_t> T6_x = {};
    std::vector<uint32_t> T6_y = {};
    std::vector<tt_xy_pair> ethernet = {};
    // Store device specific data for GS and WH depending on arch
    if(arch == tt::ARCH::GRAYSKULL) {
        grid_size = tt_xy_pair(13, 12);
        T6_x = {12, 1, 11, 2, 10, 3, 9, 4, 8, 5, 7, 6};
        T6_y = {11, 1, 10, 2, 9, 3, 8, 4, 7, 5};
    }
    else if (arch == tt::ARCH::BLACKHOLE) {
        grid_size = tt_xy_pair(17, 12);
        T6_x = {16, 1, 15, 2, 14, 3, 13, 4, 12, 5, 11, 6, 10, 7};
        T6_y = {11, 2, 10, 3, 9, 4, 8, 5, 7, 6};
    }
    else {
        grid_size = tt_xy_pair(10, 12);
        T6_x = {1, 2, 3, 4, 6, 7, 8, 9};
        T6_y = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11};
        ethernet = {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {6, 6}, {7, 6}, {8, 6}, {9, 6}};
    }

    
    if(identity_map) {
        // When device is initialized, assume no harvesting and create an identity map for cores
        // This flow is always used for GS, since there is no hardware harvesting
        for(int x = 0; x < grid_size.x; x++) {
            for(int y = 0; y < grid_size.y; y++) {
                tt_xy_pair curr_core = tt_xy_pair(x, y);
                translation_table.insert({curr_core, curr_core});
            }
        }
        return translation_table;
    }

    // If this function is called with identity_map = false, we have perform NOC translation
    // This can only happen for WH devices
    // Setup coord translation for workers. Map all worker cores
    for(int x = 0; x < grid_size.x; x++) {
        for(int y = 0; y < grid_size.y; y++) {
            tt_xy_pair curr_core = tt_xy_pair(x, y);

            if(std::find(T6_x.begin(), T6_x.end(), x) != T6_x.end() &&
            std::find(T6_y.begin(), T6_y.end(), y) != T6_y.end()) {
                // This is a worker core. Apply translation for WH.
                tt_xy_pair harvested_worker;
                if(x >= 1 && x <= 4) harvested_worker.x = x + 17;
                else if(x <= 9 && x > 5) harvested_worker.x = x + 16;
                else log_assert(false, "Invalid WH worker x coord {} when creating translation tables.", x);

                if(y >= 1 && y <= 5) harvested_worker.y = y + 17;
                else if(y <= 11 && y > 6) harvested_worker.y = y + 16;
                else log_assert(false, "Invalid WH worker y coord {} when creating translation tables.", y);
                translation_table.insert({curr_core, harvested_worker});
            }

            else if(std::find(ethernet.begin(), ethernet.end(), curr_core) != ethernet.end()){
                // This is an eth core. Apply translation for WH.
                tt_xy_pair harvested_eth_core;
                if(x >= 1 && x <= 4) harvested_eth_core.x = x + 17;
                else if(x <= 9 && x > 5) harvested_eth_core.x = x + 16;
                else log_assert(false, "Invalid WH eth_core x coord {} when creating translation tables.", x);

                if(y == 0) harvested_eth_core.y = y + 16;
                else if(y == 6) harvested_eth_core.y = y + 11;
                else log_assert(false, "Invalid WH eth_core y coord {} when creating translation tables.", y);
                translation_table.insert({curr_core, harvested_eth_core});
            }

            else {
                // All other cores for WH are not translated in case of harvesting.
                translation_table.insert({curr_core, curr_core});
            }
        }
    }
    return translation_table;
}

void tt_SiliconDevice::translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c) {
    auto translated_coords = harvested_coord_translation[device_id].at(tt_xy_pair(c, r));
    c = translated_coords.x;
    r = translated_coords.y;
}

void tt_SiliconDevice::initialize_pcie_devices() {
    log_debug(LogSiliconDriver, "tt_SiliconDevice::start");

    for (auto &device_it : m_pci_device_map){
        check_pcie_device_initialized(device_it.first);
    }

    // TODO: Implement support for multiple host channels on BLACKHOLE.
    log_assert(!(arch_name == tt::ARCH::BLACKHOLE && m_num_host_mem_channels > 1), "More channels are not yet supported for Blackhole");
    init_pcie_iatus();

    init_membars();
}

void tt_SiliconDevice::broadcast_pcie_tensix_risc_reset(chip_id_t chip_id, const TensixSoftResetOptions &soft_resets) {
    log_debug(LogSiliconDriver, "tt_SiliconDevice::broadcast_tensix_risc_reset");

    PCIDevice* device = get_pci_device(chip_id);

    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    auto logical_id = device->get_logical_id();

    log_debug(LogSiliconDriver, "== For all tensix set soft-reset for {} risc cores.", TensixSoftResetOptionsToString(valid).c_str());

    auto architecture_implementation = device->get_architecture_implementation();

    // TODO: this is clumsy and difficult to read
    auto [soft_reset_reg, _] = device->set_dynamic_tlb_broadcast(
        architecture_implementation->get_reg_tlb(),
        architecture_implementation->get_tensix_soft_reset_addr(),
        harvested_coord_translation,
        tt_xy_pair(0, 0),
        tt_xy_pair(
            architecture_implementation->get_grid_size_x() - 1,
            architecture_implementation->get_grid_size_y() - 1 - num_rows_harvested.at(logical_id)),
        TLB_DATA::Posted);
    device->write_regs(soft_reset_reg, 1, &valid);
    tt_driver_atomics::sfence();
}

std::set<chip_id_t> tt_SiliconDevice::get_target_mmio_device_ids() {
    if(!all_target_mmio_devices.size()) {
        for (const auto &it: m_pci_device_map) {
            all_target_mmio_devices.insert(it.first);
        }
    }
    return all_target_mmio_devices;
}

void tt_SiliconDevice::assert_risc_reset() {
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);
}

void tt_SiliconDevice::deassert_risc_reset() {
    broadcast_tensix_risc_reset_to_cluster(TENSIX_DEASSERT_SOFT_RESET);
}

void tt_SiliconDevice::deassert_risc_reset_at_core(tt_cxy_pair core) {
    std::uint32_t target_device = core.chip; // Get Target Device to query soc descriptor and determine location in cluster
    log_assert(std::find(get_soc_descriptor(target_device).workers.begin(), get_soc_descriptor(target_device).workers.end(), core) != get_soc_descriptor(target_device).workers.end() ||
               std::find(get_soc_descriptor(target_device).ethernet_cores.begin(), get_soc_descriptor(target_device).ethernet_cores.end(), core) != get_soc_descriptor(target_device).ethernet_cores.end(),
                                "Cannot deassert reset on a non-tensix or harvested core");
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(target_device);
    if(target_is_mmio_capable) {
        log_assert(m_pci_device_map.find(target_device) != m_pci_device_map.end(), "Could not find MMIO mapped device in devices connected over PCIe");
        send_tensix_risc_reset_to_core(core, TENSIX_DEASSERT_SOFT_RESET);
    }
    else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Can't issue access to remote core in BH");
        send_remote_tensix_risc_reset_to_core(core, TENSIX_DEASSERT_SOFT_RESET);
    }
}

void tt_SiliconDevice::assert_risc_reset_at_core(tt_cxy_pair core) {
    std::uint32_t target_device = core.chip; // Get Target Device to query soc descriptor and determine location in cluster
    log_assert(std::find(get_soc_descriptor(target_device).workers.begin(), get_soc_descriptor(target_device).workers.end(), core) != get_soc_descriptor(target_device).workers.end() ||
               std::find(get_soc_descriptor(target_device).ethernet_cores.begin(), get_soc_descriptor(target_device).ethernet_cores.end(), core) != get_soc_descriptor(target_device).ethernet_cores.end(),
                                "Cannot assert reset on a non-tensix or harvested core");
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(target_device);
    if(target_is_mmio_capable) {
        log_assert(m_pci_device_map.find(target_device) != m_pci_device_map.end(), "Could not find MMIO mapped device in devices connected over PCIe");
        send_tensix_risc_reset_to_core(core, TENSIX_ASSERT_SOFT_RESET);
    }
    else {
        send_remote_tensix_risc_reset_to_core(core, TENSIX_ASSERT_SOFT_RESET);
    }
}

// Free memory during teardown, and remove (clean/unlock) from any leftover mutexes.
void tt_SiliconDevice::cleanup_shared_host_state() {
    for(auto &mutex : hardware_resource_mutex_map) {
        mutex.second.reset();
        mutex.second = nullptr;
        named_mutex::remove(mutex.first.c_str());
    }
}

std::unordered_set<chip_id_t> tt_SiliconDevice::get_all_chips_in_cluster() {
    return ndesc -> get_all_chips();
}
int tt_SiliconDevice::get_number_of_chips_in_cluster() {
    // Returns the number of chips seen in the network descriptor
    return ndesc -> get_all_chips().size();
}

tt_ClusterDescriptor* tt_SiliconDevice::get_cluster_description() {return ndesc.get();}
// Can be used before instantiating a silicon device
int tt_SiliconDevice::detect_number_of_chips() {

    auto available_device_ids = detect_available_device_ids();
    return available_device_ids.size();

}

// Can be used before instantiating a silicon device
std::vector<chip_id_t> tt_SiliconDevice::detect_available_device_ids() {
    // TODO: The chip_id_t type is used for two types of device id:
    //  *   device id which is the N in /dev/tenstorrent/N
    //  *   "logical" id which is the id of the chip in the YAML produced by
    //      the create-ethernet-map tool
    // Maybe these should be disambiguated.  Here, what is being returned is the
    // former, the "device id" -- not to be confused with 16 bit PCI device id!
    return PCIDevice::enumerate_devices();
}

std::function<void(uint32_t, uint32_t, const uint8_t*)> tt_SiliconDevice::get_fast_pcie_static_tlb_write_callable(int device_id) {
    PCIDevice* dev = get_pci_device(device_id);

    const auto callable = [dev](uint32_t byte_addr, uint32_t num_bytes, const uint8_t* buffer_addr) {
        dev->write_block(byte_addr, num_bytes, buffer_addr);
    };

    return callable;
}

tt::Writer tt_SiliconDevice::get_static_tlb_writer(tt_cxy_pair target) {
    if (!ndesc->is_chip_mmio_capable(target.chip)) {
        throw std::runtime_error(fmt::format("Target not in MMIO chip: {}", target.str()));
    }

    if (!tlbs_init || !map_core_to_tlb) {
        throw std::runtime_error("TLBs not initialized");
    }

    auto *dev = get_pci_device(target.chip);

    if (!dev->bar0_wc) {
        throw std::runtime_error("No write-combined mapping for BAR0");
    }

    auto tlb_index = map_core_to_tlb(tt_xy_pair(target.x, target.y));
    auto tlb_data = dev->get_architecture_implementation()->describe_tlb(tlb_index);

    if (!tlb_data.has_value()) {
        throw std::runtime_error(fmt::format("No TLB mapped to core {}", target.str()));
    }

    auto [tlb_offset, tlb_size] = tlb_data.value();
    auto *base = reinterpret_cast<uint8_t *>(dev->bar0_wc);

    return tt::Writer(base + tlb_offset, tlb_size);
}

void tt_SiliconDevice::write_device_memory(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair target, std::uint32_t address, const std::string& fallback_tlb) {
    PCIDevice *dev = get_pci_device(target.chip);
    const uint8_t* buffer_addr = static_cast<const uint8_t*>(mem_ptr);

    log_debug(LogSiliconDriver, "tt_SiliconDevice::write_device_memory to chip:{} {}-{} at 0x{:x} size_in_bytes: {} small_access: {}",
        target.chip, target.x, target.y, address, size_in_bytes, small_access);

    std::int32_t tlb_index = 0;
    std::optional<std::tuple<std::uint64_t, std::uint64_t>> tlb_data = std::nullopt;
    if(tlbs_init) {
        tlb_index = map_core_to_tlb(tt_xy_pair(target.x, target.y));
        tlb_data = dev->get_architecture_implementation()->describe_tlb(tlb_index);
    }

    if (tlb_data.has_value() && address_in_tlb_space(address, size_in_bytes, tlb_index, std::get<1>(tlb_data.value()), target.chip)) {
        auto [tlb_offset, tlb_size] = tlb_data.value();
        if (dev->bar4_wc != nullptr && tlb_size == BH_4GB_TLB_SIZE) {
            // This is only for Blackhole. If we want to  write to DRAM (BAR4 space), we add offset
            // to which we write so write_block knows it needs to target BAR4
            dev->write_block((tlb_offset + address % tlb_size) + BAR0_BH_SIZE, size_in_bytes, buffer_addr);
        } else {
            dev->write_block(tlb_offset + address % tlb_size, size_in_bytes, buffer_addr);
        }
    } else {
        const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
        const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, dev->get_device_num()));

        while(size_in_bytes > 0) {

            auto [mapped_address, tlb_size] = dev->set_dynamic_tlb(tlb_index, target, address, harvested_coord_translation, dynamic_tlb_ordering_modes.at(fallback_tlb));
            uint32_t transfer_size = std::min((uint64_t)size_in_bytes, tlb_size);
            dev->write_block(mapped_address, transfer_size, buffer_addr);

            size_in_bytes -= transfer_size;
            address += transfer_size;
            buffer_addr += transfer_size;
        }
        log_debug(LogSiliconDriver, "Write done Dynamic TLB with pid={}", (long)getpid());
    }
}

void tt_SiliconDevice::read_device_memory(void *mem_ptr, tt_cxy_pair target, std::uint32_t address, std::uint32_t size_in_bytes, const std::string& fallback_tlb) {
    // Assume that mem_ptr has been allocated adequate memory on host when this function is called. Otherwise, this function will cause a segfault.
    log_debug(LogSiliconDriver, "tt_SiliconDevice::read_device_memory to chip:{} {}-{} at 0x{:x} size_in_bytes: {}", target.chip, target.x, target.y, address, size_in_bytes);
    PCIDevice *dev = get_pci_device(target.chip);

    uint8_t* buffer_addr = static_cast<uint8_t*>(mem_ptr);

    std::int32_t tlb_index = 0;
    std::optional<std::tuple<std::uint64_t, std::uint64_t>> tlb_data = std::nullopt;
    if(tlbs_init) {
        tlb_index = map_core_to_tlb(tt_xy_pair(target.x, target.y));
        tlb_data = dev->get_architecture_implementation()->describe_tlb(tlb_index);
    }
    log_debug(LogSiliconDriver, "  tlb_index: {}, tlb_data.has_value(): {}", tlb_index, tlb_data.has_value());

    if (tlb_data.has_value()  && address_in_tlb_space(address, size_in_bytes, tlb_index, std::get<1>(tlb_data.value()), target.chip)) {
        auto [tlb_offset, tlb_size] = tlb_data.value();
        if (dev->bar4_wc != nullptr && tlb_size == BH_4GB_TLB_SIZE) {
            // This is only for Blackhole. If we want to  read from DRAM (BAR4 space), we add offset
            // from which we read so read_block knows it needs to target BAR4
            dev->read_block((tlb_offset + address % tlb_size) + BAR0_BH_SIZE, size_in_bytes, buffer_addr);
        } else {
            dev->read_block(tlb_offset + address % tlb_size, size_in_bytes, buffer_addr);
        }
        log_debug(LogSiliconDriver, "  read_block called with tlb_offset: {}, tlb_size: {}", tlb_offset, tlb_size);
    } else {
        const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
        const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, dev->get_device_num()));
        log_debug(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);
        while(size_in_bytes > 0) {

            auto [mapped_address, tlb_size] = dev->set_dynamic_tlb(tlb_index, target, address, harvested_coord_translation, dynamic_tlb_ordering_modes.at(fallback_tlb));
            uint32_t transfer_size = std::min((uint64_t)size_in_bytes, tlb_size);
            dev->read_block(mapped_address, transfer_size, buffer_addr);

            size_in_bytes -= transfer_size;
            address += transfer_size;
            buffer_addr += transfer_size;
        }
        log_debug(LogSiliconDriver, "Read done Dynamic TLB with pid={}", (long)getpid());
    }
}

void tt_SiliconDevice::read_buffer(
    void* mem_ptr,
    std::uint32_t address,
    std::uint16_t channel,
    std::uint32_t size_in_bytes,
    chip_id_t src_device_id) {

    log_assert(src_device_id != -1, "Must provide src_device_id for host_resident read/write");
    log_assert(channel >= 0 && channel <= g_MAX_HOST_MEM_CHANNELS, "{} - Invalid channel {} for host_resident read/write.", __FUNCTION__, channel);
    void * user_scratchspace = nullptr;

    if(hugepage_mapping.at(src_device_id).at(channel)) {
      user_scratchspace = static_cast<char*>(hugepage_mapping.at(src_device_id).at(channel)) + (address & HUGEPAGE_MAP_MASK);
    } else {
        throw std::runtime_error(fmt::format("write_buffer: Hugepages are not allocated for src_device_id: {} ch: {}."
                                             " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
                                             src_device_id,
                                             channel));
    }

    log_debug(LogSiliconDriver, "tt_SiliconDevice::read_buffer (src_device_id: {}, ch: {}) from 0x{:x}",  src_device_id, channel, user_scratchspace);
    
    memcpy(mem_ptr, user_scratchspace, size_in_bytes);
}

void tt_SiliconDevice::write_buffer(
    const void *mem_ptr,
    std::uint32_t size,
    std::uint32_t address,
    std::uint16_t channel,
    chip_id_t src_device_id) {

    void * user_scratchspace = nullptr;
    if(hugepage_mapping.at(src_device_id).at(channel)) {
      log_assert(size <= HUGEPAGE_REGION_SIZE, "write_buffer data has larger size {} than destination buffer {}", size, HUGEPAGE_REGION_SIZE);
      log_debug(LogSiliconDriver, "Using hugepage mapping at address {} offset {} chan {} size {}",
        hugepage_mapping.at(src_device_id).at(channel),
        (address & HUGEPAGE_MAP_MASK),
        channel,
        size);
      user_scratchspace = static_cast<char*>(hugepage_mapping.at(src_device_id).at(channel)) + (address & HUGEPAGE_MAP_MASK);
    } else {
        throw std::runtime_error(fmt::format("write_buffer: Hugepage are not allocated for src_device_id: {} ch: {}",
                                             src_device_id,
                                             channel));
    }
    memcpy(user_scratchspace, mem_ptr, size);
}


uint32_t tt_SiliconDevice::get_power_state_arc_msg(chip_id_t chip_id, tt_DevicePowerState state) {
    PCIDevice* pci_device = get_pci_device(chip_id);
    uint32_t msg = 0xaa00;
    switch (state) {
        case BUSY: {
            msg |= pci_device->get_architecture_implementation()->get_arc_message_arc_go_busy();
            break;
        }
        case LONG_IDLE: {
            msg |= pci_device->get_architecture_implementation()->get_arc_message_arc_go_long_idle();
            break;
        }
        case SHORT_IDLE: {
            msg |= pci_device->get_architecture_implementation()->get_arc_message_arc_go_short_idle();
            break;
        }
        default: throw std::runtime_error("Unrecognized power state.");
    }
    return msg;
}

void tt_SiliconDevice::set_pcie_power_state(tt_DevicePowerState state) {

    for (auto &device_it : m_pci_device_map){
        int chip_id = device_it.first;
        uint32_t msg = get_power_state_arc_msg(chip_id, state);
        std::stringstream ss;
        ss << state;
        auto exit_code = arc_msg(chip_id, 0xaa00 | msg, true, 0, 0);
        if (exit_code != 0) {
            throw std::runtime_error(fmt::format("Failed to set power state to {} with exit code {}", ss.str(), exit_code));
        }
    }
}

int tt_SiliconDevice::get_clock(int logical_device_id) {

    // TODO: remove this once ARC messages work.
    // This is currently used only for testing and bringing up Blackhole on Buda.
    if (arch_name == tt::ARCH::BLACKHOLE) {
        char* clk_env_var = getenv("TT_SILICON_DRIVER_AICLK");
        if (clk_env_var != nullptr) {
            log_warning(LogSiliconDriver, "ARC messages are not enabled on Blackhole. "
                        "Using AICLK value from environment variable TT_SILICON_DRIVER_AICLK: {}" , clk_env_var);
            return std::stoi(clk_env_var);
        }
    }

    uint32_t clock;
    auto mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(logical_device_id);
    PCIDevice* pci_device = get_pci_device(mmio_capable_chip_logical);
    auto exit_code = arc_msg(logical_device_id, 0xaa00 | pci_device->get_architecture_implementation()->get_arc_message_get_aiclk(), true, 0xFFFF, 0xFFFF, 1, &clock);
    if (exit_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get aiclk value with exit code {}", exit_code));
    }
    return clock;
}

std::map<int, int> tt_SiliconDevice::get_clocks() {
    std::map<int,int> clock_freq_map;
    for (auto &device_it : m_pci_device_map){
        int d = device_it.first;
        clock_freq_map.insert({d, get_clock(d)});
    }
    return clock_freq_map;
}

tt_SiliconDevice::~tt_SiliconDevice () {

    log_debug(LogSiliconDriver, "tt_SiliconDevice::~tt_SiliconDevice");

    cleanup_shared_host_state();

    for (auto &device_it : m_pci_device_map){

        chip_id_t device_id = device_it.first;
        // PCIDevice *dev = device_it.second.get();

        for (int ch = 0; ch < m_num_host_mem_channels; ch ++) {
            if (hugepage_mapping.at(device_id).at(ch)) {
                munmap(hugepage_mapping.at(device_id).at(ch), hugepage_mapping_size.at(device_id).at(ch));
            }
        }
        
        device_it.second.reset();
    }
    m_pci_device_map.clear();
    ndesc.reset();
    soc_descriptor_per_chip.clear();
    dynamic_tlb_config.clear();
    tlb_config_map.clear();
    dynamic_tlb_ordering_modes.clear();
}

std::optional<std::tuple<uint32_t, uint32_t>> tt_SiliconDevice::get_tlb_data_from_target(const tt_xy_pair& target) {
    std::int32_t tlb_index = 0;
    std::optional<std::tuple<std::uint32_t, std::uint32_t>> tlb_data;

    if (tlbs_init) {
        tlb_index = map_core_to_tlb(target);
        auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
        tlb_data = architecture_implementation->describe_tlb(tlb_index);
    } 
    return tlb_data;
}

void tt_SiliconDevice::configure_tlb(chip_id_t logical_device_id, tt_xy_pair core, std::int32_t tlb_index, std::int32_t address, uint64_t ordering) {
    log_assert(ordering == TLB_DATA::Strict || ordering == TLB_DATA::Posted || ordering == TLB_DATA::Relaxed, "Invalid ordering specified in tt_SiliconDevice::configure_tlb");
    PCIDevice *pci_device = get_pci_device(logical_device_id);
    pci_device->set_dynamic_tlb(tlb_index, core, address, harvested_coord_translation, ordering);
    auto tlb_size = std::get<1>(pci_device->get_architecture_implementation()->describe_tlb(tlb_index).value());
    if(tlb_config_map.find(logical_device_id) == tlb_config_map.end()) tlb_config_map.insert({logical_device_id, {}});
    tlb_config_map[logical_device_id].insert({tlb_index, (address / tlb_size) * tlb_size});
}

void tt_SiliconDevice::set_fallback_tlb_ordering_mode(const std::string& fallback_tlb, uint64_t ordering) {
    log_assert(ordering == TLB_DATA::Strict || ordering == TLB_DATA::Posted || ordering == TLB_DATA::Relaxed, "Invalid ordering specified in tt_SiliconDevice::configure_tlb.");
    log_assert(dynamic_tlb_ordering_modes.find(fallback_tlb) != dynamic_tlb_ordering_modes.end(), "Invalid TLB specified in tt_SiliconDevice::set_fallback_tlb_ordering_mode.");
    log_assert(fallback_tlb != "LARGE_READ_TLB" &&  fallback_tlb != "LARGE_WRITE_TLB", "Ordering modes for LARGE_READ_TLB and LARGE_WRITE_TLB cannot be modified.");
    dynamic_tlb_ordering_modes.at(fallback_tlb) = ordering;
}

// TT<->TT P2P support removed in favor of increased Host memory.
// TODO: this is in the wrong place, it should be in the PCIDevice.
void tt_SiliconDevice::init_pcie_iatus() {
    int num_enabled_devices = m_pci_device_map.size();
    log_debug(LogSiliconDriver, "tt_SiliconDevice::init_pcie_iatus() num_enabled_devices: {}", num_enabled_devices);
    log_assert(m_num_host_mem_channels <= g_MAX_HOST_MEM_CHANNELS, "Maximum of {} 1GB Host memory channels supported.",  g_MAX_HOST_MEM_CHANNELS);

    for (auto &src_device_it : m_pci_device_map){
        int logical_id = src_device_it.first;
        PCIDevice* src_pci_device = src_device_it.second.get();

        // Device to Host (multiple channels)
        for (int channel_id = 0; channel_id < m_num_host_mem_channels; channel_id++) {
            if (hugepage_mapping.at(logical_id).at(channel_id)) {
                std::uint32_t region_size = HUGEPAGE_REGION_SIZE;
                if (channel_id == 3) region_size = 805306368; // Remove 256MB from full 1GB for channel 3 (iATU limitation)

                // This log message doesn't look right.
                log_debug(LogSiliconDriver, "Configuring ATU channel {} to point to hugepage {}.", channel_id, logical_id);
                iatu_configure_peer_region(logical_id, channel_id, hugepage_physical_address.at(logical_id).at(channel_id), region_size);

                if (host_channel_size.find(logical_id) == host_channel_size.end()) {
                     host_channel_size.insert({logical_id, {}});
                }
                host_channel_size.at(logical_id).push_back(region_size);
            } else {
                throw std::runtime_error(fmt::format("init_pcie_iatus: Hugepages are not allocated for logical device id: {} ch: {}", logical_id, channel_id));
            }
        }
    }
}

// Looks for hugetlbfs inside /proc/mounts matching desired pagesize (typically 1G)
std::string find_hugepage_dir(std::size_t pagesize)
{

    static const std::regex hugetlbfs_mount_re(fmt::format("^(nodev|hugetlbfs) ({}) hugetlbfs ([^ ]+) 0 0$", hugepage_dir));
    static const std::regex pagesize_re("(?:^|,)pagesize=([0-9]+)([KMGT])(?:,|$)");

    std::ifstream proc_mounts("/proc/mounts");

    for (std::string line; std::getline(proc_mounts, line); )
    {
        if (std::smatch mount_match; std::regex_match(line, mount_match, hugetlbfs_mount_re))
        {
            std::string options = mount_match[3];
            if (std::smatch pagesize_match; std::regex_search(options, pagesize_match, pagesize_re))
            {
                std::size_t mount_page_size = std::stoull(pagesize_match[1]);
                switch (pagesize_match[2].str()[0])
                {
                    case 'T': mount_page_size <<= 10;
                    case 'G': mount_page_size <<= 10;
                    case 'M': mount_page_size <<= 10;
                    case 'K': mount_page_size <<= 10;
                }

                if (mount_page_size == pagesize)
                {
                    return mount_match[2];
                }
            }
        }
    }

    log_warning(LogSiliconDriver, "ttSiliconDevice::find_hugepage_dir: no huge page mount found in /proc/mounts for path: {} with hugepage_size: {}.", hugepage_dir, pagesize);
    return std::string();
}

// Open a file in <hugepage_dir> for the hugepage mapping.
// All processes operating on the same pipeline must agree on the file name.
// Today we assume there's only one pipeline running within the system.
// One hugepage per device such that each device gets unique memory.
int tt_SiliconDevice::open_hugepage_file(const std::string &dir, chip_id_t physical_device_id, uint16_t channel) {
    std::vector<char> filename;
    static const char pipeline_name[] = "tenstorrent";

    filename.insert(filename.end(), dir.begin(), dir.end());
    if (filename.back() != '/') filename.push_back('/');

    // In order to limit number of hugepages while transition from shared hugepage (1 per system) to unique
    // hugepage per device, will share original/shared hugepage filename with physical device 0.
    if (physical_device_id != 0 || channel != 0){
        std::string device_id_str = fmt::format("device_{}_", physical_device_id);
        filename.insert(filename.end(), device_id_str.begin(), device_id_str.end());
    }

    if (channel != 0) {
        std::string channel_id_str = fmt::format("channel_{}_", channel);
        filename.insert(filename.end(), channel_id_str.begin(), channel_id_str.end());
    }

    filename.insert(filename.end(), std::begin(pipeline_name), std::end(pipeline_name)); // includes NUL terminator

    std::string filename_str(filename.begin(), filename.end());
    filename_str.erase(std::find(filename_str.begin(), filename_str.end(), '\0'), filename_str.end()); // Erase NULL terminator for printing.
    log_debug(LogSiliconDriver, "ttSiliconDevice::open_hugepage_file: using filename: {} for physical_device_id: {} channel: {}", filename_str.c_str(), physical_device_id, channel);

    // Save original and set umask to unrestricted.
    auto old_umask = umask(0);

    int fd = open(filename.data(), O_RDWR | O_CREAT | O_CLOEXEC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IWOTH | S_IROTH );
    if (fd == -1 && errno == EACCES) {
        log_warning(LogSiliconDriver, "ttSiliconDevice::open_hugepage_file could not open filename: {} on first try, unlinking it and retrying.", filename_str);
        unlink(filename.data());
        fd = open(filename.data(), O_RDWR | O_CREAT | O_CLOEXEC, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IWOTH | S_IROTH );
    }

    // Verify opened file size.
    struct stat st;
    if (fstat(fd, &st) == -1) {
        log_warning(LogSiliconDriver, "Error reading file size after opening: {}", filename_str);
    } else {
        if (st.st_size == 0) {
            log_warning(LogSiliconDriver, "Opened hugepage file has zero size, mapping it might fail: {}. Verify that enough hugepages are provided.", filename_str);
        }
    }

    // Restore original mask
    umask(old_umask);

    if (fd == -1) {
        log_warning(LogSiliconDriver, "open_hugepage_file failed");
        return -1;
    }

    return fd;
}

// For debug purposes when various stages fails.
void print_file_contents(std::string filename, std::string hint = ""){
    if (std::filesystem::exists(filename)){
        std::ifstream meminfo(filename);
        if (meminfo.is_open()){
            std::cout << std::endl << "File " << filename << " " << hint << " is: " << std::endl;
            std::cout << meminfo.rdbuf();
        }
    }
}

// Initialize hugepage, N per device (all same size).
bool tt_SiliconDevice::init_hugepage(chip_id_t device_id) {
    const size_t hugepage_size = HUGEPAGE_REGION_SIZE;

    // Convert from logical (device_id in netlist) to physical device_id (in case of virtualization)
    auto dev = m_pci_device_map.at(device_id).get();
    auto physical_device_id = dev->get_device_num();

    std::string hugepage_dir = find_hugepage_dir(hugepage_size);
    if (hugepage_dir.empty()) {
        log_warning(LogSiliconDriver, "ttSiliconDevice::init_hugepage: no huge page mount found for hugepage_size: {}.", hugepage_size);
        return false;
    }

    bool success = true;

    // Support for more than 1GB host memory accessible per device, via channels.
    for (int ch = 0; ch < m_num_host_mem_channels; ch++) {

        int hugepage_fd = open_hugepage_file(hugepage_dir, physical_device_id, ch);
        if (hugepage_fd == -1) {
            // Probably a permissions problem.
            log_warning(LogSiliconDriver, "ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} creating hugepage mapping file failed.", physical_device_id, ch);
            success = false;
            continue;
        }

        std::byte *mapping = static_cast<std::byte*>(mmap(nullptr, hugepage_size, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_POPULATE, hugepage_fd, 0));

        close(hugepage_fd);

        if (mapping == MAP_FAILED) {
            log_warning(LogSiliconDriver, "UMD: Mapping a hugepage failed. (device: {}, {}/{} errno: {}).", physical_device_id, ch, m_num_host_mem_channels, strerror(errno));
            print_file_contents("/proc/cmdline");\
            print_file_contents("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages"); // Hardcoded for 1GB hugepage.
            success = false;
            continue;
        }

        // Beter performance if hugepage just allocated (populate flag to prevent lazy alloc) is migrated to same numanode as TT device.
        if (!tt::cpuset::tt_cpuset_allocator::bind_area_to_memory_nodeset(physical_device_id, mapping, hugepage_size)){
            log_warning(LogSiliconDriver, "---- ttSiliconDevice::init_hugepage: bind_area_to_memory_nodeset() failed (physical_device_id: {} ch: {}). "
            "Hugepage allocation is not on NumaNode matching TT Device. Side-Effect is decreased Device->Host perf (Issue #893).",
            physical_device_id, ch);
        }

        tenstorrent_pin_pages pin_pages;
        memset(&pin_pages, 0, sizeof(pin_pages));
        pin_pages.in.output_size_bytes = sizeof(pin_pages.out);
        pin_pages.in.flags = TENSTORRENT_PIN_PAGES_CONTIGUOUS;
        pin_pages.in.virtual_address = reinterpret_cast<std::uintptr_t>(mapping);
        pin_pages.in.size = hugepage_size;

        auto fd = dev->get_fd();

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages) == -1) {
            log_warning(LogSiliconDriver, "---- ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} TENSTORRENT_IOCTL_PIN_PAGES failed (errno: {}). Common Issue: Requires TTMKD >= 1.11, see following file contents...", physical_device_id, ch, strerror(errno));
            munmap(mapping, hugepage_size);
            print_file_contents("/sys/module/tenstorrent/version", "(TTKMD version)");
            print_file_contents("/proc/meminfo");
            print_file_contents("/proc/buddyinfo");
            success = false;
            continue;
        }

        hugepage_mapping.at(device_id).at(ch) = mapping;
        hugepage_mapping_size.at(device_id).at(ch) = hugepage_size;
        hugepage_physical_address.at(device_id).at(ch) = pin_pages.out.physical_address;

        log_debug(LogSiliconDriver, "ttSiliconDevice::init_hugepage: physical_device_id: {} ch: {} mapping_size: {} physical address 0x{:x}", physical_device_id, ch, hugepage_size, (unsigned long long)hugepage_physical_address.at(device_id).at(ch));
    }

    return success;
}

int tt_SiliconDevice::test_setup_interface () {
    if (arch_name == tt::ARCH::GRAYSKULL) {
        int ret_val = 0;
        PCIDevice *dev = m_pci_device_map.begin()->second.get();

        uint32_t mapped_reg = dev->set_dynamic_tlb(dev->get_architecture_implementation()->get_reg_tlb(), tt_xy_pair(0, 0), 0xffb20108, harvested_coord_translation).bar_offset;

        uint32_t regval = 0;
        dev->read_regs(mapped_reg, 1, &regval);
        ret_val = (regval != 0xffffffff && ((regval & 0x1) == 1)) ? 0 : 1;
        return ret_val;
    }
    else if (arch_name == tt::ARCH::WORMHOLE || arch_name == tt::ARCH::WORMHOLE_B0) {
        int ret_val = 0;
        PCIDevice *dev = m_pci_device_map.begin()->second.get();

        uint32_t mapped_reg = dev->set_dynamic_tlb(dev->get_architecture_implementation()->get_reg_tlb(), tt_xy_pair(1, 0), 0xffb20108, harvested_coord_translation).bar_offset;

        uint32_t regval = 0;
        dev->read_regs(mapped_reg, 1, &regval);
        ret_val = (regval != 0xffffffff && (regval == 33)) ? 0 : 1;
        return ret_val;
    }
    else if (arch_name == tt::ARCH::BLACKHOLE) {
        // MT Inital BH - Try to enable this, but double check "regval == 33"
        // int ret_val = 0;
        // PCIDevice *dev = m_pci_device_map.begin()->second->hdev;

        // uint32_t mapped_reg = dev->set_dynamic_tlb(m_pci_device_map.begin()->second, dev->get_architecture_implementation()->get_reg_tlb(), tt_xy_pair(1, 0), 0xffb20108, harvested_coord_translation).bar_offset;

        // uint32_t regval = 0;
        // read_regs(dev, mapped_reg, 1, &regval);
        // ret_val = (regval != 0xffffffff && (regval == 33)) ? 0 : 1;
        // return ret_val;
        return 0;
    }
    else {
        throw std::runtime_error(fmt::format("Unsupported architecture: {}", get_arch_str(arch_name)));
    }
}

void tt_SiliconDevice::bar_write32 (int logical_device_id, uint32_t addr, uint32_t data) {
    PCIDevice *dev = get_pci_device(logical_device_id);

    if (addr < dev->bar0_uc_offset) {
        dev->write_block(addr, sizeof(data), reinterpret_cast<const uint8_t*>(&data)); // do we have to reinterpret_cast?
    } else {
        dev->write_regs(addr, 1, &data);
    }
}

uint32_t tt_SiliconDevice::bar_read32 (int logical_device_id, uint32_t addr) {
    PCIDevice* dev = get_pci_device(logical_device_id);

    uint32_t data;
    if (addr < dev->bar0_uc_offset) {
        dev->read_block(addr, sizeof(data), reinterpret_cast<uint8_t*>(&data));
    } else {
        dev->read_regs(addr, 1, &data);
    }
    return data;
}

// Returns 0 if everything was OK
int tt_SiliconDevice::pcie_arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done, uint32_t arg0, uint32_t arg1, int timeout, uint32_t *return_3, uint32_t *return_4) {


    if ((msg_code & 0xff00) != 0xaa00) {
        log_error("Malformed message. msg_code is 0x{:x} but should be 0xaa..", msg_code);
    }
    log_assert(arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args"); // Only 16 bits are allowed

    PCIDevice *pci_device = get_pci_device(logical_device_id);
    auto architecture_implementation = pci_device->get_architecture_implementation();

    // Exclusive access for a single process at a time. Based on physical pci interface id.
    std::string msg_type = "ARC_MSG";
    const scoped_lock<named_mutex> lock(*get_mutex(msg_type, pci_device->get_device_num()));
    uint32_t fw_arg = arg0 | (arg1<<16);
    int exit_code = 0;

    bar_write32 (logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4, fw_arg);
    bar_write32 (logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 5 * 4, msg_code);

    uint32_t misc = bar_read32 (logical_device_id, architecture_implementation->get_arc_reset_arc_misc_cntl_offset());
    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device {}", logical_device_id);
        return 1;
    } else {
        bar_write32(logical_device_id, architecture_implementation->get_arc_reset_arc_misc_cntl_offset(), misc | (1 << 16));
    }

    if (wait_for_done) {
        uint32_t status = 0xbadbad;
        auto timeout_seconds = std::chrono::seconds(timeout);
        auto start = std::chrono::system_clock::now();
        while (true) {
            if (std::chrono::system_clock::now() - start > timeout_seconds) {
                throw std::runtime_error(fmt::format("Timed out after waiting {} seconds for device {} ARC to respond", timeout, logical_device_id));
            }

            status = bar_read32(logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 5 * 4);

            if ((status & 0xffff) == (msg_code & 0xff)) {
                if (return_3 != nullptr) {
                    *return_3 = bar_read32(logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4);
                }

                if (return_4 != nullptr) {
                    *return_4 = bar_read32(logical_device_id, architecture_implementation->get_arc_reset_scratch_offset() + 4 * 4);
                }

                exit_code = (status & 0xffff0000) >> 16;
                break;
            } else if (status == MSG_ERROR_REPLY) {
                log_warning(LogSiliconDriver, "On device {}, message code 0x{:x} not recognized by FW", logical_device_id, msg_code);
                exit_code = MSG_ERROR_REPLY;
                break;
            }
        }
    }

    pci_device->detect_hang_read();
    return exit_code;
}

int tt_SiliconDevice::iatu_configure_peer_region (int logical_device_id, uint32_t peer_region_id, uint64_t bar_addr_64, uint32_t region_size) {
    uint32_t dest_bar_lo = bar_addr_64 & 0xffffffff;
    uint32_t dest_bar_hi = (bar_addr_64 >> 32) & 0xffffffff;
    std::uint32_t region_id_to_use = peer_region_id;
    if(peer_region_id == 3) region_id_to_use = 4; // Hack use region 4 for channel 3..this ensures that we have a smaller chan 3 address space with the correct start offset
    PCIDevice *pci_device = get_pci_device(logical_device_id);
    auto architecture_implementation = pci_device->get_architecture_implementation();

    // BR: ARC doesn't work yet on Blackhole, so programming ATU directly. Should be removed when arc starts working.
    // TODO: Remove when ARC is implemented on BH.
    if (arch_name == tt::ARCH::BLACKHOLE) {
        uint64_t base_addr = region_id_to_use * region_size;
        uint64_t base_size = (region_id_to_use + 1) * region_size;
        uint64_t limit_address = base_addr + base_size - 1;

        uint32_t region_ctrl_1 = 1 << 13; // INCREASE_REGION_SIZE = 1
        uint32_t region_ctrl_2 = 1 << 31; // REGION_EN = 1
        uint32_t region_ctrl_3 = 0;
        uint32_t base_addr_lo = base_addr & 0xffffffff;
        uint32_t base_addr_hi = (base_addr >> 32) & 0xffffffff;
        uint32_t limit_address_lo = limit_address & 0xffffffff;
        uint32_t limit_address_hi = (limit_address >> 32) & 0xffffffff;

        uint64_t iatu_index = 0;
        uint64_t iatu_base = UNROLL_ATU_OFFSET_BAR + iatu_index * 0x200;

        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x00), &region_ctrl_1, 1);
        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x04), &region_ctrl_2, 1);
        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x08), &base_addr_lo, 1);
        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x0c), &base_addr_hi, 1);
        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x10), &limit_address_lo, 1);
        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x14), &dest_bar_lo, 1);
        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x18), &dest_bar_hi, 1);
        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x1c), &region_ctrl_3, 1);
        pci_device->write_regs(reinterpret_cast<std::uint32_t*>(static_cast<uint8_t*>(pci_device->bar2_uc) + iatu_base + 0x20), &limit_address_hi, 1);
    }
    else {
        bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 0 * 4, region_id_to_use);
        bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 1 * 4, dest_bar_lo);
        bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 2 * 4, dest_bar_hi);
        bar_write32(logical_device_id, architecture_implementation->get_arc_csm_mailbox_offset() + 3 * 4, region_size);
        arc_msg(logical_device_id, 0xaa00 | architecture_implementation->get_arc_message_setup_iatu_for_peer_to_peer(), true, 0, 0);
    }

    // Print what just happened
    uint32_t peer_region_start = region_id_to_use*region_size;
    uint32_t peer_region_end = (region_id_to_use+1)*region_size - 1;
    log_debug(LogSiliconDriver, "    [region id {}] NOC to PCI address range 0x{:x}-0x{:x} mapped to addr 0x{:x}", peer_region_id, peer_region_start, peer_region_end, bar_addr_64);
    return 0;
}

// Returns broken rows as bits set to 1 in 'memory' and 'logic'
uint32_t tt_SiliconDevice::get_harvested_noc_rows(uint32_t harvesting_mask) {
    auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
    const std::vector<uint32_t> &harv_to_noc_loc = architecture_implementation->get_harvesting_noc_locations();
    uint32_t harv_noc_rows = 0;
    std::string harv_noc_rows_str = "";

    for (int pos=0; pos<harv_to_noc_loc.size(); ++pos) {
        bool is_row_harvested = harvesting_mask & 0x1;
        if (is_row_harvested) {
            harv_noc_rows |= (1 << harv_to_noc_loc[pos]);
            if (harv_noc_rows_str != "") harv_noc_rows_str += ", ";
            harv_noc_rows_str += std::to_string(harv_to_noc_loc[pos]);
        }
        harvesting_mask = harvesting_mask >> 1;
    }
    if (harv_noc_rows > 0) {
        log_debug(LogSiliconDriver, "HARVESTING NOC Y-LOC 0x{:x} = {{}}", harv_noc_rows, harv_noc_rows_str.c_str());
    }
    return harv_noc_rows;
}

uint32_t tt_SiliconDevice::get_harvested_rows (int logical_device_id) {
    const char* harv_override = std::getenv("T6PY_HARVESTING_OVERRIDE");
    uint32_t harv = 0xffffffff;
    if (harv_override) {
        harv = std::stoul(harv_override, nullptr, 16);
    } else {
        auto mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(logical_device_id);
        PCIDevice *pci_device = get_pci_device(mmio_capable_chip_logical);
        int harvesting_msg_code = arc_msg(logical_device_id, 0xaa00 | pci_device->get_architecture_implementation()->get_arc_message_arc_get_harvesting(), true, 0, 0, 1, &harv);
        log_assert(harvesting_msg_code != MSG_ERROR_REPLY, "Failed to read harvested rows from device {}", logical_device_id);
    }
    log_assert(harv != 0xffffffff, "Readback 0xffffffff for harvesting info. Chip is fused incorrectly!");
    log_debug(LogSiliconDriver, "HARVESTING {}, 0x{:x}", (harv==0) ? "DISABLED":"ENABLED", harv);
    
    uint32_t memory = harv & 0x3ff;
    uint32_t logic = (harv >> 10) & 0x3ff;
    return (memory|logic);
}

uint32_t tt_SiliconDevice::get_harvested_noc_rows_for_chip (int logical_device_id) {
    return get_harvested_noc_rows(get_harvested_rows(logical_device_id));
}

void tt_SiliconDevice::enable_local_ethernet_queue(const chip_id_t &device_id, int timeout) {
    uint32_t msg_success = 0x0;
    auto timeout_seconds = std::chrono::seconds(timeout);
    auto start = std::chrono::system_clock::now();
    while (msg_success != 1) {
        if (std::chrono::system_clock::now() - start > timeout_seconds) {
            throw std::runtime_error(fmt::format("Timed out after waiting {} seconds for for DRAM to finish training", timeout));
        }

        if (arc_msg(device_id, 0xaa58, true, 0xFFFF, 0xFFFF, 1, &msg_success) == MSG_ERROR_REPLY) {
            break;
        }
    }
}

void *tt_SiliconDevice::host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
    if (hugepage_mapping.at(src_device_id).at(channel) != nullptr) {
        return static_cast<std::byte*>(hugepage_mapping.at(src_device_id).at(channel)) + offset;
    } else {
        return nullptr;
    }
}

// Wrapper for throwing more helpful exception when not-enabled pci intf is accessed.
inline PCIDevice* tt_SiliconDevice::get_pci_device(int device_id) const {
    if (!m_pci_device_map.count(device_id)){
        throw std::runtime_error(fmt::format("device_id: {} attempted to be accessed, but is not enabled.", device_id));
    }
    return m_pci_device_map.at(device_id).get();
}

std::shared_ptr<boost::interprocess::named_mutex> tt_SiliconDevice::get_mutex(const std::string& tlb_name, int pci_interface_id) {
    std::string mutex_name = tlb_name + std::to_string(pci_interface_id);
    return hardware_resource_mutex_map.at(mutex_name);
}

uint64_t tt_SiliconDevice::get_sys_addr(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y, uint64_t offset) {
    uint64_t result = chip_y;
    uint64_t noc_addr_local_bits_mask = (1UL << eth_interface_params.noc_addr_local_bits) - 1;
    result <<= eth_interface_params.noc_addr_node_id_bits;
    result |= chip_x;
    result <<= eth_interface_params.noc_addr_node_id_bits;
    result |= noc_y;
    result <<= eth_interface_params.noc_addr_node_id_bits;
    result |= noc_x;
    result <<= eth_interface_params.noc_addr_local_bits;
    result |= (noc_addr_local_bits_mask & offset);
    return result;
}

uint16_t tt_SiliconDevice::get_sys_rack(uint32_t rack_x, uint32_t rack_y) {
    uint32_t result = rack_y;
    result <<= eth_interface_params.eth_rack_coord_width;
    result |= rack_x;

    return result;
}

bool tt_SiliconDevice::is_non_mmio_cmd_q_full(uint32_t curr_wptr, uint32_t curr_rptr) {
  return (curr_wptr != curr_rptr) && ((curr_wptr & eth_interface_params.cmd_buf_size_mask) == (curr_rptr & eth_interface_params.cmd_buf_size_mask));
}

/*
 *
 *                                       NON_MMIO_MUTEX Usage
 *
 * Relevant functions:
 *  - write_to_non_mmio_device
 *  - read_from_non_mmio_device
 *
 * The non-MMIO read/write functions (excluding the `*_epoch_cmd` variants) are responsible for the
 * writes/reads to/from those wormhole chips that aren't memory mapped or directly host connected.
 * To get the data to or from those other chips, there is a memory transfer protocol - initiated on
 * the host side but carried out by any number of the ethernet cores (the ethernet core pool is dictated
 * by `this->NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS`) on the MMIO chips (e.g. typically just the one chip in a galaxy).
 *
 * There is a command queue structure in ethernet core FW to accept these read/write commands. However, there is no
 * atomic increment (from host side) for the write pointers of these queues, nor is there any sort of other hardware
 * mutual exclusion (as of WH) from host side when populating commands into the queue (as in when the host pushes a
 * write command into the ethernet core's queue).
 *
 * Therefore, any of these non_mmio commands from host side need to be synchronized so they don't accidentally corrupt
 * each other. The finest granularity possible to synchronize on would be the command slot and wrptr (per core),
 * but wrptr updates also need to be coordinated:
 *  - you can't increment wrptr unless you are writing to the next index and your write is complete
 *  - if two threads could guarantee separate command slots, they'd need to order their wrptr updates from lowest to
 *    highest and based on completion of command writes.
 *
 * Stepping back a little bit, a sort of interprocess synchronization is required because the driver may be invoked
 * from several processes. Indeed from pybuda (python), we'd typically needs to spin up multiple processes:
 *   - 1 for pushing inputs
 *   - 1 for popping outputs
 *   - 1 for managing execution state
 *  (or some variation along those lines).
 *
 * The interprocess mutex from measurements takes a while. While not seconds, it's non-trivial such that locking and
 * unlocking at fine granularity would be more detrimental to performance than acquiring it for a large block.
 *
 * Considering the above, the current chosen approach is to make each of these calls acquired a shared mutex:
 * `NON_MMIO_MUTEX_NAME`
 *  - They acquire at a relatively large granularity -> for the entire duration of the function where we interact
 *    with the ethernet core (read/write) and where we use `active_core` to choose a core.
 *    - Simplifies synchronization while we reach stability
 *  - We need to include any usage (read/modify) of `active_core` in the mutex acquisition scope.
 *
 * Other schemes may be more performant.
 */


/*
 * Note that this function is required to acquire the `NON_MMIO_MUTEX_NAME` mutex for interacting with the
 * ethernet core (host) command queue DO NOT issue any pcie reads/writes to the ethernet core prior to acquiring the
 * mutex. For extra information, see the "NON_MMIO_MUTEX Usage" above
 */


void tt_SiliconDevice::write_to_non_mmio_device(
                        const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t address, 
                        bool broadcast, std::vector<int> broadcast_header) {
    
    chip_id_t mmio_capable_chip_logical;
    
    if(broadcast) {
        mmio_capable_chip_logical = core.chip;
    }
    else {
        mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(core.chip);
    }

    if (non_mmio_transfer_cores_customized) {
        log_assert(active_eth_core_idx_per_chip.find(mmio_capable_chip_logical) != active_eth_core_idx_per_chip.end(), "Ethernet Cores for Host to Cluster communication were not initialized for all MMIO devices.");
    }

    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);
    constexpr int BROADCAST_HEADER_SIZE = sizeof(data_word_t) * 8; // Broadcast header is 8 words
    const auto target_chip = ndesc->get_chip_locations().at(core.chip);

    std::string write_tlb = "LARGE_WRITE_TLB";
    std::string read_tlb = "LARGE_READ_TLB";
    std::string empty_tlb = "";
    translate_to_noc_table_coords(core.chip, core.y, core.x);
    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_q_ptrs = std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes*2 / sizeof(uint32_t));

    std::vector<std::uint32_t> data_block;

    routing_cmd_t *new_cmd;

    uint32_t buffer_id = 0;
    uint32_t timestamp = 0; //CMD_TIMESTAMP;
    bool use_dram;
    uint32_t max_block_size;

    flush_non_mmio = true;
    // Broadcast requires block writes to host dram
    use_dram = broadcast || (size_in_bytes > 256 * DATA_WORD_SIZE);
    max_block_size = use_dram ? host_address_params.eth_routing_block_size : eth_interface_params.max_block_size;

    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //
    const scoped_lock<named_mutex> lock(*get_mutex(NON_MMIO_MUTEX_NAME, this->get_pci_device(mmio_capable_chip_logical)->get_device_num()));

    int& active_core_for_txn = non_mmio_transfer_cores_customized ? active_eth_core_idx_per_chip.at(mmio_capable_chip_logical) : active_core;
    tt_cxy_pair remote_transfer_ethernet_core = remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn];

    erisc_command.resize(sizeof(routing_cmd_t)/DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t *)&erisc_command[0];
    read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_core, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, eth_interface_params.remote_update_ptr_size_bytes*2, read_tlb);
    uint32_t full_count = 0;
    uint32_t offset = 0;
    uint32_t block_size;

    bool full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];
    while (offset < size_in_bytes) {
        while (full) {
            read_device_memory(erisc_q_rptr.data(), remote_transfer_ethernet_core, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes + eth_interface_params.remote_update_ptr_size_bytes, DATA_WORD_SIZE, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0],erisc_q_rptr[0]);
            full_count++;
        }
        //full = true;
        // set full only if this command will make the q full.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.
        //full = is_non_mmio_cmd_q_full((erisc_q_ptrs[0] + 1) & CMD_BUF_PTR_MASK, erisc_q_rptr[0]);

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.cmd_buf_size_mask;
        if ((address + offset) & 0x1F) { // address not 32-byte aligned
            block_size = DATA_WORD_SIZE; // 4 byte aligned
        } else {
            // For broadcast we prepend a 32byte header. Decrease block size (size of payload) by this amount.
            block_size = offset + max_block_size > size_in_bytes + 32 * broadcast ? size_in_bytes - offset : max_block_size - 32 * broadcast;
            // Explictly align block_size to 4 bytes, in case the input buffer is not uint32_t aligned
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            block_size = (block_size + alignment_mask) & ~alignment_mask;
        }
        // For 4 byte aligned data, transfer_size always == block_size. For unaligned data, transfer_size < block_size in the last block
        uint64_t transfer_size = std::min(block_size, size_in_bytes - offset); // Host side data size that needs to be copied
        // Use block mode for broadcast
        uint32_t req_flags = (broadcast || (block_size > DATA_WORD_SIZE)) ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_wr_req | timestamp) : eth_interface_params.cmd_wr_req;
        uint32_t resp_flags = block_size > DATA_WORD_SIZE ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_wr_ack) : eth_interface_params.cmd_wr_ack;
        timestamp = 0;
        
        if(broadcast) {
            req_flags |= eth_interface_params.cmd_broadcast;
        }

        uint32_t host_dram_block_addr = host_address_params.eth_routing_buffers_start + (active_core_for_txn * eth_interface_params.cmd_buf_size + req_wr_ptr) * max_block_size;
        uint16_t host_dram_channel = 0; // This needs to be 0, since WH can only map ETH buffers to chan 0.

        if (req_flags & eth_interface_params.cmd_data_block) {
            // Copy data to sysmem or device DRAM for Block mode
            if (use_dram) {
                req_flags |= eth_interface_params.cmd_data_block_dram;
                resp_flags |= eth_interface_params.cmd_data_block_dram;
                size_buffer_to_capacity(data_block, block_size);
                memcpy(&data_block[0], (uint8_t*)mem_ptr + offset, transfer_size);
                if(broadcast) {
                    // Write broadcast header to sysmem
                    write_to_sysmem(broadcast_header.data(), broadcast_header.size() * sizeof(uint32_t), host_dram_block_addr, host_dram_channel, mmio_capable_chip_logical);
                }
                // Write payload to sysmem
                write_to_sysmem(data_block.data(), data_block.size() * DATA_WORD_SIZE, host_dram_block_addr + BROADCAST_HEADER_SIZE * broadcast, host_dram_channel, mmio_capable_chip_logical);

            } else {
                uint32_t buf_address = eth_interface_params.eth_routing_data_buffer_addr + req_wr_ptr * max_block_size;
                size_buffer_to_capacity(data_block, block_size);
                memcpy(&data_block[0], (uint8_t*)mem_ptr + offset, transfer_size);
                write_device_memory(data_block.data(), data_block.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, buf_address, write_tlb);
            }
            tt_driver_atomics::sfence();
        }

        // Send the read request
        log_assert(broadcast || (req_flags == eth_interface_params.cmd_wr_req) || (((address + offset) % 32) == 0), "Block mode address must be 32-byte aligned."); // Block mode address must be 32-byte aligned.
        
        if(broadcast) {
            // Only specify endpoint local address for broadcast
            new_cmd->sys_addr = address + offset;
        }
        else {
            new_cmd->sys_addr = get_sys_addr(std::get<0>(target_chip), std::get<1>(target_chip), core.x, core.y, address + offset);
            new_cmd->rack = get_sys_rack(std::get<2>(target_chip), std::get<3>(target_chip));
        }
            
        if(req_flags & eth_interface_params.cmd_data_block) {
            // Block mode
            new_cmd->data = block_size + BROADCAST_HEADER_SIZE * broadcast;
        }
        else {
            if(size_in_bytes - offset < sizeof(uint32_t)) {
                // Handle misalignment at the end of the buffer:
                // Assemble a padded uint32_t from single bytes, in case we have less than 4 bytes remaining
                memcpy(&new_cmd->data, static_cast<const uint8_t*>(mem_ptr) + offset, size_in_bytes - offset);
            }
            else {
                new_cmd->data = *((uint32_t*)mem_ptr + offset/DATA_WORD_SIZE);
            }
        }

        new_cmd->flags = req_flags;
        if (use_dram) {
            new_cmd->src_addr_tag = host_dram_block_addr;
        }
        write_device_memory(erisc_command.data(), erisc_command.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.request_routing_cmd_queue_base + (sizeof(routing_cmd_t) * req_wr_ptr), write_tlb);
        tt_driver_atomics::sfence();

        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        write_device_memory(erisc_q_wptr.data(), erisc_q_wptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, write_tlb);
        tt_driver_atomics::sfence();

        offset += transfer_size;

        // If there is more data to send and this command will make the q full, switch to next Q.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full((erisc_q_ptrs[0]) & eth_interface_params.cmd_buf_ptr_mask, erisc_q_rptr[0])) {
            active_core_for_txn++;
            uint32_t update_mask_for_chip = remote_transfer_ethernet_cores[mmio_capable_chip_logical].size() - 1;
            active_core_for_txn = non_mmio_transfer_cores_customized ? (active_core_for_txn & update_mask_for_chip) : ((active_core_for_txn & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID);
            // active_core = (active_core & NON_EPOCH_ETH_CORES_MASK) + NON_EPOCH_ETH_CORES_START_ID;
            remote_transfer_ethernet_core = remote_transfer_ethernet_cores.at(mmio_capable_chip_logical)[active_core_for_txn];
            read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_core, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, eth_interface_params.remote_update_ptr_size_bytes*2, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }
    }
}

/*
 * Note that this function is required to acquire the `NON_MMIO_MUTEX_NAME` mutex for interacting with the ethernet core (host) command queue
 * DO NOT use `active_core` or issue any pcie reads/writes to the ethernet core prior to acquiring the mutex. For extra information, see the "NON_MMIO_MUTEX Usage" above
 */
void tt_SiliconDevice::read_from_non_mmio_device(void* mem_ptr, tt_cxy_pair core, uint64_t address, uint32_t size_in_bytes) {

    using data_word_t = uint32_t;
    constexpr int DATA_WORD_SIZE = sizeof(data_word_t);
    std::string write_tlb = "LARGE_WRITE_TLB";
    std::string read_tlb = "LARGE_READ_TLB";
    std::string empty_tlb = "";
    translate_to_noc_table_coords(core.chip, core.y, core.x);

    const auto &mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(core.chip);
    const eth_coord_t target_chip = ndesc->get_chip_locations().at(core.chip);

    std::vector<std::uint32_t> erisc_command;
    std::vector<std::uint32_t> erisc_q_rptr;
    std::vector<std::uint32_t> erisc_q_ptrs = std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes*2 / DATA_WORD_SIZE);
    std::vector<std::uint32_t> erisc_resp_q_wptr = std::vector<uint32_t>(1);
    std::vector<std::uint32_t> erisc_resp_q_rptr = std::vector<uint32_t>(1);


    std::vector<std::uint32_t> data_block;

    routing_cmd_t *new_cmd;

    erisc_command.resize(sizeof(routing_cmd_t)/DATA_WORD_SIZE);
    new_cmd = (routing_cmd_t *)&erisc_command[0];

    //
    //                    MUTEX ACQUIRE (NON-MMIO)
    //  do not locate any ethernet core reads/writes before this acquire
    //
    const scoped_lock<named_mutex> lock(*get_mutex(NON_MMIO_MUTEX_NAME, this->get_pci_device(mmio_capable_chip_logical)->get_device_num()));
    const tt_cxy_pair remote_transfer_ethernet_core = remote_transfer_ethernet_cores[mmio_capable_chip_logical].at(0);

    read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_core, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, eth_interface_params.remote_update_ptr_size_bytes*2, read_tlb);
    read_device_memory(erisc_resp_q_wptr.data(), remote_transfer_ethernet_core, eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, DATA_WORD_SIZE, read_tlb);
    read_device_memory(erisc_resp_q_rptr.data(), remote_transfer_ethernet_core, eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes + eth_interface_params.remote_update_ptr_size_bytes, DATA_WORD_SIZE, read_tlb);

    bool full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
    erisc_q_rptr.resize(1);
    erisc_q_rptr[0] = erisc_q_ptrs[4];

    bool use_dram;
    uint32_t max_block_size;

    use_dram = size_in_bytes > 1024;
    max_block_size = use_dram ? host_address_params.eth_routing_block_size : eth_interface_params.max_block_size;

    uint32_t offset = 0;
    uint32_t block_size;
    uint32_t buffer_id = 0;

    while (offset < size_in_bytes) {
        while (full) {
            read_device_memory(erisc_q_rptr.data(), remote_transfer_ethernet_core, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes + eth_interface_params.remote_update_ptr_size_bytes, DATA_WORD_SIZE, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0],erisc_q_rptr[0]);
        }

        uint32_t req_wr_ptr = erisc_q_ptrs[0] & eth_interface_params.cmd_buf_size_mask;
        if ((address + offset) & 0x1F) { // address not 32-byte aligned
            block_size = DATA_WORD_SIZE; // 4 byte aligned block
        } else {
            block_size = offset + max_block_size > size_in_bytes ? size_in_bytes - offset : max_block_size;
            // Align up to 4 bytes.
            uint32_t alignment_mask = sizeof(uint32_t) - 1;
            block_size = (block_size + alignment_mask) & ~alignment_mask;

        }
        uint32_t req_flags = block_size > DATA_WORD_SIZE ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_rd_req) : eth_interface_params.cmd_rd_req;
        uint32_t resp_flags = block_size > DATA_WORD_SIZE ? (eth_interface_params.cmd_data_block | eth_interface_params.cmd_rd_data) : eth_interface_params.cmd_rd_data;
        uint32_t resp_rd_ptr = erisc_resp_q_rptr[0] & eth_interface_params.cmd_buf_size_mask;
        uint32_t host_dram_block_addr = host_address_params.eth_routing_buffers_start + resp_rd_ptr * max_block_size;
        uint16_t host_dram_channel = 0; // This needs to be 0, since WH can only map ETH buffers to chan 0.

        if (use_dram && block_size > DATA_WORD_SIZE) {
            req_flags |= eth_interface_params.cmd_data_block_dram;
            resp_flags |= eth_interface_params.cmd_data_block_dram;
        }

        // Send the read request
        log_assert((req_flags == eth_interface_params.cmd_rd_req) || (((address + offset) & 0x1F) == 0), "Block mode offset must be 32-byte aligned."); // Block mode offset must be 32-byte aligned.
        new_cmd->sys_addr = get_sys_addr(std::get<0>(target_chip), std::get<1>(target_chip), core.x, core.y, address + offset);
        new_cmd->rack = get_sys_rack(std::get<2>(target_chip), std::get<3>(target_chip));
        new_cmd->data = block_size;
        new_cmd->flags = req_flags;
        if (use_dram) {
            new_cmd->src_addr_tag = host_dram_block_addr;
        }
        write_device_memory(erisc_command.data(), erisc_command.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.request_routing_cmd_queue_base + (sizeof(routing_cmd_t) * req_wr_ptr), write_tlb);;
        tt_driver_atomics::sfence();

        erisc_q_ptrs[0] = (erisc_q_ptrs[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        std::vector<std::uint32_t> erisc_q_wptr;
        erisc_q_wptr.resize(1);
        erisc_q_wptr[0] = erisc_q_ptrs[0];
        write_device_memory(erisc_q_wptr.data(), erisc_q_wptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, write_tlb);
        tt_driver_atomics::sfence();
        // If there is more data to read and this command will make the q full, set full to 1.
        // otherwise full stays false so that we do not poll the rd pointer in next iteration.
        // As long as current command push does not fill up the queue completely, we do not want
        // to poll rd pointer in every iteration.

        if (is_non_mmio_cmd_q_full((erisc_q_ptrs[0]), erisc_q_rptr[0])) {
            read_device_memory(erisc_q_ptrs.data(), remote_transfer_ethernet_core, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, eth_interface_params.remote_update_ptr_size_bytes*2, read_tlb);
            full = is_non_mmio_cmd_q_full(erisc_q_ptrs[0], erisc_q_ptrs[4]);
            erisc_q_rptr[0] = erisc_q_ptrs[4];
        }

        // Wait for read request completion and extract the data into the `mem_ptr`

        // erisc firmware will:
        // 1. clear response flags
        // 2. start operation
        // 3. advance response wrptr
        // 4. complete operation and write data into response or buffer
        // 5. set response flags
        // So we have to wait for wrptr to advance, then wait for flags to be nonzero, then read data.

        do {
            read_device_memory(erisc_resp_q_wptr.data(), remote_transfer_ethernet_core, eth_interface_params.response_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, DATA_WORD_SIZE, read_tlb);
        } while (erisc_resp_q_rptr[0] == erisc_resp_q_wptr[0]);
        tt_driver_atomics::lfence();
        uint32_t flags_offset = 12 + sizeof(routing_cmd_t) * resp_rd_ptr;
        std::vector<std::uint32_t> erisc_resp_flags = std::vector<uint32_t>(1);
        do {
            read_device_memory(erisc_resp_flags.data(), remote_transfer_ethernet_core, eth_interface_params.response_routing_cmd_queue_base + flags_offset, DATA_WORD_SIZE, read_tlb);
        } while (erisc_resp_flags[0] == 0);

        if (erisc_resp_flags[0] == resp_flags) {
            tt_driver_atomics::lfence();
            uint32_t data_offset = 8 + sizeof(routing_cmd_t) * resp_rd_ptr;
            if (block_size == DATA_WORD_SIZE) {
                std::vector<std::uint32_t> erisc_resp_data = std::vector<uint32_t>(1);
                read_device_memory(erisc_resp_data.data(), remote_transfer_ethernet_core, eth_interface_params.response_routing_cmd_queue_base + data_offset, DATA_WORD_SIZE, read_tlb);
                if(size_in_bytes - offset < 4)  {
                    // Handle misaligned (4 bytes) data at the end of the block.
                    // Only read remaining bytes into the host buffer, instead of reading the full uint32_t
                    std::memcpy((uint8_t*)mem_ptr + offset, erisc_resp_data.data(), size_in_bytes - offset);
                }
                else {
                    *((uint32_t*)mem_ptr + offset/DATA_WORD_SIZE) = erisc_resp_data[0];
                }
            } else {
                // Read 4 byte aligned block from device/sysmem
                if (use_dram) {
                    size_buffer_to_capacity(data_block, block_size);
                    read_from_sysmem(data_block.data(), host_dram_block_addr, host_dram_channel, block_size, mmio_capable_chip_logical);
                } else {
                    uint32_t buf_address = eth_interface_params.eth_routing_data_buffer_addr + resp_rd_ptr * max_block_size;
                    size_buffer_to_capacity(data_block, block_size);
                    read_device_memory(data_block.data(), remote_transfer_ethernet_core, buf_address, block_size, read_tlb);
                }
                // assert(mem_ptr.size() - (offset/DATA_WORD_SIZE) >= (block_size * DATA_WORD_SIZE));
                log_assert((data_block.size() * DATA_WORD_SIZE) >= block_size, "Incorrect data size read back from sysmem/device");
                // Account for misalignment by skipping any padding bytes in the copied data_block
                memcpy((uint8_t*)mem_ptr + offset, data_block.data(), std::min(block_size, size_in_bytes - offset));
            }
        }

        // Finally increment the rdptr for the response command q
        erisc_resp_q_rptr[0] = (erisc_resp_q_rptr[0] + 1) & eth_interface_params.cmd_buf_ptr_mask;
        write_device_memory(erisc_resp_q_rptr.data(), erisc_resp_q_rptr.size() * DATA_WORD_SIZE, remote_transfer_ethernet_core, eth_interface_params.response_cmd_queue_base + sizeof(remote_update_ptr_t) + eth_interface_params.cmd_counters_size_bytes, write_tlb);
        tt_driver_atomics::sfence();
        log_assert(erisc_resp_flags[0] == resp_flags, "Unexpected ERISC Response Flags.");

        offset += block_size;
    }

}

void tt_SiliconDevice::wait_for_non_mmio_flush() {
    if(flush_non_mmio) {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO flush not supported in Blackhole");
        std::string read_tlb = "LARGE_READ_TLB";
        auto chips_with_mmio = this->get_target_mmio_device_ids();
        for(auto chip_id : chips_with_mmio) {
            auto arch = get_soc_descriptor(chip_id).arch;
            if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
                std::vector<std::uint32_t> erisc_txn_counters = std::vector<uint32_t>(2);
                std::vector<std::uint32_t> erisc_q_ptrs = std::vector<uint32_t>(eth_interface_params.remote_update_ptr_size_bytes*2 / sizeof(uint32_t));

                //wait for all queues to be empty.
                for (tt_cxy_pair &cxy : remote_transfer_ethernet_cores.at(chip_id)) {
                    do {
                        read_device_memory(erisc_q_ptrs.data(), cxy, eth_interface_params.request_cmd_queue_base + eth_interface_params.cmd_counters_size_bytes, eth_interface_params.remote_update_ptr_size_bytes*2, read_tlb);
                    } while (erisc_q_ptrs[0] != erisc_q_ptrs[4]);
                }
                //wait for all write responses to come back.
                for (tt_cxy_pair &cxy : remote_transfer_ethernet_cores.at(chip_id)) {
                    do {
                        read_device_memory(erisc_txn_counters.data(), cxy, eth_interface_params.request_cmd_queue_base, 8, read_tlb);
                    } while (erisc_txn_counters[0] != erisc_txn_counters[1]);
                }
            } else {
                break;
            }
        }
        flush_non_mmio = false;
    }
}

// Broadcast Functions
void tt_SiliconDevice::generate_tensix_broadcast_grids_for_grayskull(std::set<std::pair<tt_xy_pair, tt_xy_pair>>& broadcast_grids,  std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude) {
    // If row 0 is not explicitly excluded, exclude it here since its non-tensix
    rows_to_exclude.insert(0);
    // If row 11 is excluded, we can close the SOC grid. If not, exclude row 12 to close grid.
    if(rows_to_exclude.find(11) == rows_to_exclude.end()) {
        rows_to_exclude.insert(12);
    }
    // If col 0 is not explicitly excluded, exclude it here since its non-tensix
    cols_to_exclude.insert(0);
    // If col 12 is excluded, we can close the SOC grid. If not, exclude col 13 to close grid.
    if(cols_to_exclude.find(12) == cols_to_exclude.end()) {
        cols_to_exclude.insert(13);
    }
    std::vector<std::pair<int, int>> bb_x_coords = {};
    std::vector<std::pair<int, int>> bb_y_coords = {};

    // Generate starting and ending x coordinates of each bounding box/grid
    for(auto x_it = cols_to_exclude.begin(); x_it != cols_to_exclude.end(); x_it++) {
        if(x_it == std::prev(cols_to_exclude.end(), 1)) continue;
        if(cols_to_exclude.find(*(x_it) + 1) == cols_to_exclude.end() and cols_to_exclude.find(*(std::next(x_it, 1)) - 1) == cols_to_exclude.end()) {
            bb_x_coords.push_back({*(x_it) + 1, *(std::next(x_it, 1)) - 1});
        }
    }

    for(auto y_it = rows_to_exclude.begin(); y_it != rows_to_exclude.end(); y_it++) {
        if(y_it == std::prev(rows_to_exclude.end(), 1)) continue;
        if(rows_to_exclude.find((*y_it) + 1) == rows_to_exclude.end() and rows_to_exclude.find(*std::next(y_it, 1) - 1) == rows_to_exclude.end()) {
            bb_y_coords.push_back({*(y_it) + 1, *(std::next(y_it, 1)) - 1});
        }
    }
    // Assemble x and y coordinates into bounding box vertices
    for(const auto& x_pair : bb_x_coords) {
        for(const auto& y_pair : bb_y_coords) {
            tt_xy_pair top_left = tt_xy_pair(x_pair.first, y_pair.first);
            tt_xy_pair bot_right = tt_xy_pair(x_pair.second, y_pair.second);
            broadcast_grids.insert({top_left, bot_right});
        }
    }
}

std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& tt_SiliconDevice::get_ethernet_broadcast_headers(const std::set<chip_id_t>& chips_to_exclude) {
    // Generate headers for Ethernet Broadcast (WH) only. Each header corresponds to a unique broadcast "grid".
    if(bcast_header_cache.find(chips_to_exclude) == bcast_header_cache.end()) {
        bcast_header_cache[chips_to_exclude] = {};
        std::unordered_map<chip_id_t, std::unordered_map<chip_id_t, std::vector<int>>> broadcast_mask_for_target_chips_per_group = {};
        std::map<std::vector<int>, std::tuple<chip_id_t, std::vector<int>>> broadcast_header_union_per_group = {};
        chip_id_t first_mmio_chip = *(get_target_mmio_device_ids().begin());
        for(const auto& chip : target_devices_in_cluster) {
            if(chips_to_exclude.find(chip) == chips_to_exclude.end()) {
                // Get shelf local physical chip id included in broadcast
                chip_id_t physical_chip_id = ndesc -> get_shelf_local_physical_chip_coords(chip);
                eth_coord_t eth_coords = ndesc -> get_chip_locations().at(chip);
                // Rack word to be set in header
                uint32_t rack_word = std::get<2>(eth_coords) >> 2;
                // Rack byte to be set in header
                uint32_t rack_byte = std::get<2>(eth_coords) % 4;
                // 1st level grouping: Group broadcasts based on the MMIO chip they must go through
                // Nebula + Galaxy Topology assumption: Disjoint sets can only be present in the first shelf, with each set connected to host through its closest MMIO chip
                // For the first shelf, pass broadcasts to specific chips through their closest MMIO chip
                // All other shelves are fully connected galaxy grids. These are connected to all MMIO devices. Use any (or the first) MMIO device in the list.
                chip_id_t closest_mmio_chip = 0;
                if (std::get<2>(eth_coords) == 0 && std::get<3>(eth_coords) == 0) {
                    // Shelf 0 + Rack 0: Either an MMIO chip or a remote chip potentially connected to host through its own MMIO counterpart.
                    closest_mmio_chip = ndesc -> get_closest_mmio_capable_chip(chip);
                }
                else {
                    // All other shelves: Group these under the same/first MMIO chip, since all MMIO chips are connected.
                    closest_mmio_chip = first_mmio_chip;
                }
                if(broadcast_mask_for_target_chips_per_group.find(closest_mmio_chip) == broadcast_mask_for_target_chips_per_group.end()) {
                    broadcast_mask_for_target_chips_per_group.insert({closest_mmio_chip, {}});
                }
                // For each target physical chip id (local to a shelf), generate headers based on all racks and shelves that contain this physical id.
                if(broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).find(physical_chip_id) == broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).end()) {
                    // Target seen for the first time.
                    std::vector<int> broadcast_mask(8, 0);
                    broadcast_mask.at(rack_word) |= (1 << std::get<3>(eth_coords)) << rack_byte;
                    broadcast_mask.at(3) |= 1 << physical_chip_id;
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).insert({physical_chip_id, broadcast_mask});

                }
                else {
                    // Target was seen before -> include curr rack and shelf in header
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).at(physical_chip_id).at(rack_word) |= static_cast<uint32_t>(1 << std::get<3>(eth_coords)) << rack_byte;
                }
            }
        }
        // 2nd level grouping: For each MMIO group, further group the chips based on their rack and shelf headers. The number of groups after this step represent the final set of broadcast grids.
        for(auto& mmio_group : broadcast_mask_for_target_chips_per_group) {
            for(auto& chip : mmio_group.second) {
                // Generate a hash for this MMIO Chip + Rack + Shelf group
                std::vector<int> header_hash = {mmio_group.first, chip.second.at(0), chip.second.at(1), chip.second.at(2)};
                if(broadcast_header_union_per_group.find(header_hash) == broadcast_header_union_per_group.end()) {
                    broadcast_header_union_per_group.insert({header_hash, std::make_tuple(mmio_group.first, chip.second)});
                }
                else {
                    // If group found, update chip header entry
                    std::get<1>(broadcast_header_union_per_group.at(header_hash)).at(3) |= chip.second.at(3);
                }
            }
        }
        // Get all broadcast headers per MMIO group
        for(const auto& header : broadcast_header_union_per_group) {
            chip_id_t mmio_chip = std::get<0>(header.second);
            if(bcast_header_cache[chips_to_exclude].find(mmio_chip) == bcast_header_cache[chips_to_exclude].end()) {
                bcast_header_cache[chips_to_exclude].insert({mmio_chip, {}});
            }
            bcast_header_cache[chips_to_exclude].at(mmio_chip).push_back(std::get<1>(header.second));
        }
        // Invert headers (FW convention)
        for(auto& bcast_group : bcast_header_cache[chips_to_exclude]) {
            for(auto& header : bcast_group.second) {
                int header_idx = 0;
                for(auto& header_entry : header) {
                    if(header_idx == 4) break;
                    header_entry = ~header_entry;
                    header_idx++;
                }
            }
        }
    }
    return bcast_header_cache[chips_to_exclude];
}

void tt_SiliconDevice::pcie_broadcast_write(chip_id_t chip, const void* mem_ptr, uint32_t size_in_bytes, std::uint32_t addr, const tt_xy_pair& start, const tt_xy_pair& end, const std::string& fallback_tlb) {
    // Use the specified TLB to broadcast data to all cores included in the [start, end] grid -> GS Only. Use Ethernet Broadcast for WH.
    PCIDevice *pci_device = get_pci_device(chip);
    const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
    const uint8_t* buffer_addr = static_cast<const uint8_t*>(mem_ptr);
    const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, pci_device->get_device_num()));
    while(size_in_bytes > 0) {
        auto [mapped_address, tlb_size] = pci_device->set_dynamic_tlb_broadcast(tlb_index, addr, harvested_coord_translation, start, end, dynamic_tlb_ordering_modes.at(fallback_tlb));
        uint64_t transfer_size = std::min((uint64_t)size_in_bytes, tlb_size);
        pci_device->write_block(mapped_address, transfer_size, buffer_addr);

        size_in_bytes -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

inline bool tensix_or_eth_in_broadcast(const std::set<uint32_t>& cols_to_exclude, const tt::umd::architecture_implementation* architecture_implementation) {
    bool found_tensix_or_eth = false;
    for(const auto& col : architecture_implementation->get_t6_x_locations()) {
        found_tensix_or_eth |= (cols_to_exclude.find(col) == cols_to_exclude.end());
    }
    return found_tensix_or_eth;
}

inline bool valid_tensix_broadcast_grid(const std::set<uint32_t>& rows_to_exclude, const std::set<uint32_t>& cols_to_exclude, const tt::umd::architecture_implementation* architecture_implementation) {
    bool t6_bcast_rows_complete = true;
    bool t6_bcast_rows_empty = true;
    
    for(const auto& row : architecture_implementation->get_t6_y_locations()) {
        t6_bcast_rows_complete &= (rows_to_exclude.find(row) == rows_to_exclude.end());
        t6_bcast_rows_empty &= (rows_to_exclude.find(row) != rows_to_exclude.end());
    }
    return t6_bcast_rows_complete || t6_bcast_rows_empty;
}


void tt_SiliconDevice::ethernet_broadcast_write(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address,
                                                const std::set<chip_id_t>& chips_to_exclude, const std::set<uint32_t>& rows_to_exclude, 
                                                std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb, bool use_virtual_coords) {
    if(use_ethernet_broadcast) {
        // Broadcast through ERISC core supported
        std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& broadcast_headers = get_ethernet_broadcast_headers(chips_to_exclude);
        // Apply row and column exclusion mask explictly. Placing this here if we want to cache the higher level broadcast headers on future/
        std::uint32_t row_exclusion_mask = 0;
        std::uint32_t col_exclusion_mask = 0;
        for(const auto& row : rows_to_exclude) {
            row_exclusion_mask |= 1 << row;
        }

        for(const auto& col : cols_to_exclude) {
            col_exclusion_mask |= 1 << (16 + col);
        }
        // Write broadcast block to device.
        for(auto& mmio_group : broadcast_headers) {
            for(auto& header : mmio_group.second) {
                header.at(4) = use_virtual_coords * 0x8000; // Reset row/col exclusion masks
                header.at(4) |= row_exclusion_mask;
                header.at(4) |= col_exclusion_mask;
                // Write Target: x-y endpoint is a don't care. Initialize to tt_xy_pair(1, 1)
                write_to_non_mmio_device(mem_ptr, size_in_bytes, tt_cxy_pair(mmio_group.first, tt_xy_pair(1, 1)), address, true, header);
            }
        }
    }
    else {
        // Broadcast not supported. Implement this at the software level as a for loop
        std::vector<tt_cxy_pair> cores_to_write = {};
        for(const auto& chip : target_devices_in_cluster) {
            if(chips_to_exclude.find(chip) != chips_to_exclude.end()) continue;
            for(const auto& core : get_soc_descriptor(chip).cores) {
                if(cols_to_exclude.find(core.first.x) == cols_to_exclude.end() and rows_to_exclude.find(core.first.y) == rows_to_exclude.end() and core.second.type != CoreType::HARVESTED) {
                    write_to_device(mem_ptr, size_in_bytes, tt_cxy_pair(chip, core.first.x, core.first.y), address, fallback_tlb);
                }
            }
        }
    }
}

void tt_SiliconDevice::broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address,
                       const std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb) {
    if (arch_name == tt::ARCH::GRAYSKULL) {
        // Device FW disables broadcasts to all non tensix cores.
        std::vector<tt_xy_pair> dram_cores_to_write = {};
        std::vector<uint32_t> dram_rows = {0, 6};
        std::vector<uint32_t> dram_cols = {1, 4, 7, 10};

        for(const auto& row : dram_rows) {
            for(const auto& col : dram_cols) {
                if(rows_to_exclude.find(row) == rows_to_exclude.end() and cols_to_exclude.find(col) == cols_to_exclude.end()) {
                    dram_cores_to_write.push_back(tt_xy_pair(col, row));
                }
            }
        }
        
        std::set<std::pair<tt_xy_pair, tt_xy_pair>> broadcast_grids = {};
        generate_tensix_broadcast_grids_for_grayskull(broadcast_grids, rows_to_exclude, cols_to_exclude);
        for(const auto& chip : target_devices_in_cluster) {
            if(chips_to_exclude.find(chip) != chips_to_exclude.end()) continue;
            for(const auto& dram : dram_cores_to_write) {
                write_device_memory(mem_ptr, size_in_bytes, tt_cxy_pair(chip, dram), address, fallback_tlb);
            }
            for(const auto& grid : broadcast_grids) {
                pcie_broadcast_write(chip, mem_ptr, size_in_bytes, address, grid.first, grid.second, fallback_tlb);
            }
        } 
    }
    else if (arch_name == tt::ARCH::BLACKHOLE) {
        auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
        if(cols_to_exclude.find(0) == cols_to_exclude.end() or cols_to_exclude.find(9) == cols_to_exclude.end()) {
            log_assert(!tensix_or_eth_in_broadcast(cols_to_exclude, architecture_implementation.get()), "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Blackhole.");
            if(cols_to_exclude.find(0) == cols_to_exclude.end()) {
                // When broadcast includes column zero do not exclude anything
                std::set<uint32_t> unsafe_rows = {};
                std::set<uint32_t> cols_to_exclude_for_col_0_bcast = cols_to_exclude;
                std::set<uint32_t> rows_to_exclude_for_col_0_bcast = rows_to_exclude;
                cols_to_exclude_for_col_0_bcast.insert(9);
                rows_to_exclude_for_col_0_bcast.insert(unsafe_rows.begin(), unsafe_rows.end());
                ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                        rows_to_exclude_for_col_0_bcast, cols_to_exclude_for_col_0_bcast, fallback_tlb, false);
            }
            if(cols_to_exclude.find(9) == cols_to_exclude.end()) {
                std::set<uint32_t> cols_to_exclude_for_col_9_bcast = cols_to_exclude;
                cols_to_exclude_for_col_9_bcast.insert(0);
                ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                        rows_to_exclude, cols_to_exclude_for_col_9_bcast, fallback_tlb, false);
            }
        }
        else {
            log_assert(use_virtual_coords_for_eth_broadcast or valid_tensix_broadcast_grid(rows_to_exclude, cols_to_exclude, architecture_implementation.get()), 
                        "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                    rows_to_exclude, cols_to_exclude, fallback_tlb, use_virtual_coords_for_eth_broadcast);
        }
    }
    else {
        auto architecture_implementation = tt::umd::architecture_implementation::create(static_cast<tt::umd::architecture>(arch_name));
        if(cols_to_exclude.find(0) == cols_to_exclude.end() or cols_to_exclude.find(5) == cols_to_exclude.end()) {
            log_assert(!tensix_or_eth_in_broadcast(cols_to_exclude, architecture_implementation.get()), "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Wormhole.");
            if(cols_to_exclude.find(0) == cols_to_exclude.end()) {
                // When broadcast includes column zero Exclude PCIe, ARC and router cores from broadcast explictly, since writing to these is unsafe
                // ERISC FW does not exclude these.
                std::set<uint32_t> unsafe_rows = {2, 3, 4, 8, 9, 10};
                std::set<uint32_t> cols_to_exclude_for_col_0_bcast = cols_to_exclude;
                std::set<uint32_t> rows_to_exclude_for_col_0_bcast = rows_to_exclude;
                cols_to_exclude_for_col_0_bcast.insert(5);
                rows_to_exclude_for_col_0_bcast.insert(unsafe_rows.begin(), unsafe_rows.end());
                ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                        rows_to_exclude_for_col_0_bcast, cols_to_exclude_for_col_0_bcast, fallback_tlb, false);
            }
            if(cols_to_exclude.find(5) == cols_to_exclude.end()) {
                std::set<uint32_t> cols_to_exclude_for_col_5_bcast = cols_to_exclude;
                cols_to_exclude_for_col_5_bcast.insert(0);
                ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                        rows_to_exclude, cols_to_exclude_for_col_5_bcast, fallback_tlb, false);
            }
        }
        else {
            log_assert(use_virtual_coords_for_eth_broadcast or valid_tensix_broadcast_grid(rows_to_exclude, cols_to_exclude, architecture_implementation.get()), 
                        "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(mem_ptr, size_in_bytes, address, chips_to_exclude,
                                    rows_to_exclude, cols_to_exclude, fallback_tlb, use_virtual_coords_for_eth_broadcast);
        }
    }
}

int tt_SiliconDevice::remote_arc_msg(int chip, uint32_t msg_code, bool wait_for_done, uint32_t arg0, uint32_t arg1, int timeout, uint32_t *return_3, uint32_t *return_4) {
    constexpr uint64_t ARC_RESET_SCRATCH_ADDR = 0x880030060;
    constexpr uint64_t ARC_RESET_MISC_CNTL_ADDR = 0x880030100;

    auto core = tt_cxy_pair(chip, get_soc_descriptor(chip).arc_cores.at(0));

    if ((msg_code & 0xff00) != 0xaa00) {
        log_error("Malformed message. msg_code is 0x{:x} but should be 0xaa..", msg_code);
    }
    log_assert (arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args"); // Only 16 bits are allowed

    uint32_t fw_arg = arg0 | (arg1<<16);
    int exit_code = 0;

    {
        write_to_non_mmio_device(&fw_arg, sizeof(fw_arg),  core, ARC_RESET_SCRATCH_ADDR + 3 * 4);
    }

    {
        write_to_non_mmio_device(&msg_code, sizeof(fw_arg), core, ARC_RESET_SCRATCH_ADDR + 5 * 4);
    }

    wait_for_non_mmio_flush();
    uint32_t misc = 0;
    read_from_non_mmio_device(&misc, core, ARC_RESET_MISC_CNTL_ADDR, 4);

    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device {}", chip);
        return 1;
    } else {
        misc |= (1 << 16);
        write_to_non_mmio_device(&misc, sizeof(misc), core, ARC_RESET_MISC_CNTL_ADDR);
    }

    if (wait_for_done) {
        uint32_t status = 0xbadbad;
        auto timeout_seconds = std::chrono::seconds(timeout);
        auto start = std::chrono::system_clock::now();
        while (true) {
            if (std::chrono::system_clock::now() - start > timeout_seconds) {
                std::stringstream ss;
                ss << std::hex << msg_code;
                throw std::runtime_error(fmt::format("Timed out after waiting {} seconds for device {} ARC to respond to message 0x{}", timeout, chip, ss.str()));
            }

            uint32_t status = 0;
            read_from_non_mmio_device(&status, core, ARC_RESET_SCRATCH_ADDR + 5 * 4, sizeof(status));
            if ((status & 0xffff) == (msg_code & 0xff)) {
                if (return_3 != nullptr) {
                    read_from_non_mmio_device(return_3, core, ARC_RESET_SCRATCH_ADDR + 3 * 4, sizeof(uint32_t));
                }

                if (return_4 != nullptr) {
                    read_from_non_mmio_device(return_4, core, ARC_RESET_SCRATCH_ADDR + 4 * 4, sizeof(uint32_t));
                }

                exit_code = (status & 0xffff0000) >> 16;
                break;
            } else if (status == MSG_ERROR_REPLY) {
                log_warning(LogSiliconDriver, "On device {}, message code 0x{:x} not recognized by FW", chip, msg_code);
                exit_code = MSG_ERROR_REPLY;
                break;
            }
        }
    }
    return exit_code;
}

void tt_SiliconDevice::write_to_sysmem(const void* mem_ptr, std::uint32_t size,  uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
    write_buffer(mem_ptr, size, addr, channel, src_device_id);
}

void tt_SiliconDevice::read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
    read_buffer(mem_ptr, addr, channel, size, src_device_id);
}

void tt_SiliconDevice::set_membar_flag(const chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores, const uint32_t barrier_value, const uint32_t barrier_addr, const std::string& fallback_tlb) {
    tt_driver_atomics::sfence(); // Ensure that writes before this do not get reordered
    std::unordered_set<tt_xy_pair> cores_synced = {};
    std::vector<uint32_t> barrier_val_vec = {barrier_value};
    for (const auto& core : cores) {
        write_to_device(barrier_val_vec.data(), barrier_val_vec.size() * sizeof(uint32_t), tt_cxy_pair(chip, core), barrier_addr, fallback_tlb);
    }
    tt_driver_atomics::sfence(); // Ensure that all writes in the Host WC buffer are flushed
    while (cores_synced.size() != cores.size()) {
        for(const auto& core : cores) {
            if (cores_synced.find(core) == cores_synced.end()) {
                uint32_t readback_val;
                read_from_device(&readback_val, tt_cxy_pair(chip, core), barrier_addr, sizeof(std::uint32_t), fallback_tlb);
                if (readback_val == barrier_value) {
                    cores_synced.insert(core);
                }
                else {
                    log_info(LogSiliconDriver, "Waiting for core {} to recieve mem bar flag {} in function", core.str(), barrier_value);
                }
            }
        }
    }
    // Ensure that reads or writes after this do not get reordered.
    // Reordering can cause races where data gets transferred before the barrier has returned
    tt_driver_atomics::mfence(); 
}

void tt_SiliconDevice::insert_host_to_device_barrier(const chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores, const uint32_t barrier_addr, const std::string& fallback_tlb) {
    // Ensure that this memory barrier is atomic across processes/threads
    const scoped_lock<named_mutex> lock(*get_mutex(MEM_BARRIER_MUTEX_NAME, this->get_pci_device(chip)->get_device_num()));
    set_membar_flag(chip, cores, tt_MemBarFlag::SET, barrier_addr, fallback_tlb);
    set_membar_flag(chip, cores, tt_MemBarFlag::RESET, barrier_addr, fallback_tlb);
}

void tt_SiliconDevice::init_membars() {
    for(const auto& chip :  target_devices_in_cluster) {
        if (ndesc -> is_chip_mmio_capable(chip)) {
            set_membar_flag(chip, workers_per_chip.at(chip), tt_MemBarFlag::RESET, l1_address_params.tensix_l1_barrier_base, "LARGE_WRITE_TLB");
            set_membar_flag(chip, eth_cores, tt_MemBarFlag::RESET, l1_address_params.eth_l1_barrier_base, "LARGE_WRITE_TLB");
            set_membar_flag(chip, dram_cores, tt_MemBarFlag::RESET, dram_address_params.DRAM_BARRIER_BASE, "LARGE_WRITE_TLB");
        }
    }
}
void tt_SiliconDevice::l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {
    if (ndesc -> is_chip_mmio_capable(chip)) {
        const auto& all_workers = workers_per_chip.at(chip);
        const auto& all_eth = eth_cores;
        if (cores.size()) {
            // Insert barrier on specific cores with L1
            std::unordered_set<tt_xy_pair> workers_to_sync = {};
            std::unordered_set<tt_xy_pair> eth_to_sync = {};
            
            for (const auto& core : cores) {
                if (all_workers.find(core) != all_workers.end()) {
                    workers_to_sync.insert(core);
                } else if (all_eth.find(core) != all_eth.end()) {
                    eth_to_sync.insert(core);
                } else {
                    log_fatal("Can only insert an L1 Memory barrier on Tensix or Ethernet cores.");
                }
            }
            insert_host_to_device_barrier(chip, workers_to_sync, l1_address_params.tensix_l1_barrier_base, fallback_tlb);
            insert_host_to_device_barrier(chip, eth_to_sync, l1_address_params.eth_l1_barrier_base, fallback_tlb);
        } else {
            // Insert barrier on all cores with L1
            insert_host_to_device_barrier(chip, all_workers, l1_address_params.tensix_l1_barrier_base, fallback_tlb);
            insert_host_to_device_barrier(chip, all_eth, l1_address_params.eth_l1_barrier_base, fallback_tlb);
        }
    }
    else {
        wait_for_non_mmio_flush();
    }
}

void tt_SiliconDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {
    if (ndesc -> is_chip_mmio_capable(chip)) {
        if (cores.size()) {
            for(const auto& core : cores) {
                log_assert(dram_cores.find(core) != dram_cores.end(), "Can only insert a DRAM Memory barrier on DRAM cores.");
            }
            insert_host_to_device_barrier(chip, cores, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
        else {
            // Insert Barrier on all DRAM Cores
            insert_host_to_device_barrier(chip, dram_cores, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
    }
    else {
        wait_for_non_mmio_flush();
    }
}

void tt_SiliconDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {
    if (ndesc -> is_chip_mmio_capable(chip)) {
        if (channels.size()) {
            std::unordered_set<tt_xy_pair> dram_cores_to_sync = {};
            for(const auto& chan : channels) {
                dram_cores_to_sync.insert(get_soc_descriptor(chip).get_core_for_dram_channel(chan, 0));
            }
            insert_host_to_device_barrier(chip, dram_cores_to_sync, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
        else {
            // Insert Barrier on all DRAM Cores
            insert_host_to_device_barrier(chip, dram_cores, dram_address_params.DRAM_BARRIER_BASE, fallback_tlb);
        }
    }
    else {
        wait_for_non_mmio_flush();
    }
}

void tt_SiliconDevice::write_to_device(const void *mem_ptr, uint32_t size, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb) {
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(core.chip);
    if(target_is_mmio_capable) {
        if (fallback_tlb == "REG_TLB") {
            write_mmio_device_register(mem_ptr, core, addr, size, fallback_tlb);
        } else {
            write_device_memory(mem_ptr, size, core, addr, fallback_tlb);
        }
    } else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");
        log_assert((get_soc_descriptor(core.chip).ethernet_cores).size() > 0 && get_number_of_chips_in_cluster() > 1, "Cannot issue ethernet writes to a single chip cluster!");
        write_to_non_mmio_device(mem_ptr, size, core, addr);
    }
}

void tt_SiliconDevice::read_mmio_device_register(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    PCIDevice *pci_device = get_pci_device(core.chip);

    const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
    const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, pci_device->get_device_num()));
    log_debug(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);

    auto [mapped_address, tlb_size] = pci_device->set_dynamic_tlb(tlb_index, core, addr, harvested_coord_translation, TLB_DATA::Strict);
    // Align block to 4bytes if needed. 
    auto aligned_buf = tt_4_byte_aligned_buffer(mem_ptr, size);
    pci_device->read_regs(mapped_address, aligned_buf.block_size / sizeof(std::uint32_t), aligned_buf.local_storage);

    if(aligned_buf.input_size != aligned_buf.block_size) {
        // Copy value from aligned buffer to main buffer.
        std::memcpy(mem_ptr, aligned_buf.local_storage, size);
    }
}


void tt_SiliconDevice::write_mmio_device_register(const void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    PCIDevice *pci_device = get_pci_device(core.chip);

    const auto tlb_index = dynamic_tlb_config.at(fallback_tlb);
    const scoped_lock<named_mutex> lock(*get_mutex(fallback_tlb, pci_device->get_device_num()));
    log_debug(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);

    auto [mapped_address, tlb_size] = pci_device->set_dynamic_tlb(tlb_index, core, addr, harvested_coord_translation, TLB_DATA::Strict);
    // Align block to 4bytes if needed. 
    auto aligned_buf = tt_4_byte_aligned_buffer(mem_ptr, size);
    if(aligned_buf.input_size != aligned_buf.block_size) {
        // Copy value from main buffer to aligned buffer
        std::memcpy(aligned_buf.local_storage, mem_ptr, size);
    }
    pci_device->write_regs(mapped_address, aligned_buf.block_size / sizeof(uint32_t), aligned_buf.local_storage);
}

void tt_SiliconDevice::read_from_device(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    bool target_is_mmio_capable = ndesc -> is_chip_mmio_capable(core.chip);
    if (target_is_mmio_capable) {
        if (fallback_tlb == "REG_TLB") {
            read_mmio_device_register(mem_ptr, core, addr, size, fallback_tlb);
        } else {
            read_device_memory(mem_ptr, core, addr, size, fallback_tlb);
        }
    }
    else {
        log_assert(arch_name != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");    // MT: Use only dynamic TLBs and never program static
        log_assert((get_soc_descriptor(core.chip).ethernet_cores).size() > 0 &&  get_number_of_chips_in_cluster() > 1, "Cannot issue ethernet reads from a single chip cluster!");
        read_from_non_mmio_device(mem_ptr, core, addr, size);
    }
}

int tt_SiliconDevice::arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done, uint32_t arg0, uint32_t arg1, int timeout, uint32_t *return_3, uint32_t *return_4) {
    log_assert(arch_name != tt::ARCH::BLACKHOLE, "ARC messages not supported in Blackhole");
    if(ndesc -> is_chip_mmio_capable(logical_device_id)) {
        return pcie_arc_msg(logical_device_id, msg_code, wait_for_done, arg0, arg1, timeout, return_3, return_4);
    }
    else {
        return remote_arc_msg(logical_device_id, msg_code, wait_for_done, arg0, arg1, timeout, return_3, return_4);
    }
}

void tt_SiliconDevice::send_tensix_risc_reset_to_core(const tt_cxy_pair &core, const TensixSoftResetOptions &soft_resets) {
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type) valid;
    write_to_device(&valid_val, sizeof(uint32_t), core, 0xFFB121B0, "REG_TLB");
    tt_driver_atomics::sfence();
}

void tt_SiliconDevice::send_remote_tensix_risc_reset_to_core(const tt_cxy_pair &core, const TensixSoftResetOptions &soft_resets) {
    auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
    uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type) valid;
    write_to_non_mmio_device(&valid_val, sizeof(uint32_t), core, 0xFFB121B0);
    tt_driver_atomics::sfence();
}

int tt_SiliconDevice::set_remote_power_state(const chip_id_t &chip, tt_DevicePowerState device_state) {
    auto mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(chip);
    return remote_arc_msg(chip, get_power_state_arc_msg(mmio_capable_chip_logical, device_state), true, 0, 0, 1, NULL, NULL);
}


void tt_SiliconDevice::enable_remote_ethernet_queue(const chip_id_t& chip, int timeout) {
    uint32_t msg_success = 0x0;
    auto timeout_seconds = std::chrono::seconds(timeout);
    auto start = std::chrono::system_clock::now();
    while (msg_success != 1) {
        if (std::chrono::system_clock::now() - start > timeout_seconds) {
            throw std::runtime_error(fmt::format("Timed out after waiting {} seconds for DRAM to finish training", timeout));
        }
        int msg_rt = remote_arc_msg(chip, 0xaa58, true, 0xFFFF, 0xFFFF, 1, &msg_success, NULL);
        if (msg_rt == MSG_ERROR_REPLY) {
            break;
        }
    }
}


void tt_SiliconDevice::broadcast_tensix_risc_reset_to_cluster(const TensixSoftResetOptions &soft_resets) {
    if(arch_name == tt::ARCH::GRAYSKULL) {
        for (auto &device_it : m_pci_device_map) {
            broadcast_pcie_tensix_risc_reset(device_it.first, soft_resets);
        }
    }
    else {
        auto valid = soft_resets & ALL_TENSIX_SOFT_RESET;
        uint32_t valid_val = (std::underlying_type<TensixSoftResetOptions>::type) valid;
        std::set<chip_id_t> chips_to_exclude = {};
        std::set<uint32_t> rows_to_exclude;
        std::set<uint32_t> columns_to_exclude;
        if (arch_name == tt::ARCH::BLACKHOLE) {
            rows_to_exclude = {0, 1};
            columns_to_exclude = {0, 8, 9};
        } else {
            rows_to_exclude = {0, 6};
            columns_to_exclude = {0, 5};
        }
        std::string fallback_tlb = "LARGE_WRITE_TLB";
        broadcast_write_to_cluster(&valid_val, sizeof(uint32_t), 0xFFB121B0, chips_to_exclude, rows_to_exclude, columns_to_exclude, fallback_tlb);
        // Ensure that reset signal is globally visible
        wait_for_non_mmio_flush();
    }
}

void tt_SiliconDevice::set_power_state(tt_DevicePowerState device_state) {
    // MT Initial BH - ARC messages not supported in Blackhole
    if (arch_name != tt::ARCH::BLACKHOLE) {
        for(auto& chip : target_devices_in_cluster) {
            if(ndesc -> is_chip_mmio_capable(chip)) {
                set_pcie_power_state(device_state);
            } else {
                int exit_code = set_remote_power_state(chip, device_state);
                log_assert(exit_code == 0, "Failed to set power state to {} with exit code: {}", (int)device_state, exit_code);
            }
        }
    }
}

void tt_SiliconDevice::enable_ethernet_queue(int timeout) {
    for (const chip_id_t &chip : target_devices_in_cluster) {
        auto arch = get_soc_descriptor(chip).arch;

         switch (arch) {
            case tt::ARCH::WORMHOLE:
            case tt::ARCH::WORMHOLE_B0: {
                if (ndesc->is_chip_mmio_capable(chip)) {
                    enable_local_ethernet_queue(chip, timeout);
                } else {
                    enable_remote_ethernet_queue(chip, timeout);
                }

                break;
            case tt::ARCH::BLACKHOLE:
                log_assert(false, "Arch BLACKHOLE doesn't support ethernet queues yet");
            }
            default: {
                break;
            }
        }

    }
}

std::set<chip_id_t> tt_SiliconDevice::get_target_remote_device_ids() {
    return target_remote_chips;
}

void tt_SiliconDevice::deassert_resets_and_set_power_state() {
    // Assert tensix resets on all chips in cluster
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);

    // MT Initial BH - ARC messages not supported in Blackhole
    if (arch_name != tt::ARCH::BLACKHOLE) {
        // Send ARC Messages to deassert RISCV resets
        for (auto &device_it : m_pci_device_map){
            arc_msg(device_it.first, 0xaa00 | device_it.second.get()->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(), true, 0, 0);
        }
        if(ndesc != nullptr) {
            for(const chip_id_t& chip : target_devices_in_cluster) {
                if(!ndesc -> is_chip_mmio_capable(chip)) {
                    auto mmio_capable_chip_logical = ndesc->get_closest_mmio_capable_chip(chip);
                    auto pci_device = get_pci_device(mmio_capable_chip_logical);
                    remote_arc_msg(chip, 0xaa00 | pci_device->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(), true, 0x0, 0x0, 1, NULL, NULL);
                }
            }
            enable_ethernet_queue(30);
        }
        // Set power state to busy
        set_power_state(tt_DevicePowerState::BUSY);
    }
}

void tt_SiliconDevice::verify_eth_fw() {
    for(const auto& chip : target_devices_in_cluster) {
        uint32_t fw_version;
        std::vector<uint32_t> fw_versions;
        for (const tt_xy_pair &eth_core : get_soc_descriptor(chip).ethernet_cores) {
            read_from_device(&fw_version, tt_cxy_pair(chip, eth_core), l1_address_params.fw_version_addr, sizeof(uint32_t), "LARGE_READ_TLB");
            fw_versions.push_back(fw_version);
        }
        verify_sw_fw_versions(chip, SW_VERSION, fw_versions);
        eth_fw_version = tt_version(fw_versions.at(0));
    }
}

void tt_SiliconDevice::verify_sw_fw_versions(int device_id, std::uint32_t sw_version, std::vector<std::uint32_t> &fw_versions) {
    tt_version sw(sw_version), fw_first_eth_core(fw_versions.at(0));
    log_info(
        LogSiliconDriver,
        "Software version {}, Ethernet FW version {} (Device {})",
        sw.str(),
        fw_first_eth_core.str(),
        device_id);
    for (std::uint32_t &fw_version : fw_versions) {
        tt_version fw(fw_version);
        log_assert(fw == fw_first_eth_core, "FW versions are not the same across different ethernet cores");
        log_assert(sw.major == fw.major, "SW/FW major version number out of sync");
        log_assert(sw.minor <= fw.minor, "SW version is newer than FW version");
    }

    // Min ERISC FW version required to support ordered writes is 6.4.0
    use_ethernet_ordered_writes &= fw_first_eth_core >= tt_version(6, 4, 0);
    // Min ERISC FW version required to support ethernet broadcast is 6.5.0.
    use_ethernet_broadcast &= fw_first_eth_core >= tt_version(6, 5, 0);
    // Virtual coordinates can be used for broadcast headers if ERISC FW >= 6.8.0 and NOC translation is enabled
    // Temporarily enable this feature for 6.7.241 as well for testing.
    use_virtual_coords_for_eth_broadcast &= (fw_first_eth_core >= tt_version(6, 8, 0) || fw_first_eth_core == tt_version(6, 7, 241)) && translation_tables_en;
}

void tt_SiliconDevice::start_device(const tt_device_params &device_params) {
    if(device_params.init_device) {
        initialize_pcie_devices();
        // MT Initial BH - Ethernet firmware not present in Blackhole
        if(arch_name == tt::ARCH::WORMHOLE || arch_name == tt::ARCH::WORMHOLE_B0) {
            verify_eth_fw();
        }
        deassert_resets_and_set_power_state();
    }
}

void tt_SiliconDevice::close_device() {
    set_power_state(tt_DevicePowerState::LONG_IDLE);
    broadcast_tensix_risc_reset_to_cluster(TENSIX_ASSERT_SOFT_RESET);
}


void tt_SiliconDevice::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
    l1_address_params = l1_address_params_;
}

void tt_SiliconDevice::set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) {
    dram_address_params = dram_address_params_;
}

void tt_SiliconDevice::set_driver_host_address_params(const tt_driver_host_address_params& host_address_params_) {
    host_address_params = host_address_params_;
}

void tt_SiliconDevice::set_driver_eth_interface_params(const tt_driver_eth_interface_params& eth_interface_params_) {
    eth_interface_params = eth_interface_params_;
}

void tt_SiliconDevice::setup_core_to_tlb_map(std::function<std::int32_t(tt_xy_pair)> mapping_function) {
    map_core_to_tlb = mapping_function;
    tlbs_init = true;
}

std::uint32_t tt_SiliconDevice::get_num_dram_channels(std::uint32_t device_id) {
    log_assert(target_devices_in_cluster.find(device_id) != target_devices_in_cluster.end(), "Querying DRAM parameters for a device that does not exist.");
    return get_soc_descriptor(device_id).get_num_dram_channels();
}

std::uint64_t tt_SiliconDevice::get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    log_assert(channel < get_num_dram_channels(device_id), "Querying size for a device channel that does not exist.");
    return  get_soc_descriptor(device_id).dram_bank_size; // Space per channel is identical for now
}

std::uint32_t tt_SiliconDevice::get_num_host_channels(std::uint32_t device_id) {
    log_assert(all_target_mmio_devices.find(device_id) != all_target_mmio_devices.end(), "Querying Host Address parameters for a non-mmio device or a device does not exist.");
    return m_num_host_mem_channels; // Same number of host channels per device for now
}

std::uint32_t tt_SiliconDevice::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    log_assert(host_channel_size.size(), "Host channel size can only be queried after the device has been started.");
    log_assert(channel < get_num_host_channels(device_id), "Querying size for a host channel that does not exist.");
    return host_channel_size.at(device_id).at(channel);
}

std::uint32_t tt_SiliconDevice::get_numa_node_for_pcie_device(std::uint32_t device_id) {
    return get_pci_device(device_id)->get_numa_node();
}

std::uint64_t tt_SiliconDevice::get_pcie_base_addr_from_device(const chip_id_t chip_id) const {
    // TODO: Should probably be lowered to TTDevice.
    tt::ARCH arch = get_soc_descriptor(chip_id).arch;
    if(arch == tt::ARCH::WORMHOLE or arch == tt::ARCH::WORMHOLE_B0) {
        return 0x800000000;
    }
    else if (arch == tt::ARCH::BLACKHOLE) {
        // Enable 4th ATU window.
        return 1ULL << 60;
    }
    else {
        return 0;
    }
}

tt_version tt_SiliconDevice::get_ethernet_fw_version() const {
    log_assert(arch_name == tt::ARCH::WORMHOLE or arch_name == tt::ARCH::WORMHOLE_B0, "Can only get Ethernet FW version for Wormhole architectures.");
    log_assert(eth_fw_version.major != 0xffff and eth_fw_version.minor != 0xff and eth_fw_version.patch != 0xff, "Device must be started before querying Ethernet FW version.");
    return eth_fw_version;
}
