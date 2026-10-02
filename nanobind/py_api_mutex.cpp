// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>

#include <chrono>

#include "umd/device/utils/kmd_mutex.hpp"
#include "umd/device/utils/mutex_interface.hpp"
#include "umd/device/utils/robust_mutex.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt::umd;

void bind_mutex(nb::module_ &m) {
    // The base is bound so that the methods both backends share are defined once and so that a Python
    // caller can treat either backend as a lock without knowing which one it holds.
    nb::class_<MutexInterface>(
        m,
        "MutexInterface",
        "Common interface of UMD's cross-process locks. The lock is not recursive, and misuse (taking it twice from "
        "one thread, unlocking from a thread that does not hold it, unlocking twice) is not diagnosed: it may throw, "
        "be ignored, or block forever. Use the object as a context manager to avoid all three.")
        .def(
            "initialize",
            &MutexInterface::initialize,
            release_gil(),
            "Sets up the underlying OS resource. Must be called before any locking operation.")
        .def("lock", &MutexInterface::lock, release_gil(), "Blocks until the lock is acquired.")
        .def("unlock", &MutexInterface::unlock, release_gil(), "Releases the lock.")
        .def(
            "probe_lock",
            [](MutexInterface &self, int timeout_s) { return self.probe_lock(std::chrono::seconds(timeout_s)); },
            nb::arg("timeout_s") = 0,
            release_gil(),
            "Tries to acquire the lock, waiting up to timeout_s seconds (zero returns immediately). Returns None if "
            "the lock was acquired, in which case the caller now holds it and must unlock(). Otherwise returns the "
            "owning (pid, tid), or (0, 0) if the backend cannot identify the owner.")
        .def(
            "__enter__",
            [](nb::object self) {
                MutexInterface &mutex = nb::cast<MutexInterface &>(self);
                {
                    nb::gil_scoped_release no_gil;
                    mutex.lock();
                }
                return self;
            })
        .def(
            "__exit__",
            [](MutexInterface &self, nb::handle, nb::handle, nb::handle) { self.unlock(); },
            // The exception triple is None when the block exits normally, and nanobind rejects None
            // arguments unless they are declared to accept it.
            nb::arg("exc_type").none(),
            nb::arg("exc_value").none(),
            nb::arg("traceback").none(),
            release_gil());

    nb::class_<RobustMutex, MutexInterface> robust_mutex(
        m,
        "RobustMutex",
        "A cross-process lock backed by a robust pthread mutex in a /dev/shm file. All participants must see the "
        "same /dev/shm. If the owning process dies, the next acquirer recovers the lock.");
    robust_mutex.def(nb::init<std::string_view>(), nb::arg("mutex_name"), release_gil());

    // Exposed so that callers can find (or clean up) the /dev/shm file backing a named mutex.
    robust_mutex.attr("SHM_FILE_PREFIX") = std::string(RobustMutex::SHM_FILE_PREFIX);

    nb::class_<KmdMutex, MutexInterface>(
        m,
        "KmdMutex",
        "A cross-process lock backed by a KMD resource lock on one local PCIe device. Any process that can open "
        "/dev/tenstorrent/<N> contends over it, with no filesystem sharing needed, but its scope is that one device. "
        "Threads of one process exclude each other even when they share a single KmdMutex. KMD does not report an "
        "owner, so probe_lock() reports (0, 0) on contention.")
        .def(
            nb::init<int, uint8_t>(),
            nb::arg("pci_device_num"),
            nb::arg("lock_index"),
            release_gil(),
            "pci_device_num is N in /dev/tenstorrent/N, lock_index is the KMD resource lock index.")
        .def(
            "try_lock",
            &KmdMutex::try_lock,
            release_gil(),
            "Attempts to acquire without blocking. Returns True if acquired, False if held by another handle.")
        .def(
            "is_locked_by_anyone",
            &KmdMutex::is_locked_by_anyone,
            release_gil(),
            "Best-effort, racy query of whether the lock is held by any handle, including this one. For diagnostics "
            "only.");
}
