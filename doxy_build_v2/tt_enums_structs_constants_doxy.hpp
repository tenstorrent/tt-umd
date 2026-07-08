// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include "types/tt_xy_pair_doxy.hpp"

namespace tt::umd {

/**
 * @defgroup tt_base_types Appendix: Types, Enums, and Constants
 * @{
 *
 * \latexonly \clearpage \endlatexonly
 *
 * @brief Shared type definitions, enumerations, and constants referenced
 * throughout the base API components.
 *
 */

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/**
 * @brief Bitmask enum for selecting RISC cores on a Tensix core.
 */
enum class RiscType : std::uint64_t;

/**
 * @brief Identifies the NOC (Network on Chip) used to route a transaction.
 */
enum class NocId : uint8_t {
    DEFAULT = 0,
    NOC = 0,
    NOC0 = 0,
    NOC1 = 1,
    SYSTEM_NOC = 1,
};

/**
 * @brief Host memory caching strategy for an @ref IoWindow.
 */
enum class HostMemoryCaching {
    WC,  ///< Write-Combining — bypasses cache, batches small writes into bus bursts. Higher throughput, relaxed
         ///< ordering.
    UC,  ///< Uncacheable — bypasses cache, every access hits hardware immediately. Strict ordering.
};

/**
 * @brief Hardware link training status of an Ethernet core.
 */
enum class EthTrainingStatus {
    IN_PROGRESS = 0,
    SUCCESS = 1,
    FAIL = 2,
    NOT_CONNECTED = 3,
};

/**
 * @brief Transport used to reach a local device.
 */
enum class IODeviceType {
    PCIe,
    JTAG,
    AXI,
    UNDEFINED,
};

/**
 * @brief Hardware architecture of a Tenstorrent device.
 */
enum class ARCH {
    WORMHOLE_B0 = 2,
    BLACKHOLE = 3,
    QUASAR = 4,
    Invalid = 0xFF,
};

/**
 * @brief Functional type of a core on the SoC.
 */
enum class CoreType {
    ARC,
    DRAM,
    ACTIVE_ETH,
    IDLE_ETH,
    PCIE,
    TENSIX,
    ROUTER_ONLY,
    SECURITY,
    L2CPU,
    DISPATCH,
    HARVESTED,
    ETH,
    WORKER,
    COUNT,
    UNSPECIFIED,
};

/**
 * @brief Coordinate system used for core addressing.
 */
enum class CoordSystem : uint8_t {
    LOGICAL,
    NOC0,
    TRANSLATED,
    NOC1,
    LITERAL,  ///< Bypasses translation — coordinates used as-is.
};

/**
 * @brief Transaction attributes for @ref IoWindow mappings.
 *
 * Modifies the type of transaction issued through an @ref IoWindow.
 */
enum class WindowFlags : std::uint32_t {
    None = 0,
    Atomic = 1 << 0,  ///< Issue transaction as atomic on the target interconnect.
    Snoop = 1 << 1,   ///< Mark transaction as snoopable for cache coherency.
};

constexpr WindowFlags operator|(WindowFlags lhs, WindowFlags rhs) noexcept {
    return static_cast<WindowFlags>(
        static_cast<std::underlying_type_t<WindowFlags>>(lhs) | static_cast<std::underlying_type_t<WindowFlags>>(rhs));
}

constexpr WindowFlags operator&(WindowFlags lhs, WindowFlags rhs) noexcept {
    return static_cast<WindowFlags>(
        static_cast<std::underlying_type_t<WindowFlags>>(lhs) & static_cast<std::underlying_type_t<WindowFlags>>(rhs));
}

constexpr WindowFlags operator~(WindowFlags val) noexcept {
    return static_cast<WindowFlags>(~static_cast<std::underlying_type_t<WindowFlags>>(val));
}

constexpr WindowFlags& operator|=(WindowFlags& lhs, WindowFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

constexpr WindowFlags& operator&=(WindowFlags& lhs, WindowFlags rhs) noexcept {
    lhs = lhs & rhs;
    return lhs;
}

/**
 * @brief DRAM channel training result.
 */
enum class DramTrainingStatus : uint8_t {
    IN_PROGRESS = 0,
    FAIL = 1,
    SUCCESS = 2,
};

/**
 * @brief Hardware model or SKU of the board hosting the chip.
 */
enum class BoardType : uint32_t {
    E75,
    E150,
    E300,
    N150,
    N300,
    P100,
    P150,
    P300,
    GALAXY,
    UBB,
    UBB_WORMHOLE = UBB,
    UBB_BLACKHOLE,
    QUASAR_BOARD,
    UNKNOWN,
};

/**
 * @brief Identifies a specific GDDR module on the device.
 */
enum class GddrModule;

// ---------------------------------------------------------------------------
// Aliases
// ---------------------------------------------------------------------------

/**
 * @brief Device identifier within a cluster.
 */
using ChipId = int;

/**
 * @brief Convenience alias for xy_pair.
 */
using tt_xy_pair = xy_pair;

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

/**
 * @brief L1 address map parameters for barrier and firmware version locations.
 */
struct DeviceL1AddressParams {
    uint32_t tensix_l1_barrier_base = 0;  ///< Barrier base address in Tensix L1.
    uint32_t eth_l1_barrier_base = 0;     ///< Barrier base address in Ethernet L1.
    uint32_t fw_version_addr = 0;         ///< Firmware version address in L1.
};

/**
 * @brief Semantic version (major.minor.patch).
 */
struct SemVer {
    uint64_t major = 0;
    uint64_t minor = 0;
    uint64_t patch = 0;
    uint64_t pre_release = 0;
};

/**
 * @brief Firmware bundle version. Extends SemVer with firmware-specific comparison.
 */
struct FirmwareBundleVersion : SemVer {
    using SemVer::SemVer;
};

/**
 * @brief Coordinate pair identifying a core on the device.
 */
struct CoreCoord;

/**
 * @brief Per-block harvesting bitmasks indicating which functional units are disabled.
 */
struct HarvestingMasks {
    size_t tensix_harvesting_mask = 0;
    size_t dram_harvesting_mask = 0;
    size_t eth_harvesting_mask = 0;
    size_t pcie_harvesting_mask = 0;
    size_t l2cpu_harvesting_mask = 0;

    HarvestingMasks operator|(const HarvestingMasks& other) const {
        return HarvestingMasks{
            .tensix_harvesting_mask = this->tensix_harvesting_mask | other.tensix_harvesting_mask,
            .dram_harvesting_mask = this->dram_harvesting_mask | other.dram_harvesting_mask,
            .eth_harvesting_mask = this->eth_harvesting_mask | other.eth_harvesting_mask,
            .pcie_harvesting_mask = this->pcie_harvesting_mask | other.pcie_harvesting_mask,
            .l2cpu_harvesting_mask = this->l2cpu_harvesting_mask | other.l2cpu_harvesting_mask};
    }
};

/**
 * @brief Hardware identity and physical configuration of a chip.
 *
 * Returned by TTDevice::get_chip_info(). Describes the actual physical state
 * of the device: NOC translation, harvesting, and board placement.
 */
struct ChipInfo {
    bool noc_translation_enabled = false;       ///< True when NOC coordinate translation is active.
    HarvestingMasks harvesting_masks = {};      ///< Bitmasks of disabled functional blocks.
    BoardType board_type = BoardType::UNKNOWN;  ///< Board model or SKU.
    uint64_t board_id = 0;                      ///< Unique physical board identifier.
    uint8_t asic_location = 0;                  ///< Chip slot index on a multi-chip board.
};

/**
 * @brief Describes the device-side target for an @ref IoWindow.
 *
 * Specifies which core(s) and address the window maps to on the device, and
 * optionally which NOC to route through. When the mapped address space is
 * not NOC-routed (e.g., direct BAR register space), noc is left as std::nullopt.
 *
 * When only core_start is set, the window targets a single core. When
 * core_end is also set, the window targets a rectangular grid of cores
 * for multicast, if the hardware supports it.
 */
struct TargetIoWindowConfig {
    tt_xy_pair core_start;  ///< Target core, or upper-left corner of a multicast grid.
    std::optional<tt_xy_pair> core_end =
        std::nullopt;                         ///< Lower-right corner of a multicast grid, or nullopt for unicast.
    uint64_t addr;                            ///< Destination address on the target core(s).
    std::optional<NocId> noc = std::nullopt;  ///< Optional routing selection.
    WindowFlags flags = WindowFlags::None;    ///< Transaction attributes.
};

/**
 * @brief Describes the host-side properties for an @ref IoWindow.
 *
 * Controls the host memory caching strategy and requested window size.
 * A size of 0 is valid and delegates the window size selection to the concrete implementation.
 */
struct HostIoWindowConfig {
    HostMemoryCaching mapping = HostMemoryCaching::WC;
    size_t size = 0;
};

/**
 * @brief Result of a firmware command execution.
 *
 * Bundles the exit code and any return values from the firmware into a single
 * return type, eliminating the need for out-parameters.
 */
struct DeviceCommandResult {
    uint32_t exit_code;
    std::vector<uint32_t> return_values;
};

/**
 * @brief Aggregated GDDR telemetry across all modules on a device.
 */
struct GddrTelemetry;

/**
 * @brief Per-module GDDR telemetry.
 */
struct GddrModuleTelemetry;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/**
 * @brief Sentinel value returned when a read hits a hung or unreachable device.
 */
static constexpr uint32_t HANG_READ_VALUE = 0xFFFFFFFFu;

/**
 * @brief Default timeout constants used by the base layer API.
 */
namespace timeout {
inline constexpr auto FIRMWARE_STARTUP_TIMEOUT = std::chrono::milliseconds(300'000);
inline constexpr auto DRAM_TRAINING_TIMEOUT = std::chrono::milliseconds(300'000);
inline constexpr auto ETH_TRAINING_TIMEOUT = std::chrono::milliseconds(900'000);
}  // namespace timeout

/** @} */  // end of tt_base_types group

}  // namespace tt::umd
