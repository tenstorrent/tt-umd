// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/system_memory_allocator.hpp"
#include "umd/device/chip_helpers/system_memory_buffer.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/host_memory.hpp"

using namespace tt::umd;

using ::testing::InSequence;
using ::testing::MockFunction;
using ::testing::Return;

const uint32_t HUGEPAGE_REGION_SIZE = 1ULL << 30;  // 1GB

// ---------------------------------------------------------------------------
// Non-parametrized tests (WH-only / arch-agnostic)
// ---------------------------------------------------------------------------

TEST(ApiSimulationSysmemManager, BasicIOSingleChannel) {
    std::unique_ptr<SimulationSysmemManager> sysmem =
        std::make_unique<SimulationSysmemManager>(1, tt::ARCH::WORMHOLE_B0);

    const HugepageMapping channel_0 = sysmem->get_hugepage_mapping(0);

    EXPECT_EQ(channel_0.mapping_size, HUGEPAGE_REGION_SIZE);

    void* channel_0_mapping = channel_0.mapping;

    std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    sysmem->write_to_sysmem(0, data_write.data(), 0, data_write.size());

    std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
    sysmem->read_from_sysmem(0, data_read.data(), 0, data_read.size());

    EXPECT_EQ(data_write, data_read);

    for (int i = 0; i < data_write.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t*>(channel_0_mapping)[i], data_write[i]);
    }
}

TEST(ApiSimulationSysmemManager, BasicIOMultiChannel) {
    std::unique_ptr<SimulationSysmemManager> sysmem =
        std::make_unique<SimulationSysmemManager>(3, tt::ARCH::WORMHOLE_B0);

    for (int i = 0; i < 3; i++) {
        const HugepageMapping channel = sysmem->get_hugepage_mapping(i);

        EXPECT_EQ(channel.mapping_size, HUGEPAGE_REGION_SIZE);

        void* channel_mapping = channel.mapping;

        std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

        sysmem->write_to_sysmem(i, data_write.data(), 0, data_write.size());

        std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
        sysmem->read_from_sysmem(i, data_read.data(), 0, data_read.size());

        EXPECT_EQ(data_write, data_read);

        for (int j = 0; j < data_write.size(); j++) {
            EXPECT_EQ(static_cast<uint8_t*>(channel_mapping)[j], data_write[j]);
        }
    }
}

TEST(ApiSimulationSysmemManager, TestFourChannels) {
    std::unique_ptr<SimulationSysmemManager> sysmem =
        std::make_unique<SimulationSysmemManager>(4, tt::ARCH::WORMHOLE_B0);

    const HugepageMapping channel_3 = sysmem->get_hugepage_mapping(3);

    EXPECT_EQ(channel_3.mapping_size, HUGEPAGE_CHANNEL_3_SIZE_LIMIT);

    void* channel_3_mapping = channel_3.mapping;

    std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    sysmem->write_to_sysmem(3, data_write.data(), 0, data_write.size());

    std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
    sysmem->read_from_sysmem(3, data_read.data(), 0, data_read.size());

    EXPECT_EQ(data_write, data_read);

    for (int i = 0; i < data_write.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t*>(channel_3_mapping)[i], data_write[i]);
    }
}

namespace {

// SystemMemoryBuffer's constructor is private to allocators. Tests that need a hand-built buffer — with a
// specific deleter, NOC address or binder — go through a minimal allocator of their own rather than
// weakening the encapsulation.
class TestBufferFactory : public SystemMemoryAllocator {
public:
    std::unique_ptr<SystemMemoryBuffer> allocate_buffer(size_t, bool) override { return nullptr; }

    std::unique_ptr<SystemMemoryBuffer> map_user_buffer(void*, size_t, bool, DeviceBufferAccess) override {
        return nullptr;
    }

    int get_communication_id() const override { return 0; }

    using SystemMemoryAllocator::create_buffer;
};

}  // namespace

