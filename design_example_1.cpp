/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// clang-format off
// Option: without debug mode
// Option: forcing complete statelessness

// Dimensions represented through different scenarios:
// - Initialization
// - Write API, including CoreCoords
// - Soft reset
// - TopologyDiscovery
// - Reading telemetry
// - SysmemManager

// Device is our main basic device, can be used by everyone
//  - Device has many entries and maximum flexibility
//  - Device is strictly stateless, you have to build state manually

Device(int pci_num) {
    // creates pci based device
}

Device(Device* other_device, target_endpoint) {
    // creates an eth device through other_device
}

bool Device::init_all() {
    if (!init_arc()) {
        return false;
    }
    if (!init_dram()) {
        return false;
    }
    if (!init_eth()) {
        return false;
    }
    return true;
}

bool Device::init_arc() {
    // code here
    return true;
}

bool Device::init_dram() {
    // code here
    return true;
}

bool Device::init_eth() {
    // code here
    return true;
}

void Device::write(tt_xy_pair xy_pair_in_translated_coords) {
    // code here
}

void Device::soft_reset_reg(uint32_t val) {
    // write val to reg
}

// These are functional classes built on top of basic driver class.
// ArcTelemetry is for discussion whether it should be part of Device or not, as other classes. 
// ArcTelemetry class is tied to the chip management firmware running, which of course defines how the basic device functions, so it is probably very hard to divide or even impossible.
ArcTelemetry(Device device*) {}
SocDescriptor(Device device*) {}
SysmemManager(Device device*, num_channels) {}

// TopologyDiscovery is a utility class on top of device
// As such, it has to offer very flexible functionality in order to be useful for all clients

<ClusterDescriptor, HealthState, list<Device>>TopologyDiscovery::discover() {
    HealthState health_state;
    while(chip in chips_to_discover) {
        health_state[chip].arc_is_healthy = chip::init_arc();
        health_state[chip].dram_is_healthy = chip::init_dram();
        health_state[chip].eth_is_healthy = chip::init_eth();
        health_state[chip].is_healthy = health_state[chip].arc_is_healthy && health_state[chip].dram_is_healthy && health_state[chip].eth_is_healthy;
        if (health_state[chip].is_healthy) {
            chips_to_discover += discover_remote_chips(chip);
        }
        // fill cluster descriptor
    }
    return cluster_descriptor, health_state, chips;
}


// WorkloadDevice is for users not concerned with low level details
//  - Focus is on the ease of getting started
//  - Limited flexibility, you have to use it in one way.
//  - Not usable if device is not functional.
//  - low level details are abstracted
// It might make sense for ReadyDevice and WorkloadDevice to be separate. However, I'm not convinced at the moment that either of those is fat enough to warrant its own existance, or that we have proper scenarios.
// Through WorkloadDevice, you're glueing a couple of classes together into a bigger block: Device, SocDescriptor, SystemMemory, ?FirmwareInfoProvider, ?ArcTelemetry

WorkloadDevice(logical_chip_id, num_host_channels, sdesc_path, ...) {
    // Creates either a local or remote device, based on which logical chip identifier is given.
    Device device;
    ASSERT(device.init_all());
    soc_desc = SocDescriptor(device);
    sysmem_manager = SysmemManager(device, num_host_channels);
    // init static TLBs
    // other optimization steps if needed

}

WorkloadDevice::write(CoreCoord core_coord, ...) {
    device.write(soc_desc.translate_core_coord(core_coord), ...);
}

WorkloadDevice::write_sysmem(...) { sysmem_manager.write(...); }

WorkloadDevice::start_tensix(SoftResetEnum val) { }
WorkloadDevice::stop_tensix() { }

WorkloadDevice::start() {
    // raises AICLK
    // maps host memory to NOC
    // has a lock to signal other processes that no other workload should run in parallel.
    // potentially start all tensix/eth?
}

