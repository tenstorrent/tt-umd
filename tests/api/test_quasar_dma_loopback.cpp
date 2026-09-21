// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The device reaching host memory, which is the reverse of everything in test_quasar_device_io.cpp.
//
// A host page is pinned so the driver can give the device an address for it, and the device then
// reads and writes that page through the outbound window. This is the UMD counterpart of tt-kmd's
// tools/keraunos_dma_loopback.c and proves the same path: BAR0 inbound, up to the D2D router,
// through the translation table, out of the PCIe port and back to host memory.
//
// SAFETY. Unlike the inbound tests these touch no device register at all -- the only device-side
// address used is the outbound window plus a host address the driver just handed out, so there is
// nothing here that can reach an unmodelled region.

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "umd/device/arch/configs/keraunos_address_map.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt::umd;

namespace {

constexpr CoreCoord ORIGIN{0, 0, tt::CoreType::UNSPECIFIED, tt::CoordSystem::LITERAL};

/** A page of host memory, pinned for the device and unpinned however the test leaves. */
class PinnedPage {
public:
    PinnedPage(tt_device_t* handle, size_t size) : handle_(handle), size_(size) {
        buffer_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        if (buffer_ == MAP_FAILED) {
            buffer_ = nullptr;
            return;
        }
        pin_result_ = tt_pin_pages(handle_, buffer_, size_, 0, &dma_address_, nullptr);
    }

    ~PinnedPage() {
        if (buffer_ == nullptr) {
            return;
        }
        if (pin_result_ == 0) {
            tt_unpin_pages(handle_, buffer_, size_);
        }
        munmap(buffer_, size_);
    }

    PinnedPage(const PinnedPage&) = delete;
    PinnedPage& operator=(const PinnedPage&) = delete;

    bool mapped() const { return buffer_ != nullptr; }

    int pin_result() const { return pin_result_; }

    uint64_t dma_address() const { return dma_address_; }

    uint32_t* words() { return static_cast<uint32_t*>(buffer_); }

private:
    tt_device_t* handle_;
    size_t size_;
    void* buffer_ = nullptr;
    int pin_result_ = -1;
    uint64_t dma_address_ = 0;
};

class QuasarDmaLoopbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int device_id : PCIDevice::enumerate_devices()) {
            auto pci_device = std::make_unique<PCIDevice>(device_id);
            if (pci_device->get_arch() != tt::ARCH::QUASAR) {
                continue;
            }
            handle_ = pci_device->get_tt_device_handle();
            pci_device_ = std::move(pci_device);
            device_ = TTDevice::create(device_id, IODeviceType::PCIe);
            return;
        }
        GTEST_SKIP() << "No Quasar device is bound to the driver.";
    }

    size_t page_size() const { return static_cast<size_t>(sysconf(_SC_PAGESIZE)); }

    std::unique_ptr<PCIDevice> pci_device_;
    tt_device_t* handle_ = nullptr;
    std::unique_ptr<TTDevice> device_;
};

}  // namespace

// The device reading what the host wrote. One word is enough to show the route exists; the point
// is which memory answers, not how much of it.
TEST_F(QuasarDmaLoopbackTest, DeviceReadsWhatTheHostWrote) {
    PinnedPage page(handle_, page_size());
    ASSERT_TRUE(page.mapped()) << "Could not map a host page.";
    ASSERT_EQ(page.pin_result(), 0) << "Could not pin the host page: " << std::strerror(-page.pin_result());

    page.words()[0] = 0xfeed0001;

    const uint64_t device_address = keraunos::host_window_address(page.dma_address());
    uint32_t seen = 0;
    device_->read_from_device(&seen, ORIGIN, device_address, sizeof(seen), NocId::NOC0);

    EXPECT_EQ(seen, 0xfeed0001u);
}

// The reverse: the host reading what the device wrote. A path that only worked one way would pass
// the test above on its own.
TEST_F(QuasarDmaLoopbackTest, HostSeesWhatTheDeviceWrote) {
    PinnedPage page(handle_, page_size());
    ASSERT_TRUE(page.mapped()) << "Could not map a host page.";
    ASSERT_EQ(page.pin_result(), 0) << "Could not pin the host page: " << std::strerror(-page.pin_result());

    page.words()[0] = 0;

    const uint64_t device_address = keraunos::host_window_address(page.dma_address());
    const uint32_t written = 0xfeed0002;
    device_->write_to_device(&written, ORIGIN, device_address, sizeof(written), NocId::NOC0);

    EXPECT_EQ(page.words()[0], 0xfeed0002u);
}

// Distinct values at distinct offsets, written before any is read back, so a route that ignored
// the offset and landed on one word returns the last value written everywhere.
TEST_F(QuasarDmaLoopbackTest, OffsetsWithinThePageAreDistinct) {
    PinnedPage page(handle_, page_size());
    ASSERT_TRUE(page.mapped()) << "Could not map a host page.";
    ASSERT_EQ(page.pin_result(), 0) << "Could not pin the host page: " << std::strerror(-page.pin_result());

    constexpr uint32_t WORDS[] = {0, 1, 16, 255};
    for (uint32_t index : WORDS) {
        page.words()[index] = 0;
    }

    const uint64_t base = keraunos::host_window_address(page.dma_address());
    for (uint32_t index : WORDS) {
        const uint32_t value = 0xda7a0000 | index;
        device_->write_to_device(&value, ORIGIN, base + index * sizeof(uint32_t), sizeof(value), NocId::NOC0);
    }

    for (uint32_t index : WORDS) {
        EXPECT_EQ(page.words()[index], 0xda7a0000u | index) << "word " << index;
    }
}