// A buffer owns its memory through a deleter composed at construction. The deleter must run exactly
// once, and it must receive the page-aligned start of the mapping rather than the caller's VA — that
// is the address the pages were pinned at and the one that has to be released.
TEST(ApiSimulationSysmemManager, BufferDeleterRunsOnceWithAlignedStart) {
    const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));

    std::vector<uint8_t> backing(3 * page_size, 0);
    const uintptr_t raw = reinterpret_cast<uintptr_t>(backing.data());
    void* aligned_start = reinterpret_cast<void*>((raw + page_size - 1) & ~(page_size - 1));
    // Deliberately offset into the page so the buffer has to align downwards.
    void* user_va = static_cast<uint8_t*>(aligned_start) + 64;

    MockFunction<void(void*)> deleter;
    // Checkpoint reached while the buffer is still alive, sequenced before the deleter so that running
    // it any earlier than destruction fails the expectation.
    MockFunction<void()> buffer_still_alive;
    {
        InSequence sequence;
        EXPECT_CALL(buffer_still_alive, Call());
        EXPECT_CALL(deleter, Call(aligned_start));
    }

    {
        auto buffer = TestBufferFactory::create_buffer(
            /*tt_device=*/nullptr,
            user_va,
            128,
            /*device_io_addr=*/0x1000,
            /*communication_id=*/7,
            deleter.AsStdFunction());

        EXPECT_EQ(buffer->get_va(), user_va);
        EXPECT_EQ(buffer->get_size(), 128u);
        buffer_still_alive.Call();
    }
}

// The driver assigns the NOC address at pin time, so bind_noc_address() only confirms what already
// happened. On a bound buffer it is a no-op.
TEST(ApiSimulationSysmemManager, BindNocAddressIsNoOpWhenAlreadyBound) {
    std::vector<uint8_t> backing(4096, 0);

    auto buffer = TestBufferFactory::create_buffer(
        /*tt_device=*/nullptr,
        backing.data(),
        backing.size(),
        /*device_io_addr=*/0x1000,
        /*communication_id=*/2,
        SystemMemoryBuffer::Deleter{},
        std::optional<uint64_t>(0x1234));

    EXPECT_NO_THROW(buffer->bind_noc_address());
    EXPECT_EQ(buffer->get_noc_address().value(), 0x1234u);

    // Idempotent.
    EXPECT_NO_THROW(buffer->bind_noc_address());
    EXPECT_EQ(buffer->get_noc_address().value(), 0x1234u);
}

// An allocator that can bind after the fact injects a binder. bind_noc_address() must run it exactly
// once and cache the result, however often it is called.
TEST(ApiSimulationSysmemManager, BindNocAddressRunsInjectedBinderOnce) {
    std::vector<uint8_t> backing(4096, 0);

    MockFunction<uint64_t()> binder;
    // WillOnce fixes the cardinality at one, so both a binder that never runs and one that runs again
    // on the second bind_noc_address() fail here.
    EXPECT_CALL(binder, Call()).WillOnce(Return(0xDEADBEEF));

    auto buffer = TestBufferFactory::create_buffer(
        /*tt_device=*/nullptr,
        backing.data(),
        backing.size(),
        /*device_io_addr=*/0x1000,
        /*communication_id=*/2,
        SystemMemoryBuffer::Deleter{},
        std::nullopt,
        DeviceBufferAccess::READ_WRITE,
        binder.AsStdFunction());

    // Nothing bound yet, so construction cannot have run the binder.
    EXPECT_FALSE(buffer->get_noc_address().has_value());

    buffer->bind_noc_address();
    ASSERT_TRUE(buffer->get_noc_address().has_value());
    EXPECT_EQ(buffer->get_noc_address().value(), 0xDEADBEEFu);

    // Idempotent: the cached address is returned without running the binder again.
    buffer->bind_noc_address();
    EXPECT_EQ(buffer->get_noc_address().value(), 0xDEADBEEFu);
}

