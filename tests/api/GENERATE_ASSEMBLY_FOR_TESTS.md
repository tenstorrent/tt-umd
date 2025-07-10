# How to Generate Assembly from C++ Code Using Godbolt (Compiler Explorer) for Tensix Cores

## Overview

This guide explains how to use [Compiler Explorer (Godbolt)](https://godbolt.org/) to view assembly output generated from C++ source code. It applies to all five Tensix cores:

- BRISC  
- TRISC0, TRISC1, TRISC2  
- NCRISC

---
## Step-by-Step Instructions

### 1. Open Godbolt

- Go to: [https://godbolt.org/](https://godbolt.org/)

### 2. Select the Language

- Choose **C++** as the source language in the editor pane.

### 3. Configure Compiler and Flags

- Select a suitable compiler, e.g., `RISC-V (64-bit) gcc 15.1.0` (used for the `DeassertResetBrisc` example).
- Set the compiler flags:  
  `-Ox -march=rv32i -mabi=ilp32`  
  (replace `x` with the desired optimization level: `O0`, `O1`, `O2`, `O3`, etc.)
- In the **compiler output options**, enable:
  - Compile to binary object  
  - Intel assembly syntax  
  - Demangle identifiers  

**Note:** Optimization levels may reorder instructions. Be cautious if certain operations must occur in a specific sequence (e.g., enabling a write before accessing a register).

### 4. Write or Paste C++ Code

```cpp
// Example C++ code
int main() {
     unsigned int* a = (unsigned int*)0x10000;
     *a = 0x87654000;
     while (true);
}
```

### 5. Copy the assembly from the output:

For the code in section 4. the instrctions look like this:
```asm
main:
    lui	a5,0x10
    lui	a4,0x87654
    sw	a4,0(a5)
.L2:
    j	8 <.L2>
        R_RISCV_RVC_JUMP .L2
```
and the machine instructions in hex look like this:
```cpp
0x000107b7 // lui a5,0x10
0x87654737 // lui a4,0x87654
0x00e7a023 // sw  a4,0(a5)
0x0000006f // jal x0, 0   
```

Here is a link to the example explained above: https://godbolt.org/z/hs4oKMznv

**Note:** The BRISC core always starts running code at address `0x00000000`, while the other cores can start at different, configurable addresses. Because of this, make sure to set the starting addresses for the other cores before running the program.