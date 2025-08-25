# TTDevice Example

This example demonstrates TTDevice usage and shows which capabilities are available before and after calling `init_tt_device()`.

## Building and Running

```bash
# Configure with examples enabled
cmake -B build -DTT_UMD_BUILD_EXAMPLES=ON

# Build
cmake --build ./build

# Run
./build/examples/tt_device_example/tt_device_example
```

## What it demonstrates

TTDevice provides two levels of functionality:

### Before `init_tt_device()` (Basic Access)
- Device architecture and PCI info
- Register access via BAR operations
- Basic memory operations

### After `init_tt_device()` (Full Features)
- All other TTDevice functions become available
- Clock and temperature monitoring
- ARC communication and telemetry

## Usage Pattern

```cpp
// Create device
std::unique_ptr<TTDevice> device = TTDevice::create(device_id);

// Basic operations work immediately
tt::ARCH arch = device->get_arch();
uint32_t value = device->bar_read32(address);

// Initialize for full features
device->init_tt_device();

// Advanced operations now available
uint32_t clock = device->get_clock();
ArcMessenger* messenger = device->get_arc_messenger();
```