// A buffer that is already bound never runs its binder.
TEST(ApiSimulationSysmemManager, BindNocAddressSkipsBinderWhenAlreadyBound) {
    std::vector<uint8_t> backing(4096, 0);

    MockFunction<uint64_t()> binder;
    EXPECT_CALL(binder, Call()).Times(0);

    auto buffer = TestBufferFactory::create_buffer(
        /*tt_device=*/nullptr,
        backing.data(),
        backing.size(),
        /*device_io_addr=*/0x1000,
        /*communication_id=*/2,
        SystemMemoryBuffer::Deleter{},
        std::optional<uint64_t>(0x1234),
        DeviceBufferAccess::READ_WRITE,
        binder.AsStdFunction());

    buffer->bind_noc_address();
    EXPECT_EQ(buffer->get_noc_address().value(), 0x1234u);
}

// A buffer whose pages were not pinned with NOC access can never be given a NOC address, so asking
// must fail loudly rather than leave the caller believing the buffer is reachable over the NOC.
TEST(ApiSimulationSysmemManager, BindNocAddressThrowsWhenUnbound) {
    std::vector<uint8_t> backing(4096, 0);
    auto buffer = TestBufferFactory::create_buffer(
        /*tt_device=*/nullptr,
        backing.data(),
        backing.size(),
        /*device_io_addr=*/0x1000,
        /*communication_id=*/2,
        SystemMemoryBuffer::Deleter{});

    EXPECT_FALSE(buffer->get_noc_address().has_value());
    EXPECT_THROW(buffer->bind_noc_address(), std::exception);
    EXPECT_FALSE(buffer->get_noc_address().has_value());
}

// A buffer given no deleter must still destruct cleanly. unique_ptr with an empty std::function
// deleter would otherwise throw bad_function_call.
TEST(ApiSimulationSysmemManager, BufferWithoutDeleterDestructsCleanly) {
    std::vector<uint8_t> backing(4096, 0);
    EXPECT_NO_THROW({
        auto buffer = TestBufferFactory::create_buffer(
            /*tt_device=*/nullptr,
            backing.data(),
            backing.size(),
            /*device_io_addr=*/0x2000,
            /*communication_id=*/1,
            SystemMemoryBuffer::Deleter{});
    });
}

// The deleter is run while the buffer is being destroyed, so an exception out of it would leave the
// destructor throwing and terminate the process. Cleanup failures have to be swallowed and logged.
TEST(ApiSimulationSysmemManager, ThrowingBufferDeleterDoesNotEscapeDestruction) {
    std::vector<uint8_t> backing(4096, 0);
    EXPECT_NO_THROW({
        auto buffer = TestBufferFactory::create_buffer(
            /*tt_device=*/nullptr,
            backing.data(),
            backing.size(),
            /*device_io_addr=*/0x3000,
            /*communication_id=*/1,
            SystemMemoryBuffer::Deleter{[](void*) { throw std::runtime_error("deleter failed"); }});
    });
}

namespace {

// Resident set size in KiB, or 0 if it could not be read.
size_t read_rss_kib() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            return std::stoull(line.substr(line.find_first_of("0123456789")));
        }
    }
    return 0;
}

}  // namespace

// allocate_sysmem_buffer() mmaps the backing memory, so the buffer has to free it on destruction.
// While the manager held the mapping in owned_allocations_ instead, nothing was released until the
// manager itself went away and every allocation leaked for as long as it lived.
TEST(ApiSimulationSysmemManager, AllocatedBufferFreesBackingMemory) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, tt::ARCH::WORMHOLE_B0);

    const size_t buf_size = 16ULL << 20;
    const size_t buf_size_kib = buf_size >> 10;
    const int iterations = 8;

    // Warm up so one-time allocations do not land inside the measurement.
    { std::unique_ptr<SystemMemoryBuffer> warmup = sysmem->allocate_sysmem_buffer(buf_size); }

    const size_t rss_before = read_rss_kib();
    if (rss_before == 0) {
        GTEST_SKIP() << "Could not read VmRSS from /proc/self/status.";
    }

    const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    for (int i = 0; i < iterations; i++) {
        std::unique_ptr<SystemMemoryBuffer> buffer = sysmem->allocate_sysmem_buffer(buf_size);
        ASSERT_NE(buffer, nullptr);
        // Touch one byte per page so the whole mapping is resident. Touching only the first page would
        // leave a leak invisible: RSS would grow by a page per iteration rather than by the buffer size.
        uint8_t* bytes = static_cast<uint8_t*>(buffer->get_va());
        for (size_t offset = 0; offset < buf_size; offset += page_size) {
            bytes[offset] = static_cast<uint8_t>(i);
        }
    }

    const size_t rss_after = read_rss_kib();
    const size_t growth_kib = rss_after > rss_before ? rss_after - rss_before : 0;

    // Leaking would retain every iteration's mapping. Allow one buffer of slack for allocator noise.
    EXPECT_LT(growth_kib, buf_size_kib) << "RSS grew by " << growth_kib << " KiB across " << iterations
                                        << " allocate/destroy cycles of " << buf_size_kib << " KiB each";
}

