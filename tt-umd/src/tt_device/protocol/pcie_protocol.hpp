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
#include "tt-umd/types/tlb.hpp"

namespace tt::umd {

class ArchitectureImplementation;

/**
 * @brief Device I/O over PCIe: NOC traffic through TLB windows, register traffic through the BARs.
 *
 * Owns the PCIDevice it talks through, so the device stays open exactly as long as the protocol.
 * Safe to call from several threads: accesses that share a window are serialized.
 */
class PcieProtocol : public DeviceProtocol, public PcieInterface {
public:
    /**
     * @param pci_device The opened device; ownership is taken.
     * @param architecture_impl Supplies the TLB sizes the device offers. Non-owning, must outlive this.
     * @param io_safety Whether window accesses recover from SIGBUS instead of crashing the process.
     */
    PcieProtocol(
        std::unique_ptr<PCIDevice> pci_device,
        ArchitectureImplementation* architecture_impl,
        IoSafety io_safety = IoSafety::Disabled);

    ~PcieProtocol() override;

    // DeviceProtocol.
    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(
        void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    void write_ctrl(
        const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;
    int get_mmio_id() override;

    // PcieInterface.
    void bar_write32(uint32_t addr, uint32_t data) override;
    uint32_t bar_read32(uint32_t addr) override;
    int get_numa_node() const override;
    std::optional<uint32_t> get_lane_count() const override;
    void set_power_state(PowerState state) override;
    int export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) override;
    void set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) override;

    PCIDevice* get_pci_device();

    /**
     * @brief Allocates a TLB window of its own, configured to config.
     *
     * @param size Requested window size; 0 picks the first size the architecture offers that the
     * KMD can still allocate.
     */
    std::unique_ptr<TlbWindow> allocate_window(tlb_data config, TlbMapping mapping, size_t size);

private:
    // The window shared by every data and control access, allocated on first use.
    TlbWindow& get_shared_window();

    std::unique_ptr<PCIDevice> pci_device_;
    ArchitectureImplementation* architecture_impl_ = nullptr;
    const IoSafety io_safety_;

    // Serializes accesses through the shared window, since each one reconfigures it.
    std::mutex shared_window_mutex_;
    std::unique_ptr<TlbWindow> shared_window_;

    std::function<bool(NocId)> io_timeout_hang_check_;
};

}  // namespace tt::umd
