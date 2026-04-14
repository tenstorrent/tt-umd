# IOMMU benchmark

This benchmark contains tests that are measuring performance of different IOMMU operations through UMD and KMD.

IOMMU operations that UMD does are mapping the buffer through IOMMU and unampping it when it is not needed anymore. Both operations are done by making ioctl call to KMD.

## Results

### Mapping different sizes with IOMMU

| Name                   |   Batch size | Throughput   | Total Time   |
|:-----------------------|-------------:|:-------------|:-------------|
| Map 4096 bytes         |         4096 | 156.94 MiB/s | 2.5438 ms    |
| Unmap 4096 bytes       |         4096 | 143.82 MiB/s | 2.7580 ms    |
| Map 8192 bytes         |         8192 | 278.18 MiB/s | 2.8591 ms    |
| Unmap 8192 bytes       |         8192 | 219.79 MiB/s | 3.6009 ms    |
| Map 16384 bytes        |        16384 | 448.80 MiB/s | 3.5112 ms    |
| Unmap 16384 bytes      |        16384 | 300.94 MiB/s | 5.2819 ms    |
| Map 32768 bytes        |        32768 | 665.74 MiB/s | 4.7633 ms    |
| Unmap 32768 bytes      |        32768 | 371.12 MiB/s | 8.8764 ms    |
| Map 65536 bytes        |        65536 | 875.04 MiB/s | 7.2397 ms    |
| Unmap 65536 bytes      |        65536 | 421.29 MiB/s | 14.9727 ms   |
| Map 131072 bytes       |       131072 | 1.02 GiB/s   | 12.7788 ms   |
| Unmap 131072 bytes     |       131072 | 451.67 MiB/s | 27.8899 ms   |
| Map 262144 bytes       |       262144 | 1.12 GiB/s   | 24.2245 ms   |
| Unmap 262144 bytes     |       262144 | 466.62 MiB/s | 54.4420 ms   |
| Map 524288 bytes       |       524288 | 1.18 GiB/s   | 42.3938 ms   |
| Unmap 524288 bytes     |       524288 | 476.11 MiB/s | 106.2774 ms  |
| Map 1048576 bytes      |      1048576 | 1.19 GiB/s   | 84.0895 ms   |
| Unmap 1048576 bytes    |      1048576 | 476.43 MiB/s | 213.0898 ms  |
| Map 2097152 bytes      |      2097152 | 1.20 GiB/s   | 163.9060 ms  |
| Unmap 2097152 bytes    |      2097152 | 554.83 MiB/s | 363.5838 ms  |
| Map 4194304 bytes      |      4194304 | 1.13 GiB/s   | 348.9794 ms  |
| Unmap 4194304 bytes    |      4194304 | 553.96 MiB/s | 734.2090 ms  |
| Map 8388608 bytes      |      8388608 | 1.11 GiB/s   | 711.6350 ms  |
| Unmap 8388608 bytes    |      8388608 | 553.48 MiB/s | 1.4620 s     |
| Map 16777216 bytes     |     16777216 | 1.22 GiB/s   | 1.2999 s     |
| Unmap 16777216 bytes   |     16777216 | 575.09 MiB/s | 2.8127 s     |
| Map 33554432 bytes     |     33554432 | 1.24 GiB/s   | 2.5350 s     |
| Unmap 33554432 bytes   |     33554432 | 572.55 MiB/s | 5.5951 s     |
| Map 67108864 bytes     |     67108864 | 1.27 GiB/s   | 4.9642 s     |
| Unmap 67108864 bytes   |     67108864 | 572.56 MiB/s | 11.2045 s    |
| Map 134217728 bytes    |    134217728 | 1.28 GiB/s   | 9.8172 s     |
| Unmap 134217728 bytes  |    134217728 | 566.93 MiB/s | 22.6328 s    |
| Map 268435456 bytes    |    268435456 | 1.28 GiB/s   | 19.6021 s    |
| Unmap 268435456 bytes  |    268435456 | 562.44 MiB/s | 45.5924 s    |
| Map 536870912 bytes    |    536870912 | 1.27 GiB/s   | 39.5434 s    |
| Unmap 536870912 bytes  |    536870912 | 561.04 MiB/s | 91.4178 s    |
| Map 1073741824 bytes   |   1073741824 | 1.27 GiB/s   | 79.5407 s    |
| Unmap 1073741824 bytes |   1073741824 | 565.43 MiB/s | 181.3292 s   |
