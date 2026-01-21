<h1 align="center">
  <p>tt-dal</p>
</h1>

<p align="center">
  Tenstorrent Device Access Library
</p>

> [!NOTE]
>
> Current version **0.1.0** is an early release with API subject to
> change before 1.0.0 stabilization.

## About

`tt-dal` provides raw, stateless access to Tenstorrent device hardware
through the [tt-kmd](https://github.com/tenstorrent/tt-kmd) kernel
driver. It exposes device discovery, memory-mapped I/O via TLB
windows, ARC messaging, telemetry, and reset operations.

This library provides low-level hardware primitives for Linux systems.
Higher-level libraries should build on `tt-dal` for application-level
device management.

## Design

`tt-dal` is a stateless C API with transparent handles and no hidden
state. All operations use IOCTLs directly to the kernel driver,
dispatching architecture-specific code internally. See
[DESIGN.md](DESIGN.md) for detailed philosophy and design decisions.

## Features

`tt-dal` provides version queries, comprehensive error handling, device
discovery and lifecycle management, TLB allocations for memory-mapped
I/O, ARC controller messaging, device telemetry monitoring, and reset
operations.

## Usage

### Prerequisites

- [tt-kmd](https://github.com/tenstorrent/tt-kmd) kernel driver must be
  installed and loaded
- CMake 3.16+

### Building

```bash
# From tt-umd root directory
cmake -B build -G Ninja
cmake --build build --target ttdal
```

The library will be built as `build/dal/libttdal.a`.

See the public header for complete API documentation.

## Organization

The library is organized as a pure C implementation with
architecture-specific dispatch. Core operations are in `lib.c` with
Wormhole and Blackhole implementations in separate architecture files.

```
dal/
├── CMakeLists.txt   # build configuration
├── DESIGN.md        # design philosophy
├── README.md        # this document
├── ...
├── include/         # public interface
├── src/             # core implementation
│   ├── lib.c        # main library
│   └── arch/        # architecture code
└── tests/           # integration tests
```

## Integration

To use `tt-dal` in your project:

**CMake**:
```cmake
add_subdirectory(dal)
target_link_libraries(your_target PRIVATE ttdal)
```

**Manual**:
```bash
# Compile
gcc -c your_program.c -I dal/include -o your_program.o

# Link
gcc your_program.o -L build/dal -lttdal -o your_program
```

## Testing

Tests are not built by default. To build:

```bash
cmake -B build -DTT_UMD_BUILD_TESTS=ON
cmake --build build
```

Run tests:

```bash
./build/dal/tests/test_device
./build/dal/tests/test_tlb
./build/dal/tests/test_integration
```

## License

This project is licensed under [Apache License
2.0](../../LICENSE). You have permission to use this code under the
conditions of the license pursuant to the rights it grants.

This software assists in programming Tenstorrent products. Making,
using, or selling hardware, models, or IP may require the license of
rights (such as patent rights) from Tenstorrent or others.
