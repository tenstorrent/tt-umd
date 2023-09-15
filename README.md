# UMD

## Dependencies

Required Ubuntu dependencies:

```
sudo apt install -y libyaml-cpp-dev libhwloc-dev libgtest-dev libboost-dev
```
## Build

This target builds `libdevice.so`. Specify the `ARCH_NAME` environment variable when building (`wormhole_b0` or `grayskull`):

```
make build
```

## Test

Run this target to build library, and gtest suite.

```
make test
```

Running test suite:

```
make run
```

## Clean

```
make clean
```

## Device Selection

Change the `ARCH_NAME` flag in the top-level Makefile or run:

```
make build ARCH_NAME=...
make test ARCH_NAME=...
```
