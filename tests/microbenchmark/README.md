# UMD Microbenchmarks

This repository contains microbenchmarks for evaluating the performance of key components in the UMD system. Each benchmark is self-contained and designed to provide fine-grained measurements on specific algorithms, data structures, or critical paths.

---

## Directory Structure

- **`benchmarks/`**  
  One directory per benchmark (e.g., `test_tlb/`, `test_pcie_dma/`). Each contains:
  - `README.md` – details about the benchmark’s purpose and usage, as well as results of the benchmark.
  - Benchmark source code - multiple tests for the part of the system that is being tested

- **`common/`**  
  Shared utilities used across benchmarks, such as timing helpers or data generators.

---

## Building the benchmarks

In order to build UMD benchmarks, run following commands from **UMD root directory**

```bash
cmake -B build -G Ninja -DTT_UMD_BUILD_TESTS=ON
ninja ubench -C build
```

## Running benchmarks

In order to run benchmarks, run following command from **UMD root directory**

```bash
./build/test/umd/ubenchmarks/ubench
```

In order to filter specific benchmarks, you can use gtest filter

```bash
./build/test/umd/ubenchmarks/ubench --gtest_filter=MicrobenchmarkTlb.*
```

## Contribution guide

Follow these steps to add new tests to the suite

1. Create new directory inside **benchmarks** directory
2. Inside this directory, create C++ file named ```test_<component>.cpp```
3. Write your tests inside the file, following the rules
    - All tests in the file should be in the group ```Microbenchmark<component>```
    - Every test in the file should have comment above it explaining what is it testing
4. Create README explaining what is the benchmark testing, as well as more details if needed to understand the test suite

You can look at [TLB benchmark directory](./benchmarks/tlb/) as an example.

## Timing tool

At the moment, ```std::chrono``` is used to measure time for performance inside UMD microbenchmarks. This should be improved in the future by a more sophisticated tool.

## Machine specification

This section specifies machine specification for all configurations

### N150 Machine Configuration

**PCIe:** Gen4 x16  
**CPU:** AMD EPYC 7352 24-Core Processor (96 cores)  
**Clock Speed:** 2.3 GHz  

**Host Specifications:**
  | Metric             | Value                      |
  |--------------------|----------------------------|
  | Timestamp          | 2025-11-19T08:07:13.530674 |
  | OS                 | Linux                      |
  | Distro             | Ubuntu 22.04.5 LTS         |
  | Kernel             | 5.15.0-157-generic         |
  | Hostname           | c7dc3264cac9               |
  | Platform           | x86_64                     |
  | Python             | 3.10.12                    |
  | Memory             | 54.92 GB                   |
  | Driver             | TT-KMD 2.4.0               |
  | Hugepages          | 0                          |
  | CPU_Governor       | N/A                        |
  | CPU_Cores_Phys_Log | 16/16                      |
  | TT_PCIe_Info       | x16 Gen4                   |

## List of benchmarks

List of benchmarks in UMD at the moment is

- [TLB IO benchmark](benchmarks/tlb/README.md)
- [PCIe DMA benchmark](benchmarks/pcie_dma/README.md)
- [IOMMU benchmark](benchmarks/iommu/README.md)
- [Ethernet IO benchmark](benchmarks/ethernet_io/README.md)