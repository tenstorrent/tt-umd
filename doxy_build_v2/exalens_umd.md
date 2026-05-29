<style>table{border-collapse:collapse;width:100%}th,td{border:1px solid #333;padding:6px;vertical-align:top;font-size:12px}th{background:#e0e0e0}</style>

# tt-exalens → UMD Base API Mapping

<table>
<tr>
<th>Exalens API</th>
<th>UMD Base API</th>
<th>What the exalens API is for</th>
</tr>
<tr>
<td><code>UmdApi.__init__</code> (umd_api.py)</td>
<td><code>TopologyDiscovery::discover(TopologyDiscoveryOptions)</code> <br> <code>TTDevice(TTDeviceModel)</code> <br> <code>TTDevice::init_device()</code> <br> <code>TTDevice::get_soc_descriptor()</code> <br> <code>TTDevice::write_ctrl()</code> <br> <code>TTDevice::assert_risc_reset()</code></td>
<td>Bootstraps the whole session: enumerates devices, builds cluster topology and per-chip descriptors, boots simulator cores.</td>
</tr>
<tr>
<td><code>UmdApi.select_noc_id</code> (umd_api.py)</td>
<td>Pass <code>NocId</code> parameter directly to each I/O call — thread-local NOC selection is removed.</td>
<td>Selects which NOC the current thread's accesses go over.</td>
</tr>
<tr>
<td><code>UmdApi.warm_reset</code> (umd_api.py)</td>
<td>Out-of-scope for base API. Warm reset is a platform-level operation above TTDevice.</td>
<td>Performs a warm reset of the device.</td>
</tr>
<tr>
<td><code>UmdApi._reinit_devices_after_sigbus</code> (umd_api.py)</td>
<td><code>TTDevice::set_sigbus_safe_handler(true)</code> <br> <code>TopologyDiscovery::discover()</code> (re-discover after reset)</td>
<td>Recovers the session after a device drops off the bus by re-discovering devices.</td>
</tr>
<tr>
<td><code>Context.cluster_descriptor</code> / <code>device_ids</code> (context.py)</td>
<td><code>ClusterDescriptor</code> (returned by <code>TopologyDiscovery::discover()</code>)</td>
<td>Exposes the cluster descriptor and the list of chip IDs.</td>
</tr>
<tr>
<td><code>Device.create</code> (device.py)</td>
<td><code>TTDevice::get_arch()</code></td>
<td>Picks the correct exalens device subclass for the chip's architecture.</td>
</tr>
<tr>
<td><code>Device.board_type</code> (device.py)</td>
<td><code>TTDevice::get_board_type()</code></td>
<td>Returns the board type (e.g. N300).</td>
</tr>
<tr>
<td><code>Device.get_block_locations</code> (device.py)</td>
<td><code>SocDescriptor::get_cores(CoreType)</code> <br> <code>SocDescriptor::get_harvested_cores(CoreType)</code></td>
<td>Enumerates the active and harvested cores of a given block type.</td>
</tr>
<tr>
<td><code>UmdDevice.__init__</code> (umd_device.py)</td>
<td><code>TTDevice::get_arch()</code> <br> <code>TTDevice::is_remote()</code> <br> <code>TTDevice::get_communication_device_type()</code> <br> <code>TTDevice::get_soc_descriptor()</code> <br> <code>TTDevice::get_board_type()</code></td>
<td>Wraps a device handle and caches its key attributes.</td>
</tr>
<tr>
<td><code>UmdDevice.can_use_dma</code> (umd_device.py)</td>
<td>Check <code>TTDeviceModel::get_dma_interface() != nullptr</code></td>
<td>Decides whether DMA transfers are allowed for this device.</td>
</tr>
<tr>
<td><code>UmdDevice.__read_from_device_reg</code> (umd_device.py)</td>
<td><code>TTDevice::dma_read()</code> (falls back to <code>read_data()</code> internally) <br> <code>TTDevice::read_ctrl()</code></td>
<td>Reads device memory (DMA or MMIO depending on size), with timeout detection.</td>
</tr>
<tr>
<td><code>UmdDevice.__write_to_device_reg</code> (umd_device.py)</td>
<td><code>TTDevice::dma_write()</code> (falls back to <code>write_data()</code> internally) <br> <code>TTDevice::write_ctrl()</code></td>
<td>Writes device memory (DMA or MMIO depending on size), with timeout detection.</td>
</tr>
<tr>
<td><code>UmdDevice.bar0_read32</code> / <code>bar0_write32</code> (umd_device.py)</td>
<td><code>TTDevice::bar_read32()</code> <br> <code>TTDevice::bar_write32()</code></td>
<td>Direct 32-bit reads/writes to BAR register space.</td>
</tr>
<tr>
<td><code>UmdDevice.initialize_device_coords_cache</code> (umd_device.py)</td>
<td><code>SocDescriptor::get_all_cores()</code> <br> <code>SocDescriptor::get_all_harvested_cores()</code> <br> <code>SocDescriptor::get_coord_at()</code> <br> <code>SocDescriptor::translate_coord_to()</code></td>
<td>Builds a coordinate lookup grid for every core on the chip.</td>
</tr>
<tr>
<td><code>UmdDevice.convert_from_noc0</code> (umd_device.py)</td>
<td><code>SocDescriptor::translate_coord_to()</code></td>
<td>Converts a coordinate into other coordinate systems.</td>
</tr>
<tr>
<td><code>UmdDevice.arc_msg</code> (umd_device.py)</td>
<td><code>TTDevice::send_device_command()</code></td>
<td>Sends a firmware command and returns its response.</td>
</tr>
<tr>
<td><code>UmdDevice.read_arc_telemetry_entry</code> (umd_device.py)</td>
<td><code>FirmwareTelemetryReader::is_entry_available()</code> <br> <code>FirmwareTelemetryReader::read_entry()</code></td>
<td>Reads a telemetry value by tag.</td>
</tr>
<tr>
<td><code>UmdDevice.get_firmware_version</code> (umd_device.py)</td>
<td><code>TTDevice::get_firmware_version()</code></td>
<td>Returns the device's firmware bundle version.</td>
</tr>
<tr>
<td><code>UmdDevice.__configure_working_active_eth</code> (umd_device.py)</td>
<td>Out-of-scope — remote transport not part of the base API specification.</td>
<td>Picks a working active-Ethernet core for reaching remote chips.</td>
</tr>
<tr>
<td><code>UmdDevice.get_remote_transfer_eth_core</code> / <code>get_local_tt_device</code> (umd_device.py)</td>
<td>Out-of-scope — remote transport not part of the base API specification.</td>
<td>Returns the active Ethernet core and the local device used to reach a remote chip.</td>
</tr>
<tr>
<td><code>UmdDevice._update_device_after_sigbus</code> (umd_device.py)</td>
<td><code>TTDevice::set_sigbus_safe_handler(true)</code> <br> Re-create <code>TTDevice(TTDeviceModel)</code> + <code>init_device()</code></td>
<td>Rebuilds this device's state after a bus reset.</td>
</tr>
<tr>
<td><code>ArcBlock.set_udmiaxi_region</code> (arc_block.py)</td>
<td>Architecture-specific — handled inside <code>ArchitectureImplementation</code> or <code>DeviceFirmware</code>.</td>
<td>Applies Blackhole-specific ARC handling.</td>
</tr>
<tr>
<td><code>server.py</code> serialization (server.py)</td>
<td>All types in base API types: <code>ARCH</code>, <code>BoardType</code>, <code>IODeviceType</code>, <code>CoreType</code>, <code>CoordSystem</code>, <code>tt_xy_pair</code>, <code>CoreCoord</code>, <code>SemVer</code>, <code>FirmwareBundleVersion</code>, <code>DramTrainingStatus</code>.</td>
<td>(De)serializes UMD enums/types across the remote-server boundary.</td>
</tr>
</table>
