
#include <gtest/gtest.h>
#include "fmt/xchar.h"

#include <algorithm>
#include <vector>

#include "device/pcie/pci_device.hpp"
#include "device/ioctl.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>


TEST(PcieDeviceTest, Numa) {
    std::vector<int> nodes;

    for (auto device_id : PCIDevice::enumerate_devices()) {
        PCIDevice device(device_id);
        nodes.push_back(device.get_numa_node());
    }

    // Acceptable outcomes:
    // 1. all of them are -1 (not a NUMA system)
    // 2. all of them are >= 0 (NUMA system)
    // 3. empty vector (no devices enumerated)

    if (!nodes.empty()) {
        bool all_negative_one = std::all_of(nodes.begin(), nodes.end(), [](int node) { return node == -1; });
        bool all_non_negative = std::all_of(nodes.begin(), nodes.end(), [](int node) { return node >= 0; });

        EXPECT_TRUE(all_negative_one || all_non_negative)
            << "NUMA nodes should either all be -1 (non-NUMA system) or all be non-negative (NUMA system)"
            << " but got: " << fmt::format("{}", fmt::join(nodes, ", "));
    } else {
        SUCCEED() << "No PCIe devices were enumerated";
    }
}


TEST(PcieDeviceTest, DMA) {
    size_t size = 1 << 30;
    void *mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mapping == MAP_FAILED) {
        FAIL() << "mmap failed: ";
    }


    tenstorrent_pin_pages pin_pages{};
    pin_pages.in.output_size_bytes = sizeof(pin_pages.out);
    pin_pages.in.flags = 0;
    pin_pages.in.virtual_address = reinterpret_cast<std::uintptr_t>(mapping);
    pin_pages.in.size = size;

    int fd = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
    if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages) == -1) {
        FAIL() << "ioctl failed: " << strerror(errno);
    }

    fmt::print("DMA Address: {:x}\n", pin_pages.out.physical_address);

    size_t dmabuf_size = 0x1000;
    tenstorrent_allocate_dma_buf allocate_dma_buf{};
    allocate_dma_buf.in.buf_index = 0;
    allocate_dma_buf.in.requested_size = dmabuf_size;
    if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &allocate_dma_buf) == -1) {
        FAIL() << "ioctl failed: " << strerror(errno);
    }

    fmt::print("DMA Buffer Physical Address: {:x}\n", allocate_dma_buf.out.physical_address);

    void *dmabuf = mmap(nullptr, dmabuf_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, allocate_dma_buf.out.mapping_offset);

    if (dmabuf == MAP_FAILED) {
        FAIL() << "mmap failed: ";
    }

    std::memset(dmabuf, 0x55, dmabuf_size);
    std::memset(mapping, 0xaa, size);
}