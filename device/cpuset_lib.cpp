// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>

#include "cpuset_lib.hpp"
#include "common/logger.hpp"
#include <thread>
#include "device/device_api.h"
#include <filesystem>
namespace tt {

namespace fs = std::filesystem;
namespace cpuset {

// Unrelated to hwloc binding of threads, instead to query cpu affinity to find reasonable number of threads to parallelize over.
int get_allowed_num_threads(){
    unsigned int num_pus_in_system = sysconf(_SC_NPROCESSORS_ONLN);
    unsigned int num_threads = num_pus_in_system;

    cpu_set_t mask;
    if (sched_getaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
        log_warning(LogSiliconDriver, "Could not detect current process cpu id affinity for calculating num_threads, will use default num_threads: {}.", num_threads);
    } else{
        unsigned int visible_pu_count = CPU_COUNT(&mask);
        if (visible_pu_count < num_pus_in_system){
            num_threads = visible_pu_count;
        }
        log_trace(LogSiliconDriver, "Detected (allowed) visible_pu_count: {}, setting num_threads: {}", visible_pu_count, num_threads);
    }

    char const* override_thread_count = std::getenv("TT_BACKEND_COMPILE_THREADS");
    if (override_thread_count != nullptr && std::atoi(override_thread_count) > 0){
        num_threads = std::atoi(override_thread_count);
        log_trace(LogSiliconDriver, "Overriding via env-var to num_threads: {}", num_threads);
    }

    return num_threads;
}


/////////////////////////////////////////////////////////////////////////
// Initialization Functions /////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

// Constructor for singleton class cpu id allocator
tt_cpuset_allocator::tt_cpuset_allocator() {

    m_pid           = getpid();
    m_debug         = std::getenv("TT_BACKEND_CPUSET_ALLOCATOR_DEBUG") ? true : false;
    m_skip_singlify = std::getenv("TT_BACKEND_CPUSET_ALLOCATOR_SKIP_SINGLIFY") ? true : false;

    // Chicken bit to disable this entire feature for debug/comparison.
    bool cpuset_allocator_enable_env = std::getenv("TT_BACKEND_CPUSET_ALLOCATOR_ENABLE") ? true : false;

    auto system_tid = std::this_thread::get_id();
    log_debug(LogSiliconDriver, "Starting tt_cpuset_allocator constructor now for process_id: {} thread_id: {}", m_pid, system_tid);

    m_enable_cpuset_allocator = true;

    // NumaNodes are used for hugepage allocations, separate from CPU Pinning. Required to parse topology first.
    m_enable_cpuset_allocator &= init_topology_init_and_load();
    m_enable_cpuset_allocator &= init_get_number_of_packages();
    m_enable_cpuset_allocator &= init_find_tt_pci_devices_packages_numanodes();

    if (!cpuset_allocator_enable_env){
        m_enable_cpuset_allocator = false;
    }else{

        bool is_cpu_supported      = init_is_cpu_model_supported();

        if (is_cpu_supported){
            m_enable_cpuset_allocator &= init_determine_cpuset_allocations();
            m_enable_cpuset_allocator &= init_populate_physical_mmio_device_id_map();
        }else{
            m_enable_cpuset_allocator = false;
        }

        log_debug(LogSiliconDriver,"Finished tt_cpuset_allocator constructor now with m_enable_cpuset_allocator: {} for process_id: {} thread_id: {} ", m_enable_cpuset_allocator, m_pid, system_tid);
    }
}

// Step 1 : Initialize and perform m_topology detection
bool tt_cpuset_allocator::init_topology_init_and_load(){
    log_debug(LogSiliconDriver,"Inside tt_cpuset_allocator::topology_init_and_load()");

    if (!m_enable_cpuset_allocator){
        return false;
    }

    if (hwloc_topology_init(&m_topology)){
        log_warning(LogSiliconDriver, "Problem initializing topology");
        return false;
    }

    hwloc_topology_set_type_filter(m_topology, HWLOC_OBJ_PCI_DEVICE, HWLOC_TYPE_FILTER_KEEP_ALL); // Need to find PCI devices.

    if (hwloc_topology_load(m_topology)){
        log_warning(LogSiliconDriver, "Problem loading topology");
        return false;
    }

    return true; // Success
}

// Step 2 - Find TT PCI devices in topology by vendor_id to get their PCI bus_id and physical device_id, and package and numamode.
bool tt_cpuset_allocator::init_find_tt_pci_devices_packages_numanodes(){

    if (!m_enable_cpuset_allocator){
        return false;
    }

    log_debug(LogSiliconDriver,"Starting tt_cpuset_allocator::init_find_tt_pci_devices_packages_numanodes()");
    m_num_tt_device_by_pci_device_id_map.clear();

    hwloc_obj_t pci_device_obj = NULL;
    const std::regex tt_device_re("tenstorrent!([0-9]+)");

    while ((pci_device_obj = hwloc_get_next_pcidev(m_topology, pci_device_obj))){

        if (hwloc_obj_type_is_io(pci_device_obj->type) && (pci_device_obj->attr->pcidev.vendor_id == TENSTORRENT_VENDOR_ID)) {

            std::pair<uint16_t, uint16_t> device_id_revision = std::make_pair(pci_device_obj->attr->pcidev.device_id, pci_device_obj->attr->pcidev.revision);
            m_num_tt_device_by_pci_device_id_map[device_id_revision] += 1;

            std::string pci_bus_id_str  = get_pci_bus_id(pci_device_obj);
            std::string pci_device_dir  = "/sys/bus/pci/devices/" + pci_bus_id_str + "/tenstorrent/";
            int physical_device_id = -1;

            log_trace(LogSiliconDriver, "Found TT device with pci_bus_id_str: {} num_devices_by_pci_device_id: {}", pci_bus_id_str, m_num_tt_device_by_pci_device_id_map[device_id_revision]);

            // First, get the physical_device_id of the device.
            if (fs::exists(pci_device_dir)){
                for (const auto &entry : fs::directory_iterator(pci_device_dir)){
                    auto entry_str = entry.path().string();

                    if (std::smatch device_match; std::regex_search(entry_str, device_match, tt_device_re) and (stoi(device_match[1]) >= 0)){
                        physical_device_id = stoi(device_match[1]);
                        m_all_tt_devices.push_back(physical_device_id);
                        log_debug(LogSiliconDriver, "Found physical_device_id: {} from file: {}", physical_device_id, entry_str);
                        break;
                    }
                }

                if (physical_device_id == -1){
                    log_warning(LogSiliconDriver, "Did not find file containing physical_device_id in {}", pci_device_dir);
                    return false;
                }

                m_physical_device_id_to_pci_bus_id_map.insert({physical_device_id, pci_bus_id_str});

                // Next, get the PackageID of the device and update maps.
                auto package_id = get_package_id_from_device(pci_device_obj, physical_device_id);

                if (package_id != -1){
                    m_package_id_to_devices_map.at(package_id).push_back(physical_device_id);
                    m_physical_device_id_to_package_id_map.insert({physical_device_id, package_id});
                }else{
                    log_warning(LogSiliconDriver, "Could not find package_id for TT Device (physical_device_id: {} pci_bus_id: {})", physical_device_id, pci_bus_id_str);
                    return false;
                }

                // Next, get the NumaNode set of the device, and update maps.
                auto numa_nodeset = get_numa_nodeset_from_device(pci_device_obj, physical_device_id);
                m_physical_device_id_to_numa_nodeset_map.insert({physical_device_id, numa_nodeset});

                if (numa_nodeset == 0x0){
                    log_warning(LogSiliconDriver, "Could not find NumaNodeSet for TT Device (physical_device_id: {} pci_bus_id: {})", physical_device_id, pci_bus_id_str);
                    return false;
                }

                m_physical_device_id_to_cpusets_map.insert({physical_device_id, {}}); // Empty vector.
                m_num_cpu_cores_allocated_per_tt_device.insert({physical_device_id, 0});

            }else{
                log_fatal("Could not find {} directory - this is unexpected", pci_device_dir);
                return false;
            }
        }
    }

    if (m_all_tt_devices.size() == 0){
        log_warning(LogSiliconDriver, "Did not find any PCI devices matching Tenstorrent vendor_id 0x{:x}", TENSTORRENT_VENDOR_ID);
        return false;
    }

    log_debug(LogSiliconDriver,"Finshed tt_cpuset_allocator::init_find_tt_pci_devices_packages_numanodes() found {} devices", m_all_tt_devices.size());


    // Sort these 2 vectors of device_ids before we are done, since discovery can be in any order.
    for (auto &p: m_package_id_to_devices_map){
        std::sort(p.second.begin(), p.second.end());
    }

    std::sort(m_all_tt_devices.begin(), m_all_tt_devices.end());

    return true; // Success
}


// Step 3 : Detect the number of packages.
bool tt_cpuset_allocator::init_get_number_of_packages(){

    if (!m_enable_cpuset_allocator){
        return false;
    }

    m_num_packages = hwloc_get_nbobjs_by_type(m_topology, HWLOC_OBJ_PACKAGE);
    log_debug(LogSiliconDriver,"Found {} CPU packages", m_num_packages);

    for (int package_id=0; package_id < m_num_packages; package_id++){
        m_package_id_to_devices_map.insert({package_id, {}}); // Empty vector.
        m_package_id_to_num_l3_per_ccx_map.insert({package_id, 0}); // Initialize to zero.
        m_package_id_to_num_ccx_per_ccd_map.insert({package_id, 0}); // Initialize to zero.
    }

    return m_num_packages > 0; // Success
}

// Step 4 : Return true if all packages are models we want to support. Env-var can be used to ignore this check.
bool tt_cpuset_allocator::init_is_cpu_model_supported(){

    if (!m_enable_cpuset_allocator){
        return false;
    }

    if (m_num_packages == 0){
        log_debug(LogSiliconDriver,"init_is_cpu_model_supported(): Found 0 packages, functions run out of order?");
        return false;
    }

    bool use_any_cpu = std::getenv("TT_BACKEND_CPUSET_ALLOCATOR_SUPPORT_ANY_CPU") ? true : false;

    log_debug(LogSiliconDriver,"Inside tt_cpuset_allocator::check_if_cpu_model_supported()");

    // Supported CPU Models for enabling CPUSET Allocator.  Keep the list small to production machines to start.
    std::vector<std::string> supported_cpu_models = {   "AMD EPYC 7352 24-Core Processor",
                                                        "AMD EPYC 7532 32-Core Processor"};

    // CPU Models that have L3 per CCX and 2 CCX per CCD
    std::vector<std::string> opt_2ccx_per_ccd_cpu_models = {    "AMD EPYC 7352 24-Core Processor",
                                                                "AMD EPYC 7532 32-Core Processor"};

    for (int package_id=0; package_id < m_num_packages; package_id++){

        auto package_obj = hwloc_get_obj_by_type(m_topology, HWLOC_OBJ_PACKAGE, package_id);
        if (m_debug) print_hwloc_object(package_obj, 0, true, true);

        std::string pkg_cpu_model = hwloc_obj_get_info_by_name(package_obj, "CPUModel");

        // First find out if this CPU is supported by CPUSET Allocator at all.
        bool has_supported_cpu = use_any_cpu ? true : false;

        for (auto &supported_cpu_model : supported_cpu_models){
            has_supported_cpu |= (pkg_cpu_model.find(supported_cpu_model) != std::string::npos);
        }

        log_debug(LogSiliconDriver,"Detected package-id: {} has_supported_cpu: {} for CpuModel: {}", package_id, has_supported_cpu, pkg_cpu_model);

        if (!has_supported_cpu){
            return false;
        }

        // Then, determine if the 2CCX-PER-CCD optimization can be enabled for this CPU Model in the package.
        for (auto &opt_cpu_model : opt_2ccx_per_ccd_cpu_models){
            if (pkg_cpu_model.find(opt_cpu_model) != std::string::npos){
                m_package_id_to_num_l3_per_ccx_map.at(package_id) = 1;
                m_package_id_to_num_ccx_per_ccd_map.at(package_id) = 2;
            }
        }
    }

    return true; // Successhwloc
}


// Step 5: Get all target allocation objects (ie. L3Cache if IO thread to be allocated per L3Cache cpuset) for a given socket/package.
bool tt_cpuset_allocator::init_determine_cpuset_allocations(){

    if (!m_enable_cpuset_allocator){
        return false;
    }

    log_debug(LogSiliconDriver,"Inside tt_cpuset_allocator::init_determine_cpuset_allocations()");

    for (int package_id=0; package_id < m_num_packages; package_id++){

        auto num_tt_devices_for_cpu_package = m_package_id_to_devices_map.at(package_id).size();

        if (num_tt_devices_for_cpu_package == 0){
            log_debug(LogSiliconDriver, "init_determine_cpuset_allocations() -- no TT devices for package_id: {}, skipping.", package_id);
            continue;
        }

        log_debug(LogSiliconDriver, "init_determine_cpuset_allocations(). starting to detect allocation slots for package_id: {} ", package_id);

        auto package_obj = hwloc_get_obj_by_type(m_topology, HWLOC_OBJ_PACKAGE, package_id);
        if (m_debug) print_hwloc_object(package_obj, 0, true, true);

        auto num_alloc_slots_in_package = hwloc_get_nbobjs_inside_cpuset_by_type(m_topology, package_obj->cpuset, m_object_per_alloc_slot);
        if (num_alloc_slots_in_package == 0){
            log_warning(LogSiliconDriver, "Could not find any of the alloc objects in package_id: {} for this cpu arc", package_id);
            return false;
        }
        auto num_alloc_slots_per_tt_device = num_alloc_slots_in_package / num_tt_devices_for_cpu_package;

        // Above splits evenly by devices, leaves remainder unused in the example case of 3 devices but 8 slots.
        log_debug(LogSiliconDriver, "init_determine_cpuset_allocations(). package_id: {} num_alloc_slots_in_package: {} num_tt_devices_for_cpu_package: {} num_alloc_slots_per_tt_device: {}",
            package_id, num_alloc_slots_in_package, num_tt_devices_for_cpu_package, num_alloc_slots_per_tt_device);

        int device_idx = 0;

        for (int obj_idx = 0; obj_idx < num_alloc_slots_in_package; obj_idx++){

            auto obj = hwloc_get_obj_below_by_type(m_topology, HWLOC_OBJ_PACKAGE, package_id, m_object_per_alloc_slot, obj_idx);

            if (obj){
                if (m_debug) print_hwloc_object(obj, 1, true);

                auto physical_device_id = m_package_id_to_devices_map.at(package_id).at(device_idx);

                // Hack for maximum number of slots per device.
                // if (m_physical_device_id_to_cpusets_map.at(physical_device_id).size() < 2){
                m_physical_device_id_to_cpusets_map.at(physical_device_id).push_back(obj->cpuset);
                int num_cpus = hwloc_get_nbobjs_inside_cpuset_by_type(m_topology,obj->cpuset,HWLOC_OBJ_CORE);
                m_num_cpu_cores_allocated_per_tt_device.at(physical_device_id) += num_cpus;
                // }

                // We're distributing allocation objects per package across TT devices, so switch to next one.
                if (((obj_idx + 1) % num_alloc_slots_per_tt_device) == 0){
                    device_idx = (device_idx + 1) % num_tt_devices_for_cpu_package; // Loop around if extra slots remain. Assigned to first device for that package.
                }

            }else{
                log_warning(LogSiliconDriver, "init_determine_cpuset_allocations(). Something went wrong looking for cpuset alloc object under package");
                return false;
            }
        }

        log_debug(LogSiliconDriver, "init_determine_cpuset_allocations(). Done detecting allocation slots for package_id: {} ", package_id);
    }


    // Summary for Debug purposes.
    for (auto &physical_device_id : m_all_tt_devices){
        for (size_t device_alloc_idx=0; device_alloc_idx < m_physical_device_id_to_cpusets_map.at(physical_device_id).size(); device_alloc_idx++){
            auto cpuset = m_physical_device_id_to_cpusets_map.at(physical_device_id).at(device_alloc_idx);
            auto pu_ids_vector = get_hwloc_bitmap_vector(cpuset);
            auto num_pu_ids = pu_ids_vector.size();
            auto package_id = m_physical_device_id_to_package_id_map.at(physical_device_id);
            log_debug(LogSiliconDriver, "Done init_determine_cpuset_allocations(). Summary => for mmio physical_device_id: {} package_id: {} device_alloc_idx: {} picked {} PU's {}", physical_device_id, package_id, device_alloc_idx, num_pu_ids, pu_ids_vector);
        }
    }

    return true; // Success

}

// Step 6 - Populate map of logical to physical mmio device map.
bool tt_cpuset_allocator::init_populate_physical_mmio_device_id_map(){

    if (!m_enable_cpuset_allocator){
        return false;
    }

    log_debug(LogSiliconDriver,"Starting tt_cpuset_allocator::populate_physical_mmio_device_id_map()");

    // Get map of logical to physical device ids - FIXME: This is not accurate for some WHB0 clusters.
    std::vector<chip_id_t> available_device_ids = tt_SiliconDevice::detect_available_device_ids();
    m_logical_to_physical_mmio_device_id_map    = tt_SiliconDevice::get_logical_to_physical_mmio_device_id_map(available_device_ids);

    for (auto &d: m_logical_to_physical_mmio_device_id_map){
        auto logical_device_id = d.first;
        auto physical_device_id = d.second;
        log_debug(LogSiliconDriver, "populate_physical_mmio_device_id_map() -- available_devices: {} logical_device_id: {} => physical_device_id: {}", available_device_ids.size(), (int) logical_device_id, (int) physical_device_id);
        m_num_threads_pinned_per_tt_device.insert({physical_device_id, 0});
    }

    return true; // Success
}


/////////////////////////////////////////////////////////////////////////
// Runtime Functions ////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

// Idea  - Something to compare cpuset from Slurm to cpuset picked by this function.
hwloc_cpuset_t tt_cpuset_allocator::allocate_cpu_set_for_thread(chip_id_t physical_device_id, bool skip_singlify){

        // To prevent races on read/modify/write to m_num_threads_pinned_per_tt_device across threads to same device.
        const std::lock_guard<std::mutex> lock(allocate_cpu_id_mutex);

        int num_alloc_slots_for_tt_device   = m_physical_device_id_to_cpusets_map.at(physical_device_id).size();
        int tt_device_alloc_idx             = m_num_threads_pinned_per_tt_device.at(physical_device_id) % num_alloc_slots_for_tt_device;

        // Check if 2CCX-PER-CCD Optimization can be enabled. For AMD EPYC models : There is 1 L3Cache per CCX and 2 CCX per CCD.
        // Better perf to first allocate to unique CCD's if we have enough per device. Expand to other CPU types?
        bool enable_special_case    = true;
        auto package_id             = m_physical_device_id_to_package_id_map.at(physical_device_id);
        auto num_l3_per_ccx         = m_package_id_to_num_l3_per_ccx_map.at(package_id);
        auto num_ccx_per_ccd        = m_package_id_to_num_ccx_per_ccd_map.at(package_id);

        if (enable_special_case && num_l3_per_ccx == 1 && num_ccx_per_ccd == 2 && num_alloc_slots_for_tt_device > num_ccx_per_ccd && m_object_per_alloc_slot == HWLOC_OBJ_L3CACHE){
            int alloc_idx_for_device    = m_num_threads_pinned_per_tt_device.at(physical_device_id);
            int ccx_in_ccd              = (alloc_idx_for_device % num_alloc_slots_for_tt_device) < num_alloc_slots_for_tt_device/num_ccx_per_ccd ? 0 : 1;
            tt_device_alloc_idx         = (ccx_in_ccd + (alloc_idx_for_device * num_ccx_per_ccd)) % num_alloc_slots_for_tt_device;
            log_debug(LogSiliconDriver,"Special L3Cache case physical_device_id: {} alloc_idx_for_device: {} ccx_in_ccd: {} tt_device_alloc_idx: {}", physical_device_id, alloc_idx_for_device, ccx_in_ccd, tt_device_alloc_idx);
        }


        // Get the desired cpuset and prevent migration between different PU's in set by singlifying to single PU.
        hwloc_cpuset_t cpuset = hwloc_bitmap_dup(m_physical_device_id_to_cpusets_map.at(physical_device_id).at(tt_device_alloc_idx));
        if (!m_skip_singlify && !skip_singlify){
            hwloc_bitmap_singlify(cpuset);
        }

        // Debug
        auto tid = std::this_thread::get_id();
        log_debug(LogSiliconDriver,"Allocating for physical_device_id: {} num_alloc_slots: {} num_threads_pinned: {} alloc_idx: {} skip_singlify: {} (pid: {} tid: {}) => {} PU's {}", 
            physical_device_id, num_alloc_slots_for_tt_device, m_num_threads_pinned_per_tt_device.at(physical_device_id), tt_device_alloc_idx, skip_singlify,
            m_pid, tid, hwloc_bitmap_weight(cpuset), get_hwloc_bitmap_vector(cpuset));

        // Increment counter to keep track of number of pinned thread per device, to get unique cpuset per thread.
        m_num_threads_pinned_per_tt_device.at(physical_device_id)++;

        return cpuset;
}

void tt_cpuset_allocator::store_thread_original_cpuset(){

    auto tid = std::this_thread::get_id();
    hwloc_cpuset_t orig_cpuset = hwloc_bitmap_alloc();

    if (hwloc_get_cpubind(m_topology, orig_cpuset, HWLOC_CPUBIND_THREAD)){
        log_warning(LogSiliconDriver,"store_thread_original_cpuset() calling hwloc_get_cpubind() failed with errno: {} (pid: {} tid:{})", strerror(errno), m_pid, tid);
    }else{
        auto orig_cpuset_vector = get_hwloc_bitmap_vector(orig_cpuset);
        log_debug(LogSiliconDriver, "store_thread_original_cpuset() success - got orig cpuset: {} PU's: {} (pid: {} tid: {})", orig_cpuset_vector.size(), orig_cpuset_vector, m_pid, tid);
        m_global_thread_id_to_original_cpuset_map.insert({tid, hwloc_bitmap_dup(orig_cpuset)});
    }
    hwloc_bitmap_free(orig_cpuset);
}



// Given a logical device_id, determine the right cpu_ids associated with it and pin this thread to them.
void tt_cpuset_allocator::bind_thread_cpuset(tt_cluster_description *ndesc, chip_id_t logical_device_id, bool skip_singlify){

    auto tid = std::this_thread::get_id();

    // This needed to be protected by not-empty otherwise arithmetic error.
    if ((!m_global_thread_ids_pinned.empty() && m_global_thread_ids_pinned.count(tid)) || (!m_enable_cpuset_allocator)){
        return;
    }else{

        if (!ndesc->is_chip_mmio_capable(logical_device_id)){
            logical_device_id = ndesc->get_closest_mmio_capable_chip(logical_device_id);
        }

        log_debug(LogSiliconDriver,"bind_thread_cpuset_cpuset() for logical_device_id: {} m_logical_to_physical_mmio_device_id_map.size(): {}", logical_device_id, m_logical_to_physical_mmio_device_id_map.size());

        // If a main thread ID was captured, make sure it is not attempted to be pinned. Only IO API sub threads are expected to be pinned today.
        if (m_stored_main_thread_id && tid == m_main_thread_id){
            log_warning(LogSiliconDriver, "bind_thread_cpuset() - Skipping cpubind for runtime main thread_id: {} to prevent undesired inheritence. Consider moving device IO (ie. push/pop/get) to sub-threads for binding to be supported.", m_main_thread_id);
            return;
        }

        if (m_logical_to_physical_mmio_device_id_map.count(logical_device_id) > 0){

            auto physical_device_id = m_logical_to_physical_mmio_device_id_map.at(logical_device_id);
            auto package_id = m_physical_device_id_to_package_id_map.at(physical_device_id);

            store_thread_original_cpuset(); // Store original cpuset for later unbinding if necessary.

            // Get the cpuset, and attempt to bind thread to it.
            hwloc_cpuset_t cpuset   = allocate_cpu_set_for_thread(physical_device_id, skip_singlify);
            auto cpuset_vector      = get_hwloc_bitmap_vector(cpuset);

            if (hwloc_set_cpubind(m_topology, cpuset, HWLOC_CPUBIND_THREAD | HWLOC_CPUBIND_STRICT )){; // HWLOC_CPUBIND_NOMEMBIND
                log_warning(LogSiliconDriver,"bind_thread_cpuset() binding failed (errno: {}) for physical_device_id: {} on package_id: {} to {} PU's: {} (pid: {} tid: {})",
                    strerror(errno), physical_device_id, package_id, cpuset_vector.size(), cpuset_vector, m_pid, tid);
            }else{
                log_debug(LogSiliconDriver,"bind_thread_cpuset() binding success skip: {} for physical_device_id: {} on package_id: {} to {} PU's: {} (pid: {} tid: {})",
                    skip_singlify, physical_device_id, package_id, cpuset_vector.size(), cpuset_vector, m_pid, tid);
                // Record that this thread is pinned, no need to repeat on subsequent IO API calls.
                m_global_thread_ids_pinned.insert(tid);
                m_global_thread_id_to_physical_device_id_map.insert({tid, physical_device_id});
            }

        }else{
            log_warning(LogSiliconDriver,"Could not find logical_device_id: {} in m_logical_to_physical_mmio_device_id_map. This shouldn't happen.", logical_device_id);
        }
    }
}


// Restore thread's original cpubind. Perhaps could be simplified to not require physical_device_id or previous binding, and just always bind to MACHINE cpuset.
void tt_cpuset_allocator::unbind_thread_cpuset(){

    if (m_enable_cpuset_allocator){
        auto tid = std::this_thread::get_id();

        // Make sure this thread was successfully and previously binded to a cpuset.
        if (!m_global_thread_id_to_original_cpuset_map.count(tid)){
            log_warning(LogSiliconDriver,"unbind_thread_cpuset() called for tid: {} but no original cpuset for this thread found. Previous cpu binding skipped or failed?", tid);
            return;
        }

        if (!m_global_thread_id_to_physical_device_id_map.count(tid)){
            log_warning(LogSiliconDriver,"unbind_thread_cpuset() called for tid: {} but no physical_device_id this thread found. Previous cpu binding skipped or failed?", tid);
            return;
        }

        // Handle the case where something goes wrong during original binding above, don't want to error out.
        auto cpuset             = m_global_thread_id_to_original_cpuset_map.at(tid);
        auto physical_device_id = m_global_thread_id_to_physical_device_id_map.at(tid);
        auto cpuset_vector      = get_hwloc_bitmap_vector(cpuset); // Can tighten this up and remove, it's purely for debug anyways.

        if (hwloc_set_cpubind(m_topology, cpuset, HWLOC_CPUBIND_THREAD)){
            log_warning(LogSiliconDriver,"unbind_thread_cpuset() binding failed (errno: {}) for physical_device_id: {} to original {} PU's: {} (pid: {} tid: {})",
                strerror(errno), physical_device_id, cpuset_vector.size(), cpuset_vector, m_pid, tid);
        }else{
            log_debug(LogSiliconDriver,"unbind_thread_cpuset() binding success for physical_device_id: {} to original {} PU's: {} (pid: {} tid: {})",
                physical_device_id, cpuset_vector.size(), cpuset_vector, m_pid, tid);

            // To prevent races on read/modify/write to m_num_threads_pinned_per_tt_device across threads to same device.
            const std::lock_guard<std::mutex> lock(allocate_cpu_id_mutex);

            // Update book-keeping by removing entry, so this thread can be re-pinned in the future.
            m_num_threads_pinned_per_tt_device.at(physical_device_id)--;
            m_global_thread_ids_pinned.erase(tid);
            m_global_thread_id_to_physical_device_id_map.erase(tid);
        }
    }
}

// Teardown/Cleanup for end of process. Don't do anything if feature disabled. Probably don't even need this if process is going to be ended.
void tt_cpuset_allocator::clear_state(){
    if (m_enable_cpuset_allocator){

        auto tid = std::this_thread::get_id();
        log_debug(LogSiliconDriver,"Clearing state and unbinding entire process' cpuset (pid: {} tid: {}).", m_pid, tid);

        // Reset state variables so that next time the thread can be freshly pinned
        m_global_thread_ids_pinned.clear();
        for (auto &device: m_num_threads_pinned_per_tt_device){
            device.second = 0;
        }

        // Undo previous pinning, by binding to full machine cpuset. Alternatively could have saved and restored orig cpuset per thread.
        auto machine_obj = hwloc_get_obj_by_type(m_topology, HWLOC_OBJ_MACHINE, 0);
        if (hwloc_set_cpubind(m_topology, machine_obj->cpuset, HWLOC_CPUBIND_PROCESS)){
            log_warning(LogSiliconDriver,"clear_state() binding failed (errno: {}) to Machine cpuset (pid: {} tid: {})", strerror(errno), m_pid, tid);
        }
    }
}


// Given a physical device_id, determine the right numa nodes associated with it and attempt to membind a previously allocated memory region to it.
bool tt_cpuset_allocator::bind_area_memory_nodeset(chip_id_t physical_device_id, const void * addr, size_t len){

    auto tid = std::this_thread::get_id();
    log_debug(LogSiliconDriver,"bind_area_memory_nodeset(): Going to attempt memory binding of addr/len to NumaNode for physical_device_id: {} (pid: {} tid: {})", physical_device_id, m_pid, tid);

    if (m_physical_device_id_to_numa_nodeset_map.count(physical_device_id) == 0){
        log_fatal("bind_area_memory_nodeset(): Did not find physical_device_id: {} in numanode_mask map, this is not expected.", physical_device_id);
        return false;
    }

    auto target_nodeset = m_physical_device_id_to_numa_nodeset_map.at(physical_device_id);

    if (target_nodeset != 0){
        if (hwloc_set_area_membind(m_topology, addr, len, target_nodeset, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_BYNODESET | HWLOC_MEMBIND_STRICT | HWLOC_MEMBIND_MIGRATE) ){
            log_warning(LogSiliconDriver,"hwloc_set_area_membind(): failed for physical_device_id: {} on NodeSet: {} with errno: {} (pid: {} tid: {})", 
                physical_device_id, get_hwloc_bitmap_vector(target_nodeset), strerror(errno), m_pid, tid);
            return false;
        }else{
            log_debug(LogSiliconDriver,"hwloc_set_area_membind(): success for physical_device_id: {} on NodeSet: {} (pid: {} tid: {})", physical_device_id, get_hwloc_bitmap_vector(target_nodeset), m_pid, tid);
        }
    }else{
        log_warning(LogSiliconDriver,"bind_area_memory_nodeset(): Unable to determine TT Device to NumaNode mapping for physical_device_id: {}. Skipping membind.", physical_device_id);
        return false;
    }

    return true; // Success
}


// For checking purposes, to make sure main thread is not cpubinded accidentally.
void tt_cpuset_allocator::_set_main_thread_id(){
    m_main_thread_id = std::this_thread::get_id();
    m_stored_main_thread_id = true;
    log_debug(LogSiliconDriver,"Captured main_thread_id: {}", m_main_thread_id);
}

int tt_cpuset_allocator::_get_num_tt_pci_devices() {

    for (auto &d : m_physical_device_id_to_package_id_map) {
        log_trace(LogSiliconDriver, "Found physical_device_id: {} ", d.first);
    }
    return m_physical_device_id_to_package_id_map.size();
}




/////////////////////////////////////////////////////////////////////////
//Helper Functions //////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


std::string tt_cpuset_allocator::get_pci_bus_id(hwloc_obj_t pci_device_obj){

    std::string pci_bus_id_str = "";

    if (hwloc_obj_type_is_io(pci_device_obj->type)) {
        char pci_bus_id[14];
        auto attrs = pci_device_obj->attr->pcidev;
        snprintf(pci_bus_id, sizeof(pci_bus_id), "%04x:%02x:%02x.%01x", attrs.domain, attrs.bus, attrs.dev, attrs.func);
        pci_bus_id_str = (std::string) pci_bus_id;
    }

    return pci_bus_id_str;

}

int tt_cpuset_allocator::get_package_id_from_device(hwloc_obj_t pci_device_obj, chip_id_t physical_device_id){

    auto pci_bus_id_str = m_physical_device_id_to_pci_bus_id_map.at(physical_device_id);

    log_debug(LogSiliconDriver, "Checking TT device (physical_device_id: {} pci_bus_id: {}) to find it's corresponding CPU package", physical_device_id, pci_bus_id_str);

    hwloc_obj_t tmp_obj = hwloc_get_non_io_ancestor_obj(m_topology, pci_device_obj);
    int package_id = -1;

    // Keep going up until package/machine hierarchy is found, in case we don't find it right away.
    while (package_id == -1){

        if ((hwloc_compare_types(tmp_obj->type, HWLOC_OBJ_PACKAGE) == 0) || (hwloc_compare_types(tmp_obj->type, HWLOC_OBJ_MACHINE) == 0)){
            if (tmp_obj->os_index != (unsigned) -1){
                package_id = tmp_obj->os_index;
            }else{
                log_warning(LogSiliconDriver, "Could not find os_index of package or machine object for TT device (physical_device_id: {} pci_bus_id: {})", physical_device_id, pci_bus_id_str);
                break;
            }
        }else{
            if (tmp_obj->parent){
                tmp_obj = tmp_obj->parent;
            }else{
                break;
            }
        }
    }

    if (m_debug) print_hwloc_object(pci_device_obj, 1, true, true);
    if (m_debug) print_hwloc_object(tmp_obj, 1, true, true);

    return package_id;
}

hwloc_nodeset_t tt_cpuset_allocator::get_numa_nodeset_from_device(hwloc_obj_t pci_device_obj, chip_id_t physical_device_id){

    hwloc_nodeset_t nodeset = 0x0;

    // Currently an issue in non-EPYC machines where PCI devices are directly under Machine, and not any NumaNodes.
    // As quick workaround, skip this if there is only single numanode since returning 1 seems fine.
    if (hwloc_get_nbobjs_by_type(m_topology, HWLOC_OBJ_NUMANODE) == 1){
        auto numanode = hwloc_get_obj_by_type(m_topology, HWLOC_OBJ_NUMANODE, 0);
        return numanode->nodeset;
    }

    auto pci_bus_id_str = m_physical_device_id_to_pci_bus_id_map.at(physical_device_id);

    log_debug(LogSiliconDriver, "init_detect_tt_device_numanodes(): Checking TT device (physical_device_id: {} pci_bus_id: {}) to find it's corresponding NumaNode.", physical_device_id, pci_bus_id_str);

    hwloc_obj_t tmp_obj = pci_device_obj->parent;
    while (tmp_obj && !tmp_obj->memory_arity){
        tmp_obj = tmp_obj->parent; /* no memory child, walk up */
    }

    if (tmp_obj && tmp_obj->nodeset){
        log_debug(LogSiliconDriver, "init_detect_tt_device_numanodes(): For TT device (physical_device_id: {} pci_bus_id: {}) found NumaNodeSet: {}", physical_device_id, pci_bus_id_str, get_hwloc_bitmap_vector(tmp_obj->nodeset));
        nodeset = tmp_obj->nodeset;
    }else{
        log_warning(LogSiliconDriver, "init_detect_tt_device_numanodes(): Could not determine NumaNodeSet for TT device (physical_device_id: {} pci_bus_id: {})", physical_device_id, pci_bus_id_str);
    }

    return nodeset;

}

int tt_cpuset_allocator::_get_num_tt_pci_devices_by_pci_device_id(uint16_t device_id, uint16_t revision) {

    std::pair<uint16_t, uint16_t> device_id_revision = std::make_pair(device_id, revision);

    if (m_num_tt_device_by_pci_device_id_map.find(device_id_revision) != m_num_tt_device_by_pci_device_id_map.end()) {
        return m_num_tt_device_by_pci_device_id_map.at(device_id_revision);
    } else {
        log_fatal("Cannot find any TT device with PCI device_id: 0x{:x} and revision: {} in topology.", device_id, revision);
        return 0;
    }
}

/////////////////////////////////////////////////////////////////////////
//Debug Functions ///////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

// Get all PU ids (or numa nodes) in a vector, for legacy/back-compat/debug purposes.
std::vector<int> tt_cpuset_allocator::get_hwloc_bitmap_vector(hwloc_bitmap_t &bitmap){

    std::vector<int> indices;
    int index;
    if (bitmap){
        hwloc_bitmap_foreach_begin(index, bitmap)
            indices.push_back(index);
        hwloc_bitmap_foreach_end();
    }
    return indices;
}

std::vector<int> tt_cpuset_allocator::get_hwloc_cpuset_vector(hwloc_obj_t &obj){
    return get_hwloc_bitmap_vector(obj->cpuset);
}

std::vector<int> tt_cpuset_allocator::get_hwloc_nodeset_vector(hwloc_obj_t &obj){
    return get_hwloc_bitmap_vector(obj->nodeset);
}


// Nicer way to print pu ids as a vector on single line.
void tt_cpuset_allocator::print_hwloc_cpuset(hwloc_obj_t &obj){
    std::cout << " Number: " << hwloc_bitmap_weight(obj->cpuset) << " cpuset_pu_ids: " << get_hwloc_cpuset_vector(obj);
}

void tt_cpuset_allocator::print_hwloc_nodeset(hwloc_obj_t &obj){
    std::cout << " Number: " << hwloc_bitmap_weight(obj->nodeset) << " nodeset node_ids: " << get_hwloc_nodeset_vector(obj);
}

void tt_cpuset_allocator::print_hwloc_object(hwloc_obj_t &obj, int depth, bool verbose, bool show_cpuids){

    char type[32], attr[1024];

    hwloc_obj_type_snprintf(type, sizeof(type), obj, verbose);
    printf("%*s%s", 2*depth, "", type);
    if (obj->os_index != (unsigned) -1)
        printf("#%u", obj->os_index);
    hwloc_obj_attr_snprintf(attr, sizeof(attr), obj, " ", verbose);

    if (*attr)
        printf("(%s)", attr);
    if (show_cpuids && obj->cpuset)
        print_hwloc_cpuset(obj);

    printf("\n");
}


}  // namespace cpuset
}  // namespace tt

