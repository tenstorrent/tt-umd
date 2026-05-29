# UMD Workload API Layer Mapping

## Cluster → Chip → TTDevice

<table>
<tr>
<th><b>API</b></th>
<th><b>Parameters / Return</b></th>
<th><b>UMD Workload</b></th>
<th><b>UMD Base Layer</b></th>
<th><b>Description</b></th>
</tr>
<tr>
<td>constructor()</td>
<td><b>Parameters:</b> <br> - options: <code>ClusterOptions</code> — chip type, sdesc path, target devices, cluster descriptor, simulator directory, IO device type, topology discovery options (all defaulted) <br> <b>Returns:</b> <br> - N/A</td>
<td>1. Create ChipIds and pair with TTDevices <br> 2. Construct Chips from TTDevices <br> 3. Construct Cluster</td>
<td><b><code>TTDevice(TTDeviceModel)</code></b></td>
<td>Creates ChipIds, creates TTDevices via factory, wraps each in a Chip, assembles Cluster. <br><br> <b>Note:</b> Backend selection (silicon/emu/sim/mock) belongs in TTDevice factory, not in Cluster constructor switch case.</td>
</tr>
<tr>
<td>destructor()</td>
<td>—</td>
<td>1. Delete ClusterDescriptor</td>
<td>—</td>
<td>Destroys Cluster and its owned resources. Manual cleanup should become automatic RAII.</td>
</tr>
<tr>
<td>create_cluster_descriptor()</td>
<td><b>Parameters:</b> <br> - sdesc_path: <code>const std::string&</code> — SOC descriptor path (default: <code>{}</code>) <br> - device_type: <code>IODeviceType</code> — transport type (default: <code>PCIe</code>) <br> - topology_discovery_options: <code>const TopologyDiscoveryOptions&</code> — discovery options (default: <code>{}</code>) <br> <b>Returns:</b> <br> - <code>std::unique_ptr&lt;ClusterDescriptor&gt;</code></td>
<td>1. Modify parameters and call TopologyDiscovery (<code>TopologyDiscovery::discover()</code>)</td>
<td><b><code>TopologyDiscovery::discover()</code></b></td>
<td>Static factory. Invokes topology discovery and constructs ClusterDescriptor. Target: split into TopologyDiscoveryDescriptor (from discovery) + Cluster-level data (enriched by Cluster).</td>
</tr>
<tr>
<td>get_cluster_descriptor()</td>
<td><b>Returns:</b> <br> - <code>ClusterDescriptor*</code></td>
<td>1. Return cached ClusterDescriptor</td>
<td>—</td>
<td>Returns cached ClusterDescriptor. Pointer is valid until next <code>refresh_cluster_descriptor()</code> call.</td>
</tr>
<tr>
<td>refresh_cluster_descriptor()</td>
<td><b>Returns:</b> <br> - <code>void</code></td>
<td>1. Re-run <code>get_cluster_descriptor()</code> <br> 2. Move new ClusterDescriptor into Cluster <br> 3. Update dependent mappings</td>
<td>—</td>
<td>Refreshes cached ClusterDescriptor and updates internal mappings. Only supported for SILICON chip type.</td>
</tr>
<tr>
<td>get_target_device_ids()</td>
<td><b>Returns:</b> <br> - <code>std::set&lt;ChipId&gt;</code></td>
<td>1. Return cached device ordinals</td>
<td>—</td>
<td>Returns all device IDs in the cluster. Cached at construction.</td>
</tr>
<tr>
<td>get_target_mmio_device_ids()</td>
<td><b>Returns:</b> <br> - <code>std::set&lt;ChipId&gt;</code></td>
<td>1. Return cached local device ordinals</td>
<td>—</td>
<td>Returns device IDs for locally connected (PCIe) devices. Cached at construction.</td>
</tr>
<tr>
<td>get_target_remote_device_ids()</td>
<td><b>Returns:</b> <br> - <code>std::set&lt;ChipId&gt;</code></td>
<td>1. Return cached remote device ordinals</td>
<td>—</td>
<td>Returns device IDs for remote (ethernet-connected) devices. Cached at construction.</td>
</tr>
<tr>
<td>get_soc_descriptor()</td>
<td><b>Parameters:</b> <br> - chip_id: <code>ChipId</code> — chip to query <br> <b>Returns:</b> <br> - <code>const SocDescriptor&</code></td>
<td>1. Return descriptor via Chip (<code>TTDevice::get_soc_descriptor()</code>)</td>
<td><b><code>TTDevice::get_soc_descriptor()</code></b></td>
<td>Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>set_barrier_address_params()</td>
<td><b>Parameters:</b> <br> - barrier_address_params: <code>const BarrierAddressParams&</code> — L1 and DRAM barrier addresses <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Set barrier parameters on Chip (<code>Chip::set_barrier_address_params()</code>)</td>
<td>—</td>
<td>Sets L1/DRAM barrier addresses on the target Chip. Workload-only — no base layer interaction.</td>
</tr>
<tr>
<td>configure_active_ethernet_cores_for_mmio_device()</td>
<td><b>Parameters:</b> <br> - mmio_chip: <code>ChipId</code> — MMIO device to target <br> - active_eth_cores_per_chip: <code>const std::unordered_set&lt;CoreCoord&gt;&</code> — active ethernet cores <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Designate ethernet cores for remote transfer (LocalChip ↔ RemoteChip) <br> 2. Set ethernet cores used for broadcast on LocalChip</td>
<td><b><code>RemoteCommunication::set_remote_transfer_ethernet_cores()</code></b></td>
<td>Configures which ethernet cores are used for remote transfer and broadcast. Sets up both sides of LocalChip ↔ RemoteChip connection. <br><br> <b>Note:</b> RemoteCommunication is out-of-scope for the base API specification — not relevant for IP clients without remote devices.</td>
</tr>
<tr>
<td>start_device()</td>
<td><b>Parameters:</b> <br> - device_params: <code>const DeviceParams&</code> — initialization configuration <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::start_device()</code> per chip <br> 2. Call <code>deassert_resets_and_set_power_state()</code>: <br> &nbsp;&nbsp;• Put tensix into reset (<code>TTDevice::assert_risc_reset()</code>) <br> &nbsp;&nbsp;• Put tensix out of reset (<code>TTDevice::deassert_risc_reset()</code>) <br> &nbsp;&nbsp;• Enable eth queues (<code>TTDevice::send_device_command()</code>) <br> &nbsp;&nbsp;• Set AI clock (<code>TTDevice::set_clock_state()</code>)</td>
<td><b><code>TTDevice::assert_risc_reset()</code></b> <br> <b><code>TTDevice::deassert_risc_reset()</code></b> <br> <b><code>TTDevice::send_device_command()</code></b> <br> <b><code>TTDevice::set_clock_state()</code></b></td>
<td>Session bring-up across all chips. Calls Chip::start_device() then performs cluster-wide reset/clock sequence. Via Chip (see Chip table). <br><br> <b>Note:</b> <code>deassert_resets_and_set_power_state()</code> duplicates steps already in <code>Chip::start_device()</code>. Target: Chip owns full sequence, Cluster can disable per-chip steps when broadcast is available (e.g. <code>broadcast_tensix_risc_reset_to_cluster()</code>).</td>
</tr>
<tr>
<td>stop_device() <br> <i>currently: <code>close_device()</code></i></td>
<td><b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::stop_device()</code> on remote chips first <br> 2. Call <code>Chip::stop_device()</code> on MMIO chips second <br><br> Ordering is important — remote chips must be stopped before local chips</td>
<td>—</td>
<td>Session teardown across all chips. Via Chip (see Chip table). Remote chips stopped first, then local. Mirror of start_device().</td>
</tr>
<tr>
<td>set_clock_state() <br> <i>currently: <code>set_power_state()</code></i></td>
<td><b>Parameters:</b> <br> - state: <code>PowerState</code> — target clock state <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::set_clock_state()</code></td>
<td><b><code>TTDevice::set_clock_state()</code></b></td>
<td>Sets AICLK frequency. Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>assert_risc_reset() (broadcast)</td>
<td><b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>broadcast_tensix_risc_reset_to_cluster()</code> with assert parameter</td>
<td>—</td>
<td>Cluster-wide broadcast reset. Workload-layer orchestration — iterates over chips calling base primitives.</td>
</tr>
<tr>
<td>deassert_risc_reset() (broadcast)</td>
<td><b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>broadcast_tensix_risc_reset_to_cluster()</code> with deassert parameter</td>
<td>—</td>
<td>Cluster-wide broadcast deassert. Workload-layer orchestration — iterates over chips calling base primitives.</td>
</tr>
<tr>
<td>assert_risc_reset_at_core()</td>
<td><b>Parameters:</b> <br> - chip: <code>const ChipId</code> — chip to target <br> - core: <code>const CoreCoord</code> — core to target <br> - soft_resets: <code>const TensixSoftResetOptions&</code> — reset configuration (default: <code>TENSIX_ASSERT_SOFT_RESET</code>) <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::assert_risc_reset()</code> on target chip/core</td>
<td><b><code>TTDevice::assert_risc_reset()</code></b></td>
<td>Old per-core API using <code>TensixSoftResetOptions</code>. Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>deassert_risc_reset_at_core()</td>
<td><b>Parameters:</b> <br> - chip: <code>const ChipId</code> — chip to target <br> - core: <code>const CoreCoord</code> — core to target <br> - soft_resets: <code>const TensixSoftResetOptions&</code> — reset configuration (default: <code>TENSIX_DEASSERT_SOFT_RESET</code>) <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::deassert_risc_reset()</code> on target chip/core</td>
<td><b><code>TTDevice::deassert_risc_reset()</code></b></td>
<td>Old per-core API using <code>TensixSoftResetOptions</code>. Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>get_risc_reset_state()</td>
<td><b>Parameters:</b> <br> - chip: <code>const ChipId</code> — chip to target <br> - core: <code>const CoreCoord</code> — core to target <br> <b>Returns:</b> <br> - <code>RiscType</code></td>
<td>1. Call <code>Chip::get_risc_reset_state()</code> on target chip/core</td>
<td><b><code>TTDevice::get_risc_reset_state()</code></b></td>
<td>Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>assert_risc_reset() (per core)</td>
<td><b>Parameters:</b> <br> - chip: <code>const ChipId</code> — chip to target <br> - core: <code>const CoreCoord</code> — core to target <br> - risc_type: <code>const RiscType</code> — which riscs to assert <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::assert_risc_reset()</code> on target chip/core</td>
<td><b><code>TTDevice::assert_risc_reset()</code></b></td>
<td>New per-core API using <code>RiscType</code>. Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>deassert_risc_reset() (per core)</td>
<td><b>Parameters:</b> <br> - chip: <code>const ChipId</code> — chip to target <br> - core: <code>const CoreCoord</code> — core to target <br> - risc_type: <code>const RiscType</code> — which riscs to deassert <br> - staggered_start: <code>bool</code> — stagger risc start times (default: <code>true</code>) <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::deassert_risc_reset()</code> on target chip/core</td>
<td><b><code>TTDevice::deassert_risc_reset()</code></b></td>
<td>New per-core API using <code>RiscType</code>. Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>write_to_data() <br> <i>currently: <code>write_to_device()</code></i></td>
<td><b>Parameters:</b> <br> - mem_ptr: <code>const void*</code> — source data <br> - size_in_bytes: <code>size_t</code> — bytes to write <br> - chip: <code>ChipId</code> — chip to target <br> - core: <code>CoreCoord</code> — core to target <br> - addr: <code>uint64_t</code> — destination address <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::write_data()</code> on target chip</td>
<td><b><code>TTDevice::write_data()</code></b></td>
<td>Passthrough facade via Chip (see Chip table). Data path (WC-mapped).</td>
</tr>
<tr>
<td>read_from_data() <br> <i>currently: <code>read_from_device()</code></i></td>
<td><b>Parameters:</b> <br> - mem_ptr: <code>void*</code> — destination buffer <br> - chip: <code>ChipId</code> — chip to target <br> - core: <code>CoreCoord</code> — core to target <br> - addr: <code>uint64_t</code> — source address <br> - size: <code>size_t</code> — bytes to read <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::read_data()</code> on target chip</td>
<td><b><code>TTDevice::read_data()</code></b></td>
<td>Passthrough facade via Chip (see Chip table). Data path (WC-mapped).</td>
</tr>
<tr>
<td>write_to_regs() <br> <i>currently: <code>write_to_device_reg()</code></i></td>
<td><b>Parameters:</b> <br> - mem_ptr: <code>const void*</code> — source data <br> - size_in_bytes: <code>uint32_t</code> — bytes to write <br> - chip: <code>ChipId</code> — chip to target <br> - core: <code>CoreCoord</code> — core to target <br> - addr: <code>uint64_t</code> — register address <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::write_ctrl()</code> on target chip</td>
<td><b><code>TTDevice::write_ctrl()</code></b></td>
<td>Passthrough facade via Chip (see Chip table). Control path (UC-mapped).</td>
</tr>
<tr>
<td>read_from_regs() <br> <i>currently: <code>read_from_device_reg()</code></i></td>
<td><b>Parameters:</b> <br> - mem_ptr: <code>void*</code> — destination buffer <br> - chip: <code>ChipId</code> — chip to target <br> - core: <code>CoreCoord</code> — core to target <br> - addr: <code>uint64_t</code> — register address <br> - size: <code>uint32_t</code> — bytes to read <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::read_ctrl()</code> on target chip</td>
<td><b><code>TTDevice::read_ctrl()</code></b></td>
<td>Passthrough facade via Chip (see Chip table). Control path (UC-mapped).</td>
</tr>
<tr>
<td>write_to_core_range() <br> <i>currently: <code>noc_multicast_write()</code></i></td>
<td><b>Parameters:</b> <br> - dst: <code>void*</code> — source buffer <br> - size: <code>size_t</code> — bytes to write <br> - chip: <code>ChipId</code> — chip to target <br> - core_start: <code>CoreCoord</code> — start of core range <br> - core_end: <code>CoreCoord</code> — end of core range <br> - addr: <code>uint64_t</code> — destination address on each core <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::write_to_core_range()</code> on target chip</td>
<td><b><code>TTDevice::write_to_core_range()</code></b></td>
<td>Passthrough facade via Chip (see Chip table). NOC multicast.</td>
</tr>
<tr>
<td>dma_write_to_device()</td>
<td><b>Parameters:</b> <br> - src: <code>const void*</code> — source data <br> - size: <code>size_t</code> — bytes to write <br> - chip: <code>ChipId</code> — chip to target (must be local) <br> - core: <code>CoreCoord</code> — core to target <br> - addr: <code>uint64_t</code> — destination address <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::dma_write()</code> on target chip</td>
<td><b><code>TTDevice::dma_write()</code></b></td>
<td>Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>dma_read_from_device()</td>
<td><b>Parameters:</b> <br> - dst: <code>void*</code> — destination buffer <br> - size: <code>size_t</code> — bytes to read <br> - chip: <code>ChipId</code> — chip to target (must be local) <br> - core: <code>CoreCoord</code> — core to target <br> - addr: <code>uint64_t</code> — source address <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::dma_read()</code> on target chip</td>
<td><b><code>TTDevice::dma_read()</code></b></td>
<td>Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>dma_write_to_core_range()</td>
<td><b>Parameters:</b> <br> - src: <code>void*</code> — source data <br> - size: <code>size_t</code> — bytes to write <br> - chip: <code>ChipId</code> — chip to target (must be local) <br> - core_start: <code>CoreCoord</code> — start of core range <br> - core_end: <code>CoreCoord</code> — end of core range <br> - addr: <code>uint64_t</code> — destination address on each core <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::dma_write_to_core_range()</code> on target chip</td>
<td><b><code>TTDevice::dma_write_to_core_range()</code></b></td>
<td>Passthrough facade via Chip (see Chip table). DMA multicast.</td>
</tr>
<tr>
<td>ethernet_broadcast_write() <br> <i>currently: <code>broadcast_write_to_cluster()</code></i></td>
<td><b>Parameters:</b> <br> - mem_ptr: <code>const void*</code> — data to write <br> - size_in_bytes: <code>uint32_t</code> — bytes to write <br> - address: <code>uint64_t</code> — destination address <br> - chips_to_exclude: <code>const std::set&lt;ChipId&gt;&</code> — chips to skip <br> - rows_to_exclude: <code>std::set&lt;uint32_t&gt;&</code> — NOC0 rows to skip <br> - columns_to_exclude: <code>std::set&lt;uint32_t&gt;&</code> — NOC0 columns to skip <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>tensix_or_eth_in_broadcast()</code> <br> 2. Call <code>ethernet_broadcast_write()</code></td>
<td>—</td>
<td>Broadcasts data to remote chips via ethernet. Current implementation mixes architecture-specific logic (<code>tensix_or_eth_in_broadcast()</code>) with broadcast mechanics. <br><br> <b>Note:</b> Low priority — no remote chips for IP clients. Should be encapsulated into a dedicated class.</td>
</tr>
<tr>
<td>l1_membar()</td>
<td><b>Parameters:</b> <br> - chip: <code>const ChipId</code> — chip to target <br> - cores: <code>const std::unordered_set&lt;CoreCoord&gt;&</code> — cores to barrier (default: <code>{}</code> = all) <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::l1_membar()</code> on target chip</td>
<td>—</td>
<td>Passthrough facade. Cluster → Chip. Workload-only — no base layer interaction.</td>
</tr>
<tr>
<td>dram_membar()</td>
<td><b>Parameters (overload 1):</b> <br> - chip: <code>const ChipId</code> — chip to target <br> - channels: <code>const std::unordered_set&lt;uint32_t&gt;&</code> — DRAM channels to barrier <br> <b>Parameters (overload 2):</b> <br> - chip: <code>const ChipId</code> — chip to target <br> - cores: <code>const std::unordered_set&lt;CoreCoord&gt;&</code> — DRAM cores to barrier (default: <code>{}</code> = all) <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::dram_membar()</code> on target chip</td>
<td>—</td>
<td>Passthrough facade. Cluster → Chip. Workload-only — no base layer interaction.</td>
</tr>
<tr>
<td>wait_for_non_mmio_flush()</td>
<td><b>Parameters (overload 1):</b> <br> - none (all chips) <br> <b>Parameters (overload 2):</b> <br> - chip_id: <code>const ChipId</code> — chip to target <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Overload 1: Call <code>Chip::wait_for_non_mmio_flush()</code> on all chips <br> 2. Overload 2: Call <code>Chip::wait_for_non_mmio_flush()</code> on target chip</td>
<td><b><code>TTDevice::wait_for_non_mmio_flush()</code></b></td>
<td>Ethernet barrier. Flushes all in-flight ethernet transactions. Via Chip (see Chip table). No-op for local chips, real implementation for remote.</td>
</tr>
<tr>
<td>write_to_sysmem()</td>
<td><b>Parameters:</b> <br> - mem_ptr: <code>const void*</code> — data to write <br> - size: <code>uint32_t</code> — bytes to write <br> - offset: <code>uint64_t</code> — offset into system memory buffer <br> - src_device_id: <code>ChipId</code> — chip to target <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::write_to_sysmem()</code> on target chip</td>
<td>—</td>
<td>Passthrough facade. Cluster → Chip → SystemMemoryBuffer. Workload-only — no base layer interaction at call time.</td>
</tr>
<tr>
<td>read_from_sysmem()</td>
<td><b>Parameters:</b> <br> - mem_ptr: <code>void*</code> — destination buffer <br> - offset: <code>uint64_t</code> — offset into system memory buffer <br> - size: <code>uint32_t</code> — bytes to read <br> - src_device_id: <code>ChipId</code> — chip to target <br> <b>Returns:</b> <br> - <code>void</code></td>
<td>1. Call <code>Chip::read_from_sysmem()</code> on target chip</td>
<td>—</td>
<td>Passthrough facade. Cluster → Chip → SystemMemoryBuffer. Workload-only — no base layer interaction at call time.</td>
</tr>
<tr>
<td>host_dma_address()</td>
<td><b>Parameters:</b> <br> - offset: <code>uint64_t</code> — offset into system memory buffer <br> - src_device_id: <code>ChipId</code> — device to target <br> <b>Returns:</b> <br> - <code>void*</code></td>
<td>1. Call <code>Chip::host_dma_address()</code> on target chip</td>
<td>—</td>
<td>Returns the IOVA for a given buffer offset. Maps to <code>SystemMemoryBuffer::get_iova()</code> plus the caller-provided offset.</td>
</tr>
<tr>
<td>get_pcie_base_addr_from_device()</td>
<td><b>Parameters:</b> <br> - chip_id: <code>const ChipId</code> — chip to target <br> <b>Returns:</b> <br> - <code>uint64_t</code></td>
<td>1. Call <code>Chip::get_pcie_base_addr_from_device()</code> on target chip</td>
<td><b><code>TTDevice::get_pcie_base_addr_from_device()</code></b></td>
<td>Passthrough facade via Chip (see Chip table). <br><br> <b>Note:</b> To be renamed. Maps to <code>SystemMemoryBuffer::get_noc_address()</code> — the NOC base address for the system memory buffer as seen from the device's PCIe tile.</td>
</tr>
<tr>
<td>send_device_command() <br> <i>currently: <code>arc_msg()</code></i></td>
<td><b>Parameters:</b> <br> - logical_device_id: <code>int</code> — chip to target <br> - msg_code: <code>uint32_t</code> — command code <br> - args: <code>const std::vector&lt;uint32_t&gt;&</code> — command arguments (default: <code>{}</code>) <br> - timeout_ms: <code>std::chrono::milliseconds</code> — timeout (default: ARC_MESSAGE_TIMEOUT) <br> <b>Returns (target):</b> <br> - <code>DeviceCommandResult{exit_code, return_values}</code></td>
<td>1. Call <code>Chip::send_device_command()</code> on target chip</td>
<td><b><code>TTDevice::send_device_command()</code></b></td>
<td>Passthrough facade via Chip (see Chip table). See Chip table for <code>send_device_command()</code> details.</td>
</tr>
<tr>
<td>get_clocks()</td>
<td><b>Returns:</b> <br> - <code>std::map&lt;int, int&gt;</code> — clock freq per MMIO device</td>
<td>1. Iterate all MMIO chips, call <code>Chip::get_clock_freq()</code> on each <br> 2. Collect into map</td>
<td><b><code>TTDevice::get_clock_freq()</code></b></td>
<td>Collects AICLK frequency from all MMIO devices. Not a per-chip passthrough — iterates all local chips and aggregates results.</td>
</tr>
<tr>
<td>get_numa_node_for_pcie_device()</td>
<td><b>Parameters:</b> <br> - device_id: <code>uint32_t</code> — device to query <br> <b>Returns:</b> <br> - <code>uint32_t</code></td>
<td>1. Call <code>Chip::get_numa_node()</code> on target chip</td>
<td><b><code>TTDevice::get_numa_node()</code></b></td>
<td>Passthrough facade via Chip (see Chip table).</td>
</tr>
<tr>
<td>get_ethernet_firmware_version()</td>
<td><b>Returns:</b> <br> - <code>std::optional&lt;SemVer&gt;</code></td>
<td>1. Return cached value from ClusterDescriptor</td>
<td>—</td>
<td>Returns cached ethernet firmware version. Convenience getter over ClusterDescriptor.</td>
</tr>
<tr>
<td>get_firmware_bundle_version()</td>
<td><b>Returns:</b> <br> - <code>std::optional&lt;FirmwareBundleVersion&gt;</code></td>
<td>1. Return cached value from ClusterDescriptor</td>
<td>—</td>
<td>Returns cached firmware bundle version. Convenience getter over ClusterDescriptor.</td>
</tr>
<tr>
<td>get_chip()</td>
<td><b>Parameters:</b> <br> - device_id: <code>ChipId</code> — device to target <br> <b>Returns:</b> <br> - <code>Chip*</code></td>
<td>1. Return Chip by device ID</td>
<td>—</td>
<td>Accessor. Returns Chip pointer from internal map.</td>
</tr>
<tr>
<td>get_local_chip()</td>
<td><b>Parameters:</b> <br> - device_id: <code>ChipId</code> — device to target <br> <b>Returns:</b> <br> - <code>LocalChip*</code></td>
<td>1. Return LocalChip by device ID</td>
<td>—</td>
<td>Accessor. Returns LocalChip pointer from internal map. Asserts chip is local.</td>
</tr>
<tr>
<td>get_remote_chip()</td>
<td><b>Parameters:</b> <br> - device_id: <code>ChipId</code> — device to target <br> <b>Returns:</b> <br> - <code>RemoteChip*</code></td>
<td>1. Return RemoteChip by device ID</td>
<td>—</td>
<td>Accessor. Returns RemoteChip pointer from internal map. Asserts chip is remote.</td>
</tr>
<tr>
<td>get_tt_device()</td>
<td><b>Parameters:</b> <br> - device_id: <code>ChipId</code> — device to target <br> <b>Returns:</b> <br> - <code>TTDevice*</code></td>
<td>1. Get TTDevice via Chip</td>
<td>—</td>
<td>Accessor. Returns TTDevice pointer from Chip. Workload convenience — no base layer call.</td>
</tr>
</table>
