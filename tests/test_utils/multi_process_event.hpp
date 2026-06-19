// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/eventfd.h>
#include <sys/select.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace tt::umd::test_utils {

// Direction-neutral signaling primitive that works across fork().
// Unlike pipe-based approaches, eventfd uses a single FD per slot —
// no read/write ends to close after fork.
class MultiProcessEvent {
public:
    explicit MultiProcessEvent(int count) {
        fds.reserve(count);
        for (int i = 0; i < count; ++i) {
            int fd = eventfd(0, 0);
            if (fd == -1) {
                for (int f : fds) {
                    close(f);
                }
                throw std::runtime_error("Failed to create eventfd");
            }
            fds.push_back(fd);
        }
    }

    MultiProcessEvent(const MultiProcessEvent&) = delete;
    MultiProcessEvent& operator=(const MultiProcessEvent&) = delete;

    ~MultiProcessEvent() {
        for (int fd : fds) {
            if (fd != -1) {
                close(fd);
            }
        }
    }

    void notify(int slot) {
        uint64_t val = 1;
        if (write(fds[slot], &val, sizeof(val)) != sizeof(val)) {
            perror("MultiProcessEvent: signal failed");
        }
    }

    bool wait_for(int slot, int timeout_seconds = 5) {
        fd_set read_set;
        struct timeval timeout;
        int ret;

        while (true) {
            FD_ZERO(&read_set);
            FD_SET(fds[slot], &read_set);
            timeout = {timeout_seconds, 0};
            ret = select(fds[slot] + 1, &read_set, nullptr, nullptr, &timeout);
            if (ret != -1 || errno != EINTR) {
                break;
            }
        }

        if (ret <= 0) {
            return false;
        }

        uint64_t val;
        return read(fds[slot], &val, sizeof(val)) == sizeof(val);
    }

    bool wait_for_all(int timeout_seconds_per_slot = 5) {
        for (size_t i = 0; i < fds.size(); ++i) {
            if (!wait_for(i, timeout_seconds_per_slot)) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<int> fds;
};

}  // namespace tt::umd::test_utils
