/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// clang-format off
// READ ALL "DIFFNOTE"
// Option: with debug mode
// Option: holds state

// Dimensions represented through different scenarios:
// - Initialization
// - Write API, including CoreCoords
// - Soft reset
// - TopologyDiscovery
// - Reading telemetry
// - SysmemManager

// Device is the only layer, both servicing our main basic device that can be used by everyone, and the user which is not concerned with low level details
//  - Device has many entries and maximum flexibility
//  - Device builds state by itself however far it cans.
//  - Easy to getting started for a non power user, but still offers flexibility to a power user.

Device(int pci_num, num_host_channels, bool debug_mode = false) {
    // DIFFNOTE: device is trying automatically to initialize as far as it can go
    debug_mode_ = debug_mode;
    init_all(num_host_channels);
    // DIFFNOTE: This code also intializes workload optimization stuff. Probably a good idea to have an argument to skip such init.
    // init static TLBs
    // other optimization steps if needed
}

Device(Device* other_device, target_endpoint) {
    // creates an eth device through other_device
}

bool Device::init_all(num_host_channels) {
    if (!init_arc()) {
        return false;
    }
    if (!init_dram()) {
        return false;
    }
    if (!init_eth()) {
        return false;
    }

    // DIFFNOTE: Device holds these helper structures automatically if it can initialize them
    SocDescriptor soc_desc = SocDescriptor(this);
    ArcTelemetry arc_telemetry = ArcTelemetry(this);
    SysmemManager sysmem_manager = SysmemManager(this, num_host_channels);
    return true;
}

bool Device::init_arc() {
    // code here
    if (some failure) {
        // DIFFNOTE: Many functions have this simple check to alter behavior based on debug mode.
        if (debug_mode_) {
            return false;
        } else {
            throw
        }
    }
    return true;
}

bool Device::init_dram() {
    // code here
    if (some failure) {
        if (debug_mode_) {
            return false;
        } else {
            throw
        }
    }
    return true;
}

bool Device::init_eth() {
    // code here
    if (some failure) {
        if (debug_mode_) {
            return false;
        } else {
            throw
        }
    }
    return true;
}

void Device::write(tt_xy_pair xy_pair_in_translated_coords, ...) {
    // code here
}

bool Device::write(CoreCoord core_coord, ...) {
    if (!soc_desc) {
        if (debug_mode_) {
            return false;
        } else {
            throw
        }
    }
    write(soc_desc.translate(core_coord), ...)
}

bool Device::write_sysmem(...) {
    if (!sysmem_manager) {
        if (debug_mode_) {
            return false;
        } else {
            throw
        }
    }
    sysmem_manager->write();
}

void Device::soft_reset_reg(uint32_t val) {
    // write val to reg
}

// DIFFNOTE: Note that this layer would hold both low level and higher level concepts
// This can be seen here through start_tensix function. 
//   - Note that we HAVE TO expose soft_reset_reg function due to client 4 which wants bare access and able to set random values here.
//   - Otherwise we limit the flexibility of our driver
// This can be seen above for write functions as well, having both xy and corecoord entries.
Device::start_tensix(SoftResetEnum val) { soft_reset_reg(...) }
Device::stop_tensix() { }

bool Device::start() {
    if (!is_healthy) {
        if (debug_mode_) {
            return false;
        } else {
            throw
        }
    }
    // raises AICLK
    // maps host memory to NOC
    // has a lock to signal other processes that no other workload should run in parallel.
    // potentially start all tensix/eth?
}

// TopologyDiscovery is a utility class on top of device
// As such, it has to offer very flexible functionality in order to be useful for all clients
// DIFFNOTE: Note that effectivelly, this is like in the option 1 creating a TopologyDiscovery over WorkloadDevice.
// TopologyDiscovery now needs to take some additional parameters due to that.
<ClusterDescriptor, HealthState, list<Device>>TopologyDiscovery::discover(debug_mode=false, num_host_channels, sdesc_path, ...) {
    HealthState health_state;
    while(chip in chips_to_discover) {
        chip = Device(debug_mode, num_host_channels, sdesc_path, ...);
        // DIFFNOTE: This code is simpler here, since the init code is hidden inside Device class.
        health_state = chip.get_health_state();
        if (health_state[chip].is_healthy) {
            chips_to_discover += discover_remote_chips(chip);
        }
        // fill cluster descriptor
    }
    return cluster_descriptor, health_state, chips;
}

// Workload cluster is a layer on the same level as WorkloadDevice, meaning it has the same focus and functionality.
// WorkloadCluster is a collection of WorkloadDevices, but also offers functionality reserved for a collection of devices.

WorkloadCluster::WorkloadCluster(debug_mode = false, num_host_channels, sdesc_path, ...) {
    cluster_desc, health_states, devices = TopologyDiscovery::discover(debug_mode, num_host_channels, sdesc_path, ...);

    // DIFFNOTE: since the chips are already created during topo discovery, we can just consume them here.
    devices_ = devices;
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
    health_state, devices = TopologyDiscovery(debug_mode=true);
    for device in devices {
        if (!health_state[device].is_arc_healthy) {
            log(arc not healthy for device);
            // DIFFNOTE: don't have to initiailze telemetry.
        }
        if (!health_state[device].is_dram_healthy) {
            log(dram not healthy for device);
        }
        if (!health_state[device].is_eth_healthy) {
            log(eth not healthy for device);
        }
        // DIFFNOTE: don't have to initiailze soc desc or whatever.
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
    // DIFFNOTE: Note that this function still needs to exist in very similar form, even if we didn't have to initialize the soc_desc.
    // Likely, the client would customize how it reponds in this scenario (of soc_desc not being able to initialize)
    if (devices[i].is_healthy() && devices[i].get_soc_desc()) {
        devices[i].write(devices[i].get_soc_desc().translate_core_coord(core_coord));
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
        // DIFFNOTE: we don't have to init soc_desc manually here, but we do need to grab it to get all unharvested cores.
        // soc_desc = SocDescriptor(device);
        soc_desc = device.get_soc_descriptor();
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

// DIFFNOTE: This example should demonstrate that going with option 2 DOES NOT SIMPLIFY client code. It does make our code less clean.
