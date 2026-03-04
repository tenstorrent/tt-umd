# UMD Test Organization

This directory contains all tests for the Tenstorrent UMD (User Mode Driver) library.

## Directory Structure

### `device/`
Contains all tests that **require a device** to run. Tests in this directory interact with actual hardware or simulators.

#### Organization by Component
Tests are organized into subdirectories based on the primary UMD class or component being tested:

- **`cluster/`** - Tests for the `Cluster` class and cluster-level operations
- **`chip/`** - Tests for the `Chip` class
- **`tt_device/`** - Tests for the `TTDevice` class (including SPI variants)
- **`arc_telemetry/`** - Tests for `ArcTelemetryReader` class
- **`soc_descriptor/`** - Tests for `SocDescriptor` class
- **`cluster_descriptor/`** - Tests for `ClusterDescriptor` class
- **`sysmem_manager/`** - Tests for system memory management
- **`pci_device/`** - Tests for PCIe device operations
- **`jtag/`** - Tests for JTAG interface
- **`unified/`** - Cross-component integration tests

#### Test File Naming Convention
Each subdirectory should follow these guidelines:

1. **Primary test file**: `test_<class>.cpp`
   - Main test file for the class (e.g., `test_cluster.cpp`, `test_chip.cpp`)
   - Contains core functionality tests for that class

2. **Feature-specific test files**: `test_<feature>.cpp`
   - Separate files for testing specific features or scenarios related to the class
   - Examples:
     - `test_tt_visible_devices.cpp` - Tests for TT_VISIBLE_DEVICES filtering
     - `test_warm_reset.cpp` - Tests for warm reset functionality
     - `test_risc_program.cpp` - Tests for RISC programming features

**When adding new tests:**
- Place tests in the directory corresponding to the primary API layer being tested
- Use the main class as the dominant interface in your tests
- Create separate files for distinct features that are substantial enough to warrant their own test suite
- Keep test files focused and cohesive

### `baremetal/`
Contains tests that **do not require a device** to run. These are offline tests that can run without hardware, typically testing:
- Configuration parsing
- Coordinate translation
- Descriptor validation
- Utility functions (assertions, version parsing)
- Other pure software functionality

### `arch/`
Contains architecture-specific tests that run on devices but have **architecture-dependent behavior**.

- **`arch/wormhole/`** - Tests specific to Wormhole architecture
- **`arch/blackhole/`** - Tests specific to Blackhole architecture

These tests may use device-specific features, register layouts, or behaviors unique to a particular chip architecture.

### Other Directories

- **`galaxy/`** - Tests for multi-chip galaxy configurations
- **`simulation/`** - Simulator-specific tests
- **`test_utils/`** - Shared test utilities and helper functions
- **`microbenchmark/`** - Performance benchmarking tests

## Adding New Tests

1. **Determine the primary class/component** your test exercises
2. **Check if a device is required**:
   - If yes → place in `device/<component>/`
   - If no → place in `baremetal/`
   - If architecture-specific → place in `arch/<architecture>/`
3. **Choose the appropriate file**:
   - Add to existing `test_<class>.cpp` if testing core functionality
   - Create new `test_<feature>.cpp` if testing a distinct, substantial feature
4. **Update CMakeLists.txt** in the corresponding directory to include new test files

## Building and Running Tests

Build all tests:
```bash
cmake --build build
```

Run all tests:
```bash
./build/test/umd/device/device_tests          # Device tests
./build/test/umd/baremetal/baremetal_tests    # Baremetal tests
./build/test/umd/blackhole/unit_tests         # Blackhole tests
./build/test/umd/wormhole_b0/unit_tests       # Wormhole tests
./build/test/umd/galaxy/unit_tests_glx        # Galaxy tests
```

Run specific test suites using gtest filters:
```bash
./build/test/umd/device/device_tests --gtest_filter="TestCluster.*"
```
