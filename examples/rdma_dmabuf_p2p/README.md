# RDMA dma-buf peer-to-peer example

Validates `Cluster::export_dmabuf()` over real RDMA hardware and measures sustained bandwidth: a
NIC on one host issues repeated RDMA operations, over the network, directly against a TLB window
over a DRAM core on the *other* host's card — landing in device NOC memory with no host-CPU copy on
the target side.

- `dmabuf_target.cpp` — runs on the host whose TLB window is the RDMA target. Exports the dma-buf
  via UMD, registers it as an RDMA MR, publishes QP/rkey info over a TCP handshake, waits for the
  peer to finish, then reads device memory back via UMD (a path completely independent of the
  exported window) to verify.
- `dmabuf_initiator.cpp` — runs on the peer host. Registers a plain host-memory MR, connects to the
  target, brings up an RC QP, and issues `--iters` back-to-back RDMA operations, timing the batch to
  report bandwidth. Links no UMD at all.
- `rdma_common.hpp` — the TCP handshake, RDMA device/GID selection, RC QP bring-up and verification
  pattern shared by the two.

**Scope: Blackhole only.** This is currently intended for Blackhole Galaxy systems, and has only been
developed and run there. Wormhole is not a supported target: nothing in the code rejects it, but none
of it has been exercised on Wormhole and the defaults below are picked for Blackhole. Treat a Wormhole
run as unverified.

For a single-host smoke test that needs no peer and no network config, see
`TestDmabufRdmaLoopback.ExportedDmabufIsRdmaReadable` in `tests/rdma/test_dmabuf_loopback.cpp`
instead — it exercises the same export path over an RDMA loopback QP pair on one NIC port. It lives
in the `rdma_tests` target, behind the same `TT_UMD_BUILD_RDMA` flag as this example:

```bash
cmake -B build -DTT_UMD_BUILD_TESTS=ON -DTT_UMD_BUILD_RDMA=ON
cmake --build build --target rdma_tests
./build/test/umd/rdma/rdma_tests
```

## Prerequisites (both hosts)

Four separate things have to be in place. Only the first comes from a package; the rest are part of
bringing up the NIC and the driver.

### 1. Userspace RDMA libraries

```bash
sudo apt-get install -y libibverbs-dev rdma-core ibverbs-utils
```

`libibverbs-dev` is what this example links against (library *and* headers — `libibverbs1` alone is
not enough), and `ibverbs-utils` provides the `ibv_devices` / `ibv_devinfo` diagnostics used below.
On RHEL-family distros the equivalents are `rdma-core-devel` and `libibverbs-utils`.

### 2. A working RDMA NIC

The kernel driver and NIC firmware are vendor-specific and are part of installing the NIC, not
something the packages above provide. Most current NICs are served by an in-tree kernel module
(`mlx5_core` for NVIDIA/Mellanox, `bnxt_re` for Broadcom), in which case `rdma-core` is all the
userspace you need; a vendor stack such as NVIDIA DOCA-OFED replaces both and works too.

```bash
ibv_devices    # the NIC shows up here only once its RDMA driver is loaded
ibv_devinfo    # state must be PORT_ACTIVE; note link_layer (Ethernet = RoCE, InfiniBand)
```

An empty `ibv_devices` means the RDMA driver is not loaded, and nothing below will work.

### 3. The RoCE port configured

For RoCE the port needs an IP address, because the routable RoCEv2 GID the binaries auto-select is
derived from it. This is ordinary host networking and fabric configuration, outside the scope of
these binaries. `show_gids` (from `ibverbs-utils`) lists what is available; if it shows no RoCEv2
IPv4-mapped entry, the address is missing or the port is on RoCEv1 only.

### 4. tt-kmd new enough to export a dma-buf

```bash
cat /sys/module/tenstorrent/version   # need >= 2.10.0-rc1 for TENSTORRENT_IOCTL_EXPORT_TLB_DMABUF
uname -r                              # need kernel >= 5.8
```

If either is too old, `export_dmabuf()` throws with a message naming both versions, and the kmd on
that host needs updating first.