// The buffer owns its mapping outright, so it stays usable after the manager that handed it over is
// gone. While the manager owned the mapping, its destructor munmapped memory a live buffer was still
// pointing at.
TEST(ApiSimulationSysmemManager, AllocatedBufferOutlivesItsManager) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, tt::ARCH::WORMHOLE_B0);

    const size_t buf_size = 4096;
    std::unique_ptr<SystemMemoryBuffer> buffer = sysmem->allocate_sysmem_buffer(buf_size);
    ASSERT_NE(buffer, nullptr);

    uint8_t* bytes = static_cast<uint8_t*>(buffer->get_va());
    std::memset(bytes, 0xA5, buf_size);

    sysmem.reset();

    // Reading and writing here would fault if the manager had unmapped the buffer's pages.
    for (size_t offset = 0; offset < buf_size; offset++) {
        ASSERT_EQ(bytes[offset], 0xA5) << "at offset " << offset;
    }
    std::memset(bytes, 0x5A, buf_size);
    EXPECT_EQ(bytes[buf_size - 1], 0x5A);
}

// ---------------------------------------------------------------------------
// Mapped buffer tests — parametrized over ARCH (WH and BH).
//
// The mapped-buffer registry keys buffers by their absolute device IO address
// (pcie_base + arena_offset).  Tests use write_mapped_buffer /
// read_mapped_buffer directly, which is the same path taken by
// TTSimTTDevice::pci_dma_{write,read}_bytes after converting the craq-sim
// offset to the absolute key.
// ---------------------------------------------------------------------------

class ApiSimulationSysmemManagerByArch : public ::testing::TestWithParam<tt::ARCH> {};

INSTANTIATE_TEST_SUITE_P(
    Archs,
    ApiSimulationSysmemManagerByArch,
    ::testing::Values(tt::ARCH::WORMHOLE_B0, tt::ARCH::BLACKHOLE),
    [](const ::testing::TestParamInfo<tt::ARCH>& info) {
        switch (info.param) {
            case tt::ARCH::WORMHOLE_B0:
                return "WORMHOLE_B0";
            case tt::ARCH::BLACKHOLE:
                return "BLACKHOLE";
            default:
                return "UNKNOWN";
        }
    });

TEST_P(ApiSimulationSysmemManagerByArch, AllocateSysmemBufferReturnsValidBuffer) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    const size_t buffer_size = 4096;
    auto buffer = sysmem->allocate_sysmem_buffer(buffer_size);
    ASSERT_NE(buffer, nullptr);
    EXPECT_NE(buffer->get_va(), nullptr);
    EXPECT_GE(buffer->get_size(), buffer_size);
    // device_io_addr should be non-zero (pcie_base + offset).
    EXPECT_GT(buffer->get_iova(), 0u);
}

TEST_P(ApiSimulationSysmemManagerByArch, MapExternalBufferCreatesEntry) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    // Allocate a user-managed buffer and map it through the sysmem manager.
    const size_t buffer_size = 8192;
    std::vector<uint8_t> external_buf(buffer_size, 0);
    auto buffer = sysmem->map_sysmem_buffer(external_buf.data(), buffer_size);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->get_va(), external_buf.data());
    EXPECT_GT(buffer->get_iova(), 0u);
}

