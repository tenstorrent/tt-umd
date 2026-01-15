# Topology Discovery Example

This example demonstrates how to use the `TopologyDiscovery` API to discover all devices connected to the system and create `TTDevice` instances for them.

## Building and Running

```bash
# Configure with examples enabled
cmake -B build -DTT_UMD_BUILD_EXAMPLES=ON

# Build
cmake --build ./build

# Run
./build/examples/tt_topology_discovery_example/tt_topology_discovery_example
```

## What it demonstrates

TopologyDiscovery provides a comprehensive way to discover all Tenstorrent devices in a system, including those connected via Ethernet.

### Key Features Demonstrated

1. **Cluster Discovery** - Discovers all connected chips and their relationships
2. **MMIO vs Remote Chips** - Distinguishes between locally-connected (PCIe) and remote (Ethernet) chips
3. **TTDevice Creation** - Creates and initializes TTDevice instances for discovered chips
4. **Ethernet Topology** - Shows how chips are interconnected via Ethernet links
5. **Memory Operations** - Performs basic read/write operations on discovered devices

### TopologyDiscoveryOptions

```cpp
TopologyDiscoveryOptions options;
options.no_remote_discovery = false;      // Enable/disable remote discovery via Ethernet
options.no_wait_for_eth_training = false; // Skip waiting for ETH core training
options.no_eth_firmware_strictness = false; // Allow different ETH firmware versions
```

### Basic Usage Pattern

```cpp
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/tt_device.hpp"

// Configure discovery options
TopologyDiscoveryOptions options;

// Discover topology - returns cluster descriptor and discovered devices
auto [cluster_desc, discovered_devices] = TopologyDiscovery::discover(options);

// Get all chips in the cluster
auto all_chips = cluster_desc->get_all_chips();

// Check if a chip is local (MMIO-capable) or remote
for (ChipId chip_id : all_chips) {
    if (cluster_desc->is_chip_mmio_capable(chip_id)) {
        // Local chip - accessible via PCIe
        int pci_device_num = cluster_desc->get_chips_with_mmio().at(chip_id);
        auto device = TTDevice::create(pci_device_num);
        device->init_tt_device();
    } else {
        // Remote chip - accessible via Ethernet through a local gateway
        ChipId closest_mmio = cluster_desc->get_closest_mmio_capable_chip(chip_id);
        // ... create remote device using RemoteCommunication
    }
}
```

### Cluster Information Available

- Total number of chips in the cluster
- List of MMIO-capable (local) chips
- List of remote chips
- Ethernet connections between chips
- Chip locations (rack, shelf, x, y coordinates)
- Architecture type for each chip