## Build (both hosts)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DTT_UMD_BUILD_EXAMPLES=ON -DTT_UMD_BUILD_RDMA=ON
cmake --build build -j"$(nproc)"
```

Binaries land in `build/examples/rdma_dmabuf_p2p/`. `TT_UMD_BUILD_RDMA` is OFF by default, so a
normal build never needs libibverbs; with it ON, missing or too-old libibverbs is a configure error
naming what to install rather than a silent skip.

## Run

On the target host (owns the exported dma-buf):

```bash
./build/examples/rdma_dmabuf_p2p/dmabuf_target --port 9999 --chip 0 --size 2097152
```

It blocks waiting for a TCP connection from the initiator, exchanges QP/rkey info automatically,
waits for the initiator's pass/fail signal, then verifies and prints PASS/FAIL. A failure reported by
the initiator becomes a non-zero exit status here too, so watching this side alone is enough for CI.

On the initiator host:

```bash
./build/examples/rdma_dmabuf_p2p/dmabuf_initiator --host <target-host> --port 9999 \
    --size 2097152 --iters 200
```

It times the batch and prints something like:

```
Wrote 13421772800 bytes in 1.842 s => 7.287 GB/s
```

### Options

| Flag | Side | Default | Meaning |
| --- | --- | --- | --- |
| `--port` | both | — | TCP port for the out-of-band handshake |
| `--host` | initiator | — | Target hostname |
| `--chip` | target | 0 | Chip id to export from |
| `--addr` | target | 0 | DRAM address to export (page-aligned) |
| `--size` | both | 2 MiB | Bytes per RDMA operation; must match on both sides (enforced via the handshake) |
| `--iters` | initiator | 100 | RDMA operations issued back-to-back before timing |
| `--dev` | both | auto | RDMA device name; auto-picks the first with an ACTIVE port |
| `--ib-port` | both | auto | HCA port number; auto-picks the first ACTIVE port |
| `--gid-index` | both | auto | GID index; auto-detects a RoCEv2 IPv4-mapped GID |
| `--ordering` | target | `relaxed` | TLB window ordering for the export: `relaxed`, `posted` or `strict` |
| `--op` | both | `write` | `write` (host → device) or `read` (device → host); must match |

`--op read` reverses the direction: the target seeds the pattern via `write_to_device()` before the
handshake, the initiator RDMA-READs it into a sentinel-filled buffer, and the **initiator** prints
PASS/FAIL since that is where the data lands.

Expect read to be markedly slower than write, for a reason unrelated to link width: PCIe writes are
*posted* (fire-and-forget, they pipeline), while reads are *non-posted* — the NIC must issue read
requests against the card's BAR and wait for each completion, so throughput is governed by
read-completion concurrency and latency rather than raw link bandwidth.

## Notes

- `addr + size` must stay within a single DRAM bank. On Blackhole the usable bank space ends short
  of the nominal `DRAM_BANK_SIZE` — keep clear of the tail when picking large `--size`/`--addr`.
- The underlying TLB window size is chosen by `tt_tlb_alloc()`, which accepts only specific size
  classes — on Blackhole exactly 2 MiB or 4 GiB (see the size class tables in
  `device/arch/architecture_tlbs.cpp`). `export_dmabuf()` rounds `--size` up to the smallest class
  that fits, so no manual adjustment is needed. This is why the default is 2 MiB: anything larger
  consumes one of the few 4 GiB windows.
- `--size` is capped at 4 GiB minus one byte by the `uint32_t` length of a single RDMA work request,
  and further by the port's `max_msg_sz` (often 1 GiB). Both are rejected up front.
- GID selection matters and is easy to get wrong. On some NICs GID index 0 is RoCE *v1* with a
  link-local address; RoCEv1 is non-routable, and connecting with it produces traffic the peer never
  ACKs — the symptom is a first operation failing with "transport retry counter exceeded", which
  looks nothing like a fabric-config problem. The auto-detection in `rdma_common.hpp` prefers a
  RoCEv2 IPv4-mapped GID for this reason.
- Large `--size` raises the initiator's pinned host memory; if `ibv_reg_mr` fails, raise `ulimit -l`.
- Both sides must be built from the same commit: the handshake struct is exchanged as a raw struct
  and carries a magic value, so a mismatched peer build is rejected rather than silently misparsed.
