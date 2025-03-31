/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <boost/interprocess/sync/named_mutex.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace boost::interprocess {
class named_mutex;
}

namespace tt::umd {

enum class MutexType {
    // Used to serialize communication with the ARC.
    ARC_MSG,
    // Used to serialize IO operations which are done directly through TTDevice. This is needed since it goes through a
    // single TLB.
    TT_DEVICE_IO,
    // Used to serialize non-MMIO operations over ethernet.
    NON_MMIO,
    // Used to serialize memory barrier operations.
    MEM_BARRIER,
    // Used for calling CEM tool.
    CREATE_ETH_MAP,
};

class TLBManager;
class TTDevice;

class LockManager {
public:
    LockManager();
    ~LockManager();

    // This set of functions is used to manage mutexes which are system wide and not chip specific.
    static void initialize_mutex(MutexType mutex_type, const bool clear_mutex);
    static void clear_mutex(MutexType mutex_type);
    static std::unique_lock<boost::interprocess::named_mutex> get_mutex(MutexType mutex_type);

    // This set of functions is used to manage mutexes which are chip specific.
    static void initialize_mutex(MutexType mutex_type, TTDevice* tt_device, const bool clear_mutex);
    static void clear_mutex(MutexType mutex_type, TTDevice* tt_device);
    static std::unique_lock<boost::interprocess::named_mutex> get_mutex(MutexType mutex_type, TTDevice* tt_device);

    // This set of functions is used to manage mutexes which are chip specific. This variant accepts custom mutex name.
    static void initialize_mutex(std::string mutex_prefix, TTDevice* tt_device, const bool clear_mutex);
    static void clear_mutex(std::string mutex_prefix, TTDevice* tt_device);
    static std::unique_lock<boost::interprocess::named_mutex> get_mutex(std::string mutex_prefix, TTDevice* tt_device);

    // Commonly used set of mutexes for a chip.
    static void initialize_default_chip_mutexes(TTDevice* tt_device, TLBManager* tlb_manager, const bool clear_mutex);

private:
    static void initialize_mutex_internal(const std::string& mutex_name, const bool clear_mutex);
    static void clear_mutex_internal(const std::string& mutex_name);
    static std::unique_lock<boost::interprocess::named_mutex> get_mutex_internal(const std::string& mutex_name);

    // Const map of mutex names for each of the types listed in the enum.
    static const std::unordered_map<MutexType, std::string> MutexTypeToString;

    // Maps from mutex name to an initialized mutex.
    // Mutex names are made from mutex type name or directly mutex name combined with device number.
    static std::unordered_map<std::string, std::unique_ptr<boost::interprocess::named_mutex>> mutexes;
};

}  // namespace tt::umd
