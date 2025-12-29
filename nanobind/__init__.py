# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

"""
tt_umd Python package

This package provides Python bindings for the Tenstorrent UMD (Unified Memory Device) library.
"""

# Import all symbols from the compiled extension module
from .tt_umd import *  # noqa: F401, F403

# Re-export __version__ if it exists in the extension
try:
    from .tt_umd import __version__
except ImportError:
    # Version will be set by build system if not in extension
    pass
