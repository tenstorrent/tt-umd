# Emulation
Emulation is done using Zebu EP1 emulator. Zebu is a HW emulator that
allows us to run tensix RTL on it. It is a HW board with FPGA and
custom HW that can run tensix RTL. It is connected to a PC that runs
Zebu software. Zebu software is used to compile tensix RTL and run
emulation. It also provides a way to connect to the board and
communicate with it. Zebu software is installed on a separate machine
that has access to Zebu board.



## Emulation UMD device
tt_emulation_device implements interface to the emulation UMD device.
Implemened are device open/close, axi read/write and tensix
reset assert/deassert. It uses a wrapper around the Zebu model to
communicate with the emulated device. This wrapper is implemented
separately and compiled into a shared library - `libtt_zebu_wrapper.so`.
This library is located in a shared location specified by
`TENSIX_EMULATION_ROOT` env variable, and there you can find needed
header files as well, and also compiled Zebu model and scripts needed
for emulation.

## `TENSIX_EMULATION_ROOT` location:
Currently my local folder is used, but we will move this to a shared
location later.
`export TENSIX_EMULATION_ROOT=/proj_soc/mlausin/emu_buda/ws_buda_1/tensix_emulation_targets`

Contents of `TENSIX_EMULATION_ROOT`:
```bash 
targets/tensix_2x2_1dram_BH/zcui.work - Zebu model of 2x2 grid with 2 tensix and 1 dram endpoint
zebu/include - contains header files for zebu wrapper library
zebu/lib - contains zebu wrapper library
zebu/scripts - contains scripts for running emulation
```

More details about tensix emulation and used commands can be found on
original tensix emulation repo's README file:
https://yyz-tensix-gitlab.local.tenstorrent.com/tensix-hw/tensix_emulation

## Machines:
Current setup is using machines in austin for compiling hw/sw and
running emulation:
```bash 
aus-rv-l-7 # used for compiling hw/sw
aus-rv-zebu-03 # used for running emulation
```


## Env setup/compile/run commands:
### Container setup and run:
To compile and run UMD with emulation device, you need to use a
container. This container is located in
`umd/emulation_rocky8.def`
To build the container, run:
```bash 
apptainer build emulation_rocky8.sif emulation_rocky8.def
```
To run the container:
```bash
# to compile sw on aus-rv-l-7, start container with: 
apptainer shell -s /usr/bin/bash -B /tools_soc/,/tools_vendor/,/site/lsf/,/site/zebu/,/proj_soc/ emulation_rocky8.sif
# to run emu on aus-rv-zebu-03, start container with: 
apptainer shell -s /usr/bin/bash -B /tools_soc/,/tools_vendor/,/site/lsf/,/site/zebu/,/proj_soc/,/zebu/ emulation_rocky8.sif
```

### `.bashrc`:
I have this in my `.bashrc` file to setup initial env inside container:
```bash
if [ "${APPTAINER_NAME}" == "emulation_rocky8.sif" ]; then
    echo "Running inside apptainer shell for BudaBackEnd sw emulation"

    source /site/lsf/aus-hw/conf/profile.lsf # for bsub
    source /tools_soc/tt/Modules/init/bash   # for module load
    module load python
    source /opt/rh/gcc-toolset-10/enable     # for gcc 10
	export LD_LIBRARY_PATH="/usr/local/lib:$LD_LIBRARY_PATH"
fi
```

### Compiling sw with emulation device:
Once inside container, before compiling emulation hw/sw or running
emulation, you need to set up the environment:
```bash
export TENSIX_EMULATION_ROOT=/proj_soc/mlausin/emu_buda/ws_buda_1/tensix_emulation_targets
source ${TENSIX_EMULATION_ROOT}/zebu/scripts/env.bash
```

To compile UMD with emulation device:
```bash
make EMULATION_DEVICE_EN=1 build
```

Also a basic test is implemented in `tests/emulation/test_emulation_device.cpp`
To compile the test:
```bash
make EMULATION_DEVICE_EN=1 test
```

