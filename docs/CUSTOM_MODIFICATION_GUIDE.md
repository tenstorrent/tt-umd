# Guide for custom SoC

The User Mode Driver (UMD) is a set of low-level API calls that compilers built on top of it would call to interact with the IP.
UMD needs to maintain the expected API call signatures and implement their SoC specific function.

UMD includes several layers of abstraction and implementation. Ideally only a small layer should be specific to the exact SoC layout, but that might not be completely true currently.

## Descriptor files

SoC Descriptor is one of those components which is reusable for other grid-like SoC. More importantly, higher software stacks built on top of UMD use this layout format as a contract with UMD. See [SOC_DESCRIPTOR.md](./SOC_DESCRIPTOR.md) for more information. An example 
can be found in [blackhole_simulation_1x2.yaml](../tests/soc_descs/blackhole_simulation_1x2.yaml). Note that tt-metal uses slightly different soc descriptors with a couple of additional fields, so if instantiating device through tt-metal, a minimal example can be found in [blackhole_simulation_1x2_arch.yaml](https://github.com/tenstorrent/tt-metal/blob/main/tt_metal/soc_descriptors/blackhole_simulation_1x2_arch.yaml).

Another system descriptor is ClusterDescriptor. These files are describing the topology of the connected system, along with some additional chip specific details which might be important for the topology. An example of minimal cluster descriptor can be found in [wormhole_N150.yaml](../tests/api/cluster_descriptor_examples/wormhole_N150.yaml). This file describes a topology of one device, without any special features, and without any harvesting.

TT-metal also uses core descriptor files, which are files defining how some of the cores are being used. Examples can be found in [core_descriptor](https://github.com/tenstorrent/tt-metal/blob/main/tt_metal/core_descriptors) folder, a minimal example of this file would be [blackhole_simulation_1x2_arch.yaml](https://github.com/tenstorrent/tt-metal/blob/main/tt_metal/core_descriptors/blackhole_simulation_1x2_arch.yaml). The grid range and the array of dispatch cores should be changed accordingly.
It is important to note that this file is not utilized by UMD, and therefore, further details regarding its usage are beyond the scope of this documentation.


## API changes

At the moment, most of the API that needs to be reimplemented for custom SoC is located in the [cluster.h](../device/api/umd/device/cluster.h) file and in the [tt_cluster_descriptor.h](../device/api/umd/device/tt_cluster_descriptor.h). The files have header comments on all the functions which are the current contract with higher software stacks like tt-metal.

The current implementation maintains a tt_SocDescriptor for each chip. This functionality must also be implemented within the class derived from a custom Chip, as there is an API available to retrieve the descriptors.

For an example of how to create your tt_SocDescriptor inside your constructor, you can find an example in [soc_descriptor api tests](../tests/api/test_soc_descriptor.cpp). When creating SocDescriptor, if the device doesn't support harvesting, 0 can be passed as harvesting mask.

### Changes needed in tt-metal

#### tt_cluster.cpp

Most of the interfacing with the UMD is done through this [tt_cluster.cpp](https://github.com/tenstorrent/tt-metal/blob/main/tt_metal/llrt/tt_cluster.cpp). To change the usage to a custom driver for custom device, one should exchange the creation of tt::umd::Cluster
with the class having the same header. For instructions on what each of the functions represents and how to implement them, please see [cluster.h](../device/api/umd/device/cluster.h).

#### soc_descriptor in tt_metal

The Soc descriptors used by tt_metal include additional fields that store parameters related to DRAM access, such as the ports through which it is accessed and the associated offsets. You can leave it with default values, [here's an example](https://github.com/tenstorrent/tt-metal/blob/783d35aea7df03f9927934ec7b9a640a8032d371/tt_metal/soc_descriptors/blackhole_simulation_1x2_arch.yaml#L14C1-L21C8).

#### config_tlb

[Configuring static TLBs](https://github.com/tenstorrent/tt-metal/blob/main/tt_metal/llrt/tt_cluster.cpp#L295) should be skipped in tt_metal.

### DRAM address offset
An assumption is made regarding the DRAM address offset for each DRAM bank in a TT silicon device. This assumption is that the allocated buffer for the respective DRAM bank begins at address 0. While this assumption is valid for many configurations, it may not align with the memory mapping scheme of custom SoCs. However, there are several options for modifying the address offset to accommodate this:

1. Manipulating the AXI Address Map:
You can adjust the AXI interconnect address map to account for the required DRAM offset. This modification allows the memory mapping to support the custom offset without changing higher-level components.

2. Modifying the tt-metal Source Code:
If adjusting the AXI address map is not feasible, another option is to modify the tt-metal source code. You can add the offset directly in the code, which will be applied to the addresses issued by a Tensix Core to the NoC2AXI interface.
This can be manipulated in [write_dram_vec and read_dram_vec](https://github.com/tenstorrent/tt-metal/blob/9edf9a0cac90d4f89262165dbe3fc4f4feac18be/tt_metal/llrt/tt_cluster.cpp#L506C1-L539C2) functions in referenced tt_cluster.cpp 

3. Using the soc_Descriptor File:
Another option is to define the DRAM offset directly in the soc_Descriptor file. This can be done by adding a dram_offset field, as shown in the following example:
[dram_address_offsets:](https://github.com/tenstorrent/tt-metal/blob/6c87f4c91a9ab7078af8da6513929f7ce121f2b3/tt_metal/soc_descriptors/blackhole_140_arch.yaml#L33-L34)
