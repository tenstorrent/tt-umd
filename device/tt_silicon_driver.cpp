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

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#include <wait.h>
#include <errno.h>
#include <linux/pci.h>

#include "device/architecture.h"
#include "device/architecture_implementation.h"
#include "device/tlb.h"
#include "device/tt_arch_types.h"
#include "tt_device.h"
#include "kmdif.h"
#include "ioctl.h"

//#include "epoch_q.h"

#include <algorithm>
#include "yaml-cpp/yaml.h"
#include <filesystem>
#include <string.h>

#include <stdarg.h>
#include "device/cpuset_lib.hpp"
#include "common/logger.hpp"
#include "device/driver_atomics.h"

#define WHT "\e[0;37m"
#define BLK "\e[0;30m"
#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YEL "\e[0;33m"
#define BLU "\e[0;34m"
#define RST "\e[0m"
#define PRINT(...) clr_printf("",__VA_ARGS__)                       // What users should see
// #define LOG(...) if (false) clr_printf("", __VA_ARGS__)   // Mostly debugging
// #define PRINT(...) if (false) clr_printf(BLK, __VA_ARGS__)                       // What users should see
#define WARN(...)  clr_printf(YEL, __VA_ARGS__)                       // Something wrong
#define ERROR(...) clr_printf(RED, __VA_ARGS__)                       // Something very bad

using namespace boost::interprocess;
using namespace tt;
void clr_printf(const char *clr, const char *fmt, ...) {
    va_list args; va_start(args, fmt); printf ("%s", clr); vprintf(fmt, args); printf (RST); va_end(args);
}





// Foward declarations
PCIdevice ttkmd_open(DWORD device_id, bool sharable /* = false */);
int ttkmd_close(struct PCIdevice &device);

void write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len);



template <typename T>
void size_buffer_to_capacity(std::vector<T> &data_buf, std::size_t size_in_bytes) {
    std::size_t target_size = 0;
    if (size_in_bytes > 0) {
        target_size = ((size_in_bytes - 1) / sizeof(T)) + 1;
    }
    data_buf.resize(target_size);
}




tt::ARCH detect_arch(PCIdevice *pci_device) {
    return pci_device->hdev->get_arch();
}

tt::ARCH detect_arch(uint16_t device_id) {
    tt::ARCH arch_name = tt::ARCH::Invalid;
    if (find_device(device_id) == -1) {
        WARN("---- tt_SiliconDevice::detect_arch did not find silcon device_id: %d\n", device_id);
        return arch_name;
    }
    struct PCIdevice pci_device = ttkmd_open((DWORD)device_id, false);

    arch_name = detect_arch(&pci_device);
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
