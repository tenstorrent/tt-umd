// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <cstring>
#include <functional>

#include "system_memory.hpp"
#include "tt_device.hpp"
#include "tt_device_model.hpp"

namespace tt::umd {

// --- Mock Models ---

class MockPcieModel : public TTDeviceModel {
    int device_id_;

public:
    explicit MockPcieModel(int device_id) : device_id_(device_id) {}

    std::unique_ptr<DeviceInfo> create_device_info() override;
    std::unique_ptr<DeviceProtocol> create_device_protocol() override;
    std::unique_ptr<DeviceFirmware> create_device_firmware() override;
    std::unique_ptr<ArchitectureImplementation> create_architecture_impl() override;
    std::unique_ptr<HangDetector> create_hang_detector() override;
    std::unique_ptr<FirmwareTelemetryReader> create_firmware_telemetry_reader() override;
    std::unique_ptr<FirmwareInfoProvider> create_firmware_info_provider() override;
};

class MockJtagModel : public TTDeviceModel {
    int device_id_;

public:
    explicit MockJtagModel(int device_id) : device_id_(device_id) {}

    std::unique_ptr<DeviceInfo> create_device_info() override;
    std::unique_ptr<DeviceProtocol> create_device_protocol() override;
    std::unique_ptr<DeviceFirmware> create_device_firmware() override;
    std::unique_ptr<ArchitectureImplementation> create_architecture_impl() override;
};

// --- Mock System Memory ---

class MockSystemMemoryBuffer : public SystemMemoryBuffer {
    void *ptr_;
    size_t size_;
    std::function<void(void *)> deleter_;

public:
    explicit MockSystemMemoryBuffer(size_t size) :
        ptr_(std::malloc(size)), size_(size), deleter_([](void *p) { std::free(p); }) {
        std::memset(ptr_, 0, size);
    }

    ~MockSystemMemoryBuffer() override {
        if (ptr_) {
            deleter_(ptr_);
        }
    }

    void *get_ptr() const override { return ptr_; }

    uint64_t get_iova() const override { return reinterpret_cast<uint64_t>(ptr_); }

    size_t get_size() const override { return size_; }
};

class MockSystemMemoryAllocator : public SystemMemoryAllocator {
public:
    std::unique_ptr<SystemMemoryBuffer> allocate(size_t size) override {
        return std::make_unique<MockSystemMemoryBuffer>(size);
    }
};

// --- Factory ---

std::unique_ptr<TTDevice> create_mock_device(IODeviceType type, int device_id = 0);

}  // namespace tt::umd
