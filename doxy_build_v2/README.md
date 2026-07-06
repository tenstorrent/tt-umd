# UMD Base Layer Documentation

## How to Read These Documents

This package contains the specification and reference material for the UMD base layer.
Document filenames include a version suffix (e.g. `_1.0`) matching the release.
The structure and reading order below apply to all versions.

### Integration Overview

The mapping tables document how existing upper layers depend on the base API. Each table traces the call path from a workload-layer or tool API down to the corresponding base layer primitive.

| Document | Content |
|----------|---------|
| `chip_tt_device_mapping` | Chip / LocalChip / RemoteChip → TTDevice dependency mapping. Per-device workload layer as used by tt-metal. |
| `cluster_tt_device_mapping` | Cluster → TTDevice dependency mapping. Multi-device orchestration layer as used by tt-metal. |
| `tt_exalens_tt_umd_mapping` | tt-exalens → UMD base API mapping. Debugger integration through Python nanobind bindings. |

### TTDevice Facade

TTDevice is the per-device facade through which all upper-layer operations are routed. The header and implementation are provided as reference — the API surface and delegation pattern are considered stable.

| Document | Content |
|----------|---------|
| `base_tt_device_reference` | TTDevice API reference with full header and implementation source. Covers coordinate translation, DMA fallback logic, firmware delegation, hang detection, etc. |

### Base Layer Components

TTDevice delegates all hardware interaction to component interfaces injected via TTDeviceModel at construction. The base components document specifies each interface — which are required, which can be stubbed, and which require a client-provided implementation (.cpp).

| Document | Content |
|----------|---------|
| `base_components_umd` | API reference for all base layer component interfaces: DeviceProtocol, DeviceFirmware, ArchitectureImplementation, IoWindow, FirmwareInfoProvider, DmaInterface, HangDetector, PcieInterface, JtagInterface, RemoteInterface, SystemMemory, TopologyDiscovery, SocArchDescriptor, SocDescriptor. Includes shared type definitions and constants. |

### Recommended Reading Order

1. Skim a mapping table to see how the base APIs are consumed.
2. Read the TTDevice reference to understand the facade structure.
3. Read the base components spec to understand the interfaces that need to be implemented.
4. Refer back to the mapping tables during integration.
