# Cluster Example

This example demonstrates Cluster usage and showcases the Tensix soft reset functionality using the new unified RiscType enum.

## Building and Running

```bash
# Configure with examples enabled
cmake -B build -DTT_UMD_BUILD_EXAMPLES=ON

# Build
cmake --build ./build

# Run
./build/examples/cluster_example/cluster_example
```

## What it demonstrates

The Cluster API provides high-level device management capabilities, and this example specifically focuses on:

### Tensix Soft Reset Control
- Reading current soft reset state from Tensix cores
- Asserting reset for specific RISC cores (BRISC, TRISC0-2, NCRISC)
- Deasserting reset with optional staggered start
- Architecture-agnostic usage with unified RiscType enum

### RiscType Features
- Individual core targeting (`RiscType::BRISC | RiscType::TRISC0`)
- Architecture-agnostic flags (`RiscType::ALL_TRISCS`, `RiscType::ALL_DMS`)
- String formatting for debugging (`RiscTypeToString()`)
- Bitwise operations and boolean contexts

### Cluster Management
- Device enumeration and chip selection
- Core coordinate handling (logical coordinates)
- Exception handling and error management

## Usage Pattern

```cpp
// Create cluster and get first available device
std::unique_ptr<Cluster> cluster = Cluster::create();
chip_id_t chip = cluster->get_all_chips()[0];
CoreCoord core(1, 1, CoreType::TENSIX, CoordSystem::LOGICAL);

// Check current reset state
RiscType current_state = cluster->get_soft_reset_state(chip, core);
std::cout << "Current state: " << RiscTypeToString(current_state) << std::endl;

// Assert reset for specific cores
RiscType cores_to_reset = RiscType::BRISC | RiscType::TRISC0;
cluster->assert_risc_reset(chip, core, cores_to_reset);

// Deassert with staggered start
cluster->deassert_risc_reset(chip, core, cores_to_reset, true);

// Use architecture-agnostic flags
cluster->assert_risc_reset(chip, core, RiscType::ALL_TRISCS);
```

## Important Notes

⚠️ **Warning**: Deasserting reset without proper firmware/programs loaded on the cores may cause the cores to crash or behave unpredictably. Ensure appropriate programs are loaded before deasserting reset signals.

## RiscType Reference

### Individual Cores
- `RiscType::BRISC` - Business logic RISC
- `RiscType::TRISC0/1/2` - Tensor RISC cores
- `RiscType::NCRISC` - Network/communication RISC

### Architecture-Agnostic Groups
- `RiscType::ALL` - All cores for the current architecture
- `RiscType::ALL_TRISCS` - All tensor RISC cores
- `RiscType::ALL_DMS` - All data movement cores

### NEO Architecture Cores (Future)
- `RiscType::NEO0_TRISC0` through `RiscType::NEO3_TRISC2`
- `RiscType::DM0` through `RiscType::DM7`
