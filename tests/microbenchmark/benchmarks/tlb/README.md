# TLB benchmark

This benchmark contains tests that are measuring performance of multiple usages of TLBs inside UMD.

To get a better understanding of what TLBs are, you can look at [Tenstorrent ISA documentation](https://github.com/tenstorrent/tt-isa-documentation/blob/main/WormholeB0/PCIExpressTile/TLBs.md).


## Results

### Dynamic TLB DRAM core

| Size (bytes) | Dynamic TLB: Host -> Device DRAM (MB/s) | Dynamic TLB: Device DRAM -> Host (MB/s) |
|---|---|---|
| 1.00 | 0.26 | 0.42 |
| 2.00 | 0.75 | 0.90 |
| 4.00 | 3.06 | 1.82 |
| 8.00 | 5.95 | 2.28 |
| 1024.00 | 762.34 | 2.94 |
| 2048.00 | 1449.87 | 2.95 |
| 4096.00 | 2516.91 | 2.96 |
| 8192.00 | 2221.99 | 2.95 |
| 1048576.00 | 2141.41 | 2.80 |
| 2097152.00 | 2339.76 | 2.86 |
| 4194304.00 | 2341.43 | 2.87 |
| 8388608.00 | 2322.71 | 2.87 |


### Dynamic TLB Tensix core

| Size (bytes) | Static TLB: Host -> Device Tensix L1 (MB/s) | Static TLB: Device Tensix L1 -> Host (MB/s) |
|---|---|---|
| 1.00 | 0.15 | 0.58 |
| 2.00 | 0.93 | 1.20 |
| 4.00 | 18.07 | 2.39 |
| 8.00 | 36.33 | 2.58 |
| 1024.00 | 2516.91 | 2.77 |
| 2048.00 | 4798.83 | 2.79 |
| 4096.00 | 3450.75 | 2.79 |
| 8192.00 | 2192.06 | 2.78 |
| 1048576.00 | 1879.69 | 2.70 |

### Static TLB Tensix core

| Size (bytes) | Static TLB: Host -> Device Tensix L1 (MB/s) | Static TLB: Device Tensix L1 -> Host (MB/s) |
|---|---|---|
| 1.00 | 0.15 | 0.58 |
| 2.00 | 0.93 | 1.20 |
| 4.00 | 18.07 | 2.39 |
| 8.00 | 36.33 | 2.58 |
| 1024.00 | 2516.91 | 2.77 |
| 2048.00 | 4798.83 | 2.79 |
| 4096.00 | 3450.75 | 2.79 |
| 8192.00 | 2192.06 | 2.78 |

### Static TLB DRAM core

| Size (bytes) | Static TLB: Host -> Device DRAM (MB/s) | Static TLB: Device DRAM -> Host (MB/s) |
|---|---|---|
| 1.00 | 0.46 | 0.63 |
| 2.00 | 1.04 | 1.28 |
| 4.00 | 19.27 | 2.58 |
| 8.00 | 38.53 | 2.74 |
| 1024.00 | 2583.50 | 2.92 |
| 2048.00 | 4532.66 | 2.96 |
| 4096.00 | 4182.28 | 2.94 |
| 8192.00 | 3054.14 | 2.89 |
| 1048576.00 | 2487.64 | 2.85 |
| 2097152.00 | 2484.70 | 2.87 |
| 4194304.00 | 2461.43 | 2.89 |
| 8388608.00 | 2452.78 | 2.90 |
| 16777216.00 | 2325.68 | 2.89 |
| 33554432.00 | 2395.44 | 2.85 |

### Dynamic TLB ETH core

| Size (bytes) | Dynamic TLB: Host -> Device ETH L1 (MB/s) | Dynamic TLB: Device ETH L1 -> Host (MB/s) |
|---|---|---|
| 1.00 | 0.34 | 0.43 |
| 2.00 | 0.74 | 0.86 |
| 4.00 | 3.08 | 1.73 |
| 8.00 | 5.99 | 2.17 |
| 1024.00 | 764.79 | 2.79 |
| 2048.00 | 1351.64 | 2.80 |
| 4096.00 | 2455.22 | 2.50 |
| 8192.00 | 2205.05 | 2.78 |
| 131072.00 | 2044.55 | 2.74 |

### Static TLB ETH core

| Size (bytes) | Static TLB: Host -> Device ETH L1 (MB/s) | Static TLB: Device ETH L1 -> Host (MB/s) |
|---|---|---|
| 1.00 | 0.42 | 0.59 |
| 2.00 | 0.94 | 1.21 |
| 4.00 | 18.88 | 2.43 |
| 8.00 | 37.96 | 2.61 |
| 1024.00 | 2484.89 | 2.79 |
| 2048.00 | 5222.26 | 2.81 |
| 4096.00 | 3456.86 | 2.81 |
| 8192.00 | 2555.61 | 2.76 |
| 131072.00 | 2034.54 | 2.77 |