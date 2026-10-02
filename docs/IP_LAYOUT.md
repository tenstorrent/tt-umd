# IP layout

`IpLayout` ([ip_layout.hpp](../device/api/umd/device/topology/ip_layout.hpp)) describes how an IP's access points split it into devices.
The access points may be reached over one or more host connections (e.g. `/dev/tenstorrent/<N>` fds, or one simulator run).
Mapping an access point to its connection is up to the implementation.

- **Access point:** a host access resource over which the IP is reached, identified by `AccessPointId`. The id is an opaque ordinal (`0..N-1`) and does not correspond to a location in the IP.
- **Device:** one ATT view, identified by `IpDeviceId`, an ordinal within the IP. `get_devices()` maps each device to the access points that see that view, which form the pool of resources for one TTDevice. `IpDeviceId` is neither an mmio id nor a `ChipId`: Cluster assigns a `ChipId` per (IP, `IpDeviceId`).
- **Location:** `get_location()` returns an access point's `coord` and `flat_address`. Each field is set once known:
  - `coord`: the access point's tile, in the frame of the device it belongs to.
  - `flat_address`: the global address at which the host reaches the access point through the ATT. It is distinct for every access point. It is unset when the ATT is off (gasket mode) or when the host is coupled to the access point directly.

The layout states the expected configuration. Checking it against the ATT is a separate step.

## ATT

A device is a view of the IP set by its Address Translation Table (ATT). Every NOC endpoint (each tile, and each host bridge such as a NOC2AXI) has one:

- **Mask entries** match address ranges.
- **The endpoint table** gives the tile each range reaches.
- The rest of the address becomes the tile-local address.

Programming the ATTs so that a device's ranges reach only its own tiles splits one IP into several devices. With the ATT off (gasket mode), the host addresses tiles directly by coordinate, and the split is only a convention between the users of the access points.

## Constraints

- Current consumers assume one host connection (fd) per IP.
- The interface allows several connections, but an implementation that uses several needs rework of the components tied to the fd:
  - DMA pinning and sysmem, which are per connection: an IOVA is valid only for the fd that pinned it;
  - `TTDevice::get_communication_device_id()`, which returns the mmio id and is used as both a connection id and a device id.

## RTL simulation / emulation

`RtlSimIpLayout` reads `<simulator_directory>/ip_layout.yaml`.

Each access point has two sides:

- `host_access`: how the host reaches the access point. Its meaning is up to the implementation: in RTL simulation/emulation it names the socket (UMD serves `NNG_SOCKET_ADDR_<host_access>`, matching the simulator's device id); on silicon it could be, for example, the AXI base address of the IP block in the host's address space.
- `coord`: the access point's NOC2AXI tile, in the frame of the device it belongs to.
- `flat_address`: the access point's global address through the ATT, distinct for every access point. Omitted here: each Horizon socket is coupled directly to its column's NOC2AXI, with no shared host address space in front of it.

`host_access` is required. `coord` and `flat_address` are optional, since they may be unknown until the ATT is known.

Other rules:

- An access point's id is its position in `access_points`.
- A device's id is its `id` field, or its position in `devices` when no device has one. Give every device an `id`, or none. Ids must be `0..N-1`, each once.
- Each device needs a `soc_descriptor`. Its path is relative to `simulator_directory`, and the file must exist.

One device (the whole IP), reached over both columns' NOC2AXI tiles:

```yaml
access_points:
  - host_access: col0            # socket NNG_SOCKET_ADDR_col0
    coord: [0, 2]                # NOC2AXI tile, in the device's frame
  - host_access: col1
    coord: [1, 2]
devices:
  - access_points: [0, 1]
    soc_descriptor: soc_descriptor.yaml
```

Two devices, one per column (ATT split). Both use the same soc descriptor, since each device addresses its tiles in its own frame; the ATT behind each access point maps that frame to its column's tiles:

```yaml
access_points:
  - host_access: col0            # left column's NOC2AXI
    coord: [0, 2]
  - host_access: col1            # right column's NOC2AXI, (0, 2) in its device's frame
    coord: [0, 2]
devices:
  - access_points: [0]
    soc_descriptor: soc_descriptor.yaml
  - access_points: [1]
    soc_descriptor: soc_descriptor.yaml
```

## Opening a layout

A build with an `ip_layout.yaml` opens as one chip per device: `create_rtl_sim_ip_layout_tt_devices()` starts one `RtlSimSession` over the devices' sockets and creates one `RtlSimulationTTDevice` per device, each on its device's socket and with its device's SoC descriptor. For now a device must have exactly one access point.

## One process or several

A build with an `ip_layout.yaml` opens as one chip per device. A process opens the devices selected by `ClusterOptions::target_devices`, or else by `TT_VISIBLE_DEVICES`, renumbered from 0, and serves only their sockets.

- One process, all devices: select nothing. The process serves every socket and launches `run.sh` once.
- Several processes, each with its own devices: give each process its devices, for example `TT_VISIBLE_DEVICES=0` and `TT_VISIBLE_DEVICES=1`. Exactly one of them launches `run.sh`; set `TT_UMD_SIMULATOR_LAUNCH=0` on the others.
  - Fix every socket's port: each process sets `NNG_SOCKET_LOCAL_PORT_<host_access>` for the sockets it serves. Without it, a process picks a random port that the others can't know.
  - The launching process needs every socket's `NNG_SOCKET_ADDR_<host_access>` in its environment, the other processes' sockets included, since `run.sh` passes them all to the simulator.

## Validation

`validate_ip_layout()` throws when:

- there are no devices;
- device ids aren't `0..N-1`;
- a device has no access points, or lists one twice;
- a device uses an access point outside `0..get_num_access_points()-1`;
- an access point isn't owned by exactly one device.

It follows that the number of devices is at most the number of access points.
