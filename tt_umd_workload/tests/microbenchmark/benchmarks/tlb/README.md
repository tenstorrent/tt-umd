# TLB benchmark

This benchmark contains tests that are measuring performance of multiple usages of TLBs inside UMD.

To get a better understanding of what TLBs are, you can look at [Tenstorrent ISA documentation](https://github.com/tenstorrent/tt-isa-documentation/blob/main/WormholeB0/PCIExpressTile/TLBs.md).

## Example results

### DRAM
```
|             ns/byte |              byte/s |    err% |     total | TLB_DRAM
|--------------------:|--------------------:|--------:|----------:|:---------
|            1,368.63 |          730,656.25 |    0.1% |      0.01 | `Dynamic TLB, write, 1 bytes`
|              684.85 |        1,460,183.91 |    0.1% |      0.01 | `Dynamic TLB, write, 2 bytes`
|              113.58 |        8,804,586.98 |    0.1% |      0.01 | `Dynamic TLB, write, 4 bytes`
|               56.73 |       17,627,165.23 |    0.1% |      0.01 | `Dynamic TLB, write, 8 bytes`
|                0.54 |    1,866,772,127.75 |    0.0% |      0.01 | `Dynamic TLB, write, 1024 bytes`
|                0.47 |    2,133,470,379.17 |    0.0% |      0.01 | `Dynamic TLB, write, 2048 bytes`
|                0.43 |    2,321,584,738.43 |    0.0% |      0.01 | `Dynamic TLB, write, 4096 bytes`
|                0.41 |    2,446,006,493.14 |    0.2% |      0.01 | `Dynamic TLB, write, 8192 bytes`
|                0.48 |    2,082,648,436.29 |    0.2% |      0.01 | `Dynamic TLB, write, 1048576 bytes`
|                0.48 |    2,087,994,058.06 |    0.3% |      0.01 | `Dynamic TLB, write, 2097152 bytes`
|                0.48 |    2,084,332,314.10 |    0.1% |      0.02 | `Dynamic TLB, write, 4194304 bytes`
|                0.48 |    2,084,242,721.64 |    0.0% |      0.04 | `Dynamic TLB, write, 8388608 bytes`
|                0.48 |    2,082,053,985.52 |    0.0% |      0.09 | `Dynamic TLB, write, 16777216 bytes`
|                0.48 |    2,080,155,310.97 |    0.1% |      0.18 | `Dynamic TLB, write, 33554432 bytes`
|            1,293.37 |          773,176.67 |    0.0% |      0.01 | `Dynamic TLB, read, 1 bytes`
|              649.36 |        1,539,980.76 |    0.4% |      0.01 | `Dynamic TLB, read, 2 bytes`
|              323.41 |        3,092,002.81 |    0.0% |      0.01 | `Dynamic TLB, read, 4 bytes`
|              270.37 |        3,698,595.73 |    0.1% |      0.01 | `Dynamic TLB, read, 8 bytes`
|              215.45 |        4,641,511.25 |    0.0% |      0.01 | `Dynamic TLB, read, 1024 bytes`
|              215.39 |        4,642,653.93 |    0.1% |      0.01 | `Dynamic TLB, read, 2048 bytes`
|              215.46 |        4,641,286.14 |    0.2% |      0.01 | `Dynamic TLB, read, 4096 bytes`
|              215.15 |        4,647,856.24 |    0.0% |      0.02 | `Dynamic TLB, read, 8192 bytes`
|              215.14 |        4,648,210.91 |    0.0% |      2.48 | `Dynamic TLB, read, 1048576 bytes`
|              215.11 |        4,648,834.08 |    0.0% |      4.96 | `Dynamic TLB, read, 2097152 bytes`
|              215.14 |        4,648,078.24 |    0.0% |      9.93 | `Dynamic TLB, read, 4194304 bytes`
|              215.14 |        4,648,053.75 |    0.0% |     19.85 | `Dynamic TLB, read, 8388608 bytes`
|              215.16 |        4,647,734.47 |    0.0% |     39.71 | `Dynamic TLB, read, 16777216 bytes`
|              215.20 |        4,646,880.18 |    0.0% |     79.43 | `Dynamic TLB, read, 33554432 bytes`
|            1,316.37 |          759,663.81 |    0.0% |      0.01 | `Static TLB, write, 1 bytes`
|              658.52 |        1,518,554.02 |    0.0% |      0.01 | `Static TLB, write, 2 bytes`
|               22.93 |       43,616,028.19 |    0.0% |      0.01 | `Static TLB, write, 4 bytes`
|               11.47 |       87,158,408.00 |    0.1% |      0.01 | `Static TLB, write, 8 bytes`
|                0.38 |    2,618,420,846.92 |    0.0% |      0.01 | `Static TLB, write, 1024 bytes`
|                0.38 |    2,618,352,932.03 |    0.0% |      0.01 | `Static TLB, write, 2048 bytes`
|                0.38 |    2,618,643,445.14 |    0.0% |      0.01 | `Static TLB, write, 4096 bytes`
|                0.38 |    2,615,896,850.77 |    0.0% |      0.01 | `Static TLB, write, 8192 bytes`
|                0.38 |    2,613,497,146.59 |    0.1% |      0.01 | `Static TLB, write, 1048576 bytes`
|                0.38 |    2,617,726,101.09 |    0.1% |      0.01 | `Static TLB, write, 2097152 bytes`
|                0.38 |    2,616,667,852.00 |    0.1% |      0.02 | `Static TLB, write, 4194304 bytes`
|                0.38 |    2,614,787,816.37 |    0.0% |      0.04 | `Static TLB, write, 8388608 bytes`
|                0.38 |    2,614,544,954.73 |    0.0% |      0.07 | `Static TLB, write, 16777216 bytes`
|                0.48 |    2,080,574,889.53 |    0.0% |      0.18 | `Static TLB, write, 33554432 bytes`
|              959.94 |        1,041,734.55 |    0.0% |      0.01 | `Static TLB, read, 1 bytes`
|              479.96 |        2,083,488.49 |    0.0% |      0.01 | `Static TLB, read, 2 bytes`
|              239.99 |        4,166,796.72 |    0.0% |      0.01 | `Static TLB, read, 4 bytes`
|              223.45 |        4,475,193.40 |    0.1% |      0.01 | `Static TLB, read, 8 bytes`
|              215.28 |        4,645,035.80 |    0.0% |      0.01 | `Static TLB, read, 1024 bytes`
|              215.19 |        4,647,025.72 |    0.1% |      0.01 | `Static TLB, read, 2048 bytes`
|              215.21 |        4,646,577.63 |    0.2% |      0.01 | `Static TLB, read, 4096 bytes`
|              215.23 |        4,646,174.42 |    0.1% |      0.02 | `Static TLB, read, 8192 bytes`
|              215.20 |        4,646,788.47 |    0.0% |      2.48 | `Static TLB, read, 1048576 bytes`
|              215.15 |        4,647,909.50 |    0.0% |      4.96 | `Static TLB, read, 2097152 bytes`
|              215.11 |        4,648,712.53 |    0.0% |      9.93 | `Static TLB, read, 4194304 bytes`
|              215.11 |        4,648,681.43 |    0.0% |     19.85 | `Static TLB, read, 8388608 bytes`
|              215.25 |        4,645,751.59 |    0.0% |     39.72 | `Static TLB, read, 16777216 bytes`
|              215.26 |        4,645,648.24 |    0.0% |     79.46 | `Static TLB, read, 33554432 bytes`
```

### Tensix
```
|             ns/byte |              byte/s |    err% |     total | TLB_Tensix
|--------------------:|--------------------:|--------:|----------:|:-----------
|            1,544.17 |          647,597.51 |    0.2% |      0.01 | `Dynamic TLB, write, 1 bytes`
|              770.74 |        1,297,457.18 |    0.1% |      0.01 | `Dynamic TLB, write, 2 bytes`
|              129.09 |        7,746,512.92 |    0.0% |      0.01 | `Dynamic TLB, write, 4 bytes`
|               64.55 |       15,492,921.29 |    0.0% |      0.01 | `Dynamic TLB, write, 8 bytes`
|                0.63 |    1,579,983,783.93 |    0.0% |      0.01 | `Dynamic TLB, write, 1024 bytes`
|                0.55 |    1,816,443,887.15 |    0.0% |      0.01 | `Dynamic TLB, write, 2048 bytes`
|                0.51 |    1,963,261,127.06 |    0.0% |      0.01 | `Dynamic TLB, write, 4096 bytes`
|                0.49 |    2,045,921,985.12 |    0.0% |      0.01 | `Dynamic TLB, write, 8192 bytes`
|                0.47 |    2,135,627,464.40 |    0.1% |      0.01 | `Dynamic TLB, write, 1048576 bytes`
|            1,387.13 |          720,912.22 |    0.1% |      0.01 | `Dynamic TLB, read, 1 bytes`
|              693.36 |        1,442,245.35 |    0.1% |      0.01 | `Dynamic TLB, read, 2 bytes`
|              346.55 |        2,885,567.89 |    0.1% |      0.01 | `Dynamic TLB, read, 4 bytes`
|              292.76 |        3,415,760.94 |    0.1% |      0.01 | `Dynamic TLB, read, 8 bytes`
|              238.44 |        4,193,850.93 |    0.0% |      0.01 | `Dynamic TLB, read, 1024 bytes`
|              238.20 |        4,198,106.34 |    0.0% |      0.01 | `Dynamic TLB, read, 2048 bytes`
|              238.12 |        4,199,505.20 |    0.0% |      0.01 | `Dynamic TLB, read, 4096 bytes`
|              238.47 |        4,193,460.21 |    0.1% |      0.02 | `Dynamic TLB, read, 8192 bytes`
|              238.15 |        4,198,957.00 |    0.0% |      2.75 | `Dynamic TLB, read, 1048576 bytes`
|            1,510.41 |          662,071.23 |    0.0% |      0.01 | `Static TLB, write, 1 bytes`
|              755.30 |        1,323,984.06 |    0.0% |      0.01 | `Static TLB, write, 2 bytes`
|               23.22 |       43,058,340.57 |    0.4% |      0.01 | `Static TLB, write, 4 bytes`
|               12.06 |       82,912,393.34 |    0.2% |      0.01 | `Static TLB, write, 8 bytes`
|                0.47 |    2,136,112,646.57 |    0.0% |      0.01 | `Static TLB, write, 1024 bytes`
|                0.47 |    2,136,388,864.52 |    0.0% |      0.01 | `Static TLB, write, 2048 bytes`
|                0.47 |    2,136,553,360.41 |    0.0% |      0.01 | `Static TLB, write, 4096 bytes`
|                0.47 |    2,135,193,641.69 |    0.0% |      0.01 | `Static TLB, write, 8192 bytes`
|                0.47 |    2,137,595,303.14 |    0.1% |      0.01 | `Static TLB, write, 1048576 bytes`
|            1,029.11 |          971,713.32 |    0.2% |      0.01 | `Static TLB, read, 1 bytes`
|              513.76 |        1,946,447.87 |    0.1% |      0.01 | `Static TLB, read, 2 bytes`
|              256.72 |        3,895,233.00 |    0.1% |      0.01 | `Static TLB, read, 4 bytes`
|              248.26 |        4,028,097.45 |    0.1% |      0.01 | `Static TLB, read, 8 bytes`
|              237.99 |        4,201,904.81 |    0.0% |      0.01 | `Static TLB, read, 1024 bytes`
|              238.06 |        4,200,603.43 |    0.0% |      0.01 | `Static TLB, read, 2048 bytes`
|              237.98 |        4,202,077.24 |    0.0% |      0.01 | `Static TLB, read, 4096 bytes`
|              238.45 |        4,193,745.73 |    0.1% |      0.02 | `Static TLB, read, 8192 bytes`
|              238.05 |        4,200,816.49 |    0.0% |      2.75 | `Static TLB, read, 1048576 bytes`
```
### Ethernet

```
|             ns/byte |              byte/s |    err% |     total | TLB_Ethernet
|--------------------:|--------------------:|--------:|----------:|:-------------
|            1,530.12 |          653,542.58 |    0.0% |      0.01 | `Dynamic TLB, write, 1 bytes`
|              765.12 |        1,306,981.41 |    0.0% |      0.01 | `Dynamic TLB, write, 2 bytes`
|              129.10 |        7,745,690.51 |    0.0% |      0.01 | `Dynamic TLB, write, 4 bytes`
|               64.56 |       15,490,356.10 |    0.0% |      0.01 | `Dynamic TLB, write, 8 bytes`
|                0.63 |    1,578,441,940.61 |    0.0% |      0.01 | `Dynamic TLB, write, 1024 bytes`
|                0.55 |    1,814,457,399.66 |    0.0% |      0.01 | `Dynamic TLB, write, 2048 bytes`
|                0.51 |    1,960,052,749.82 |    0.0% |      0.01 | `Dynamic TLB, write, 4096 bytes`
|                0.49 |    2,042,481,031.22 |    0.0% |      0.01 | `Dynamic TLB, write, 8192 bytes`
|                0.59 |    1,707,788,885.58 |    0.2% |      0.01 | `Dynamic TLB, write, 131072 bytes`
|            1,386.73 |          721,118.94 |    0.0% |      0.01 | `Dynamic TLB, read, 1 bytes`
|              693.15 |        1,442,680.10 |    0.0% |      0.01 | `Dynamic TLB, read, 2 bytes`
|              345.78 |        2,891,971.16 |    0.0% |      0.01 | `Dynamic TLB, read, 4 bytes`
|              291.50 |        3,430,519.82 |    0.0% |      0.01 | `Dynamic TLB, read, 8 bytes`
|              238.35 |        4,195,423.14 |    0.0% |      0.01 | `Dynamic TLB, read, 1024 bytes`
|              238.34 |        4,195,663.80 |    0.1% |      0.01 | `Dynamic TLB, read, 2048 bytes`
|              238.17 |        4,198,687.30 |    0.0% |      0.01 | `Dynamic TLB, read, 4096 bytes`
|              238.52 |        4,192,462.27 |    0.0% |      0.02 | `Dynamic TLB, read, 8192 bytes`
|              238.21 |        4,197,911.92 |    0.0% |      0.34 | `Dynamic TLB, read, 131072 bytes`
|            1,501.16 |          666,150.77 |    0.0% |      0.01 | `Static TLB, write, 1 bytes`
|              750.79 |        1,331,928.65 |    0.1% |      0.01 | `Static TLB, write, 2 bytes`
|               22.80 |       43,850,274.87 |    0.1% |      0.01 | `Static TLB, write, 4 bytes`
|               11.40 |       87,717,681.75 |    0.1% |      0.01 | `Static TLB, write, 8 bytes`
|                0.47 |    2,135,792,821.99 |    0.0% |      0.01 | `Static TLB, write, 1024 bytes`
|                0.47 |    2,135,845,651.78 |    0.0% |      0.01 | `Static TLB, write, 2048 bytes`
|                0.47 |    2,135,869,159.69 |    0.0% |      0.01 | `Static TLB, write, 4096 bytes`
|                0.47 |    2,134,137,339.94 |    0.0% |      0.01 | `Static TLB, write, 8192 bytes`
|                0.61 |    1,632,685,600.40 |    0.2% |      0.01 | `Static TLB, write, 131072 bytes`
|            1,017.84 |          982,471.79 |    0.2% |      0.01 | `Static TLB, read, 1 bytes`
|              509.57 |        1,962,447.32 |    0.1% |      0.01 | `Static TLB, read, 2 bytes`
|              254.05 |        3,936,254.75 |    0.1% |      0.01 | `Static TLB, read, 4 bytes`
|              245.64 |        4,071,036.47 |    0.0% |      0.01 | `Static TLB, read, 8 bytes`
|              238.05 |        4,200,814.52 |    0.0% |      0.01 | `Static TLB, read, 1024 bytes`
|              238.03 |        4,201,146.29 |    0.0% |      0.01 | `Static TLB, read, 2048 bytes`
|              238.03 |        4,201,137.67 |    0.0% |      0.01 | `Static TLB, read, 4096 bytes`
|              238.02 |        4,201,322.97 |    0.0% |      0.02 | `Static TLB, read, 8192 bytes`
|              238.17 |        4,198,613.73 |    0.0% |      0.34 | `Static TLB, read, 131072 bytes`
```