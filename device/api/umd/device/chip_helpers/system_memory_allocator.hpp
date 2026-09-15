// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include "umd/device/chip_helpers/system_memory_buffer.hpp"
#include "umd/device/types/host_memory.hpp"

namespace tt::umd {

/**
 * Creates or maps device-visible host memory buffers for a single device.
 *
 * An allocator is created per device by whichever layer owns that device's transport, and is paired
 * with its TTDevice by matching get_communication_id(). Buffers can be reached by the device in two
 * ways: through their IOVA, which only the PCIe tile uses, or through a NOC address, which lets every
 * tile reach the buffer once it has been bound.
 */
class SystemMemoryAllocator {
public:
    virtual ~SystemMemoryAllocator() = default;

    /**
     * Allocates a pinned, device-visible host memory buffer.
     *
     * The allocator creates contiguous host memory (via hugepage, IOMMU, or both), pins it, and maps it
     * for device DMA access. The returned buffer owns that memory and frees it on destruction.
     *
     * @param size Requested buffer size in bytes.
     * @param bind_to_noc If true, additionally binds the buffer to a NOC address so that all device tiles,
     * not just the PCIe tile, can reach it.
     * @return An exclusively owned system memory buffer.
     */
    virtual std::unique_ptr<SystemMemoryBuffer> allocate_buffer(size_t size, bool bind_to_noc = false) = 0;

    /**
     * Pins and maps a caller-provided host memory buffer for device DMA access.
     *
     * The caller retains ownership of the underlying memory. The allocator pins the pages and creates an
     * IOMMU mapping so the device can reach it; destroying the buffer unpins the pages but does not free
     * the memory. Requires an IOMMU, since without one arbitrary user pointers cannot be made
     * device-visible.
     *
     * @param user_ptr Pointer to the caller-allocated host memory.
     * @param size Size of the caller's buffer in bytes.
     * @param bind_to_noc If true, additionally binds the buffer to a NOC address.
     * @param device_access What the device is allowed to do with the mapping. READ_ONLY pins the pages so the
     * device cannot write through them, which needs KMD support the concrete allocator reports separately.
     * @return An exclusively owned handle to the mapped buffer.
     */
    virtual std::unique_ptr<SystemMemoryBuffer> map_user_buffer(
        void* user_ptr,
        size_t size,
        bool bind_to_noc = false,
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE) = 0;

    /**
     * Returns the identifier of the device context this allocator pins memory for.
     *
     * An allocator pins for exactly one device — the one whose file descriptor and IOMMU context it holds —
     * and stamps this value into every buffer it produces.
     */
    virtual int get_communication_id() const = 0;

protected:
    /**
     * Creates a buffer, forwarding its arguments to the private SystemMemoryBuffer constructor.
     *
     * Friendship is not inherited, so a concrete allocator cannot reach that constructor itself and
     * builds every buffer through this helper.
     */
    template <typename... Args>
    static std::unique_ptr<SystemMemoryBuffer> create_buffer(Args&&... args) {
        return std::unique_ptr<SystemMemoryBuffer>(new SystemMemoryBuffer(std::forward<Args>(args)...));
    }
};

}  // namespace tt::umd
