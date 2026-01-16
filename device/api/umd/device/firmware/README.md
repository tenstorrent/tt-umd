## Firmware Compatibility Layer

This module provides a data-driven way to handle different firmware versions and architectures.

### Design Overview

`FirmwareInfoProvider` is a single, data-driven class that abstracts away firmware version and architecture differences. The class uses a configuration map (`TelemetryFeatureMap`) that determines how each telemetry feature is read and transformed at runtime.

#### Key Components

- **TelemetryFeatureMap**: A mapping from `FirmwareFeature` to `FeatureProfile`, which specifies:
  - **Source**: Where to read the data from:
    - `StandardTag`: Standard telemetry tag (modern firmware)
    - `SmBusTag`: Legacy SMBus-style telemetry tag (Wormhole legacy)
    - `FixedValue`: A constant value (for unavailable/hardcoded features)
  - **Transform**: How to transform the raw value:
    - `LinearTransform`: Apply bit extraction and linear scaling
    - `NotAvailable`: Mark the feature as unavailable (returns `std::nullopt`)

- **Factory Method**: `create_telemetry_feature_map()` builds the appropriate configuration based on architecture and firmware version, handling special cases like:
  - Legacy Wormhole (≤18.3, 18.4-18.7)
  - Legacy Blackhole (≤18.7)
  - Modern firmware (>18.7)

### Return Types

Most getter functions return `std::optional<T>` to indicate whether a feature is available:

- **Returns `std::optional`**: Functions that depend on telemetry features which may not be available on all firmware versions or architectures:
  - `get_eth_fw_version()`, `get_eth_fw_version_semver()`
  - `get_gddr_fw_version()`, `get_cm_fw_version()`, `get_dm_app_fw_version()`, `get_dm_bl_fw_version()`, `get_tt_flash_version()`
  - `get_aiclk()`, `get_axiclk()`, `get_arcclk()`
  - `get_fan_speed()`, `get_tdp()`, `get_tdc()`, `get_vcore()`
  - `get_board_temperature()`

- **Returns non-optional**: Functions that are always available or have guaranteed fallback behavior:
  - `get_firmware_version()`: Always available from device
  - `get_board_id()`, `get_asic_temperature()`, `get_max_clock_freq()`, `get_asic_location()`, `get_heartbeat()`: Core telemetry always present
  - `get_dram_training_status()`: Returns empty vector if unavailable

### Adding New Features

1. Add a new entry to `FirmwareFeature` enum in `telemetry_mapping.hpp`
2. Add the feature to the appropriate base map (`create_modern_base()` or `create_legacy_wormhole_18_3_base()`)
3. Override the feature in `create_telemetry_feature_map()` for any architecture/version that needs different behavior
4. Add the corresponding getter function that calls `read_scalar<T>(FirmwareFeature::YOUR_FEATURE)`

### Benefits

- **No Inheritance Complexity**: Single class handles all versions without virtual functions
- **Data-Driven**: Feature behavior is configured through maps, not code branches
- **Explicit Availability**: `std::optional` return types make feature availability explicit at compile time
- **Easy to Extend**: New architectures/versions only require adding map entries
- **Easy to Deprecate**: Removing old firmware support means removing map factory cases
