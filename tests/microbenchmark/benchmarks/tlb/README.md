# TLB benchmark

This benchmark contains tests that are measuring performance of multiple usages of TLBs inside UMD.

To get a better understanding of what TLBs are, you can look at [Tenstorrent ISA documentation](https://github.com/tenstorrent/tt-isa-documentation/blob/main/WormholeB0/PCIExpressTile/TLBs.md).

Each case measures three row families over the same batch sizes:

- `Cluster Strict` goes through `Cluster::write_to_device` / `read_from_device`, which reconfigures a window per chunk, takes the chip wide window lock, translates coordinates, and uses `IoOrdering::Strict`.
- `IoWindow Relaxed` and `IoWindow Strict` drive one caller-owned window, mapped once with no lock and no reconfigure between transfers. The pair isolates the cost of the ordering mode, and the gap from `IoWindow Strict` to `Cluster Strict` isolates what the `Cluster` path adds on top of the same ordering.

## Example results

Measured on a Wormhole n150 (bgd-lab-06 machine), from the benchmark workflow run on [#3454](https://github.com/tenstorrent/tt-umd/pull/3454).

### DRAM
```
|             ns/byte |              byte/s |    err% |     total | TLB_DRAM
|--------------------:|--------------------:|--------:|----------:|:---------
|            1,441.08 |          693,925.67 |    0.1% |      0.01 | `Cluster Strict, write, 1 bytes`
|              723.55 |        1,382,066.97 |    0.5% |      0.01 | `Cluster Strict, write, 2 bytes`
|              106.66 |        9,375,860.15 |    0.0% |      0.01 | `Cluster Strict, write, 4 bytes`
|               98.23 |       10,180,094.71 |    0.1% |      0.01 | `Cluster Strict, write, 8 bytes`
|                6.00 |      166,669,766.92 |    0.0% |      0.01 | `Cluster Strict, write, 1024 bytes`
|                6.00 |      166,679,757.92 |    0.0% |      0.01 | `Cluster Strict, write, 2048 bytes`
|                6.00 |      166,671,598.95 |    0.0% |      0.01 | `Cluster Strict, write, 4096 bytes`
|                6.00 |      166,670,057.58 |    0.4% |      0.01 | `Cluster Strict, write, 8192 bytes`
|                6.02 |      166,156,165.02 |    0.4% |      0.07 | `Cluster Strict, write, 1048576 bytes`
|                6.00 |      166,654,786.32 |    0.1% |      0.14 | `Cluster Strict, write, 2097152 bytes`
|                6.00 |      166,663,467.95 |    0.0% |      0.28 | `Cluster Strict, write, 4194304 bytes`
|                6.00 |      166,668,978.03 |    0.0% |      0.55 | `Cluster Strict, write, 8388608 bytes`
|                6.00 |      166,616,809.05 |    0.0% |      1.11 | `Cluster Strict, write, 16777216 bytes`
|                6.01 |      166,526,358.82 |    0.0% |      2.22 | `Cluster Strict, write, 33554432 bytes`
|            1,175.24 |          850,890.12 |    0.3% |      0.01 | `Cluster Strict, read, 1 bytes`
|              587.00 |        1,703,582.45 |    0.1% |      0.01 | `Cluster Strict, read, 2 bytes`
|              288.59 |        3,465,112.90 |    0.2% |      0.01 | `Cluster Strict, read, 4 bytes`
|              258.96 |        3,861,647.42 |    0.2% |      0.01 | `Cluster Strict, read, 8 bytes`
|               28.15 |       35,526,806.53 |    0.1% |      0.01 | `Cluster Strict, read, 1024 bytes`
|               28.07 |       35,622,760.53 |    0.1% |      0.01 | `Cluster Strict, read, 2048 bytes`
|               28.03 |       35,671,467.18 |    0.3% |      0.01 | `Cluster Strict, read, 4096 bytes`
|               27.95 |       35,772,582.10 |    0.2% |      0.01 | `Cluster Strict, read, 8192 bytes`
|               27.93 |       35,807,460.32 |    0.0% |      0.32 | `Cluster Strict, read, 1048576 bytes`
|               28.31 |       35,329,179.48 |    0.0% |      0.65 | `Cluster Strict, read, 2097152 bytes`
|               27.87 |       35,883,761.19 |    0.0% |      1.29 | `Cluster Strict, read, 4194304 bytes`
|               27.90 |       35,844,069.43 |    0.0% |      2.57 | `Cluster Strict, read, 8388608 bytes`
|               27.92 |       35,817,205.98 |    0.0% |      5.15 | `Cluster Strict, read, 16777216 bytes`
|               28.03 |       35,674,589.36 |    0.0% |     10.35 | `Cluster Strict, read, 33554432 bytes`
|            1,366.01 |          732,061.28 |    0.1% |      0.01 | `IoWindow Relaxed, write, 1 bytes`
|              684.61 |        1,460,680.06 |    0.1% |      0.01 | `IoWindow Relaxed, write, 2 bytes`
|               32.05 |       31,198,004.52 |    0.0% |      0.01 | `IoWindow Relaxed, write, 4 bytes`
|               32.03 |       31,222,651.75 |    0.0% |      0.01 | `IoWindow Relaxed, write, 8 bytes`
|                0.38 |    2,620,249,773.38 |    0.0% |      0.01 | `IoWindow Relaxed, write, 1024 bytes`
|                0.38 |    2,620,718,778.19 |    0.0% |      0.01 | `IoWindow Relaxed, write, 2048 bytes`
|                0.38 |    2,621,763,207.53 |    0.0% |      0.01 | `IoWindow Relaxed, write, 4096 bytes`
|                0.38 |    2,616,786,410.00 |    0.0% |      0.01 | `IoWindow Relaxed, write, 8192 bytes`
|                0.38 |    2,619,545,195.64 |    0.0% |      0.01 | `IoWindow Relaxed, write, 1048576 bytes`
|                0.38 |    2,618,912,749.20 |    0.0% |      0.01 | `IoWindow Relaxed, write, 2097152 bytes`
|              959.99 |        1,041,677.55 |    0.0% |      0.01 | `IoWindow Relaxed, read, 1 bytes`
|              479.99 |        2,083,364.68 |    0.0% |      0.01 | `IoWindow Relaxed, read, 2 bytes`
|              239.99 |        4,166,768.27 |    0.0% |      0.01 | `IoWindow Relaxed, read, 4 bytes`
|              240.00 |        4,166,742.12 |    0.0% |      0.01 | `IoWindow Relaxed, read, 8 bytes`
|               27.85 |       35,904,663.30 |    0.1% |      0.01 | `IoWindow Relaxed, read, 1024 bytes`
|               27.86 |       35,887,508.45 |    0.1% |      0.01 | `IoWindow Relaxed, read, 2048 bytes`
|               27.83 |       35,934,174.49 |    0.1% |      0.01 | `IoWindow Relaxed, read, 4096 bytes`
|               27.84 |       35,919,332.34 |    0.0% |      0.01 | `IoWindow Relaxed, read, 8192 bytes`
|               27.84 |       35,915,063.01 |    0.0% |      0.32 | `IoWindow Relaxed, read, 1048576 bytes`
|               27.85 |       35,901,080.57 |    0.0% |      0.64 | `IoWindow Relaxed, read, 2097152 bytes`
|            1,366.49 |          731,800.94 |    0.1% |      0.01 | `IoWindow Strict, write, 1 bytes`
|              685.40 |        1,459,003.82 |    0.0% |      0.01 | `IoWindow Strict, write, 2 bytes`
|               95.59 |       10,461,497.96 |    0.2% |      0.01 | `IoWindow Strict, write, 4 bytes`
|               95.74 |       10,444,898.59 |    0.2% |      0.01 | `IoWindow Strict, write, 8 bytes`
|                5.99 |      166,908,820.10 |    0.0% |      0.01 | `IoWindow Strict, write, 1024 bytes`
|                5.99 |      166,901,689.60 |    0.0% |      0.01 | `IoWindow Strict, write, 2048 bytes`
|                5.99 |      167,015,469.00 |    0.1% |      0.01 | `IoWindow Strict, write, 4096 bytes`
|                5.99 |      166,845,836.56 |    0.2% |      0.01 | `IoWindow Strict, write, 8192 bytes`
|                6.02 |      166,069,430.20 |    0.1% |      0.07 | `IoWindow Strict, write, 1048576 bytes`
|                6.03 |      165,778,114.53 |    0.0% |      0.14 | `IoWindow Strict, write, 2097152 bytes`
|              959.99 |        1,041,677.82 |    0.0% |      0.01 | `IoWindow Strict, read, 1 bytes`
|              480.00 |        2,083,343.85 |    0.0% |      0.01 | `IoWindow Strict, read, 2 bytes`
|              240.00 |        4,166,706.60 |    0.0% |      0.01 | `IoWindow Strict, read, 4 bytes`
|              240.00 |        4,166,712.76 |    0.0% |      0.01 | `IoWindow Strict, read, 8 bytes`
|               27.89 |       35,861,028.01 |    0.2% |      0.01 | `IoWindow Strict, read, 1024 bytes`
|               27.82 |       35,944,580.76 |    0.1% |      0.01 | `IoWindow Strict, read, 2048 bytes`
|               27.86 |       35,899,453.48 |    0.2% |      0.01 | `IoWindow Strict, read, 4096 bytes`
|               27.83 |       35,929,099.68 |    0.1% |      0.01 | `IoWindow Strict, read, 8192 bytes`
|               27.85 |       35,901,991.41 |    0.1% |      0.32 | `IoWindow Strict, read, 1048576 bytes`
|               27.84 |       35,915,285.05 |    0.0% |      0.64 | `IoWindow Strict, read, 2097152 bytes`
```

### Tensix
```
|             ns/byte |              byte/s |    err% |     total | TLB_Tensix
|--------------------:|--------------------:|--------:|----------:|:-----------
|            1,592.73 |          627,853.00 |    0.3% |      0.01 | `Cluster Strict, write, 1 bytes`
|              795.72 |        1,256,726.36 |    0.3% |      0.01 | `Cluster Strict, write, 2 bytes`
|              129.10 |        7,746,164.16 |    0.0% |      0.01 | `Cluster Strict, write, 4 bytes`
|              122.36 |        8,172,343.34 |    0.1% |      0.01 | `Cluster Strict, write, 8 bytes`
|                7.17 |      139,484,285.81 |    0.0% |      0.01 | `Cluster Strict, write, 1024 bytes`
|                7.14 |      140,112,874.79 |    0.0% |      0.01 | `Cluster Strict, write, 2048 bytes`
|                7.12 |      140,430,208.36 |    0.0% |      0.01 | `Cluster Strict, write, 4096 bytes`
|                7.11 |      140,588,263.34 |    0.0% |      0.01 | `Cluster Strict, write, 8192 bytes`
|                7.13 |      140,177,135.77 |    0.2% |      0.08 | `Cluster Strict, write, 1048576 bytes`
|            1,266.46 |          789,604.11 |    0.3% |      0.01 | `Cluster Strict, read, 1 bytes`
|              631.08 |        1,584,578.15 |    0.1% |      0.01 | `Cluster Strict, read, 2 bytes`
|              310.78 |        3,217,678.78 |    0.1% |      0.01 | `Cluster Strict, read, 4 bytes`
|              281.47 |        3,552,771.00 |    0.1% |      0.01 | `Cluster Strict, read, 8 bytes`
|               30.78 |       32,488,746.43 |    0.0% |      0.01 | `Cluster Strict, read, 1024 bytes`
|               30.66 |       32,619,028.90 |    0.0% |      0.01 | `Cluster Strict, read, 2048 bytes`
|               30.59 |       32,686,675.55 |    0.0% |      0.01 | `Cluster Strict, read, 4096 bytes`
|               30.61 |       32,672,934.83 |    0.1% |      0.01 | `Cluster Strict, read, 8192 bytes`
|               30.57 |       32,714,933.31 |    0.1% |      0.35 | `Cluster Strict, read, 1048576 bytes`
|            1,582.19 |          632,035.72 |    0.0% |      0.01 | `IoWindow Relaxed, write, 1 bytes`
|              791.11 |        1,264,050.98 |    0.0% |      0.01 | `IoWindow Relaxed, write, 2 bytes`
|                9.78 |      102,294,400.92 |    0.9% |      0.01 | `IoWindow Relaxed, write, 4 bytes`
|                8.66 |      115,535,779.72 |    0.2% |      0.01 | `IoWindow Relaxed, write, 8 bytes`
|                0.47 |    2,135,980,565.22 |    0.0% |      0.01 | `IoWindow Relaxed, write, 1024 bytes`
|                0.47 |    2,136,025,290.39 |    0.0% |      0.01 | `IoWindow Relaxed, write, 2048 bytes`
|                0.47 |    2,136,513,248.70 |    0.0% |      0.01 | `IoWindow Relaxed, write, 4096 bytes`
|                0.47 |    2,136,671,883.15 |    0.0% |      0.01 | `IoWindow Relaxed, write, 8192 bytes`
|                0.47 |    2,135,496,983.84 |    0.0% |      0.01 | `IoWindow Relaxed, write, 1048576 bytes`
|            1,015.89 |          984,354.92 |    0.0% |      0.01 | `IoWindow Relaxed, read, 1 bytes`
|              508.53 |        1,966,451.31 |    0.1% |      0.01 | `IoWindow Relaxed, read, 2 bytes`
|              252.80 |        3,955,681.21 |    0.0% |      0.01 | `IoWindow Relaxed, read, 4 bytes`
|              252.59 |        3,959,060.48 |    0.1% |      0.01 | `IoWindow Relaxed, read, 8 bytes`
|               30.54 |       32,741,341.27 |    0.0% |      0.01 | `IoWindow Relaxed, read, 1024 bytes`
|               30.54 |       32,738,724.31 |    0.0% |      0.01 | `IoWindow Relaxed, read, 2048 bytes`
|               30.53 |       32,749,514.72 |    0.0% |      0.01 | `IoWindow Relaxed, read, 4096 bytes`
|               30.54 |       32,743,848.14 |    0.0% |      0.01 | `IoWindow Relaxed, read, 8192 bytes`
|               30.57 |       32,710,465.36 |    0.0% |      0.35 | `IoWindow Relaxed, read, 1048576 bytes`
|            1,581.24 |          632,415.33 |    0.0% |      0.01 | `IoWindow Strict, write, 1 bytes`
|              791.11 |        1,264,039.56 |    0.0% |      0.01 | `IoWindow Strict, write, 2 bytes`
|                9.53 |      104,949,778.80 |    1.6% |      0.01 | `IoWindow Strict, write, 4 bytes`
|                8.64 |      115,772,161.25 |    0.2% |      0.01 | `IoWindow Strict, write, 8 bytes`
|                7.09 |      140,972,253.28 |    0.0% |      0.01 | `IoWindow Strict, write, 1024 bytes`
|                7.09 |      140,969,777.13 |    0.0% |      0.01 | `IoWindow Strict, write, 2048 bytes`
|                7.09 |      140,981,227.56 |    0.0% |      0.01 | `IoWindow Strict, write, 4096 bytes`
|                7.07 |      141,477,446.08 |    0.7% |      0.01 | `IoWindow Strict, write, 8192 bytes`
|                7.13 |      140,262,057.68 |    0.1% |      0.08 | `IoWindow Strict, write, 1048576 bytes`
|            1,016.48 |          983,789.17 |    0.0% |      0.01 | `IoWindow Strict, read, 1 bytes`
|              508.50 |        1,966,570.23 |    0.0% |      0.01 | `IoWindow Strict, read, 2 bytes`
|              252.80 |        3,955,683.92 |    0.0% |      0.01 | `IoWindow Strict, read, 4 bytes`
|              252.79 |        3,955,797.60 |    0.0% |      0.01 | `IoWindow Strict, read, 8 bytes`
|               30.55 |       32,729,628.97 |    0.0% |      0.01 | `IoWindow Strict, read, 1024 bytes`
|               30.55 |       32,733,200.97 |    0.0% |      0.01 | `IoWindow Strict, read, 2048 bytes`
|               30.55 |       32,735,206.60 |    0.0% |      0.01 | `IoWindow Strict, read, 4096 bytes`
|               30.55 |       32,734,427.57 |    0.0% |      0.01 | `IoWindow Strict, read, 8192 bytes`
|               30.58 |       32,699,038.75 |    0.0% |      0.35 | `IoWindow Strict, read, 1048576 bytes`
```

### Ethernet
```
|             ns/byte |              byte/s |    err% |     total | TLB_Ethernet
|--------------------:|--------------------:|--------:|----------:|:-------------
|            1,688.04 |          592,403.66 |    0.2% |      0.01 | `Cluster Strict, write, 1 bytes`
|              843.12 |        1,186,072.95 |    0.1% |      0.01 | `Cluster Strict, write, 2 bytes`
|              129.09 |        7,746,439.19 |    0.0% |      0.01 | `Cluster Strict, write, 4 bytes`
|               64.57 |       15,487,443.94 |    0.0% |      0.01 | `Cluster Strict, write, 8 bytes`
|                7.17 |      139,472,255.03 |    0.0% |      0.01 | `Cluster Strict, write, 1024 bytes`
|                7.14 |      140,098,910.48 |    0.0% |      0.01 | `Cluster Strict, write, 2048 bytes`
|                7.12 |      140,416,026.21 |    0.0% |      0.01 | `Cluster Strict, write, 4096 bytes`
|                7.11 |      140,578,358.45 |    0.0% |      0.01 | `Cluster Strict, write, 8192 bytes`
|                7.10 |      140,783,529.43 |    0.3% |      0.01 | `Cluster Strict, write, 131072 bytes`
|            1,276.14 |          783,610.81 |    0.6% |      0.01 | `Cluster Strict, read, 1 bytes`
|              633.44 |        1,578,679.77 |    0.2% |      0.01 | `Cluster Strict, read, 2 bytes`
|              311.93 |        3,205,891.66 |    0.1% |      0.01 | `Cluster Strict, read, 4 bytes`
|              282.84 |        3,535,568.29 |    0.4% |      0.01 | `Cluster Strict, read, 8 bytes`
|               30.87 |       32,393,701.58 |    0.2% |      0.01 | `Cluster Strict, read, 1024 bytes`
|               30.80 |       32,465,815.03 |    0.0% |      0.01 | `Cluster Strict, read, 2048 bytes`
|               30.63 |       32,652,898.53 |    0.0% |      0.01 | `Cluster Strict, read, 4096 bytes`
|               30.60 |       32,683,389.24 |    0.0% |      0.01 | `Cluster Strict, read, 8192 bytes`
|               30.73 |       32,541,155.60 |    0.1% |      0.04 | `Cluster Strict, read, 131072 bytes`
|            1,604.45 |          623,266.98 |    1.0% |      0.01 | `IoWindow Relaxed, write, 1 bytes`
|              791.43 |        1,263,534.24 |    0.1% |      0.01 | `IoWindow Relaxed, write, 2 bytes`
|                9.52 |      105,088,841.29 |    0.8% |      0.01 | `IoWindow Relaxed, write, 4 bytes`
|                8.67 |      115,296,170.14 |    0.4% |      0.01 | `IoWindow Relaxed, write, 8 bytes`
|                0.47 |    2,135,561,940.11 |    0.0% |      0.01 | `IoWindow Relaxed, write, 1024 bytes`
|                0.47 |    2,135,659,857.05 |    0.0% |      0.01 | `IoWindow Relaxed, write, 2048 bytes`
|                0.47 |    2,136,016,333.02 |    0.0% |      0.01 | `IoWindow Relaxed, write, 4096 bytes`
|                0.47 |    2,135,851,116.16 |    0.0% |      0.01 | `IoWindow Relaxed, write, 8192 bytes`
|                0.47 |    2,135,563,366.69 |    0.0% |      0.01 | `IoWindow Relaxed, write, 131072 bytes`
|            1,016.43 |          983,836.55 |    0.1% |      0.01 | `IoWindow Relaxed, read, 1 bytes`
|              508.30 |        1,967,332.52 |    0.0% |      0.01 | `IoWindow Relaxed, read, 2 bytes`
|              252.80 |        3,955,649.82 |    0.0% |      0.01 | `IoWindow Relaxed, read, 4 bytes`
|              252.80 |        3,955,688.82 |    0.2% |      0.01 | `IoWindow Relaxed, read, 8 bytes`
|               30.53 |       32,752,133.41 |    0.0% |      0.01 | `IoWindow Relaxed, read, 1024 bytes`
|               30.53 |       32,755,334.60 |    0.0% |      0.01 | `IoWindow Relaxed, read, 2048 bytes`
|               30.53 |       32,755,618.38 |    0.0% |      0.01 | `IoWindow Relaxed, read, 4096 bytes`
|               30.53 |       32,757,222.87 |    0.0% |      0.01 | `IoWindow Relaxed, read, 8192 bytes`
|               30.58 |       32,704,504.21 |    0.1% |      0.04 | `IoWindow Relaxed, read, 131072 bytes`
|            1,581.41 |          632,347.96 |    0.1% |      0.01 | `IoWindow Strict, write, 1 bytes`
|              790.92 |        1,264,355.91 |    0.1% |      0.01 | `IoWindow Strict, write, 2 bytes`
|                9.47 |      105,557,975.98 |    0.5% |      0.01 | `IoWindow Strict, write, 4 bytes`
|                8.63 |      115,849,151.34 |    0.2% |      0.01 | `IoWindow Strict, write, 8 bytes`
|                7.09 |      140,958,723.54 |    0.0% |      0.01 | `IoWindow Strict, write, 1024 bytes`
|                7.09 |      140,961,788.89 |    0.0% |      0.01 | `IoWindow Strict, write, 2048 bytes`
|                7.07 |      141,389,106.55 |    0.1% |      0.01 | `IoWindow Strict, write, 4096 bytes`
|                7.09 |      141,034,921.79 |    0.3% |      0.01 | `IoWindow Strict, write, 8192 bytes`
|                6.97 |      143,532,957.21 |    2.1% |      0.01 | `IoWindow Strict, write, 131072 bytes`
|            1,016.95 |          983,328.99 |    0.0% |      0.01 | `IoWindow Strict, read, 1 bytes`
|              508.53 |        1,966,462.73 |    0.0% |      0.01 | `IoWindow Strict, read, 2 bytes`
|              252.80 |        3,955,745.10 |    0.0% |      0.01 | `IoWindow Strict, read, 4 bytes`
|              252.80 |        3,955,645.06 |    0.0% |      0.01 | `IoWindow Strict, read, 8 bytes`
|               30.56 |       32,720,912.68 |    0.0% |      0.01 | `IoWindow Strict, read, 1024 bytes`
|               30.56 |       32,719,869.07 |    0.0% |      0.01 | `IoWindow Strict, read, 2048 bytes`
|               30.55 |       32,729,393.86 |    0.0% |      0.01 | `IoWindow Strict, read, 4096 bytes`
|               30.56 |       32,727,051.91 |    0.0% |      0.01 | `IoWindow Strict, read, 8192 bytes`
|               30.61 |       32,669,786.45 |    0.1% |      0.04 | `IoWindow Strict, read, 131072 bytes`
```