WorkloadDevice::stop() { ... }

// Workload cluster is a layer on the same level as WorkloadDevice, meaning it has the same focus and functionality.
// WorkloadCluster is a collection of WorkloadDevices, but also offers functionality reserved for a collection of devices.

WorkloadCluster::WorkloadCluster(num_host_channels, sdesc_path, ...) {
    cluster_desc, health_state = TopologyDiscovery::discover();
    ASSERT(health_state.is_healthy);
    for chip in cluster_desc.chips {
        chip = WorkloadDevice(chip_id, num_host_channels, sdesc_path, ...);
    }

    
}

WorkloadCluster::get_chip(logical_chip_id) { 
    return chips[logical_chip_id]; // You can do whatever you would with a single WorkloadDevice here.
}    

WorkloadCluster::warm_reset() {
    WarmReset(cluster_desc.chips);
}


// Client 1: tt-metal runtime
// - Init everything
// - Fail if anything is not functional
// Focus:
// - Simple usage
// - Flexibility: fine if limited, they only want what they use

WorkloadCluster cluster();

num_chips = cluster.get_cluster_desc().get_chips();
cluser.get_chip(i).write(...);
cluster.start(...);
cluster.stop(...);
telem_value = cluster.get_chip(i).get_arc_telemetry().get_telemetry_entry(...);


// Client 2: exalens
// - Init everything
// - Provide a mechanism to see health, and work on unhealthy devices
// Focus:
// - Power user, fine to have more complex usage
// - Flexibility: More is better
// - They already have their own Chip/Cluster abstraction around our API.

ExalensCluster() {
    health_state, devices = TopologyDiscovery();
    for device in devices {
        if (!health_state[device].is_arc_healthy) {
            log(arc not healthy for device);
            telemetries.append(ArcTelemetry(device));
        }
        if (!health_state[device].is_dram_healthy) {
            log(dram not healthy for device);
        }
        if (!health_state[device].is_eth_healthy) {
            log(eth not healthy for device);
        }
        if device.is_healthy() {
            soc_descs.append(SocDescriptor(device));
            // Do we even need sysmem?
        }
    }
}

ExalensCluster::write(i, xy_pair) {
    if (devices[i].is_healthy()) {
        devices[i].write(xy_pair);
    } else {
        log(device not healthy);
    }
}

ExalensCluster::write(i, CoreCoord) {
    if (devices[i].is_healthy() && soc_descs[i]) {
        devices[i].write(soc_descs[i].translate_core_coord(core_coord));
    } else {
        log(device not healthy);
    }
}


// Client 3: control-plane
// - Finer init granularity, in particular for eth
// - Maybe only healthy maybe not
// Focus:
// - Power user, fine to have more complex usage
// - Flexibility: More is better
// - They already have their ControlPlane abstraction with specific flow and functionality.
// - They dont use tt_metal tt_Cluster, they use umd::Cluster currently.

ControlPlane() {
    health_state, devices = TopologyDiscovery(.skip_eth_wait);
    for device in devices {
        assert(health_state[device].is_arc_healthy && health_state[device].is_dram_healthy);
    }
}

ControlPlane()::wait_all_eth() {
    for device in devices {
        device.wait_all_eth()
    }
}

ControlPlane()::some_random_eth_config() {
    for device in devices {
        soc_desc = SocDescriptor(device);
        for unharvested_eth_core in soc_desc.get_eth_cores() {
            device.write(unharvested_eth_core, ...);
        }
    }
}

// Client 4: syseng
// - Don't do anything, send pci transactions, do bare minimum through kmd
// Focus:
// - Very power user
// - Flexibility: Is a must
// - They need to have bare access without any state, initialization, or abstractions.

?

// Client 5: topology monitor in tt-distributed
// - ?
// Focus:
// - Simple usage but over very specific scope
// - Flexibility: Fine if offered what we give them
// This one would probably fit either more like client 1 or client 3

?
