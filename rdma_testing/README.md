# Cross-host RDMA dma-buf P2P bandwidth test (bh-glx6u-22 <-> bh-glx6u-18)

Validates `Cluster::export_dmabuf()` end-to-end and measures sustained bandwidth: NIC on one host
issues repeated RDMA WRITEs, over the network, directly into a TLB window over a DRAM core on the
*other* host's Blackhole card — landing in device NOC memory with no host-CPU copy on the target
side.

Files:
- `rdma_common.hpp` — shared TCP out-of-band handshake + small helpers.
- `dmabuf_target.cpp` — runs on the box whose Blackhole TLB window (over a DRAM core) is the RDMA
  target. Exports the dma-buf via UMD, registers it as an RDMA MR, publishes QP/rkey info, waits for
  the peer to finish, then reads back device memory via UMD (a completely separate path from the
  exported window) to verify the final write landed.
- `dmabuf_initiator.cpp` — runs on the peer box. Registers a normal host-memory MR with a known test
  pattern, connects to the target, brings up an RC QP, and issues `--iters` back-to-back RDMA
  WRITEs of `--size` bytes each into the same remote window, timing the batch to report bandwidth.
- `window_read_target.cpp` / `window_read_initiator.cpp` — a **second, separate test pair** for read
  throughput specifically *through* the exported window (allocating and holding the `TlbWindow` itself
  rather than using `Cluster::export_dmabuf()`, so the same window can be read from the host too). See
  section 5; the first pair is unaffected by it.

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

## 2. Pick a target address and size

Both tools default to `addr = 0`, `size = 64 MiB` against the first DRAM core (in TRANSLATED
coords) on chip 0. `addr + size` must stay within a single DRAM bank — Blackhole's `DRAM_BANK_SIZE`
is 4 GiB (`device/api/umd/device/arch/blackhole_implementation.hpp`), far roomier than the old
Tensix-L1-backed test (1.5 MiB ceiling). Use `tools/topology` (already in this repo,
`tools/topology.cpp`) on the target host to confirm chip id 0 and pick a real DRAM core coordinate
if you don't want the first-DRAM-core default.

`dmabuf_target`'s underlying TLB window size is chosen internally by `tt_tlb_alloc()`, which only
accepts specific size classes (1/2/16 MiB on Wormhole, 2 MiB or 4 GiB on Blackhole — see
`device/api/umd/device/tt_kmd_lib/tt_kmd_lib.h`); `export_dmabuf()` rounds `--size` up to the
smallest class that fits, so no manual adjustment is needed. `--size` above 2 MiB forces the 4 GiB
class on Blackhole — still valid, just make sure `addr + size` stays inside the bank.

`--iters` (initiator only, default 100) controls how many `--size`-byte RDMA operations are issued
back-to-back against the same remote window before the batch is timed; bump it for a longer,
lower-noise bandwidth sample.

`--op write|read` (default `write`, **must match on both sides**) picks the direction:

- `write` — host memory → device NOC. The initiator fills a buffer with the test pattern and
  RDMA-WRITEs it; the target verifies via `read_from_device()`.
