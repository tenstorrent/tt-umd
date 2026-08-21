// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gmock/gmock.h>

#include <cstddef>
#include <cstdint>
#include <functional>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/power_state.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd::test_utils {

// Mocks for the protocol interfaces components take instead of a TTDevice. Shared so that every
// component test drives the same doubles.
class MockDeviceProtocol : public DeviceProtocol {
public:
    MOCK_METHOD(void, read_data, (void*, tt_xy_pair, uint64_t, size_t, NocId), (override));
    MOCK_METHOD(void, write_data, (const void*, tt_xy_pair, uint64_t, size_t, NocId), (override));
    MOCK_METHOD(void, read_ctrl, (void*, tt_xy_pair, uint64_t, size_t, NocId), (override));
    MOCK_METHOD(void, write_ctrl, (const void*, tt_xy_pair, uint64_t, size_t, NocId), (override));
    MOCK_METHOD(bool, write_to_core_range, (const void*, tt_xy_pair, tt_xy_pair, uint64_t, size_t, NocId), (override));
    MOCK_METHOD(int, get_mmio_id, (), (override));
};

class MockPcieInterface : public PcieInterface {
public:
    MOCK_METHOD(void, bar_write32, (uint32_t, uint32_t), (override));
    MOCK_METHOD(uint32_t, bar_read32, (uint32_t), (override));
    MOCK_METHOD(int, get_numa_node, (), (const, override));
    MOCK_METHOD(void, set_power_state, (PowerState), (override));
    MOCK_METHOD(int, export_dmabuf, (tt_xy_pair, uint64_t, size_t, uint64_t, NocId), (override));
    MOCK_METHOD(void, set_io_timeout_callback, (const std::function<bool(NocId)>&), (override));
};

class MockJtagInterface : public JtagInterface {
public:
    MOCK_METHOD(void, mmio_write32, (uint32_t, uint32_t), (override));
    MOCK_METHOD(uint32_t, mmio_read32, (uint32_t), (override));
};

}  // namespace tt::umd::test_utils
