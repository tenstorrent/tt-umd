// SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstring>
#include <functional>
#include <memory>
#include <optional>

namespace tt::umd {

class SystemMemoryBuffer {
public:
    using Deleter = std::function<void(void *)>;
    using NocBinder = std::function<uint64_t()>;

    void write_to_sysmem(const void *src, size_t size, size_t offset) {
        std::memcpy(static_cast<uint8_t *>(system_memory_ptr_.get()) + offset, src, size);
    }

    void read_from_sysmem(void *dest, size_t size, size_t offset) const {
        std::memcpy(dest, static_cast<const uint8_t *>(system_memory_ptr_.get()) + offset, size);
    }

    uint64_t get_iova() const { return iova_; }

    std::optional<uint64_t> get_noc_address() const { return noc_addr_; }

    void bind_noc_address() {
        if (!noc_addr_ && noc_binder_) {
            noc_addr_ = noc_binder_();
        }
    }

    size_t get_size() const { return size_; }

private:
    SystemMemoryBuffer(void *host_ptr, size_t size, uint64_t iova, Deleter deleter, NocBinder noc_binder = nullptr) :
        system_memory_ptr_(host_ptr, std::move(deleter)),
        size_(size),
        iova_(iova),
        noc_binder_(std::move(noc_binder)) {}

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

    virtual std::unique_ptr<SystemMemoryBuffer> allocate_buffer(size_t size, bool bind_to_noc = false) = 0;

    virtual std::unique_ptr<SystemMemoryBuffer> map_user_buffer(
        void *user_ptr, size_t size, bool bind_to_noc = false) = 0;

protected:
    static std::unique_ptr<SystemMemoryBuffer> make_buffer(
        void *host_ptr,
        size_t size,
        uint64_t iova,
        SystemMemoryBuffer::Deleter deleter,
        SystemMemoryBuffer::NocBinder noc_binder = nullptr) {
        return std::unique_ptr<SystemMemoryBuffer>(
            new SystemMemoryBuffer(host_ptr, size, iova, std::move(deleter), std::move(noc_binder)));
    }
};

}  // namespace tt::umd
