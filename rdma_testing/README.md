# Cross-host RDMA dma-buf P2P test (bh-glx6u-08 <-> bh-glx6u-18)

Validates `Cluster::export_dmabuf()` end-to-end: NIC on one host does an RDMA WRITE, over the
network, directly into a TLB window on the *other* host's Blackhole card — landing in device NOC
memory with no host-CPU copy on the target side.

Files:
- `rdma_common.hpp` — shared TCP out-of-band handshake + small helpers.
- `dmabuf_target.cpp` — runs on the box whose Blackhole TLB window is the RDMA target. Exports the
  dma-buf via UMD, registers it as an RDMA MR, publishes QP/rkey info, waits for the peer to finish,
  then reads back device memory via UMD (a completely separate path from the exported window) to
  verify the write landed.
- `dmabuf_initiator.cpp` — runs on the peer box. Registers a normal host-memory MR with a known test
  pattern, connects to the target, brings up an RC QP, and issues the RDMA WRITE.

## 0. Prerequisites (both hosts)

```bash
# tt-kmd version — need >= 2.10.0-rc1 for TENSTORRENT_IOCTL_EXPORT_TLB_DMABUF
cat /sys/module/tenstorrent/version

# kernel — need >= 5.8
uname -r

# RDMA NIC present and active
ibv_devices
ibv_devinfo   # note the device name and port link_layer (Ethernet=RoCE vs InfiniBand)

# rdma-core dev headers, if not already present
sudo apt-get install -y libibverbs-dev rdma-core ibverbs-utils
```

If `/sys/module/tenstorrent/version` is older than 2.10.0-rc1, the kmd on that host needs to be
updated first — `export_dmabuf()` will throw `"... requires KMD >= 2.10.0"` otherwise (and the two
new gtests in `tests/unified/test_tlb.cpp` will `GTEST_SKIP()`).

## 1. Build tt-umd + this test pair (both hosts)

`rdma_testing/` is wired into the main tt-umd CMake build behind the `TT_UMD_BUILD_RDMA_TESTS`
option (requires `libibverbs-dev` from the prerequisites step above), so no separate manual
compile step is needed:

```bash
cd tt-umd   # this branch: ajovanovic/rdma_tlw_window_support
cmake -B build -DCMAKE_BUILD_TYPE=Release -DTT_UMD_BUILD_RDMA_TESTS=ON
cmake --build build -j"$(nproc)"
```

Binaries land at `build/rdma_testing/dmabuf_target` and `build/rdma_testing/dmabuf_initiator`.

(`dmabuf_initiator` doesn't link UMD at all — it only speaks RDMA to a plain host buffer.)

## 2. Pick a target address

Any tensix L1 address works as a first smoke test. `addr = 0`, `size = 1 MiB` is the simplest
choice. Note this must stay under Blackhole's `TENSIX_L1_SIZE` (1.5 MiB,
`device/api/umd/device/arch/blackhole_implementation.hpp`) — the 2 MiB window size used in
`tests/unified/test_tlb.cpp`'s export-only gtests works there only because those tests never
actually write data across the full window; this test does, so `addr + size` must fit inside L1
or the RDMA WRITE will land outside valid tensix memory. Use `tools/topology` (already in this
repo, `tools/topology.cpp`) on the target host to confirm chip id 0 and pick a real tensix core
coordinate if you don't want to hardcode (0,0)-ish defaults — the example below defaults to the
first tensix core in TRANSLATED coords, same as the new gtest does.

## 3. Run

On **bh-glx6u-08** (the target — owns the exported dma-buf):
```bash
./build/rdma_testing/dmabuf_target --port 9999 --chip 0 --size 1048576
```
It prints its own QP/rkey info is exchanged automatically over the socket; you don't need to copy
anything by hand. It blocks waiting for a TCP connection from the initiator, then waits for the
initiator's "done" signal, then verifies and prints PASS/FAIL.

On **bh-glx6u-18** (the initiator):
```bash
./build/rdma_testing/dmabuf_initiator --host bh-glx6u-08 --port 9999 --size 1048576
```
It connects, exchanges QP info, posts the RDMA WRITE, polls for local completion, and signals done.

## 4. What "pass" looks like

`dmabuf_target` reads back the same NOC address via `cluster->read_from_device()` (ordinary
register/DMA readback, independent of the TLB window that was exported) and compares it against the
pattern `dmabuf_initiator` wrote. A byte-for-byte match with no `UMD_THROW` along the way confirms:
- `PCIDevice::is_tlb_dmabuf_export_supported()` gated correctly on the live kmd version,
- `Cluster::export_dmabuf()` produced a valid, importable dma-buf fd,
- the peer NIC's RDMA WRITE actually reached the NOC-mapped window with no host involvement on the
  target's data path,
- the window's `FREE_TLB` firing immediately (per the design) didn't invalidate the export while the
  RDMA WRITE was in flight (kmd's pin-until-fd-closed contract held).

## Known gaps / things to double check on real hardware (I couldn't run this — no RDMA NIC or
Blackhole card on this machine, only compiled the pieces I could against local headers)

- **GID index**: `dmabuf_target.cpp`/`dmabuf_initiator.cpp` default to `gid_index=0` and assume
  RoCEv2 (`ibv_query_port` link_layer == Ethernet). If the NICs are InfiniBand instead, switch the
  `use_gid` branch to use LID-based addressing (see comments in `rdma_common.hpp`).
- **MTU**: defaults to `IBV_MTU_1024`; bump to `IBV_MTU_4096` if both NICs' active MTU supports it
  (`ibv_devinfo -v` shows `active_mtu`).
- **iova**: the target registers the dma-buf MR with `iova = 0`; the initiator's remote_addr must
  match (`0` in the code below) — the dma-buf MR has no CPU-side `.addr`, so don't try to print or
  dereference `mr->addr`.
- If your NICs are on a private/isolated RDMA fabric rather than the general network, replace the
  plain-TCP handshake socket in `rdma_common.hpp` with whatever management/OOB network path is
  actually reachable between the two hosts.
