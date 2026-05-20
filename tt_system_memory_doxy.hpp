// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <functional>
#include <memory>
#include <optional>

class SystemMemoryBuffer {
public:
    // memcpy to system memory buffer from src
    void write_to_sysmem(const void* src, size_t size, size_t offset);
    // memcpy to dst from system memory buffer
    void read_from_sysmem(void* dest, size_t size, size_t offset);

    // IO Virtual Address (IOVA) that represents the starting address of
    // the System Memory Buffer from the IOMMU perspective. The device (more precisely
    // the PCIe tile) needs to know it's value for sending data to it. If there is
    // no IOMMMU the IOVA is the same as the Physical Address (PA) of the starting address allocated buffer.
    // Note: The necessity for this stems from the fact that the device needs to see the address space of the host
    // as contiguous. If there is an IOMMU then this is solved via this machinery. If there is no IOMMU, the biggest
    // that the System Memory buffer can is the biggest size of the page that the kernel can give and for this case
    // the hugepage is used. There are also setups where a hugepage is backed by an IOMMU to produce more performant
    // results as the IOMMU becomes a linear remapping mechanism (instead of searching for scattered pages in the kernel
    // address space).
    uint64_t get_iova() const;

    // The NOC address that is (optionally) tied to the IOVA. It's the address via which all other tiles (through the
    // NOC) access host memory.
    std::optional<uint64_t> get_noc_address() const;

    // The NOC address that is (optionally) tied to the IOVA. It's the address via which all other tiles, aside from the
    // PCIe tile, access host memory (the PCIe tile can also access this memory and perform what is called a NOC
    // loopback - where the PCIe tile sends data through this address via the NOC which generates traffic on PCIe
    // communication and subsequently the data ends up in the system memory buffer).
    void bind_noc_address();

    // Get the size of the system memory buffer.
    size_t get_size() const;

private:
    // Custom deleter set by the system memory allocator for the allocated or mapped memory for deallcation or unmaping.
    using Deleter = std::function<void(void*)>;

    // System memory buffer constructor used for the allocation path where the user requests system
    // memory of a specific size and the SysteMemoryAllocator allocates the buffer which has to be contiguous
    // from the device point of view (usually IOMMU, hugepage or IOMMU-backed hugepage).
    SystemMemoryBuffer(uint64_t iova, std::optional<uint64_t> noc_addr) : iova_(iova), noc_addr_(noc_addr) {}

    // System memory buffer constructor used for the mapping path where the user requests to perform
    // a mapping of the user-allocated memory with a specific size. This mapping can be only performed
    // if the IOMMU is present. The user must provide the pointer to the (user) allocated memory and the size of it.
    SystemMemoryBuffer(void* user_ptr, size_t size, uint64_t iova, std::optional<uint64_t> noc_addr, Deleter deleter) :
        system_memory_ptr_(user_ptr, std::move(deleter)), size_(size), iova_(iova), noc_addr_(noc_addr) {}

    // System memory pointer used for memory management of the system memory buffer. Points to the Virtual Address (VA)
    // of the user process which maps to the physical address of the System Memory Buffer.
    std::unique_ptr<void, Deleter> system_memory_ptr_;

    size_t size_;
    uint64_t iova_;
    std::optional<uint64_t> noc_addr_;

    friend class SystemMemoryAllocator;
};

class SystemMemoryAllocator {
public:
    virtual ~SystemMemoryAllocator() = default;

    // Allocates host memory, pins it, makes it device-visible.
    // Returned buffer owns the memory — unpins+frees on destruction.
    virtual std::unique_ptr<SystemMemoryBuffer> allocate_buffer(size_t size, bool bind_to_noc = false) = 0;

    // Pins a user-provided pointer, by mapping makes it device-visible.
    // Does NOT own the memory — only unpins on destruction.
    virtual std::unique_ptr<SystemMemoryBuffer> map_user_buffer(
        void* user_ptr, size_t size, bool bind_to_noc = false) = 0;
};