// Verify that write_mapped_buffer / read_mapped_buffer correctly address the
// backing allocation via the registry key (absolute device_io_addr).  This
// exercises the same path as TTSimTTDevice::pci_dma_{write,read}_bytes after
// it adds pcie_base to the craq-sim offset.
TEST_P(ApiSimulationSysmemManagerByArch, WriteReadThroughMappedBuffer) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    const size_t buffer_size = 4096;
    auto buffer = sysmem->allocate_sysmem_buffer(buffer_size);
    ASSERT_NE(buffer, nullptr);

    const uint64_t device_addr = buffer->get_iova();
    EXPECT_GT(device_addr, 0u);

    // Write a pattern via write_mapped_buffer — this mirrors the
    // pci_dma_write_bytes path (TTSimTTDevice adds pcie_base to the craq-sim
    // offset before calling write_mapped_buffer).
    std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    bool written = sysmem->write_mapped_buffer(device_addr, pattern.data(), pattern.size());
    EXPECT_TRUE(written);

    // Read it back via read_mapped_buffer.
    std::vector<uint8_t> readback(pattern.size(), 0);
    bool read_ok = sysmem->read_mapped_buffer(device_addr, readback.data(), readback.size());
    EXPECT_TRUE(read_ok);
    EXPECT_EQ(pattern, readback);

    // Confirm the data is also visible through the buffer VA.
    EXPECT_EQ(0, std::memcmp(buffer->get_va(), pattern.data(), pattern.size()));
}

// SystemMemoryBuffer::write_to_sysmem / read_from_sysmem are pure host-side copies against the
// buffer VA, bounded by the user-requested size.
TEST_P(ApiSimulationSysmemManagerByArch, HostCopyThroughBufferRoundTrips) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    const size_t buffer_size = 4096;
    auto buffer = sysmem->allocate_sysmem_buffer(buffer_size);
    ASSERT_NE(buffer, nullptr);

    const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

    // Round-trip at the start of the buffer.
    buffer->write_to_sysmem(pattern.data(), pattern.size(), 0);
    std::vector<uint8_t> readback(pattern.size(), 0);
    buffer->read_from_sysmem(readback.data(), readback.size(), 0);
    EXPECT_EQ(pattern, readback);
    EXPECT_EQ(0, std::memcmp(buffer->get_va(), pattern.data(), pattern.size()));

    // Round-trip at a non-zero offset, leaving the earlier bytes untouched.
    const size_t offset = 512;
    buffer->write_to_sysmem(pattern.data(), pattern.size(), offset);
    std::fill(readback.begin(), readback.end(), 0);
    buffer->read_from_sysmem(readback.data(), readback.size(), offset);
    EXPECT_EQ(pattern, readback);
    EXPECT_EQ(0, std::memcmp(static_cast<uint8_t*>(buffer->get_va()) + offset, pattern.data(), pattern.size()));

    // The last byte of the buffer is addressable.
    const uint8_t sentinel = 0xA5;
    buffer->write_to_sysmem(&sentinel, sizeof(sentinel), buffer_size - 1);
    uint8_t sentinel_readback = 0;
    buffer->read_from_sysmem(&sentinel_readback, sizeof(sentinel_readback), buffer_size - 1);
    EXPECT_EQ(sentinel, sentinel_readback);
}

// A manager must be usable purely through the SystemMemoryAllocator interface, since that is what the
// Base API hands to upper layers.
TEST_P(ApiSimulationSysmemManagerByArch, UsableThroughTheAllocatorInterface) {
    const uint32_t chip_id = 5;
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam(), chip_id);
    SystemMemoryAllocator* allocator = sysmem.get();

    EXPECT_EQ(allocator->get_communication_id(), static_cast<int>(chip_id));

    auto allocated = allocator->allocate_buffer(4096);
    ASSERT_NE(allocated, nullptr);
    EXPECT_NE(allocated->get_va(), nullptr);
    EXPECT_EQ(allocated->get_communication_id(), static_cast<int>(chip_id));
    EXPECT_FALSE(allocated->get_noc_address().has_value());

    std::vector<uint8_t> external(8192, 0);
    auto mapped = allocator->map_user_buffer(external.data(), external.size());
    ASSERT_NE(mapped, nullptr);
    EXPECT_EQ(mapped->get_va(), external.data());
    // Mappings are read-write unless the caller asks otherwise.
    EXPECT_EQ(mapped->get_device_access(), DeviceBufferAccess::READ_WRITE);

    // The interface carries device access, so a read-only mapping is reachable without dropping to the
    // concrete manager.
    std::vector<uint8_t> read_only(4096, 0);
    auto pinned_read_only = allocator->map_user_buffer(
        read_only.data(), read_only.size(), /*bind_to_noc=*/false, DeviceBufferAccess::READ_ONLY);
    ASSERT_NE(pinned_read_only, nullptr);
    EXPECT_EQ(pinned_read_only->get_device_access(), DeviceBufferAccess::READ_ONLY);

    // bind_to_noc reaches the same path as the concrete map_to_noc flag.
    auto bound = allocator->allocate_buffer(4096, /*bind_to_noc=*/true);
    ASSERT_NE(bound, nullptr);
    EXPECT_TRUE(bound->get_noc_address().has_value());
}

