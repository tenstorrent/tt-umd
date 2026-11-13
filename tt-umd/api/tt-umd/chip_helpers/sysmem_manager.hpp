// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/chip_helpers/system_memory_allocator.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {
class PCIDevice;

class SysmemManager : public SystemMemoryAllocator {
public:
    SysmemManager() = default;
    ~SysmemManager() override = default;

    /**
     * SystemMemoryAllocator surface. These are the Base API Specification names; they forward to the
     * allocate_sysmem_buffer() / map_sysmem_buffer() implementations below.
     */
    std::unique_ptr<SysmemBuffer> allocate_buffer(size_t size, bool bind_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_user_buffer(
        void* user_ptr,
        size_t size,
        bool bind_to_noc = false,
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE) override;

    virtual std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) = 0;

    virtual std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer,
        size_t sysmem_buffer_size,
        const bool map_to_noc = false,
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE) = 0;

    /**
     * @return whether this manager can pin host pages such that the device may read them but not write them,
     * i.e. whether DeviceBufferAccess::READ_ONLY is enforceable for map_sysmem_buffer().
     */
    virtual bool is_read_only_page_pinning_supported() const;

    uint64_t get_pcie_base() const { return pcie_base_; }

    /**
     * Returns the identifier of the device context this manager pins memory for. A manager pins for
     * exactly one device and stamps this value into every buffer it produces.
     */
    int get_communication_id() const override { return communication_id_; }

    static uint64_t get_pcie_base_for_arch(tt::ARCH arch);

protected:
    PCIDevice* pci_device_ = nullptr;
    // TODO: Properly initialize for SimulationSysmemManager.
    uint64_t pcie_base_ = 0;

    // Device this manager pins for. Set by the concrete manager's constructor. -1 when unknown.
    int communication_id_ = -1;
};

}  // namespace tt::umd
