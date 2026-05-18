// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_manager.hpp"

#include <cstring>
#include <vector>

#include "tt-umd/chip_helpers/sysmem_buffer.hpp"

namespace tt::umd {

class MockSysmemManager : public SysmemManager {
public:
    MockSysmemManager() : storage_(kMockSysmemSize, 0) {}

    void write_to_sysmem(uint16_t, const void* src, uint64_t sysmem_dest, uint32_t size) override {
        const uint64_t offset = sysmem_dest % storage_.size();
        std::memcpy(storage_.data() + offset, src, size);
    }

    void read_from_sysmem(uint16_t, void* dest, uint64_t sysmem_src, uint32_t size) override {
        const uint64_t offset = sysmem_src % storage_.size();
        std::memcpy(dest, storage_.data() + offset, size);
    }

    bool pin_or_map_sysmem_to_device() override { return true; }

    void unpin_or_unmap_sysmem() override {}

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(size_t, const bool) override { return nullptr; }

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(void*, size_t, const bool) override { return nullptr; }

protected:
    bool init_sysmem(uint32_t) override { return true; }

private:
    static constexpr size_t kMockSysmemSize = 1ULL << 20;  // 1 MiB scratch space.
    std::vector<uint8_t> storage_;
};

// SysmemManager non-virtual / base virtual method stubs.

std::unique_ptr<SysmemManager> SysmemManager::create(TLBManager*, uint32_t) {
    return std::make_unique<MockSysmemManager>();
}

void SysmemManager::write_to_sysmem(uint16_t, const void*, uint64_t, uint32_t) {}

void SysmemManager::read_from_sysmem(uint16_t, void*, uint64_t, uint32_t) {}

size_t SysmemManager::get_num_host_mem_channels() const { return hugepage_mapping_per_channel.size(); }

HugepageMapping SysmemManager::get_hugepage_mapping(size_t channel) const {
    if (hugepage_mapping_per_channel.size() <= channel) {
        return {nullptr, 0, 0};
    }
    return hugepage_mapping_per_channel[channel];
}

SysmemBuffer::~SysmemBuffer() = default;

uint64_t SysmemManager::get_pcie_base_for_arch(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return 0x800000000;
        case tt::ARCH::BLACKHOLE:
            return 4ULL << 58;
        default:
            return 0;
    }
}

}  // namespace tt::umd