// The manager stamps its communication id into every buffer it produces, so a caller can confirm a
// buffer belongs to the device it is about to be used with.
TEST_P(ApiSimulationSysmemManagerByArch, BuffersCarryTheManagerCommunicationId) {
    const uint32_t chip_id = 3;
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam(), chip_id);
    EXPECT_EQ(sysmem->get_communication_id(), static_cast<int>(chip_id));

    auto allocated = sysmem->allocate_sysmem_buffer(4096);
    ASSERT_NE(allocated, nullptr);
    EXPECT_EQ(allocated->get_communication_id(), sysmem->get_communication_id());

    std::vector<uint8_t> external_buf(8192, 0);
    auto mapped = sysmem->map_sysmem_buffer(external_buf.data(), external_buf.size());
    ASSERT_NE(mapped, nullptr);
    EXPECT_EQ(mapped->get_communication_id(), sysmem->get_communication_id());

    // A manager for a different chip stamps a different id.
    auto other_sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam(), chip_id + 1);
    auto other_buffer = other_sysmem->allocate_sysmem_buffer(4096);
    ASSERT_NE(other_buffer, nullptr);
    EXPECT_NE(other_buffer->get_communication_id(), allocated->get_communication_id());
}

TEST_P(ApiSimulationSysmemManagerByArch, HostCopyOutOfBoundsThrows) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    const size_t buffer_size = 4096;
    auto buffer = sysmem->allocate_sysmem_buffer(buffer_size);
    ASSERT_NE(buffer, nullptr);

    std::vector<uint8_t> scratch(16, 0);

    // Offset past the end.
    EXPECT_THROW(buffer->write_to_sysmem(scratch.data(), 1, buffer_size), std::exception);
    EXPECT_THROW(buffer->read_from_sysmem(scratch.data(), 1, buffer_size), std::exception);

    // Offset in range, but the range runs off the end. This is the case the read path
    // used to miss.
    EXPECT_THROW(buffer->write_to_sysmem(scratch.data(), 2, buffer_size - 1), std::exception);
    EXPECT_THROW(buffer->read_from_sysmem(scratch.data(), 2, buffer_size - 1), std::exception);

    // Size larger than the whole buffer.
    EXPECT_THROW(buffer->write_to_sysmem(scratch.data(), buffer_size + 1, 0), std::exception);
    EXPECT_THROW(buffer->read_from_sysmem(scratch.data(), buffer_size + 1, 0), std::exception);
}

