// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/power_state.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Transport for a device on the local PCIe bus whose NOC is reached through TLB windows (Wormhole and
 * Blackhole).
 *
 * Owns the PCI device. Every access is safe to issue from several threads at once: each path serializes on its own
 * window, so a data transfer never waits for a register access or the other way round.
 */
class PcieProtocol : public DeviceProtocol, public PcieInterface {
public:
    /**
     * @param pci_device The device to talk to.
     * @param io_safety Applied to every window this protocol reaches the device through; with IoSafety::Enabled a
     *                  device that disappears mid-access surfaces as error::SigbusError.
     */
    explicit PcieProtocol(std::unique_ptr<PCIDevice> pci_device, IoSafety io_safety = IoSafety::Disabled);

    PCIDevice* get_pci_device() const { return pci_device_.get(); }

    // --- DeviceProtocol ---
    // Data goes through a write-combined window, control through an uncached one, so register accesses reach the
    // device one by one and in order.
    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(
        void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    void write_ctrl(
        const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    // Always multicasts in hardware.
    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;
    // The PCI device number, N in /dev/tenstorrent/N.
    int get_mmio_id() override;

    // --- PcieInterface ---
    // BAR addresses are BAR0 offsets inside the window PCIDevice maps: its top PCIDevice::bar0_size bytes, which
    // hold the ARC and NOC interface registers.
    void bar_write32(uint32_t addr, uint32_t data) override;
    uint32_t bar_read32(uint32_t addr) override;
    int get_numa_node() const override;
    std::optional<uint32_t> get_lane_count() const override;
    void set_power_state(PowerState state) override;
    int export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) override;
    // Applies to accesses through this protocol's own windows; the check must not issue I/O through this protocol.
    void set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) override;

private:
    // A cached window and the lock that gives one thread at a time the right to reconfigure and use it.
    struct LockedWindow {
        std::mutex mutex;
        std::unique_ptr<TlbWindow> window;
    };

    // Declared first so that the windows, which hold TLBs of this device, are released before it is closed.
    std::unique_ptr<PCIDevice> pci_device_;
    LockedWindow data_window_;
    LockedWindow ctrl_window_;
};

}  // namespace tt::umd
