# tt-exalens → UMD API mapping (aggregated by exalens API)

tt-exalens does **not** link UMD as C++ — it imports the `tt_umd` Python module
(pybind bindings) and calls it from Python. The integration lives in three layers:

- `ttexalens/umd_api.py` — discovery, cluster setup, device creation, reset
- `ttexalens/umd_device.py` — per-device wrapper (read/write, ARC, telemetry, coords)
- `ttexalens/device.py` — high-level abstraction over `UmdDevice`

Each row below is a distinct exalens API, the full set of UMD APIs it calls, and what the
exalens API is for.

| Exalens API | UMD APIs used | What the exalens API is for |
|---|---|---|
| `UmdApi.__init__` (umd_api.py) | `TopologyDiscovery.discover`, `TopologyDiscoveryOptions`, `ClusterDescriptor.create_from_yaml_content`, `ClusterDescriptor.get_all_chips`, `.get_ethernet_connections`, `.get_chip_unique_ids`, `.is_chip_mmio_capable`, `.get_closest_mmio_capable_chip`, `.get_active_eth_channels`, `SocDescriptor`, `SocDescriptor.get_eth_cores_for_channels`, `TTSimTTDevice.create`, `RtlSimulationTTDevice.create`, `tt_device.get_soc_descriptor`, `tt_device.noc_write32`, `tt_device.send_tensix_risc_reset` | Bootstraps the whole session: enumerates devices (PCIe/JTAG or simulator), builds the cluster topology and per-chip descriptors, works out which chips are reachable and how, and boots simulator cores. |
| `UmdApi.select_noc_id` (umd_api.py) | `set_thread_noc_id` | Selects which NOC (NOC0/NOC1, or SYSTEM_NOC on Quasar) the current thread's accesses go over. |
| `UmdApi.warm_reset` (umd_api.py) | `WarmReset.ubb_warm_reset`, `WarmReset.warm_reset` | Performs a warm reset of the device (UBB variant on Galaxy, otherwise standard). |
| `UmdApi._reinit_devices_after_sigbus` (umd_api.py) | `TopologyDiscovery.discover`, `SigbusError` | Recovers the session after a device drops off the bus (post-reset) by re-discovering devices. |
| `Context.cluster_descriptor` / `device_ids` (context.py) | `ClusterDescriptor.get_all_chips` | Exposes the cluster descriptor and the list of chip IDs to the rest of exalens. |
| `Device.create` (device.py) | `ARCH.WORMHOLE_B0 / BLACKHOLE / QUASAR` | Picks the correct exalens device subclass for the chip's architecture. |
| `Device.board_type` (device.py) | `ClusterDescriptor.get_board_type` | Returns the board type (e.g. N300). |
| `Device.get_block_locations` (device.py) | `SocDescriptor.get_cores`, `SocDescriptor.get_harvested_cores` | Enumerates the active and harvested cores of a given block type. |
| `UmdDevice.__init__` (umd_device.py) | `TTDevice.get_arch`, `.is_remote`, `.get_communication_device_type`, `SocDescriptor`, `ClusterDescriptor.get_board_type` | Wraps a device handle and caches its key attributes (architecture, MMIO/JTAG capability, SoC descriptor, board type). |
| `UmdDevice.can_use_dma` (umd_device.py) | `ARCH.BLACKHOLE` | Decides whether DMA transfers are allowed for this device. |
| `UmdDevice.__read_from_device_reg` (umd_device.py) | `TTDevice.dma_read_from_device`, `TTDevice.noc_read`, `SocDescriptor.translate_coord_to` | Reads device memory (DMA or NOC depending on size), with timeout detection. |
| `UmdDevice.__write_to_device_reg` (umd_device.py) | `TTDevice.dma_write_to_device`, `TTDevice.noc_write`, `SocDescriptor.translate_coord_to` | Writes device memory (DMA or NOC depending on size), with timeout detection. |
| `UmdDevice.bar0_read32` / `bar0_write32` (umd_device.py) | `TTDevice.bar_read32`, `TTDevice.bar_write32` | Direct 32-bit reads/writes to PCI BAR0 (config/register space). |
| `UmdDevice.initialize_device_coords_cache` (umd_device.py) | `SocDescriptor.get_all_cores`, `.get_all_harvested_cores`, `.get_coord_at`, `.translate_chip_coord_to_translated_coord` | Builds a coordinate lookup grid for every core on the chip. |
| `UmdDevice.convert_from_noc0` (umd_device.py) | `CoreType`, `CoordSystem`, `CoreCoord`, `SocDescriptor.translate_coord_to`, `.translate_chip_coord_to_translated_coord` | Converts a NOC0 coordinate into other coordinate systems (logical/translated/etc.). |
| `UmdDevice.arc_msg` (umd_device.py) | `TTDevice.arc_msg` | Sends an ARC firmware message and returns its response. |
| `UmdDevice.read_arc_telemetry_entry` (umd_device.py) | `TTDevice.get_arc_telemetry_reader`, `ArcTelemetryReader.is_entry_available`, `.read_entry` | Reads an ARC telemetry value (temperatures, voltages, etc.) by tag. |
| `UmdDevice.get_firmware_version` (umd_device.py) | `TTDevice.get_firmware_info_provider`, `FirmwareInfoProvider.get_firmware_version` | Returns the device's firmware bundle version. |
| `UmdDevice.__configure_working_active_eth` (umd_device.py) | `RemoteCommunication.set_remote_transfer_ethernet_cores` | Picks a working active-Ethernet core for reaching remote chips. |
| `UmdDevice.get_remote_transfer_eth_core` / `get_local_tt_device` (umd_device.py) | `TTDevice.get_remote_communication`, `RemoteCommunication.get_remote_transfer_ethernet_core`, `.get_local_device` | Returns the active Ethernet core and the local (MMIO) device used to reach a remote chip. |
| `UmdDevice._update_device_after_sigbus` (umd_device.py) | `SocDescriptor`, `SigbusError` | Rebuilds this device's SoC descriptor after a bus reset. |
| `ArcBlock.set_udmiaxi_region` (arc_block.py) | `ARCH.BLACKHOLE` | Applies Blackhole-specific ARC handling. |
| `server.py` serialization (server.py) | `ARCH`, `BoardType`, `IODeviceType`, `CoreType`, `CoordSystem`, `tt_xy_pair`, `CoreCoord`, `SemVer`, `FirmwareBundleVersion`, `TelemetryTag`, `DramTrainingStatus` | (De)serializes UMD enums/types across the remote-server (Pyro5) boundary. |
