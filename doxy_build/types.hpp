// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// --- xy_pair (real, stripped of fmt) ---

namespace tt {

struct xy_pair {
    constexpr xy_pair() = default;

    constexpr xy_pair(std::size_t x, std::size_t y) : x(x), y(y) {}

    std::size_t x = 0;
    std::size_t y = 0;
};

constexpr bool operator==(const xy_pair &a, const xy_pair &b) { return a.x == b.x && a.y == b.y; }

constexpr bool operator!=(const xy_pair &a, const xy_pair &b) { return !(a == b); }

constexpr bool operator<(const xy_pair &left, const xy_pair &right) {
    return (left.x < right.x || (left.x == right.x && left.y < right.y));
}

}  // namespace tt

using tt_xy_pair = tt::xy_pair;

namespace std {
template <>
struct hash<tt::xy_pair> {
    std::size_t operator()(tt::xy_pair const &o) const {
        std::size_t seed = 0;
        seed ^= std::hash<std::size_t>()(o.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::size_t>()(o.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
}  // namespace std

// --- CoreType, CoordSystem, CoreCoord (real, stripped of fmt) ---

namespace tt {

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

enum class CoordSystem : std::uint8_t {
    LOGICAL,
    NOC0,
    TRANSLATED,
    NOC1,
    LITERAL,
};

namespace umd {

struct CoreCoord : public xy_pair {
    CoreCoord() = default;

    constexpr CoreCoord(
        const size_t x,
        const size_t y,
        const CoreType type = CoreType::UNSPECIFIED,
        const CoordSystem coord_system = CoordSystem::LITERAL) :
        xy_pair(x, y), core_type(type), coord_system(coord_system) {}

    constexpr CoreCoord(
        const xy_pair core,
        const CoreType type = CoreType::UNSPECIFIED,
        const CoordSystem coord_system = CoordSystem::LITERAL) :
        xy_pair(core), core_type(type), coord_system(coord_system) {}

    CoreType core_type = CoreType::UNSPECIFIED;
    CoordSystem coord_system = CoordSystem::LITERAL;

    bool operator==(const CoreCoord &other) const {
        return x == other.x && y == other.y && core_type == other.core_type && coord_system == other.coord_system;
    }

    bool operator<(const CoreCoord &o) const {
        if (x != o.x) {
            return x < o.x;
        }
        if (y != o.y) {
            return y < o.y;
        }
        if (core_type != o.core_type) {
            return core_type < o.core_type;
        }
        return coord_system < o.coord_system;
    }
};

}  // namespace umd
}  // namespace tt

// --- NocId (real) ---

namespace tt::umd {

enum class NocId : uint8_t {
    DEFAULT_NOC = 0,
    NOC0 = 0,
    NOC1 = 1,
    SYSTEM_NOC = 2,
};

// Alias used in TTDevice default params.
constexpr NocId NocId_DEFAULT = NocId::DEFAULT_NOC;

}  // namespace tt::umd

// --- RiscType (real, stripped of fmt) ---

namespace tt::umd {

enum class RiscType : std::uint64_t {
    NONE = 0,
    ALL = 1ULL << 0,
    ALL_TRISCS = 1ULL << 1,
    ALL_DATA_MOVEMENT = 1ULL << 2,
    BRISC = 1ULL << 3,
    TRISC0 = 1ULL << 4,
    TRISC1 = 1ULL << 5,
    TRISC2 = 1ULL << 6,
    NCRISC = 1ULL << 7,
    ALL_TENSIX_TRISCS = TRISC0 | TRISC1 | TRISC2,
    ALL_TENSIX_DMS = BRISC | NCRISC,
    ALL_TENSIX = ALL_TENSIX_TRISCS | ALL_TENSIX_DMS,
};

constexpr RiscType operator|(RiscType lhs, RiscType rhs) {
    return static_cast<RiscType>(static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs));
}

constexpr RiscType operator&(RiscType lhs, RiscType rhs) {
    return static_cast<RiscType>(static_cast<uint64_t>(lhs) & static_cast<uint64_t>(rhs));
}

constexpr RiscType operator~(RiscType operand) { return static_cast<RiscType>(~static_cast<uint64_t>(operand)); }

}  // namespace tt::umd

// --- ARCH (stub enum) ---

namespace tt {
enum class ARCH { Invalid = 0, WORMHOLE_B0, BLACKHOLE, QUASAR };
}

// --- Stub types for heavy classes ---

namespace tt::umd {

enum class IODeviceType { UNDEFINED = 0, PCIE, JTAG, REMOTE };

enum class BoardType : uint32_t {
    UNKNOWN = 0,
    P100,
    P150,
    P300,
    N150,
    N300,
    UBB,
    UBB_BLACKHOLE,
    QUASAR_BOARD,
};

enum class EthTrainingStatus { NOT_CONNECTED = 0, CONNECTED, TRAINING, TRAINED };

struct HarvestingMasks {
    uint32_t tensix_mask = 0;
    uint32_t dram_mask = 0;
    uint32_t eth_mask = 0;
};

struct ChipInfo {
    bool noc_translation_enabled = false;
    uint64_t board_id = 0;
    BoardType board_type = BoardType::UNKNOWN;
    uint8_t asic_location = 0;
    HarvestingMasks harvesting_masks;
};

struct DeviceCommandResult {
    uint32_t exit_code = 0;
    std::vector<uint32_t> return_values;
};

class FirmwareBundleVersion {
public:
    FirmwareBundleVersion() = default;

    FirmwareBundleVersion(uint16_t major, uint16_t minor, uint16_t patch) :
        major_(major), minor_(minor), patch_(patch) {}

    uint16_t major_ = 0, minor_ = 0, patch_ = 0;
};

struct TargetIoWindowConfig {
    CoreCoord core;
    uint64_t addr = 0;
    NocId noc = NocId::DEFAULT_NOC;
};

enum class HostMemoryCaching { WC, UC };

struct HostIoWindowConfig {
    HostMemoryCaching caching = HostMemoryCaching::WC;
    size_t size = 0;
};

class SocArchDescriptor {
public:
    explicit SocArchDescriptor(tt::ARCH) {}
};

class SocDescriptor {
public:
    SocDescriptor() = default;

    SocDescriptor(std::shared_ptr<SocArchDescriptor>, ChipInfo) {}

    tt_xy_pair translate_chip_coord_to_translated(const CoreCoord &core) const { return tt_xy_pair(core.x, core.y); }
};

namespace timeout {
constexpr std::chrono::milliseconds FIRMWARE_STARTUP_TIMEOUT{30000};
constexpr std::chrono::milliseconds ETH_TRAINING_TIMEOUT{30000};
constexpr std::chrono::milliseconds DRAM_TRAINING_TIMEOUT{30000};
constexpr std::chrono::milliseconds FIRMWARE_MESSAGE_TIMEOUT{5000};
}  // namespace timeout

constexpr uint32_t HANG_READ_VALUE = 0xFFFFFFFF;

inline BoardType get_board_type_from_board_id(uint64_t board_id) { return BoardType::UNKNOWN; }

}  // namespace tt::umd
