#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for Python bindings of UMD logging configuration."""

import pytest


def test_logging_level_enum():
    """Test that the logging Level enum is available and has all expected values."""
    from tt_umd.logging import Level

    # Check all enum values exist
    assert hasattr(Level, "TRACE")
    assert hasattr(Level, "DEBUG")
    assert hasattr(Level, "INFO")
    assert hasattr(Level, "WARN")
    assert hasattr(Level, "ERROR")
    assert hasattr(Level, "CRITICAL")
    assert hasattr(Level, "OFF")

    # Check that we can create instances
    level = Level.INFO
    assert level is not None


def test_set_level_function():
    """Test that set_level function is available and callable."""
    from tt_umd.logging import Level, set_level

    # Should not raise any exceptions
    set_level(Level.INFO)
    set_level(Level.DEBUG)
    set_level(Level.WARN)
    set_level(Level.ERROR)
    set_level(Level.CRITICAL)
    set_level(Level.TRACE)
    set_level(Level.OFF)


def test_set_level_with_all_levels():
    """Test that we can set each level without errors."""
    from tt_umd.logging import Level, set_level

    levels = [
        Level.TRACE,
        Level.DEBUG,
        Level.INFO,
        Level.WARN,
        Level.ERROR,
        Level.CRITICAL,
        Level.OFF,
    ]

    for level in levels:
        set_level(level)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
