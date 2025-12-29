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
    assert hasattr(Level, "Trace")
    assert hasattr(Level, "Debug")
    assert hasattr(Level, "Info")
    assert hasattr(Level, "Warning")
    assert hasattr(Level, "Error")
    assert hasattr(Level, "Critical")
    assert hasattr(Level, "Off")

    # Check that we can create instances
    level = Level.Info
    assert level is not None


def test_set_level_function():
    """Test that set_level function is available and callable."""
    from tt_umd.logging import Level, set_level

    # Should not raise any exceptions
    set_level(Level.Info)
    set_level(Level.Debug)
    set_level(Level.Warning)
    set_level(Level.Error)
    set_level(Level.Critical)
    set_level(Level.Trace)
    set_level(Level.Off)


def test_set_level_with_all_levels():
    """Test that we can set each level without errors."""
    from tt_umd.logging import Level, set_level

    levels = [
        Level.Trace,
        Level.Debug,
        Level.Info,
        Level.Warn,
        Level.Error,
        Level.Critical,
        Level.Off,
    ]

    for level in levels:
        set_level(level)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
