/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

// Message types for inter-process communication
enum class MessageType : uint32_t {
    START_DEVICE = 1,
    CLOSE_DEVICE = 2,
    WRITE_TO_DEVICE = 3,
    READ_FROM_DEVICE = 4,
    SEND_TENSIX_RISC_RESET = 5,
    ASSERT_RISC_RESET = 6,
    DEASSERT_RISC_RESET = 7,
    CONNECT_ETH_LINKS = 8,
    EXIT = 9,
    RESPONSE = 10
};

// Message structure for inter-process communication
struct Message {
    MessageType type;
    uint32_t size;  // Size of data payload
};

// Message data structures for inter-process communication
// These must be identical in both parent and child processes

struct WriteMessageData {
    tt_xy_pair translated_core;
    uint64_t l1_dest;
    uint32_t size;
    // Variable length data follows
};

struct ReadMessageData {
    tt_xy_pair translated_core;
    uint64_t l1_src;
    uint32_t size;
};

struct TensixResetMessageData {
    tt_xy_pair translated_core;
    TensixSoftResetOptions soft_resets;
};

struct AssertResetMessageData {
    tt_xy_pair translated_core;
    RiscType selected_riscs;
};

struct DeassertResetMessageData {
    tt_xy_pair translated_core;
    RiscType selected_riscs;
    bool staggered_start;
};

// Safe read wrapper that handles partial reads for large data transfers
inline ssize_t safe_read(int fd, void* buf, size_t count) {
    size_t total_read = 0;
    uint8_t* buffer = static_cast<uint8_t*>(buf);

    while (total_read < count) {
        ssize_t bytes_read = read(fd, buffer + total_read, count - total_read);
        if (bytes_read == 0) {
            // Connection closed
            return total_read;
        } else if (bytes_read < 0) {
            // Error occurred
            return -1;
        }
        total_read += bytes_read;
    }
    return total_read;
}

// Safe write wrapper that handles partial writes for large data transfers
inline ssize_t safe_write(int fd, const void* buf, size_t count) {
    size_t total_written = 0;
    const uint8_t* buffer = static_cast<const uint8_t*>(buf);

    while (total_written < count) {
        ssize_t bytes_written = write(fd, buffer + total_written, count - total_written);
        if (bytes_written < 0) {
            // Error occurred
            return -1;
        }
        total_written += bytes_written;
    }
    return total_written;
}

}  // namespace tt::umd
