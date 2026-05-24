// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <functional>
#include <memory>
#include <optional>

class SystemMemoryAllocator;

class SystemMemoryBuffer {
public:
    /**
     * @brief Copies data from a host buffer into the system memory buffer.
     *
     * Copies from the caller-provided source buffer into the
     * system memory buffer at the specified offset.
     *
     * @param src Pointer to the source host memory.
     * @param size Number of bytes to copy.
     * @param offset Byte offset within the system memory buffer to write to.
     */
    void write_to_sysmem(const void* src, size_t size, size_t offset);

    /**
     * @brief Copies data from the system memory buffer into a host buffer.
     *
     * Copies from the system memory buffer at the specified offset
     * into the caller-provided destination buffer.
     *
     * @param dest Pointer to the destination host memory.
     * @param size Number of bytes to copy.
     * @param offset Byte offset within the system memory buffer to read from.
     */
    void read_from_sysmem(void* dest, size_t size, size_t offset);

    /**
     * @brief Retrieves the I/O Virtual Address (IOVA) of the system memory buffer.
     *
     * The IOVA is the address through which the PCIe tile on the device accesses
     * this buffer. When an IOMMU is present, the IOVA is a virtual address that
     * the IOMMU translates to the physical pages backing the buffer, presenting
     * them as contiguous to the device. When no IOMMU is present, the IOVA equals
     * the Physical Address (PA) of the buffer, which must be physically contiguous
     * (typically backed by a hugepage).
     *
     * Some configurations combine both: a hugepage-backed buffer with IOMMU linear
     * remapping for improved performance.
     *
     * @return uint64_t The IOVA of the start of the buffer.
     */
    uint64_t get_iova() const;

    /**
     * @brief Retrieves the NOC address mapped to this buffer, if bound.
     *
     * The NOC address allows all tiles on the device (not just the PCIe tile)
     * to access host memory by routing through the Network on Chip. The PCIe tile
     * can also use this address, performing a NOC loopback where traffic is routed
     * over the NOC back through PCIe to reach system memory.
     *
     * This address is only available after bind_noc_address() has been called.
     *
     * @return std::optional<uint64_t> The NOC address, or std::nullopt if not bound.
     */
    std::optional<uint64_t> get_noc_address() const;

    /**
     * @brief Binds a NOC address to this buffer's IOVA.
     *
     * Programs the hardware address translation (e.g., PCIe iATU) so that all
     * device tiles can access this system memory buffer via the NOC, not just
     * the PCIe tile which can target the IOVA directly.
     *
     * The binding operation is injected at construction by the SystemMemoryAllocator
     * as a callable that holds the necessary device context (e.g., PCIDevice).
     * This ensures the buffer does not directly depend on PCIDevice while still
     * supporting deferred NOC binding.
     *
     * May only be called once. Subsequent calls are no-ops if already bound.
     */
    void bind_noc_address();

    /**
     * @brief Retrieves the size of the system memory buffer in bytes.
     * @return size_t Buffer size.
     */
    size_t get_size() const;

private:
    /**
     * @brief Custom deleter type used for resource cleanup on destruction.
     *
     * Set by the SystemMemoryAllocator. For driver-allocated buffers, the deleter
     * unmaps DMA and frees the memory. For user-mapped buffers, it only unpins the
     * pages.
     */
    using Deleter = std::function<void(void*)>;

    /**
     * @brief Callback type for deferred NOC address binding.
     *
     * Injected by the SystemMemoryAllocator at construction. The callable holds
     * the device context (e.g., PCIDevice) needed to program the hardware address
     * translation. Returns the resulting NOC address.
     */
    using NocBinder = std::function<uint64_t()>;

    /**
     * @brief Constructs a SystemMemoryBuffer.
     *
     * Private constructor — only SystemMemoryAllocator (friend) may create instances.
     *
     * Used for both allocation and mapping paths:
     * - Allocation path: the allocator creates contiguous host memory (via hugepage,
     *   IOMMU, or both), pins it, and maps it for device DMA access. The deleter
     *   performs DMA unmap + munmap on destruction.
     * - Mapping path: the allocator pins a user-provided pointer and creates an
     *   IOMMU mapping. The deleter only unpins the pages on destruction (the user
     *   retains ownership of the underlying memory).
     *
     * @param host_ptr Pointer to the host virtual address of the buffer.
     * @param size Size of the buffer in bytes.
     * @param iova I/O Virtual Address for PCIe tile access.
     * @param deleter Cleanup function invoked on destruction. Encodes the difference
     *        between the allocation path (unmap + free) and mapping path (unpin only).
     * @param noc_binder Optional callback for deferred NOC address binding. If provided,
     *        bind_noc_address() will invoke it to program the hardware translation and
     *        cache the resulting NOC address.
     */
    SystemMemoryBuffer(void* host_ptr, size_t size, uint64_t iova, Deleter deleter, NocBinder noc_binder = nullptr);

    /**
     * @brief Host virtual address of the system memory buffer.
     *
     * Points to the process-local virtual address that maps to the physical pages
     * backing this buffer. Wrapped in a unique_ptr with a custom deleter for
     * automatic cleanup on destruction.
     */
    std::unique_ptr<void, Deleter> system_memory_ptr_;

    size_t size_;
    uint64_t iova_;
    std::optional<uint64_t> noc_addr_;
    NocBinder noc_binder_;

    friend class SystemMemoryAllocator;
};

class SystemMemoryAllocator {
public:
    virtual ~SystemMemoryAllocator() = default;

    /**
     * @brief Allocates a pinned, device-visible host memory buffer.
     *
     * The allocator creates contiguous host memory (via hugepage, IOMMU, or both),
     * pins it, and maps it for device DMA access. The returned buffer owns the
     * memory — on destruction, the custom deleter unmaps DMA and frees the allocation.
     *
     * @param size Requested buffer size in bytes.
     * @param bind_to_noc If true, additionally binds the buffer to a NOC address so
     *        that all device tiles (not just the PCIe tile) can access it.
     * @return std::unique_ptr<SystemMemoryBuffer> An exclusively owned system memory buffer.
     */
    virtual std::unique_ptr<SystemMemoryBuffer> allocate_buffer(size_t size, bool bind_to_noc = false) = 0;

    /**
     * @brief Pins and maps a user-provided host memory buffer for device DMA access.
     *
     * The user retains ownership of the underlying memory. The allocator pins the
     * pages and creates an IOMMU mapping so the device can access it. On destruction,
     * the custom deleter unpins the pages but does not free the memory.
     *
     * Requires an IOMMU to be present — without one, arbitrary user pointers cannot
     * be made device-visible.
     *
     * @param user_ptr Pointer to the user-allocated host memory.
     * @param size Size of the user buffer in bytes.
     * @param bind_to_noc If true, additionally binds the buffer to a NOC address so
     *        that all device tiles (not just the PCIe tile) can access it.
     * @return std::unique_ptr<SystemMemoryBuffer> An exclusively owned handle to the
     *         mapped buffer. Destruction unpins the pages.
     */
    virtual std::unique_ptr<SystemMemoryBuffer> map_user_buffer(
        void* user_ptr, size_t size, bool bind_to_noc = false) = 0;
};