- `read` — device NOC → host memory. The *target* seeds the pattern into device memory via
  `write_to_device()` before the handshake, the initiator RDMA-READs it into a buffer prefilled with
  a `0xAA` sentinel (so a read that moved nothing can't false-pass), and the **initiator** prints
  PASS/FAIL.

Expect read to be markedly slower than write, and for a different reason than the link width. PCIe
writes are *posted* — fire-and-forget, and they pipeline. PCIe reads are *non-posted*: the target NIC
must issue read requests against the card's BAR and wait for each completion, so throughput is
governed by read-completion concurrency and latency rather than raw link bandwidth. The same
asymmetry shows up for host-side TLB access in `tests/microbenchmark/benchmarks/tlb/test_tlb.cpp`.

## 3. Run

On **bh-glx6u-22** (the target — owns the exported dma-buf):
```bash
./build/rdma_testing/dmabuf_target --port 9999 --chip 0 --size 67108864
```
It prints its own QP/rkey info is exchanged automatically over the socket; you don't need to copy
anything by hand. It blocks waiting for a TCP connection from the initiator, then waits for the
initiator's "done" signal, then verifies and prints PASS/FAIL.

On **bh-glx6u-18** (the initiator):
```bash
./build/rdma_testing/dmabuf_initiator --host bh-glx6u-22 --port 9999 --size 67108864 --iters 200
```
It connects, exchanges QP info, posts 200 RDMA WRITEs one at a time (waiting for each one's
completion before posting the next — see `SIGNAL_INTERVAL` in `dmabuf_initiator.cpp`), times the
batch, and prints something like:
```
Wrote 13421772800 bytes in 1.842 s => 7.287 GB/s
```
then signals the target that it's done so the target can verify and print PASS/FAIL.

To measure the other direction, add `--op read` to **both** commands. The PASS/FAIL then prints on the
initiator instead of the target, since that's where the data lands.

## 4. What "pass" and the bandwidth number mean

`dmabuf_target` reads back the same NOC address via `cluster->read_from_device()` (ordinary
register/DMA readback, independent of the TLB window that was exported) and compares it against the
pattern `dmabuf_initiator` last wrote. A byte-for-byte match with no `UMD_THROW` along the way
confirms:
- `PCIDevice::is_tlb_dmabuf_export_supported()` gated correctly on the live kmd version,
- `Cluster::export_dmabuf()` produced a valid, importable dma-buf fd,
- the peer NIC's repeated RDMA WRITEs actually reached the NOC-mapped window with no host
  involvement on the target's data path,
- the window's `FREE_TLB` firing immediately (per the design) didn't invalidate the export while the
  RDMA WRITEs were in flight (kmd's pin-until-fd-closed contract held).

The printed GB/s is wall-clock `(size * iters) / elapsed_time` measured on the initiator around the
post/poll loop — it reflects sustained NIC-to-DRAM-over-NOC write throughput for this specific
window, not a generic multi-connection or full-link RDMA benchmark.

## 5. Second test pair: reading device DRAM *through* the exported window

`window_read_target` / `window_read_initiator` are a separate, self-contained pair that answer a
different question: does a read of device DRAM issued **through the very TLB window that was
exported** work, and how fast is it — first from the target's own CPU, then from the peer's NIC.

The distinction matters because `Cluster::export_dmabuf()` cannot be used for this. It allocates a
dedicated `TlbWindow`, hands the fd to the kmd (which pins the mapping until the fd closes) and
immediately drops its own handle, so nothing on the host is left holding a mapping of the exported
window. `cluster->read_from_device()` would therefore read the same NOC address through a *different*
window. `window_read_target` instead drives the pieces directly and keeps the window alive:

```cpp
PCIDevice::allocate_tlb(window_size, TlbMapping::WC)   // -> TlbHandle
SiliconTlbWindow(std::move(handle), config)            // the live window, kept for the whole run
window->export_dmabuf(0, size)                         // -> fd registered as the RDMA MR
window->read_block(0, dst, size)                       // a device read through THAT window
```

so the host CPU and the remote NIC reach DRAM through one and the same TLB window. The window size
class is picked with the same rule `Cluster::export_dmabuf()` applies internally (smallest class where
`(addr % class) + size <= class`), since `tt_tlb_alloc()` only accepts exact classes.

Per iteration:

```
initiator                            target
  send 'G'  (1 byte, TCP)   ------>  window->read_block(0, host_copy, --size)
                                     ^ DRAM -> this host, through the exported window; blocks until done
  recv 'R'  (1 byte, TCP)   <------  send 'R'
  IBV_WR_RDMA_READ  ------------->   serviced by the target NIC against the same exported window
  poll completion                    (no target CPU involvement in this step)
```

The handshake is one byte each way on the TCP socket that is already open for the QP exchange — no
extra MRs, no receive queues, no RNR handling. It costs a TCP round trip per iteration, which is why
the initiator times step 3 separately.

Run it the same way as the first pair (the target seeds the pattern itself via
`cluster->write_to_device()`, a path independent of the window under test, so an address-configuration
bug can't pass by having the write and the read agree while both land in the wrong place):

```bash
# target host
./build/rdma_testing/window_read_target --port 9999 --chip 0 --size 67108864

# initiator host
./build/rdma_testing/window_read_initiator --host bh-glx6u-22 --port 9999 --size 67108864 --iters 200
```

`--size` must match on both sides; `--iters` is sent over the handshake socket, so only the initiator
needs it. Output:

- initiator — `NIC read over the exported window: ... GB/s`, the RDMA_READ steps in isolation. This is
  the read-throughput number.
- initiator — `Full round trip ... GB/s`, including the target's CPU read and both handshake bytes.
  Always lower; the gap is the target-side read plus TCP latency.
- target — `Window read (CPU): ... GB/s`, DRAM to target host memory through the exported window.
- both — a PASS/FAIL. The target's covers its own `read_block()`, so a mismatch there localizes the
  fault to the window config or the UMD read path before the NIC or network is implicated; the
  initiator's covers the bytes that actually crossed the wire.

Both readers are bound by *non-posted* PCIe reads (each read request against the card needs a
completion to come back), so expect both numbers to be well under the write bandwidth the first test
pair reports.

One thing to be clear about when reading these numbers: a TLB window is an **aperture**, not storage.
There is no buffer inside the window for DRAM contents to be staged into — a read through the window
*is* a NOC read of DRAM. So step 1 and step 3 are two independent reads of the same DRAM through the
same window, not a fetch-into-the-window followed by a pickup. What step 1 buys is proof that a real
device read over the exported window completed on the target before the NIC read was allowed to start.

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
- **SIGNAL_INTERVAL and in-flight bytes**: `dmabuf_initiator.cpp` polls a completion after every
  single WRITE (`SIGNAL_INTERVAL = 1`) — confirmed on real bh-glx6u-22/18 hardware that raising this
  (previously 32) left ~2 GiB of unpaced, unacknowledged 64 MiB WRITEs outstanding and blew through
  the QP's retry budget (`transport retry counter exceeded`), likely from overrunning switch/NIC
  buffers on a non-lossless fabric. The QP's `max_send_wr` is sized to match `SIGNAL_INTERVAL`. Only
  raise it after confirming (empirically, on your fabric) how many bytes can be safely outstanding
  at your chosen `--size` — and bump `max_send_wr` to match or `ibv_post_send` will fail once the
  send queue fills.
- **Large `--size` and pinned memory**: `--size` above the default 64 MiB increases both the
  initiator's `local_buf` (regular pageable-then-pinned host memory via `ibv_reg_mr`) and, above
  2 MiB, forces the target's TLB window to the 4 GiB class. Neither has been exercised at very large
  sizes on real hardware — watch for `ibv_reg_mr`/`ibv_reg_dmabuf_mr` failures from ulimited
  lockable memory (`ulimit -l`) if you push `--size` much higher.
