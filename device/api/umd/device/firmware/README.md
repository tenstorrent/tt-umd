## Firmware Compatibility Layer

This module provides a structured way to handle different firmware versions while keeping the codebase clean and maintainable.

### Design Overview

- FirmwareInfoProvider (Base Class)

    - Represents the implementation for the latest firmware version.

    - Always contains the most up-to-date behavior.

    - New firmware features should be added here.

- Derived Classes (Older Firmware Versions)

    - For each older firmware version, a class is derived from FirmwareInfoProvider.

    - These derived classes override only the functions whose behavior differs from the latest firmware.

    - This minimizes code duplication and isolates version-specific logic.

### Version Selection

The correct implementation is chosen at runtime based on the device’s firmware version and architecture.
This ensures that the application interacts with a consistent interface regardless of the firmware version.

### Deprecation Policy

- Older firmware versions may eventually be deprecated.

- When a firmware version is no longer supported, its derived class should be removed.

- Over time, the goal is to have only the FirmwareInfoProvider base class remain.

- This ensures the code reflects only the currently supported firmware set.

### Benefits

- Clarity: The base class always reflects the latest firmware specification.

- Maintainability: Only differences are overridden, reducing code duplication.

- Flexibility: Supports multiple firmware versions without complex conditionals.

- Future-Proofing: Easy to remove deprecated versions without breaking newer ones.