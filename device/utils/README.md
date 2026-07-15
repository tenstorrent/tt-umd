# UMD locking backends

UMD provides two cross-process lock implementations. They expose the same small interface
(`initialize()`, `lock()`, `try_lock()`, `unlock()`, `probe_lock()`) and meet the C++ `Lockable`
requirement, so they work with `std::lock_guard` / `std::unique_lock` and can be swapped for one
another. They differ in *how* processes find each other and in the performance/robustness trade-off.

| Backend | Mechanism | Coordination requires | Lock/unlock cost | Crash-robust | Scope |
|---|---|---|---|---|---|
| `RobustMutex` | pthread robust mutex in a `/dev/shm` file | shared `/dev/shm` | fastest — userspace fast path, no syscall when uncontended | yes (`EOWNERDEAD` recovery) | any name |
| `KmdMutex` | KMD resource lock via `ioctl` | shared access to `/dev/tenstorrent/<N>` | slower — an `ioctl` on every lock and unlock | yes (released on fd close / process death) | a single silicon device |

## RobustMutex

A robust pthread mutex living in a shared-memory (`/dev/shm`) file. Because the mutex is in shared
memory, the uncontended `lock()`/`unlock()` path is pure userspace (a futex that never enters the
kernel), which makes it the **faster** of the two. It is robust: if the owning process dies, the
next acquirer recovers the mutex (`EOWNERDEAD` → `pthread_mutex_consistent`).

Cost: it requires every participant to see the **same `/dev/shm`**. Processes in separate containers
must share `/dev/shm` explicitly. It also carries the most implementation complexity (shared-memory
layout, pthread-version assumptions, dead-owner recovery).

## KmdMutex

A lock backed by the kernel-mode driver's resource locks (`TENSTORRENT_IOCTL_LOCK_CTL`). It is
**slower** than `RobustMutex` because every operation is a (custom) `ioctl` syscall, with no userspace
fast path. The ioctl set used exposes only a non-blocking acquire, so a contended `lock()` polls with a
short backoff rather than waiting efficiently in the kernel (this also avoids KMD's blocking-acquire
path, which has had a deadlock against device reset).

Its distinguishing property: it needs **no filesystem sharing beyond the device itself**. The lock
lives in the driver, keyed by the device and a lock index, so any process that can open
`/dev/tenstorrent/<N>` contends over the same lock — across containers and mount namespaces, with no
`/dev/shm` to share. The lock is released automatically on fd close / process death.

Limitation: the lock is **tied to a single silicon device**. There is no global lock spanning
devices, and a remote chip reachable through more than one local gateway cannot be serialized by a
single KMD lock (each gateway has its own independent lock table). Indices `0..15` are reserved by
convention for ERISC cores; use higher indices for general coordination.

## Choosing

- Hot, fine-grained locks taken in inner loops, where a shared `/dev/shm` exists → `RobustMutex` (the
  userspace fast path matters).
- Coordination **without** a shared filesystem (e.g. across containers that only share the device) →
  `KmdMutex`, accepting the per-device scope.

## Benchmark

Relative performance can be measured with the locks microbenchmark
(`tests/microbenchmark/host_benchmark/test_locks.cpp`):

It is built as its own executable (`umd_locks_benchmark`), separate from the regular
`umd_microbenchmark`, since it is a one-off comparison rather than a routine benchmark:

```bash
cmake -B build -G Ninja -DTT_UMD_BUILD_TESTS=ON
ninja umd_locks_benchmark -C build
./build/test/umd/microbenchmark/umd_locks_benchmark
```

It reports initialize, single-thread lock/unlock, contended `probe_lock`, and multi-threaded
contended throughput per backend. `KmdMutex` cases are skipped when no device is present.
