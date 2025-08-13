# TTDevice Example

This example demonstrates TTDevice usage and shows which capabilities are available before and after calling `init_tt_device()`.

## Building and Running

```bash
cmake --build ./build
./build/example/tt_device_example
```

## What it demonstrates

TTDevice provides two levels of functionality:

### Before `init_tt_device()` (Basic Access)
- `get_arch()` - Device architecture
- `get_pci_device()` - PCI device info
- `bar_read32()` / `bar_write32()` - Register access
- `read_from_device()` / `write_to_device()` - Memory operations

### After `init_tt_device()` (Full Features)
- `get_clock()` - Clock frequency
- `get_board_id()` - Board ID
- `get_asic_temperature()` - Temperature
- `get_arc_messenger()` - ARC communication
- `get_arc_telemetry_reader()` - Telemetry data

## Usage Pattern

```cpp
// Create device
std::unique_ptr<TTDevice> device = TTDevice::create(device_id);

// Basic operations work immediately
tt::ARCH arch = device->get_arch();
uint32_t value = device->bar_read32(address);

// Initialize for full features
device->init_tt_device();
device->wait_arc_core_start();

// Advanced operations now available
uint32_t clock = device->get_clock();
ArcMessenger* messenger = device->get_arc_messenger();
```