// get_mapped_host_ptr resolves a device_io_addr to the in-place host pointer (zero-copy),
// at the correct within-buffer offset, and returns nullptr for an unmapped address. This is
// the accessor emule's NOC resolver uses to reach host sysmem in place.
TEST_P(ApiSimulationSysmemManagerByArch, GetMappedHostPtrResolvesOffsetAndMiss) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    const size_t buffer_size = 4096;
    auto buffer = sysmem->allocate_sysmem_buffer(buffer_size);
    ASSERT_NE(buffer, nullptr);
    const uint64_t device_addr = buffer->get_iova();
    ASSERT_GT(device_addr, 0u);

    // Base address resolves to the buffer VA; an interior address to VA + offset.
    void* base_ptr = sysmem->get_mapped_host_ptr(device_addr);
    ASSERT_NE(base_ptr, nullptr);
    EXPECT_EQ(base_ptr, buffer->get_va());

    const uint64_t kOffset = 128;
    void* mid_ptr = sysmem->get_mapped_host_ptr(device_addr + kOffset);
    ASSERT_NE(mid_ptr, nullptr);
    EXPECT_EQ(mid_ptr, static_cast<uint8_t*>(buffer->get_va()) + kOffset);

    // The pointer aliases the backing (zero-copy): a write through it is visible via read_mapped_buffer.
    std::vector<uint8_t> pattern = {0x11, 0x22, 0x33, 0x44};
    std::memcpy(base_ptr, pattern.data(), pattern.size());
    std::vector<uint8_t> readback(pattern.size(), 0);
    ASSERT_TRUE(sysmem->read_mapped_buffer(device_addr, readback.data(), readback.size()));
    EXPECT_EQ(pattern, readback);

    // A miss (address in no mapped buffer) returns nullptr — derived from the buffer's own bounds:
    // one byte below its start, and one past its end.
    EXPECT_EQ(sysmem->get_mapped_host_ptr(device_addr - 1), nullptr);
    EXPECT_EQ(sysmem->get_mapped_host_ptr(device_addr + buffer_size), nullptr);
}

TEST_P(ApiSimulationSysmemManagerByArch, MultipleMappedBuffersAreIndependent) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    auto buf_a = sysmem->allocate_sysmem_buffer(4096);
    auto buf_b = sysmem->allocate_sysmem_buffer(4096);
    ASSERT_NE(buf_a, nullptr);
    ASSERT_NE(buf_b, nullptr);

    // Buffers should have different device IO addresses.
    EXPECT_NE(buf_a->get_iova(), buf_b->get_iova());
    // And different virtual addresses.
    EXPECT_NE(buf_a->get_va(), buf_b->get_va());
}

TEST_P(ApiSimulationSysmemManagerByArch, DestroyedBufferUnmapsCleanly) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    {
        auto buffer = sysmem->allocate_sysmem_buffer(4096);
        ASSERT_NE(buffer, nullptr);
        // buffer goes out of scope here, triggering unmap callback.
    }

    // Allocating another buffer should succeed (no leaked state).
    auto buffer2 = sysmem->allocate_sysmem_buffer(4096);
    EXPECT_NE(buffer2, nullptr);
}

// Verify that concurrent allocations do not crash (tests the mutex on owned_allocations_).
TEST_P(ApiSimulationSysmemManagerByArch, ConcurrentAllocateDoesNotCrash) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    constexpr int kThreads = 4;
    constexpr size_t kBufSize = 4096;
    std::vector<std::unique_ptr<SystemMemoryBuffer>> results(kThreads);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&sysmem, &results, i]() { results[i] = sysmem->allocate_sysmem_buffer(kBufSize); });
    }
    for (auto& t : threads) {
        t.join();
    }

    // Every allocation should have succeeded with a unique device address.
    for (int i = 0; i < kThreads; ++i) {
        ASSERT_NE(results[i], nullptr) << "Thread " << i << " got null buffer";
        EXPECT_NE(results[i]->get_va(), nullptr);
    }
    for (int i = 0; i < kThreads; ++i) {
        for (int j = i + 1; j < kThreads; ++j) {
            EXPECT_NE(results[i]->get_iova(), results[j]->get_iova())
                << "Buffers from threads " << i << " and " << j << " have same device addr";
        }
    }
}

// Destroy the SimulationSysmemManager while a SystemMemoryBuffer still exists.
// The buffer's unmap callback must not crash (weak_ptr / captured-reference safety).
TEST_P(ApiSimulationSysmemManagerByArch, ManagerDestroyedBeforeBuffer) {
    std::unique_ptr<SystemMemoryBuffer> buffer;
    {
        auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());
        buffer = sysmem->allocate_sysmem_buffer(4096);
        ASSERT_NE(buffer, nullptr);
        // sysmem is destroyed here.
    }
    // buffer goes out of scope — its unmap callback fires after the manager is gone.
    // This must not crash.
    buffer.reset();
    SUCCEED();
}
