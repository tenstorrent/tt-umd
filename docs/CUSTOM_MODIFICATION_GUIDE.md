# Guide for custom SoC

Note: This document can become outdated very quickly due to API changes, so always double check how up to date this is.

The User Mode Driver (UMD) is a set of low-level API calls that compilers built on top of it would call to interact with the IP.
UMD needs to maintain the expected API call signatures and implement their SoC specific function.

UMD includes several layers of abstraction and implementation. Ideally only a small layer should be specific to the exact SoC layout, but that might not be completely true currently.

## Descriptor files

SoC Descriptor is one of those components which is reusable for other grid-like SoC. More importantly, higher software stacks built on top of UMD use this layout format as a contract with UMD. See [SOC_DESCRIPTOR.md](./SOC_DESCRIPTOR.md) for more information. An example of minimal SoC
can be found in [blackhole_simulation_1x2.yaml](../tests/soc_descs/blackhole_simulation_1x2.yaml).

Another system descriptor is ClusterDescriptor. These files are describing the topology of the connected system, along with some additional chip specific details which might be important for the topology. An example of minimal cluster descriptor can be found in [grayskull_150.yaml](../tests/api/cluster_descriptor_examples/grayskull_E150.yaml). This file describes a topology of one device, without any special features, and without any harvesting.

## API changes

At the moment, most of the API that needs to be reimplemented for custom SoC is located in the [cluster.h](../device/api/umd/device/cluster.h) file. The file has header comments on all the functions defined by the tt_device, which is the current contract with higher software stacks like tt-metal.

Current implementation holds a tt_SocDescriptor for each of the chips. This needs to be implemented as well in the class derived from tt_device, since there is an API to fetch these.

For an example of how to create your tt_SocDescriptor inside your constructor, you can find an example in [soc_descriptor api tests](../tests/api/test_soc_descriptor.cpp). When creating SocDescriptor, if the device doesn't support harvesting, 0 can be passed as harvesting mask.

There is a set of functions which has CoreCoord arguments (such as write_to_device). This is the current situation due to UMD being in the middle of two phase API change. At the moment tt_metal uses the API without CoreCoord, but will switch to the new API, and then the old one will be deprecated. Currently only the APIs without CoreCoord are used.

Functions related to harvesting can return identity maps or zeros, whatever makes sense as a default. They will soon be removed from the API.

For the examples of how to implement other functions, you can view the current implementation in [cluster.cpp](../device/cluster.cpp). It provides simple example to some of the api functions, such as set_barrier_address_params, get_dram_channel_size, get_clocks, get_num_dram_channels, get_num_host_channels, set_power_state, get_clocks, etc.

### Changes needed in tt-metal

#### get_platform_architecture()
The function [get_platform_architecture](https://github.com/tenstorrent/tt-metal/blob/9edf9a0cac90d4f89262165dbe3fc4f4feac18be/tt_metal/api/tt-metalium/get_platform_architecture.hpp#L50) can be reimplemented to return any architecture (for example tt::ARCH::GRAYSKULL, or a new ARCH type if one is added) instead of the current implementation.

#### tt_cluster.cpp

Most of the interfacing with the UMD is done through this [tt_cluster.cpp](https://github.com/tenstorrent/tt-metal/blob/main/tt_metal/llrt/tt_cluster.cpp). To change the usage to a custom driver for custom device, one should exchange the creation of tt::umd::Cluster
with the class derived from tt_device. For instructions on what each of the functions represents and how to implement them, please see [cluster.h](../device/api/umd/device/cluster.h).

#### soc_descriptor in tt_metal

Soc descriptors used from tt_metal have additional fields which hold additional dram parameters related to through which ports and with which offsets it is accessed. You can just leave it with default values, [here's an example](https://github.com/tenstorrent/tt-metal/blob/783d35aea7df03f9927934ec7b9a640a8032d371/tt_metal/soc_descriptors/blackhole_simulation_1x2_arch.yaml#L14C1-L21C8).

#### config_tlb

[Configuring static TLBs](https://github.com/tenstorrent/tt-metal/blob/main/tt_metal/llrt/tt_cluster.cpp#L295) should be skipped in tt_metal.

### DRAM address offset
An assumption is made on the DRAM address offset at each DRAM coordinate in a TT
silicon device. The assumption is that the allocated buffer in the respective DRAM coordinate starts from address
0. This might not be true for custom SoC's memory mapping scheme, but it could be made this way, by simply
manipulating the AXI interconnect address map to support said offset. If this is not possible, another possibility is to
manipulate the tt-metal source code to include an offset, which would be added to the addresses that a
Tensix Core issues to a given NoC2AXI interface.

This can be manipulated in [write_dram_vec and read_dram_vec](https://github.com/tenstorrent/tt-metal/blob/9edf9a0cac90d4f89262165dbe3fc4f4feac18be/tt_metal/llrt/tt_cluster.cpp#L506C1-L539C2) functions in referenced tt_cluster.cpp 