### Running sw with emulation device:
To run the test use lsf to reserve ZEBU emulation module 3 and `aus-rv-zebu-03`
**(please try to exit the session when you're done so it gets freed up)**

```bash
#if you are missing bsub, see .bashrc section above
bsub -q zebu-run -R rusage[ep1_module=1.00,ep1_M0=0.00,ep1_M1=0.00,ep1_M2=0.00,ep1_M3=1.00] -m aus-rv-zebu-03 -Is bash
```

Then you need to enter the container and setup env:
```bash
apptainer shell -s /usr/bin/bash -B /tools_soc/,/tools_vendor/,/site/lsf/,/site/zebu/,/proj_soc/,/zebu/ emulation_rocky8.sif
```

Once inside container setup env and run the test:
```bash
export TENSIX_EMULATION_ROOT=/proj_soc/mlausin/emu_buda/ws_buda_1/tensix_emulation_targets
source ${TENSIX_EMULATION_ROOT}/zebu/scripts/env.bash
ZEBU_PHYSICAL_LOCATION=U0.M0=U0.M3 make EMULATION_DEVICE_EN=1 run-emu
```

### Waveform processing and viewing:
Waveforms are generated by emulator, but need to be postprocessed before they can be viewd in verdi
```bash
make EMULATION_DEVICE_EN=1 wave-conv-emu # convert waveform
make EMULATION_DEVICE_EN=1 wave-view-emu # open waveforms in verdi(inside vnc session)
```

## RTL device diagram
This shows curent RTL device setup for emulation. It is a 2x2 grid with
2 tensix, 1 AXI and 1 dram endpoin and these are connected using NOC. Dram endpoint has 2 AXI ports, 
one is connected to NOC0 other to NOC1, and these connect to AXI crossbar to merge trafic before
connecting to memory model.

Axi2noc endpoint is where emulation UMD device is connected and write_to/read_from device functions
are driving data on this interface.


```shell
                                    ┌─────────────────────────────────────────────────────────┐
                                    │ RTL on ZEBU Board                                       │
                                    │                X0                        X1             │
                                    │                                                         │
                                    │       ┌───────────────────┐     ┌───────────────────┐   │
                                    │       │                   │NOC  │                   │   │
                                    │       │      Tensix       ◄─────┤      Tensix       │   │
                                    │ Y1    │      (0,1)        │     │      (1,1)        │   │
                                    │       │                   ├─────►                   │   │
                                    │       │                   │     │                   │   │
                                    │       └───────▲───┬───────┘     └───────▲────┬──────┘   │
                                    │               │   │                     │    │          │
                                    │               │   │ NOC                 │    │ NOC      │
                                    │       ┌───────┴───▼───────┐     ┌───────┴────▼──────┐   │
                                    │       │                   │     │                   │   │
                                    │       │      AXI2NOC      │NOC  │       DRAM        │   │
 ┌─────────────────────────────┐    │ Y0    │      endpoint     ├─────►     endpoint      │   │
 │ SW on PC                    │    │       │       (0,0)       │     │       (1,0)       │   │
 │                             │    │       │                   ◄─────┤                   │   │
 │                             │    │       └──────────▲────────┘     └────┬────────┬─────┘   │
 │     ┌─────────────────┐     │    │                  │                   │  AXI   │         │
 │     │ tt_emu_axi_xtor │     │    │                  │AXI              ┌─▼────────▼──┐      │
 │     │                 │     │    │                  │                 │   AXI XBAR  │      │
 │     │    ┌─────────┐  │     │    │                  │                 └──────┬──────┘      │
 │     │    │ ZEBU AXI│  │     │    │           ┌──────┴──────┐                 │AXI          │
 │     │    │ XTOR SW └──┼─────┼────┼───────────►    ZEBU     │          ┌──────▼──────┐      │
 │     │    │         ◄──┼─────┼────┼───────────┐   AXI XTOR  │          │  MEM model  │      │
 │     │    └─────────┘  │     │    │           │             │          │             │      │
 │     │                 │     │    │           └─────────────┘          └─────────────┘      │
 │     └─────────────────┘     │    │                                                         │
 │                             │    │                                                         │
 └─────────────────────────────┘    └─────────────────────────────────────────────────────────┘
```

## BudaBackEnd specifics

Currently one example BudaBackEnd test `loader/tests/test_pybuda_ram.cpp` is modified to use emulation device.
To compile the test you need to be inside a container and to setup same env as for [emulation UMD device setup/compilation/run](#Env-setup/compile/run-commands:) .

```bash
# compile
make EMULATION_DEVICE_EN=1 -j32  && \
make EMULATION_DEVICE_EN=1 -j32 loader/tests/test_pybuda_ram

# reserve module
bsub -q zebu-run -R rusage[ep1_module=1.00,ep1_M0=0.00,ep1_M1=0.00,ep1_M2=0.00,ep1_M3=1.00] -m aus-rv-zebu-03 -Is bash
# enter container on aus-rv-zebu-03
apptainer shell -s /usr/bin/bash -B /tools_soc/,/tools_vendor/,/site/lsf/,/site/zebu/,/proj_soc/,/zebu/ umd/emulation_rocky8.sif

# setup env
export TENSIX_EMULATION_ROOT=/proj_soc/mlausin/emu_buda/ws_buda_1/tensix_emulation_targets
source ${TENSIX_EMULATION_ROOT}/zebu/scripts/env.bash

# run test
cd build/rundir_zebu
ZEBU_PHYSICAL_LOCATION=U0.M0=U0.M3 ./build/test/loader/tests/test_pybuda_ram --emulation --arch grayskull --netlist /proj_soc/mlausin/emu_buda/ws_buda_1/BudaBackEnd/loader/tests/net_basic/ram.yaml --device-desc ../../device/emulation_1x1_arch.yaml --num-pushes 128 |& tee emu.log
```

