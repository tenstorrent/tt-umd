// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <cstring>

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
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) override;
    std::unique_ptr<DeviceController> create_device_controller() override;
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
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) override;
    std::unique_ptr<DeviceController> create_device_controller() override;
};

// --- Mock System Memory ---

class MockSystemMemoryAllocator : public SystemMemoryAllocator {
public:
    std::unique_ptr<SystemMemoryBuffer> allocate_buffer(size_t size, bool bind_to_noc = false) override {
        void *ptr = std::malloc(size);
        std::memset(ptr, 0, size);
        uint64_t iova = reinterpret_cast<uint64_t>(ptr);

        SystemMemoryBuffer::NocBinder binder = nullptr;
        if (bind_to_noc) {
            binder = [iova]() -> uint64_t { return iova + 0x20000000000ULL; };
        }

        auto buf = make_buffer(
            ptr, size, iova, [](void *p) { std::free(p); }, std::move(binder));
        if (bind_to_noc) {
            buf->bind_noc_address();
        }
        return buf;
    }

    std::unique_ptr<SystemMemoryBuffer> map_user_buffer(
        void *user_ptr, size_t size, bool bind_to_noc = false) override {
        uint64_t iova = reinterpret_cast<uint64_t>(user_ptr);

        SystemMemoryBuffer::NocBinder binder = nullptr;
        if (bind_to_noc) {
            binder = [iova]() -> uint64_t { return iova + 0x20000000000ULL; };
        }

        auto buf = make_buffer(
            user_ptr, size, iova, [](void *) {}, std::move(binder));
        if (bind_to_noc) {
            buf->bind_noc_address();
        }
        return buf;
    }
};

// --- Factory ---

std::unique_ptr<TTDevice> create_mock_device(IODeviceType type, int device_id = 0);

}  // namespace tt::umd
