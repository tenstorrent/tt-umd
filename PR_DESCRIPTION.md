### Issue
(Link to Github issue(s))

### Description
Part of enabling `-Wconversion`: the simulator interfaces are 32-bit (`libttsim`'s C ABI takes `uint32_t` coordinates and sizes, and the RTL flatbuffer schema and `SimulationServerDeviceInfo` carry 32-bit sizes and harvesting masks), so `RtlSimCommunicator` and `TTSimCommunicator` keep their `uint32_t` parameters, and the devices that hold `size_t` coordinates and sizes narrow them explicitly when they call into the simulator.

### List of the changes
- `static_cast<uint32_t>` for the core coordinates and sizes passed to the communicators by `TTSimTTDevice`, `RtlSimulationTTDevice`, `RtlSimTlbWindow`, `RtlSimRiscReset` and `RtlSimAttProgram`, using typed `x`/`y`/`n` locals where a function calls the communicator several times.
- `static_cast<uint32_t>` where the RTL communicator and channel fill the flatbuffer size, and where `SimulationServerDeviceInfo` is filled with the harvesting masks.
- The `RISC_TYPES_DMS` loop index in `RtlSimRiscReset` is `uint32_t`, matching the communicator's `dm_index`.

### Testing
CI

### API Changes
This PR has no API changes.
