# UMD Examples

This directory contains examples demonstrating how to use various software components in the Tenstorrent Unified Memory Driver (UMD).

## Available Examples

### `tt_device_example/`
Demonstrates TTDevice usage, showcasing basic device operations and the difference between functionality available before and after calling `init_tt_device()`.

### `rdma_dmabuf_p2p/`
Two-host benchmark for `Cluster::export_dmabuf()`: a peer NIC RDMA-writes (or reads) directly against a TLB window over a DRAM core on the other host's card. Blackhole only at the moment (intended for Blackhole Galaxy systems). Requires RDMA hardware on both hosts and libibverbs (library + headers, e.g. `libibverbs-dev`). Built only with `-DTT_UMD_BUILD_RDMA=ON`, which is OFF by default; with it ON, missing or too-old libibverbs is a configure error rather than a silent skip.

## Building Examples

Examples are not built by default. To build them:

```bash
# Configure with examples enabled
cmake -B build -DTT_UMD_BUILD_EXAMPLES=ON

# Build
cmake --build build
```

`rdma_dmabuf_p2p` additionally needs `-DTT_UMD_BUILD_RDMA=ON`, since it requires libibverbs at build
time and RDMA hardware to run.

Each example directory contains its own README with specific usage instructions.

## Adding New Examples

When adding new examples:
1. Create a new subdirectory with a descriptive name
2. Include a README.md explaining the example's purpose and usage
3. Add your example to the main `CMakeLists.txt` in this directory
4. Update this README to list the new example
