# Support Matrix

Support levels:
- **Tested**: built and tested in CI.
- **Built**: built in CI, but not tested. Expected to work, without guarantees.
- **Unsupported**: neither built nor tested. Issues are not prioritized.

## TT HW support

| Arch | Config | Board type | Level |
|---|---|---|---|
| Wormhole | n150 | `N150` | Tested |
| Wormhole | n300 | `N300` | Tested |
| Wormhole | LoudBox / QuietBox (4x n300) | `N300` | Tested |
| Wormhole | Galaxy 6U | `UBB_WORMHOLE` | Tested |
| Wormhole | Galaxy 4U / TGG | `GALAXY` | Unsupported, last supported in [v0.9.1](https://github.com/tenstorrent/tt-umd/releases/tag/v0.9.1) |
| Blackhole | p100a | `P100` | Tested |
| Blackhole | p150a / p150b / p150c | `P150` | Tested |
| Blackhole | p300a / p300c | `P300` | Tested |
| Blackhole | QuietBox (2x p300) | `P300` | Tested |
| Blackhole | Galaxy 6U | `UBB_BLACKHOLE` | Tested |
| Blackhole | Galaxy CF | `UBB_BLACKHOLE_CF` | Built |
| Quasar | Simulator only | `QUASAR_BOARD` | Tested |
| Grayskull | All | - | Unsupported, removed before the first release [v0.1.0](https://github.com/tenstorrent/tt-umd/releases/tag/v0.1.0) |

Systems that mix devices of different architectures (for example Wormhole and Blackhole on the same host) are not tested.

Both host memory setups are tested:
- IOMMU enabled.
- IOMMU disabled or in passthrough mode, with 1G hugepages. See the [README](../README.md#iommu-and-hugepage-requirements).

Tests run inside Docker containers, on both bare metal hosts and VMs (with vIOMMU).
Simulated Wormhole, Blackhole and Quasar devices are also tested, see [SIMULATION_SERVER.md](SIMULATION_SERVER.md).

## Host HW support

| Host arch | Level |
|---|---|
| x86_64 | Tested |
| aarch64 | Built. DEB packages and Python wheels are released. |
| riscv64 | Built |

## TT SW support

| Component | Minimum version | Below minimum |
|---|---|---|
| [TT-KMD](https://github.com/tenstorrent/tt-kmd) | 2.0.0 | Device open fails. |
| FW bundle, Wormhole | 18.3.0 | Topology discovery reports `UnsupportedCMFWError`. |
| FW bundle, Blackhole | 18.5.0 | Topology discovery reports `UnsupportedCMFWError`. |
| ETH FW, Wormhole | 6.14.0 | Topology discovery warns and marks the chip unhealthy. |
| ETH FW, Blackhole | 1.4.1 | Topology discovery warns and marks the chip unhealthy. |

FW bundles are tested up to 19.14. Newer bundles are accepted, but their newest features may not be supported.
Legacy FW bundles versioned 80.x and above predate 18.x and are unsupported.

Some features require versions newer than the minimum:

| Feature | Requires |
|---|---|
| Architecture agnostic reset | KMD 2.4.1 |
| Power state management, PCIe BAR invalidation on reset | KMD 2.6.0 |
| Read-only page pinning | KMD 2.9.0 |
| TLB dma-buf export | KMD 2.10.0 and Linux 5.8 |
| ARC clock telemetry on Wormhole | FW bundle 18.7.0 |
| ETH link retraining on Wormhole | ETH FW 7.2.0 |

Version requirements are defined in [kmd_versions.hpp](../device/api/umd/device/utils/kmd_versions.hpp), [firmware_utils.cpp](../device/firmware/firmware_utils.cpp) and [erisc_firmware.hpp](../device/firmware/erisc_firmware.hpp).

## Non-TT SW support

| Component | Supported |
|---|---|
| Linux | Tested on Ubuntu 22.04. Built on Ubuntu 24.04 and Fedora 39. |
| macOS | Unsupported |
| Windows | Unsupported |
| Linux kernel | 5.4 or newer, as required by [TT-KMD](https://github.com/tenstorrent/tt-kmd#supported-kernels). |
| Compiler | GCC 11 or newer, Clang 13 or newer. MSVC is unsupported. |
| C++ standard | C++17 |
| CMake | 3.25 or newer |
| glibc | 2.28 or newer for the released Python wheels (manylinux_2_28). |
| Python | 3.9 to 3.13 |
