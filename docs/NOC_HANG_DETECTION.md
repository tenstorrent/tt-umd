# NOC Hang Detection — PR #2629 Review

**PR:** [#2629](https://github.com/tenstorrent/tt-umd/pull/2629) — *Add per-op MMIO timeout for TLB-mapped device access* (draft)
**Issue:** [#1673](https://github.com/tenstorrent/tt-umd/issues/1673) — *Investigate host crashes for bad NOC reads*

## What problem are we solving

- Reading a hung NOC core (repro: WH tensix `0,0` @ NOC addr `0xFFB11030`) returns `0xFFFFFFFF` after ~700 ms.
- **Keep reading from that core → the host crashes.**
- Two failure modes today's protection does **not** cover:
  - **Slow degradation** — transactions still complete, just orders of magnitude slower; the host just grinds.
  - **Posted-write pile-up** — small posted writes succeed locally before back-pressure stalls the CPU. A bulk transfer can push hundreds of KB into a dead device.

## Current state

- We perform stores/loads to/from the TLB window **without ever checking whether the NOC is hung.**
- When the NOC is hung, those stores pile up in the PCIe core's command buffer. Enough pile-up and **we kill the host.**
- `memcpy_to_device` / `memcpy_from_device` issue AVX2 / SSE / 4-byte / byte-wide loads and stores against TLB-mapped device memory — all unguarded by any liveness check.
- **Net: today we have no guard against crashing the host.**
- A **`HangDetector`** already exists but is **not wired into the I/O path**:
  - `is_pcie_hung()` — BAR read, checks for the sentinel value.
  - `is_noc_hung(NocId)` — reads the ARC node-id register over the NOC (MMIO protocols only).

## Solution in this PR

**Give each store/load a preset timeout; if an op exceeds it, check whether the NOC is hung — and abort before the pile-up can kill the host.**

- A **per-op wall-clock budget** inside the memcpy functions. Each TLB-touching op must finish within a hard budget — **default 100 ms**, override via `TT_UMD_MMIO_OP_TIMEOUT_MS`.
- **How we check the NOC is hung:** the memcpy layer can't do this itself, so the caller passes a callback (`MemcpyTimeoutFn`) that performs the actual liveness check. The intended callback is the existing **`HangDetector`** — a slow op triggers a real NOC probe.
- On overrun, behavior depends on that callback:
  - **no callback** → throw `DeviceTimeoutError` (default for any caller that doesn't wire one up).
  - **callback returns `true`** (NOC confirmed hung) → throw.
  - **callback returns `false`** (false positive, device healthy) → continue; next op gets a fresh budget.
- Because the check is a caller-supplied callback, **any code path that goes through the API can wire in the NOC hang detector** — each call site decides whether to plug it in. And the callback is generic: a custom caller can pass whatever check they want, not just the `HangDetector`.
- New `DeviceTimeoutError` exception so callers can branch on retry policy.
- Opt-in timing instrumentation (`TT_UMD_MEMCPY_TIMING=1`) — per-op deltas dumped to stderr.

## Alternative solutions

- **Timeout only, no liveness check** — just abort on overrun. This was the **original implementation here**, but we hit false positives: some reads during UMD startup after a reset legitimately take longer, so a bare timeout aborts a healthy device. If that false-positive risk is acceptable, this is the simplest option.

## Performance — does this slow down read/write?

Open question raised on the PR. The data so far is mixed:

- **CI aggregate: no real impact (−0.3 %).** Deltas swing both ways and are dominated by runner/single-sample noise; the BH runners actually came out slightly faster (−0.6 % to −3.8 %). `SysmemReadWrite`, which doesn't go through the timeout path at all, shows the same ±tens-of-ms spread — that's the noise floor.
- **But on the dedicated bh-40 machine, API tests slowed `3m13.660s → 3m27.312s` (~+7 %).** This is the number to dig into — a dedicated machine should be far less noisy than CI runners.
- Reviewer note (tt-vjovanovic): if the impact turns out real, the likely culprit is **heap allocations from `std::function<>`**; switch the callback to a **template function argument** rather than relying on the optimizer to inline everything.
- Author measured locally and saw no impact, preferring the `std::function<>` form for readability.

**To discuss:** is the bh-40 slowdown real or noise? If real, do we move to a templated callback to kill the `std::function` allocation?

## For the meeting

**Recommendation:** keep the 100 ms per-op timeout *and* the NOC-hang callback. The 100 ms budget is comfortable for the slow post-reset / remote ops, and the callback only fires on an overrun — so it costs nothing on the happy path while removing the false-positive risk that timeout-only had.

Open:

- OK to land detection now and wire production call sites in the follow-up?
