# UMD Examples

This directory contains examples demonstrating how to use various software components in the Tenstorrent Unified Memory Driver (UMD).

## Available Examples

### `tt_device_example/`
Demonstrates TTDevice usage, showcasing basic device operations and the difference between functionality available before and after calling `init_tt_device()`.

## Building Examples

Examples are not built by default. To build them:

```bash
# Configure with examples enabled
cmake -B build -DTT_UMD_BUILD_EXAMPLES=ON

# Build
cmake --build build
```

Each example directory contains its own README with specific usage instructions.

## Adding New Examples

When adding new examples:
1. Create a new subdirectory with a descriptive name
2. Include a README.md explaining the example's purpose and usage
3. Add your example to the main `CMakeLists.txt` in this directory
4. Update this README to list the new example
