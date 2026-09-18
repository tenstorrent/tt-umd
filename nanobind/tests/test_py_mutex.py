#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for Python bindings of UMD's cross-process locks.

The locking semantics themselves are covered by the C++ suites; these check that the bindings
expose them, and double as usage examples.
"""

import os
import threading
import uuid

import pytest
import tt_umd

# A lock index UMD itself never takes (it uses 16-21), and distinct from the one the C++ KmdMutex
# tests use, so the two suites cannot contend with each other.
TEST_KMD_LOCK_INDEX = 34


@pytest.fixture
def mutex_name():
    """A mutex name no other test or process uses, with its /dev/shm file removed afterwards.

    The name has to be unique because a leftover backing file from a previous run would carry over
    its lock state.
    """
    name = f"UMD_PY_TEST_MUTEX_{os.getpid()}_{uuid.uuid4().hex}"
    yield name
    shm_path = f"/dev/shm/{tt_umd.RobustMutex.SHM_FILE_PREFIX}{name}"
    if os.path.exists(shm_path):
        os.remove(shm_path)


def probe_from_contender(mutex, timeout_s=0):
    """Probe the lock from a thread that does not hold it, which is what a contender looks like.

    A probe that succeeds leaves the caller holding the lock, so it is released again in the same
    thread that took it.
    """
    result = []

    def probe():
        owner = mutex.probe_lock(timeout_s)
        if owner is None:
            mutex.unlock()
        result.append(owner)

    thread = threading.Thread(target=probe)
    thread.start()
    thread.join()
    return result[0]


def test_robust_mutex(mutex_name):
    mutex = tt_umd.RobustMutex(mutex_name)
    mutex.initialize()

    # A second object over the same name is another view of the same lock, as another process has.
    contender = tt_umd.RobustMutex(mutex_name)
    contender.initialize()

    with mutex:
        owner = probe_from_contender(contender)
        assert owner is not None, "A second view acquired a lock that was already held"
        owner_pid, owner_tid = owner
        assert owner_pid == os.getpid()
        assert owner_tid != 0

    assert probe_from_contender(contender) is None, "Lock was not released"


def test_kmd_mutex():
    devices = tt_umd.PCIDevice.enumerate_devices()
    if not devices:
        pytest.skip("No /dev/tenstorrent device present")

    mutex = tt_umd.KmdMutex(devices[0], TEST_KMD_LOCK_INDEX)
    mutex.initialize()

    # A second handle on the same device lock is what another process looks like to KMD.
    contender = tt_umd.KmdMutex(devices[0], TEST_KMD_LOCK_INDEX)
    contender.initialize()

    with mutex:
        assert (
            not contender.try_lock()
        ), "A second handle acquired a lock that was already held"
        assert contender.is_locked_by_anyone()

    assert contender.try_lock(), "Lock was not released"
    contender.unlock()


def test_context_manager_releases_on_exception(mutex_name):
    mutex = tt_umd.RobustMutex(mutex_name)
    mutex.initialize()

    with pytest.raises(ValueError):
        with mutex:
            raise ValueError("propagates out of the with block")

    assert probe_from_contender(mutex) is None, "Lock was not released while unwinding"


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
