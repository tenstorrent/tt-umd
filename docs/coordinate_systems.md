Wiki on coordinates understood by open-UMD

- [API and Coordinates Usage](#api-and-coordinates-usage)
  * [Important Notes](#important-notes)
- [Coordinates System Definitions](#coordinates-system-definitions)
  * [Physical Coordinates - Any Arch](#physical-coordinates---any-arch)
    + [Important Notes](#important-notes-1)
    + [Example](#example)
    + [Mapping relationship between NOC0/NOC1](#mapping-relationship-between-noc0-noc1)
  * [Logical Coordinates - Any Arch](#logical-coordinates---any-arch)
    + [Important Notes](#important-notes-2)
    + [Example](#example-1)
  * [Hardware Translated Coordinates - Only for Wormhole and beyond](#hardware-translated-coordinates---only-for-wormhole-and-beyond)
    + [Important Notes](#important-notes-3)
    + [Example](#example-2)
  * [Virtualized coordinates (Inputs and outputs from UMD)](#virtualized-coordinates--inputs-and-outputs-from-umd-)
    + [Important Notes](#important-notes-4)
- [Harvesting and Coordinates](#harvesting-and-coordinates)
  * [Harvesting and Virtualization Example (Wormhole)](#harvesting-and-virtualization-example--wormhole-)
    + [Physical Coordinates](#physical-coordinates)
    + [Translated Coordinates](#translated-coordinates)
    + [Virtual Coordinates](#virtual-coordinates)
  * [Harvesting and Virtualization Example (Grayskull)](#harvesting-and-virtualization-example--grayskull-)
      - [Physical Coordinates](#physical-coordinates-1)
      - [Equivalent Virtual/Translated Coordinates](#equivalent-virtual-translated-coordinates)
  * [Important Notes about Harvesting](#important-notes-about-harvesting)
    + [Additional Notes about the Translation Scheme in UMD](#additional-notes-about-the-translation-scheme-in-umd)


# API and Coordinates Usage
## Important Notes
* UMD accepts Virtual Coordinates (these match Physical Coordinates on Grayskull)
* Any application programming overlay hardware/streams must use Translated Coordinates on a Wormhole Device with translation tables enabled
* Regular Physical Coordinates can be used on a Wormhole Device with Translation Tables disabled
* Translation Tables are a prerequisite for Wormhole Harvesting
* As part of product definition, translation tables are enabled on Wormhole devices regardless of their grid size

# Coordinates System Definitions
## Physical Coordinates - Any Arch
These are the NOC coordinates that the hardware understands, there are two distinct variations for NOC0 and NOC1.  In hardware, each node is given an ID (which is different for each NOC) which can be used to identify this node.  In the SOC descriptor, physical coordinates are specified for NOC0. 

### Important Notes 
* The coordinates are **NOT** contiguous for tensix cores -- In the example shown, ethernet corresponds to physical coords `[*-6]`:  this row is not on in the grid of worker cores
* The coordinates are statically assigned to each "node" regardless of harvesting
* `grayskull` are listed in `[y-x]` while `wormhole_b0` are listed in `[x-y]`

### Example
Example for `wormhole_b0`, where coordinates are specified in `[x-y]` pairs for `NOC0`
```yaml
eth:
  [   # Each node specifies the coordinates for NOC0 specifically.  We need to translate these to NOC1 if we are using NOC1 coordinates
      1-0, 2-0, 3-0, 4-0, 6-0, 7-0, 8-0, 9-0,
      1-6, 2-6, 3-6, 4-6, 6-6, 7-6, 8-6, 9-6,
  ]
functional_workers:
  [   # Each node specifies the coordinates for NOC0 specifically.  We need to translate these to NOC1 if we are using NOC1 coordinates
      1-1,   2-1,   3-1,   4-1,   6-1,   7-1,   8-1,   9-1,
      1-2,   2-2,   3-2,   4-2,   6-2,   7-2,   8-2,   9-2,
      1-3,   2-3,   3-3,   4-3,   6-3,   7-3,   8-3,   9-3,
      1-4,   2-4,   3-4,   4-4,   6-4,   7-4,   8-4,   9-4, 
      1-5,   2-5,   3-5,   4-5,   6-5,   7-5,   8-5,   9-5,    
      1-7,   2-7,   3-7,   4-7,   6-7,   7-7,   8-7,   9-7,    
      1-8,   2-8,   3-8,   4-8,   6-8,   7-8,   8-8,   9-8,    
      1-9,   2-9,   3-9,   4-9,   6-9,   7-9,   8-9,   9-9, 
      1-10,  2-10,  3-10,  4-10,  6-10,  7-10,  8-10,  9-10,   
      1-11,  2-11,  3-11,  4-11,  6-11,  7-11,  8-11,  9-11,
  ]
```
A similar example displaying physical coordinates on Grayskull can be found in `tests/soc_descs/grayskull_10x12.yaml` inside the UMD repo.

### Mapping relationship between NOC0/NOC1
Below is the way we map the physical to `NOC0`/`NOC1` coordinates -- Notice that physical maps to `NOC0`directly and `noc_size_*` changes depending on `ARCH`
```cpp
#define NOC_X(x) (noc == NOC0 ? (x) : (noc_size_x-1-(x)))
#define NOC_Y(y) (noc == NOC0 ? (y) : (noc_size_y-1-(y)))
```

## Logical Coordinates - Any Arch
This coordinate system is not used within UMD. It hides the details of physical coordinates and allows upper layers of the stack to access tensix endpoints through a set of traditional Cartesian Coordinates (starting at `0-0`).

### Important Notes
* These coordinates cannot be used in APIs provided by UMD. UMD expects virtual coordinates to be passed into its APIs (see below for definition).
* This coordinate system is easier to use than physical coordinates and can be used by Buda or Metal. However, certain operations may require the use of physical coordinates. For this, the SOC descriptor class in UMD presents the following translation layers:

```
std::unordered_map<int, int> worker_log_to_routing_x; // worker logical to routing (x)
std::unordered_map<int, int> worker_log_to_routing_y; // worker logical to routing (y)
```

### Example
Given the physical worker grid for `wormhole_b0` in the diagram above, the following set of logical coordinates (in `x-y`) can be used to access each physical core:

```yaml
functional_workers:
  [   # Each node specifies logical coords specifically.  
      0-0,   1-0,   2-0,   3-0,   4-0,   5-0,   6-0,   7-0,   
      0-1,   1-1,   2-1,   3-1,   4-1,   5-1,   6-1,   7-1,   
      0-2,   1-2,   2-2,   3-2,   4-2,   5-2,   6-2,   7-2,   
      0-3,   1-3,   2-3,   3-3,   4-3,   5-3,   6-3,   7-3,   
      0-4,   1-4,   2-4,   3-4,   4-4,   5-4,   6-4,   7-4,   
      0-5,   1-5,   2-5,   3-5,   4-5,   5-5,   6-5,   7-5,   
      0-6,   1-6,   2-6,   3-6,   4-6,   5-6,   6-6,   7-6,   
      0-7,   1-7,   2-7,   3-7,   4-7,   5-7,   6-7,   7-7,   
      0-8,   1-8,   2-8,   3-8,   4-8,   5-8,   6-8,   7-8,   
      0-9,   1-9,   2-9,   3-9,   4-9,   5-9,   6-9,   7-9,
  ]
```

## Hardware Translated Coordinates - Only for Wormhole and beyond
**Note: This is an implementation detail in UMD that upper layers of the stack are not exposed to. However, device binaries using streams should use the translated coordinate scheme presented here.**

**Motivation: Allow binaries to be compatible across different Wormhole devices, as long as their grid sizes are identical.**

Wormhole_b0 and later architectures implement a programmable coordinate translation table in hardware for each row and column.  Programming is done ahead of time by FW (ARC) -- see example for `wormhole_b0` mapping.
This coordinate system aims to abstract away the effects of harvesting (see below) by relying on a convex grid of worker cores. This allows each layer to be oblivious to the effects of harvesting,

Translated coordinates start at 16-16 (due to hardware design features) and go through a Hardware based LUT to access physical tensix endpoints. Since using translated coordinates is difficult in the upper layers of software, UMD exposes virtual coordinates (discussed later), which are then translated to this system through software.

### Important Notes 
* The coordinates are contiguous for tensix cores 
* The coordinates are dynamically mapped to each physical node through hardware LUTs, which depend on the harvesting configuration
* This system preserves the physical coordinate values for DRAM/PCIE/ARC (identity mapping between physical and translated nodes)
* ETH/Tensix are remapped and UMD -> Device communication is done using translated coordinates

### Example
Given the physical coordinates grid shown above the translated grid is as follows (a write to core `18-18` will ping physical core `1-1`):

```yaml
eth:
   [   # Each node specifies the translated coordinates used to access ethernet
       25-16, 18-16, 24-16, 19-16, 23-16, 20-16, 22-16, 21-16,
       25-17, 18-17, 24-17, 19-17, 23-17, 20-17, 22-17, 21-17,
   ]
functional_workers:
   [   # Each node specifies the translated coordinates used to access workers
       18-18,  19-18,  20-18,  21-18,  22-18,  23-18,  24-18,  25-18,
       18-19,  19-19,  20-19,  21-19,  22-19,  23-19,  24-19,  25-19,
       18-20,  19-20,  20-20,  21-20,  22-20,  23-20,  24-20,  25-20,
       18-21,  19-21,  20-21,  21-21,  22-21,  23-21,  24-21,  25-21,
       18-22,  19-22,  20-22,  21-22,  22-22,  23-22,  24-22,  25-22,
       18-23,  19-23,  20-23,  21-23,  22-23,  23-23,  24-23,  25-23,
       18-24,  19-24,  20-24,  21-24,  22-24,  23-24,  24-24,  25-24,
       18-25,  19-25,  20-25,  21-25,  22-25,  23-25,  24-25,  25-25,
       18-26,  19-26,  20-26,  21-26,  22-26,  23-26,  24-26,  25-26, 
       18-27,  19-27,  20-27,  21-27,  22-27,  23-27,  24-27,  25-27, 
   ]
```
## Virtualized coordinates (Inputs and outputs from UMD)
The virtual coordinate system is to be used when specifying cores through UMD APIs. During initialization, UMD will provide users with SOC descriptors consisting of virtual coordinates, which can be used across the stack. 

**Virtual Coordinates are a subset of the full chip Physical Coordinates shown above, allowing users to treat either coordinate system in a similar manner.**

Caveats due to harvesting are provided in the next section.

### Important Notes
**Wormhole Scheme - Relies on Translation Tables**

When no harvesting has taken place (chip has full grid size):
* Virtual Coordinates == Physical Coordinates for all cores
* Virtual Coordinates != Translated Coordinates for Tensix and Ethernet
* Virtual Coordinates == Translated Coordinates for ARC/PCIE/DRAM

When harvesting is perfomed on a chip:
* Virtual Coordinates == Physical Coordinates == Translated Coordinates for ARC/PCIE/DRAM
* Virtual Coordinates != Translated Coordinates for Tensix and Ethernet
* Virtual Coordinates != Physical Coordinates for Tensix and Ethernet

**Grayskull Scheme**
* Virtual Coordinates == Physical Coordinates == Translated Coordinates for all cases (no translation tables on this Device).

# Harvesting and Coordinates
Harvesting refers to rows of tensix being disabled due to binning. Workloads cannot be run on these rows and they can only be used for data routing. However, we still need to navigate these cores. In this section, we discuss how virtualized and translated coordinates are used to do this for Wormhole and how physical coordinates are used for harvested Grayskull devices (since translation tables don't exist on here).

## Harvesting and Virtualization Example (Wormhole)
The following example assumes that we disable rows 4 and 8 (physical coordinates).

### Physical Coordinates
```yaml
eth:
  [   # Each node specifies the coordinates for NOC0 specifically.  We need to translate these to NOC1 if we are using NOC1 coordinates
      1-0, 2-0, 3-0, 4-0, 6-0, 7-0, 8-0, 9-0,
      1-6, 2-6, 3-6, 4-6, 6-6, 7-6, 8-6, 9-6,
  ]
functional_workers:
  [   # Each node specifies the coordinates for NOC0 specifically.  We need to translate these to NOC1 if we are using NOC1 coordinates
      1-1,   2-1,   3-1,   4-1,   6-1,   7-1,   8-1,   9-1, # Row 1
      1-2,   2-2,   3-2,   4-2,   6-2,   7-2,   8-2,   9-2, # Row 2
      1-3,   2-3,   3-3,   4-3,   6-3,   7-3,   8-3,   9-3, # Row 3
      1-4,   2-4,   3-4,   4-4,   6-4,   7-4,   8-4,   9-4, # Harvested row 4
      1-5,   2-5,   3-5,   4-5,   6-5,   7-5,   8-5,   9-5, # Row 5    
      1-7,   2-7,   3-7,   4-7,   6-7,   7-7,   8-7,   9-7, # Row 6    
      1-8,   2-8,   3-8,   4-8,   6-8,   7-8,   8-8,   9-8, # Row 7     
      1-9,   2-9,   3-9,   4-9,   6-9,   7-9,   8-9,   9-9, # Harvested row 8 
      1-10,  2-10,  3-10,  4-10,  6-10,  7-10,  8-10,  9-10, # Row 9   
      1-11,  2-11,  3-11,  4-11,  6-11,  7-11,  8-11,  9-11, # Row 10
  ]
```
### Translated Coordinates
This coordinate system will translate the above non-convex grid to a set of contiguous cores. The mapping between translated and physical coordinates is as follows:

```yaml
eth:
 [Translated x: 18   19   20   21   22   23   24   25                                            Translated y
                1-0, 2-0, 3-0, 4-0, 6-0, 7-0, 8-0, 9-0,                                               16
                1-6, 2-6, 3-6, 4-6, 6-6, 7-6, 8-6, 9-6,                                               17 
 ]
functional_workers:
 [Translated x: 18     19     20     21     22     23     24     25
                                                                                                 Translated y
                1-1,   2-1,   3-1,   4-1,   6-1,   7-1,   8-1,   9-1, # Row 1                         18
                1-2,   2-2,   3-2,   4-2,   6-2,   7-2,   8-2,   9-2, # Row 2                         19
                1-3,   2-3,   3-3,   4-3,   6-3,   7-3,   8-3,   9-3, # Row 3                         20
                1-4,   2-4,   3-4,   4-4,   6-4,   7-4,   8-4,   9-4, # Harvested row 4
                1-5,   2-5,   3-5,   4-5,   6-5,   7-5,   8-5,   9-5, # Row 5                         21
                1-7,   2-7,   3-7,   4-7,   6-7,   7-7,   8-7,   9-7, # Row 6                         22
                1-8,   2-8,   3-8,   4-8,   6-8,   7-8,   8-8,   9-8, # Row 7                         23
                1-9,   2-9,   3-9,   4-9,   6-9,   7-9,   8-9,   9-9, # Harvested row 8   
                1-10,  2-10,  3-10,  4-10,  6-10,  7-10,  8-10,  9-10, # Row 9                        24
                1-11,  2-11,  3-11,  4-11,  6-11,  7-11,  8-11,  9-11, # Row 10                       25
 ]
```

The hardware is responsible for pointing the coordinates shown below to the appropriate set of physical coordinates. Note: _n_ random rows disabled in the physical coordinate space will map to the bottom _n_ rows being disabled in the translated coordinate space.

 ```yaml
eth:
   [   # Each node specifies the translated coordinates used to access ethernet
       25-16, 18-16, 24-16, 19-16, 23-16, 20-16, 22-16, 21-16,
       25-17, 18-17, 24-17, 19-17, 23-17, 20-17, 22-17, 21-17,
   ]
functional_workers:
   [   # Each node specifies the translated coordinates used to access workers
       18-18,  19-18,  20-18,  21-18,  22-18,  23-18,  24-18,  25-18,
       18-19,  19-19,  20-19,  21-19,  22-19,  23-19,  24-19,  25-19,
       18-20,  19-20,  20-20,  21-20,  22-20,  23-20,  24-20,  25-20,
       18-21,  19-21,  20-21,  21-21,  22-21,  23-21,  24-21,  25-21,
       18-22,  19-22,  20-22,  21-22,  22-22,  23-22,  24-22,  25-22,
       18-23,  19-23,  20-23,  21-23,  22-23,  23-23,  24-23,  25-23,
       18-24,  19-24,  20-24,  21-24,  22-24,  23-24,  24-24,  25-24,
       18-25,  19-25,  20-25,  21-25,  22-25,  23-25,  24-25,  25-25,
   ]
```

### Virtual Coordinates
Given the convex grid of translated coordinates presented above, virtual coordinates map this back to "physical space" to hide the details of hardware translation from the rest of the software. As such, similar to translated coordinates: _n_ random rows disabled in the physical coordinate space will map to the bottom _n_ rows being disabled in the virtual coordinate space.

The mapping between virtual and physical coordinates is as follows:

```yaml
eth:
 [Virtual x:    1    2    3    4    6    7    8    9                                              Virtual y
                1-0, 2-0, 3-0, 4-0, 6-0, 7-0, 8-0, 9-0,                                               0
                1-6, 2-6, 3-6, 4-6, 6-6, 7-6, 8-6, 9-6,                                               6 
 ]
functional_workers:
 [Virtual x:    1      2      3      4      6      7      8      9
                                                                                                  Virtual y
                1-1,   2-1,   3-1,   4-1,   6-1,   7-1,   8-1,   9-1, # Row 1                         1
                1-2,   2-2,   3-2,   4-2,   6-2,   7-2,   8-2,   9-2, # Row 2                         2
                1-3,   2-3,   3-3,   4-3,   6-3,   7-3,   8-3,   9-3, # Row 3                         3
                1-4,   2-4,   3-4,   4-4,   6-4,   7-4,   8-4,   9-4, # Harvested row 4
                1-5,   2-5,   3-5,   4-5,   6-5,   7-5,   8-5,   9-5, # Row 5                         4
                1-7,   2-7,   3-7,   4-7,   6-7,   7-7,   8-7,   9-7, # Row 6                         5
                1-8,   2-8,   3-8,   4-8,   6-8,   7-8,   8-8,   9-8, # Row 7                         7
                1-9,   2-9,   3-9,   4-9,   6-9,   7-9,   8-9,   9-9, # Harvested row 8   
                1-10,  2-10,  3-10,  4-10,  6-10,  7-10,  8-10,  9-10, # Row 9                        8
                1-11,  2-11,  3-11,  4-11,  6-11,  7-11,  8-11,  9-11, # Row 10                       9
 ]
```

A translation layer in UMD is responsible for mapping the virtual coordinates (API inputs) to translated coordinates (device inputs), which are then routed to the correct physical cores through HW translation. The virtual grid for this example is as follows (provided by UMD):

```yaml
eth:
  [   # Each node specifies the coordinates for NOC0 specifically.  We need to translate these to NOC1 if we are using NOC1 coordinates
      1-0, 2-0, 3-0, 4-0, 6-0, 7-0, 8-0, 9-0,
      1-6, 2-6, 3-6, 4-6, 6-6, 7-6, 8-6, 9-6,
  ]
functional_workers:
  [   # Each node specifies the coordinates for NOC0 specifically.  We need to translate these to NOC1 if we are using NOC1 coordinates
      1-1,   2-1,   3-1,   4-1,   6-1,   7-1,   8-1,   9-1,
      1-2,   2-2,   3-2,   4-2,   6-2,   7-2,   8-2,   9-2,
      1-3,   2-3,   3-3,   4-3,   6-3,   7-3,   8-3,   9-3,
      1-4,   2-4,   3-4,   4-4,   6-4,   7-4,   8-4,   9-4,
      1-5,   2-5,   3-5,   4-5,   6-5,   7-5,   8-5,   9-5,  
      1-7,   2-7,   3-7,   4-7,   6-7,   7-7,   8-7,   9-7, 
      1-8,   2-8,   3-8,   4-8,   6-8,   7-8,   8-8,   9-8,    
      1-9,   2-9,   3-9,   4-9,   6-9,   7-9,   8-9,   9-9,
  ]
```

If device binaries are compiled using virtual coordinates, they will be compatible across all Wormhole machines with the same grid size.

Considering the harvesting example so far, **a logical coordinate of `4-3` would correspond to a virtual coordinate of `6-4` (input to UMD). UMD would translate this endpoint to `22-21` when pinging the device, which would target core `6-5` post hardware translation.** These details are hidden when using logical and virtual coordinates.

## Harvesting and Virtualization Example (Grayskull)
As mentioned above, translation tables are not available on this architecture. Hence all three coordinate systems are identical and the effects of harvesting are not hidden from code running on host or device. This means that **binaries are not compatible across harvested Grayskull devices**.

There is a request for unifying the definition of virtual coordinates across devices, however, this is not done due to software/hardware limitations. See: https://yyz-gitlab.local.tenstorrent.com/tenstorrent/open-umd/-/issues/3


#### Physical Coordinates
```yaml
functional_workers:
  [   # Each node specifies the coordinates for NOC0 specifically.  We need to translate these to NOC1 if we are using NOC1 coordinates
      1-1,  1-2,  1-3,  1-4,  1-5,  1-7,  1-8,  1-9,  1-10,  1-11, # Row 1
      2-1,  2-2,  2-3,  2-4,  2-5,  2-7,  2-8,  2-9,  2-10,  2-11, # Row 2
      3-1,  3-2,  3-3,  3-4,  3-5,  3-7,  3-8,  3-9,  3-10,  3-11, # Row 3
      4-1,  4-2,  4-3,  4-4,  4-5,  4-7,  4-8,  4-9,  4-10,  4-11, # Harvested Row 4
      5-1,  5-2,  5-3,  5-4,  5-5,  5-7,  5-8,  5-9,  5-10,  5-11, # Row 5
      6-1,  6-2,  6-3,  6-4,  6-5,  6-7,  6-8,  6-9,  6-10,  6-11, # Row 6
      7-1,  7-2,  7-3,  7-4,  7-5,  7-7,  7-8,  7-9,  7-10,  7-11, # Row 7 
      8-1,  8-2,  8-3,  8-4,  8-5,  8-7,  8-8,  8-9,  8-10,  8-11, # Harvested Row 8
      9-1,  9-2,  9-3,  9-4,  9-5,  9-7,  9-8,  9-9,  9-10,  9-11, # Row 9
      10-1, 10-2, 10-3, 10-4, 10-5, 10-7, 10-8, 10-9, 10-10, 10-11, # Row 10
      11-1, 11-2, 11-3, 11-4, 11-5, 11-7, 11-8, 11-9, 11-10, 11-11, # Row 11
      12-1, 12-2, 12-3, 12-4, 12-5, 12-7, 12-8, 12-9, 12-10, 12-11 # Row 12
  ]
```
#### Equivalent Virtual/Translated Coordinates
```yaml
functional_workers:
  [   # Each node specifies the coordinates for NOC0 specifically.  We need to translate these to NOC1 if we are using NOC1 coordinates
      1-1,  1-2,  1-3,  1-4,  1-5,  1-7,  1-8,  1-9,  1-10,  1-11, # Row 1
      2-1,  2-2,  2-3,  2-4,  2-5,  2-7,  2-8,  2-9,  2-10,  2-11, # Row 2
      3-1,  3-2,  3-3,  3-4,  3-5,  3-7,  3-8,  3-9,  3-10,  3-11, # Row 3
      # 4-1,  4-2,  4-3,  4-4,  4-5,  4-7,  4-8,  4-9,  4-10,  4-11, # Harvested Row 4
      5-1,  5-2,  5-3,  5-4,  5-5,  5-7,  5-8,  5-9,  5-10,  5-11, # Row 5
      6-1,  6-2,  6-3,  6-4,  6-5,  6-7,  6-8,  6-9,  6-10,  6-11, # Row 6
      7-1,  7-2,  7-3,  7-4,  7-5,  7-7,  7-8,  7-9,  7-10,  7-11, # Row 7 
      # 8-1,  8-2,  8-3,  8-4,  8-5,  8-7,  8-8,  8-9,  8-10,  8-11, # Harvested Row 8
      9-1,  9-2,  9-3,  9-4,  9-5,  9-7,  9-8,  9-9,  9-10,  9-11, # Row 9
      10-1, 10-2, 10-3, 10-4, 10-5, 10-7, 10-8, 10-9, 10-10, 10-11, # Row 10
      11-1, 11-2, 11-3, 11-4, 11-5, 11-7, 11-8, 11-9, 11-10, 11-11, # Row 11
      12-1, 12-2, 12-3, 12-4, 12-5, 12-7, 12-8, 12-9, 12-10, 12-11 # Row 12
  ]
```
An identity mapping between physical and virtual coordinates is maintained. UMD will remove rows 4 and 8 when presenting users with virtualized SOC descriptors, as these cores are not to be accessed.


## Important Notes about Harvesting
* Only tensix/worker rows are removed during the harvesting process. UMD does not support chips with PCIe, ARC, Ethernet or DRAM cores disabled.
* Only rows can be disabled for Grayskull and Wormhole devices.

### Additional Notes about the Translation Scheme in UMD
During initialization, UMD queries the device to determine if translation tables are enabled. If so, it will always map virtual coordinates to the translated space when accessing the device. If not, UMD has not concept of translated coordinates, and will use an identity mapping between virtual and "translated" coordinates. In this case: physical, virtual and translated coordinates are identical (this is the case for Grayskull, since it does not have translation tables).

**Wormhole harvesting relies on translation tables, i.e. if this feature is not enabled, UMD will assert during initialization.**

Since DRAM, PCIe and ARC cores are not harvested; they are unaffected by translation tables: NOC0, virtual and translated coordinates for them are identical in all cases. 

Translated coordinates are exposed to the user through the following UMD API: 
```
translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c);
```

A user may choose to use translated coordinates in customized parts of their stack. For example, Buda uses this system when generating overlay binaries (since these program streams).

Even when translation tables are enabled, all endpoints are accessible through their original NOC0/NOC1 coordinates. Customized firmware/kernels (except for streams) running on device should be able to access all cores using either system.

**As mentioned previously overlay hardware/streams must use translated coordinates if this feature is enabled.**