#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: Apache-2.0

"""
Generate __init__.py for tt_umd package based on detected submodules from stub files.
"""

import sys
from pathlib import Path

def generate_init_py(stub_dir: Path, output_file: Path):
    """Generate __init__.py based on .pyi files in stub_dir."""
    
    # Find all submodule stub files (exclude __init__.pyi)
    submodules = []
    if stub_dir.exists():
        for stub_file in stub_dir.glob("*.pyi"):
            if stub_file.stem != "__init__":
                submodules.append(stub_file.stem)
    
    # Sort for consistent output
    submodules.sort()
    
    # Generate __init__.py content
    content = [
        "# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.",
        "# SPDX-License-Identifier: Apache-2.0",
        "",
        '"""',
        "tt_umd Python package",
        "",
        "This package provides Python bindings for the Tenstorrent UMD (Unified Memory Device) library.",
        '"""',
        "",
        "# Import all symbols from the compiled extension module",
        "from .tt_umd import *  # noqa: F401, F403",
        "",
    ]
    
    # Add submodule imports if any found
    if submodules:
        content.append("# Import submodules")
        for submod in submodules:
            content.append(f"from .tt_umd import {submod}  # noqa: F401")
        content.append("")
    
    # Add version handling
    content.extend([
        "# Re-export __version__ if it exists in the extension",
        "try:",
        "    from .tt_umd import __version__",
        "except ImportError:",
        "    # Version will be set by build system if not in extension",
        "    pass",
    ])
    
    # Write the file
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text("\n".join(content) + "\n")
    print(f"Generated {output_file} with submodules: {', '.join(submodules) if submodules else 'none'}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <stub_dir> <output_file>")
        sys.exit(1)
    
    stub_dir = Path(sys.argv[1])
    output_file = Path(sys.argv[2])
    
    generate_init_py(stub_dir, output_file)
