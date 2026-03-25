// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <stdexcept>
#include <string>

#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

template <typename DATA_T>
class UMDException {
public:
    UMDException(const std::string& what, const DATA_T& data) : what_string_(what), exception_data_(data) {}

    std::string& what() { return what_string_; }

    const std::string& what() const noexcept { return what_string_; }

    DATA_T& data() { return exception_data_; }

    const DATA_T& data() const noexcept { return exception_data_; }

private:
    std::string what_string_;
    DATA_T exception_data_;
    // TODO: Try adding stack trace and source line information.
};

struct CoreExceptionData {
public:
    xy_pair core;
};

struct ETHHeartbeatFailureData : public CoreExceptionData {
    uint32_t postcode;
    uint32_t heartbeat_value;
};

class ETHHeartbeatError : public UMDException<ETHHeartbeatFailureData> {
public:
    explicit ETHHeartbeatError(tt_xy_pair eth_core, uint32_t postcode, uint32_t heartbeat_value);
};

/**
 * @brief Exception thrown when a SIGBUS signal is intercepted.
 * This indicates a hardware access error, likely due to a reset or
 * hanging device while accessing mapped memory.
 */
class SigbusError : public std::runtime_error {
public:
    explicit SigbusError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace tt::umd
