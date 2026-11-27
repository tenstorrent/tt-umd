// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/pcie/tlb_handle.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/tlb.hpp"

namespace tt::umd {

/**
 * Safe-I/O policy of a SiliconTlbWindow, fixed for its lifetime.
 *
 * With Enabled, the window's memory-access methods and configure() recover from a SIGBUS raised by a
 * failing device access and throw SigbusError instead of letting the fault take the process down. This
 * needs the process-wide handler installed by SiliconTlbWindow::set_sigbus_safe_handler(true).
 *
 * The underlying type is bool so that a caller still holding a flag can cast it directly.
 */
enum class IoSafety : bool {
    Disabled = false,
    Enabled = true,
};

/**
 * IoWindow over a TLB mapped into a PCIe BAR: every access is a direct load or store to the mapping.
 *
 * The TLB entry maps a TLB-size aligned device address. A target address that is not aligned places the
 * window base that far into the mapping, and get_size() shrinks by the same amount.
 *
 * configure() refuses WindowFlags the architecture's TLB entry cannot carry, and any ordering other than
 * RELAXED where the entry has no ordering field. Without an explicit ordering it maps STRICT where the
 * entry carries ordering and RELAXED where it does not.
 */
class SiliconTlbWindow : public IoWindow {
public:
    // Takes ownership of handle and programs it with config under the same alignment rule as
    // configure(const tlb_data&). This initial programming is not covered by IoSafety.
    SiliconTlbWindow(
        std::unique_ptr<TlbHandle> handle, const tlb_data config = {}, IoSafety io_safety = IoSafety::Disabled);

    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write64(uint64_t offset, uint64_t value) override;
    uint64_t read64(uint64_t offset) override;
    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;
    void write_aligned(uint64_t offset, const void* data, size_t size) override;
    void read_aligned(uint64_t offset, void* data, size_t size) override;
    void configure(const TargetIoWindowConfig& config) override;
    void configure(const TargetIoWindowConfig& config, IoOrdering ordering) override;
    TargetIoWindowConfig get_target_config() const override;
    IoOrdering get_io_ordering() const override;
    size_t get_size() const override;
    HostMemoryCaching get_memory_caching_type() const override;

    // Transfers size / 4 consecutive 32-bit words as individual register accesses. Unlike
    // write_aligned()/read_aligned(), offset and size are not checked for 4-byte alignment.
    void write_register(uint64_t offset, const void* data, size_t size);
    void read_register(uint64_t offset, void* data, size_t size);

    // Wires the per-op MMIO timeout: on a single-op overrun hang_check is consulted for the window's
    // currently configured NOC. Returning true means that NOC is hung (the transfer aborts with
    // DeviceTimeoutError); false means it is healthy (a slow-but-completing op is not a false positive).
    // When empty (the default), overruns are treated as false alarms and never abort on time alone.
    void set_io_timeout_hang_check(const std::function<bool(NocId)>& hang_check);

    // Programs the TLB entry from a raw descriptor; new_config.local_offset need not be TLB-size aligned
    // (see the class comment). get_target_config() keeps reporting the WindowFlags of the last
    // configure(TargetIoWindowConfig), even where the descriptor has a field of its own for one
    // (noncacheable). The per-op timeout hang check follows the NOC selected here.
    void configure(const tlb_data& new_config);

    // The TLB handle backing this window. The window owns it.
    TlbHandle& handle_ref() const;

    // Device address the window base maps to: the TLB entry's aligned address plus the offset into the mapping.
    uint64_t get_base_address() const;

    // Installs (true) or removes (false) the process-wide SIGBUS handler that IoSafety::Enabled relies on.
    static void set_sigbus_safe_handler(bool set_safe_handler);

private:
    // NonCacheable lives in the window's configuration on an architecture whose entries carry AXI
    // transaction attributes; elsewhere there is no field for it and configure() refuses it.
    WindowFlags supported_window_flags() const;

    // A silicon window programs a real TLB entry, so it carries ordering only where the architecture's
    // entry has a field for it.
    bool carries_ordering() const;

    void validate(uint64_t offset, size_t size) const;
    uint64_t get_total_offset(uint64_t offset) const;

    // Rebuilds io_timeout_callback_ (an OpTimeoutGuard is_false_alarm callback) for the window's currently
    // configured NOC: true means "healthy, false positive" so the overrun is ignored, false confirms it.
    // The NOC is read from the live TLB config, so this must be called whenever the config or hang check
    // changes (construction, configure(), set_io_timeout_hang_check()). When no hang check is wired it
    // installs a callback that always reports a false alarm, so a bare per-op timeout never aborts on
    // time alone (avoids false-positive NOC hangs; see #2981).
    void update_io_timeout_callback();

    // Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
    // Both routines assume that misaligned accesses are permitted on host memory.
    //
    // 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
    // which glibc's memcpy may perform when unrolling. This affects from and to device.
    // 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
    // to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.
    static void memcpy_from_device(
        void* dest, const volatile void* src, std::size_t num_bytes, const std::function<bool()>& on_timeout);
    static void memcpy_to_device(
        void* dest, const void* src, std::size_t num_bytes, const std::function<bool()>& on_timeout);

    void write_regs(
        volatile uint32_t* dest, const uint32_t* src, size_t word_len, const std::function<bool()>& on_timeout);
    void read_regs(void* src_reg, size_t word_len, void* data, const std::function<bool()>& on_timeout);

    template <typename Func, typename... Args>
    decltype(auto) execute_safe(Func&& func, Args&&... args);

    // Unguarded bodies of the memory-access methods above; the public methods route through
    // execute_safe() when io_safety_ is enabled and call these directly otherwise.
    void write16_impl(uint64_t offset, uint16_t value);
    uint16_t read16_impl(uint64_t offset);
    void write32_impl(uint64_t offset, uint32_t value);
    uint32_t read32_impl(uint64_t offset);
    void write64_impl(uint64_t offset, uint64_t value);
    uint64_t read64_impl(uint64_t offset);
    void write_register_impl(uint64_t offset, const void* data, size_t size);
    void read_register_impl(uint64_t offset, void* data, size_t size);
    void write_block_impl(uint64_t offset, const void* data, size_t size);
    void read_block_impl(uint64_t offset, void* data, size_t size);
    void configure_impl(const tlb_data& new_config);

    std::unique_ptr<TlbHandle> tlb_handle_;
    // Distance from the TLB-size aligned address the entry maps to the window base.
    uint64_t offset_from_aligned_addr_ = 0;
    const IoSafety io_safety_;
    // Non-direction WindowFlags of the last configure(TargetIoWindowConfig), reported by get_target_config().
    WindowFlags window_flags_ = WindowFlags::None;

    std::function<bool(NocId)> hang_check_;
    // Cached is_false_alarm callback bound to the currently configured NOC; refreshed by
    // update_io_timeout_callback() so the IO paths need not rebuild it per call.
    std::function<bool()> io_timeout_callback_;
};

}  // namespace tt::umd
