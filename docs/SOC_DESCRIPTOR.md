# Overview
The Tenstorrent (TT) software stack was intended to be used with a variety of Tenstorrent silicon devices, e.g.,
Grayskull™, Wormhole™, etc. All of these devices have a particular, grid-like SoC architecture that the software
was tailored towards.

(TBD: insert image)

More specifically, here are some (but not all) of that assumptions that were considered when developing our
software stacks:
1. The SoC is built in a grid configuration, where each coordinate could be associated with a particular SoC
function, e.g., coordinate 0,0 is a Tensix Compute Core, coordinate 7,3 is a DRAM controller port, etc.
2. The SoC has some interchain links that are implemented using Ethernet and/or PCIe. Those inter-chip links
diTer from one version of the TT SoC to another.
3. The DRAM coordinate address range starts from offset 0, and goes from 0 to the DRAM bank size that is
defined in the SoC descriptor YAwML file.
4. The traffic originating from different cores to other cores and/or to DRAM would be placed on different
NoC’s, potentially to increase throughput.
5. If the IP is reached through a PCIe, then both Tensix L1 memory and DRAM should have an address
allocated for a ‘barrier’ function, where higher software stacks can set / reset to synchronize between kernel dispatching and
data sending.

## SoC Descriptor explanation

The SoC descriptor file tells UMD what resources it could provide for a given workload on a particular SoC. Example SoC descriptors
can be found in [soc_descs folder](../tests/soc_descs/).

A guide on writing up a custom SoC descriptor:
1. Set the proper grid size with X/Y coordinates (including NoC2AXI coordinates).
2. Leave pcie/eth/arc/harvested_workers/routers_only empty, as these are SoC specific
resources configured in a very specific way in our silicon devices.
3. Define the grid of Tensix Cores in the functional_workers section.
4. In the DRAM section, set the coordinates of your NoC2AXI components. DRAM section is two dimensional, where the first dimension (channel) should
be 1-1 mapping with a DRAM bank, and the second dimension (subchannel) lists all the coordinates on the NOC grid which have protocol in place to
communicate with this bank. For example, if one dram is present such that it is connected to two noc ports on the noc grid, the field could look
like: dram: [[0-0], [0-1]].
5. Set the dram_bank_size to the desired size to be allocated on the device, where the software stack
can store input command/data queues, output command/data queues, epoch data/command binaries
queues, etc.
6. The memory size of a singular tensix is defined in worker_l1_size.
7. The arch_name field defines at some places how our Driver initializes the device, mostly how harvesting is done. Legal values for this field
can be found in [arch type definition file](../device/api/umd/device/types/arch.h).
