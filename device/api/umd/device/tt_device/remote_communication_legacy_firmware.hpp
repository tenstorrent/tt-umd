/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <functional>
#include <set>
#include <unordered_set>

#include "umd/device/tt_device/remote_communication.hpp"

namespace tt::umd {

class SysmemManager;

class RemoteCommunicationLegacyFirmware : public RemoteCommunication {
public:
    RemoteCommunicationLegacyFirmware(
        TTDevice* local_tt_device, EthCoord target_chip, SysmemManager* sysmem_manager = nullptr);

    void read_non_mmio(
        tt_xy_pair target_core,
        void* dest,
        uint64_t core_src,
        uint32_t size_in_bytes,
        const uint64_t timeout_ms = 5000) override;

    void read_non_mmio_reg(
        tt_xy_pair target_core,
        void* dest,
        uint64_t core_src,
        uint32_t size_in_bytes,
        const uint64_t timeout_ms = 5000) override;

    void write_to_non_mmio(
        tt_xy_pair target_core,
        const void* src,
        uint64_t core_dest,
        uint32_t size_in_bytes,
        bool broadcast = false,
        std::vector<int> broadcast_header = {},
        const uint32_t timeout_ms = 5000) override;

    void write_to_non_mmio_reg(
        tt_xy_pair target_core,
        const void* src,
        uint64_t core_dest,
        uint32_t size_in_bytes,
        bool broadcast = false,
        std::vector<int> broadcast_header = {},
        const uint32_t timeout_ms = 5000) override;

    void wait_for_non_mmio_flush(const uint32_t timeout_ms = 5000) override;

private:
    void read_non_mmio_impl(
        tt_xy_pair target_core,
        void* dest,
        uint64_t core_src,
        uint32_t size_in_bytes,
        const uint64_t timeout_ms,
        std::function<void(void*, const tt_xy_pair&, std::uint64_t, std::uint32_t)> read_from_device_fn,
        std::function<void(const void*, const tt_xy_pair&, std::uint64_t, std::uint32_t)> write_to_device_fn);

    void write_to_non_mmio_impl(
        tt_xy_pair target_core,
        const void* src,
        uint64_t core_dest,
        uint32_t size_in_bytes,
        bool broadcast,
        std::vector<int> broadcast_header,
        const uint32_t timeout_ms,
        std::function<void(void*, const tt_xy_pair&, std::uint64_t, std::uint32_t)> read_from_device_fn,
        std::function<void(const void*, const tt_xy_pair&, std::uint64_t, std::uint32_t)> write_to_device_fn);

    EthCoord target_chip;
};

}  // namespace tt::umd
