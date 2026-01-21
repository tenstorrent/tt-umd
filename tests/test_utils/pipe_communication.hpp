// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/select.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace tt::umd::test_utils {

class MultiProcessPipe {
private:
    static constexpr int PIPE_READ = 0;
    static constexpr int PIPE_WRITE = 1;

    // Stores [read_fd, write_fd] for each child.
    std::vector<std::array<int, 2>> child_pipes;
    int num_children;

public:
    explicit MultiProcessPipe(int count) : num_children(count) {
        child_pipes.resize(num_children);
        for (int i = 0; i < num_children; ++i) {
            if (pipe(child_pipes[i].data()) == -1) {
                int saved_errno = errno;
                for (int j = 0; j < i; ++j) {
                    close(child_pipes[j][PIPE_READ]);
                    close(child_pipes[j][PIPE_WRITE]);
                }
                errno = saved_errno;
                throw std::runtime_error("Failed to create synchronization pipe");
            }
        }
    }

    MultiProcessPipe(const MultiProcessPipe&) = delete;
    MultiProcessPipe& operator=(const MultiProcessPipe&) = delete;

    ~MultiProcessPipe() {
        for (auto& p : child_pipes) {
            if (p[PIPE_READ] != -1) {
                close(p[PIPE_READ]);
            }
            if (p[PIPE_WRITE] != -1) {
                close(p[PIPE_WRITE]);
            }
        }
    }

    // Called by the Child process after it is fully initialized.
    void signal_ready_from_child(int child_index) {
        // Close the read end we don't need in the child.
        close(child_pipes[child_index][PIPE_READ]);
        child_pipes[child_index][PIPE_READ] = -1;

        char sync_token = '1';
        if (write(child_pipes[child_index][PIPE_WRITE], &sync_token, 1) == -1) {
            perror("Barrier: Failed to write sync token");
        }

        // Close the write end after signaling.
        close(child_pipes[child_index][PIPE_WRITE]);
        child_pipes[child_index][PIPE_WRITE] = -1;
    }

    // Called by the Parent process to block until all children signal.
    bool wait_for_all_children(int timeout_seconds_per_process = 5) {
        for (int i = 0; i < num_children; ++i) {
            // Close the write end we don't need in the parent.
            if (child_pipes[i][PIPE_WRITE] != -1) {
                close(child_pipes[i][PIPE_WRITE]);
                child_pipes[i][PIPE_WRITE] = -1;
            }

            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(child_pipes[i][PIPE_READ], &read_set);

            struct timeval timeout;
            timeout.tv_sec = timeout_seconds_per_process;
            timeout.tv_usec = 0;

            // select() blocks until the pipe has data to read, or the timeout expires.
            // Args: (nfds, readfds, writefds, exceptfds, timeout)
            //   - nfds: highest fd + 1 (select scans fds from 0 to nfds-1)
            //   - readfds: set of fds to monitor for incoming data (we watch the pipe's read end)
            //   - writefds: set of fds to monitor for write-ready (unused, nullptr)
            //   - exceptfds: set of fds to monitor for errors (unused, nullptr)
            //   - timeout: max time to wait before returning 0
            // Returns: >0 if fd is ready, 0 if timeout, -1 on error.
            int ready = select(child_pipes[i][PIPE_READ] + 1, &read_set, nullptr, nullptr, &timeout);

            if (ready <= 0) {
                return false;
            }

            char sync_token;
            if (read(child_pipes[i][PIPE_READ], &sync_token, 1) <= 0) {
                return false;
            }

            close(child_pipes[i][PIPE_READ]);
            child_pipes[i][PIPE_READ] = -1;
        }
        return true;
    }
};

}  // namespace tt::umd::test_utils
